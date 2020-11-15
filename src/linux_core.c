/**
 *
 * @author Sanzhar Kudaibergen
 * @created 11/10/2020
 */

#include <sys/mman.h>   // mmap
#include <pthread.h>

#import "oscore.h"

// FIXME: TEMPORARY HERE
#include <stdio.h> // for printf

#define ARENA_SIZE 4 * 1024 * 1024     // 4MB = 4194304 bytes

static pthread_mutex_t alloc_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Allocation arena - large chunk of anonymous memory
 */
static void *main_arena = NULL;

/**
 * Current program break;
 */
static char *_brk = NULL;

void *incr_brk(intptr_t increment)
{
    if (!main_arena)
    {
        // blocks of memory are always gonna be aligned by multiple of pageSize 4KB, so bottom 3 bits are always 0
        main_arena = mmap(0, ARENA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        _brk = main_arena;
    }

    if (increment == 0)
        return _brk;

    char *heap_top = (char *) main_arena + ARENA_SIZE;
    if (_brk + increment > heap_top)
    {
        printf("out of memory error. curr_brk = %p, heap_top = %p incr = %zu\n", _brk, heap_top, increment);
        return (void *) -1;
    }

    // increase the program break
    _brk += increment;
    printf("finished sbrk(increment = %zu) curr brk = %p, heap top = %p\n", increment, _brk, (char *) main_arena + ARENA_SIZE);

    return _brk;
}

void* resize_arena()
{
    char *heap_top = (char *) main_arena + ARENA_SIZE;

    main_arena = mmap(heap_top, ARENA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    _brk = main_arena;

    printf("resize_arena() prev heap_top = %p next_arena = %p\n", heap_top, main_arena);
    return _brk;
}

void *request_mem(size_t size)
{
    if (size < 1)
        return (void *) -1;

    // obtaining the pointer to the current heap break — the beginning position of the newly allocated block
    void *curr_brk = incr_brk(0);
    printf("request_mem(size = %zu). current heap break: %p\n", size, curr_brk);

    // increase the break position, sbrk() returns the previous program break
    if (incr_brk(size) == (void *) -1)
    {
        // out of memory, return newly allocated arena
        return resize_arena();
    }

    return curr_brk;
}

void alloc_lock_acquire()
{
    pthread_mutex_lock(&alloc_mutex);
}

void alloc_lock_release()
{
    pthread_mutex_unlock(&alloc_mutex);
}