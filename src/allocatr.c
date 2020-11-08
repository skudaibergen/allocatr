//
// Created by Sanzhar Kudaibergen on 10/26/19.
//

// To use sbrk with clang on Mac OS without warnings
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include "allocatr.h"

#include <unistd.h>   // for sbrk
#include <stdbool.h> // for bool

// FIXME: TEMPORARY DEPS
#include <assert.h> // for assert
#include <stdio.h> // for assert


//#define WORD_SIZE size_t
#define ALIGN_BLOCK (size) (size + (sizeof(WORD_SIZE) - 1)) & (~(sizeof(WORD_SIZE) - 1))

size_t align_mem(size_t size) {
    size_t bound = sizeof(size_t) - 1;
    return (size + bound) & (~bound);
}

/**
 * 32-bytes sized structure = 24 bytes meta + 8 byte userData pointer
 */
struct mem_block
{
    size_t size;
    bool used;

    struct mem_block *next;
    // struct mem_block *prev; // FIXME: can break hardcoded obj header checks

    /** user data pointer **/
    size_t data[1];
};

/**
 * Heap start. Initialized on first allocation.
 */
static struct mem_block *heap_start = NULL;

/**
 * Current top. Updated on each allocation.
 */
static struct mem_block *top = NULL;

/**
 * Segregated lists. Reserve the space with NULL.
 * Each list is initialized on first allocation.
 */
static struct mem_block *segregated_lists[] =
{
        NULL,   //   8       bytes
        NULL,   //  16       bytes
        NULL,   //  32       bytes
        NULL,   //  64       bytes
        NULL,   // 128       bytes
        NULL,   // any > 128 bytes
};

/**
 * Segregated tops.
 */
static struct mem_block *segregated_tops[] =
{
        NULL,   //   8       bytes
        NULL,   //  16       bytes
        NULL,   //  32       bytes
        NULL,   //  64       bytes
        NULL,   // 128       bytes
        NULL,   // any > 128 bytes
};

/**
 * Gets the bucket number from segregatedLists
 * based on the size.
 */
int get_bucket(size_t size)
{
    return ((int) (size / sizeof(size_t))) -  1;
}

/**
 * 24bytes
 * @return size of object header minus user data pointer size
 */
size_t mem_block_header_size()
{
    return sizeof(struct mem_block) - sizeof(size_t);
}

/**
 * userSize + 24bytes
 * @return total size to allocate including object header
 */
size_t alloc_size(size_t size)
{
    return size + mem_block_header_size();
}

/**
 * @return Address of object header from user data pointer
 */
struct mem_block* get_header(size_t *data)
{
    return (struct mem_block *)
            (((char *) data) + sizeof(size_t) - sizeof(struct mem_block));
}

struct mem_block *request_mem(size_t size)
{
    // obtaining the pointer to the current heap break — the beginning position of the newly allocated block
    struct mem_block *curr_brk = (struct mem_block *) sbrk(0);
    size_t total_size = alloc_size(size);
    printf("request_mem(size = %zu): total_size = %zu, current heap break: %p\n", size, total_size, curr_brk);

    // increase the break position, sbrk() returns the previous program break
    if (sbrk(total_size) == (void *) -1)
    {
        // out of memory
        return NULL;
    }

    return curr_brk;
}

/**
 * Mode for searching a free block.
 */
enum search_mode
{
    FIRST_FIT,
    NEXT_FIT,
    BEST_FIT,
    SEGREGATED_LIST
};

/**
 * Previously found block. Updated in `nextFit`.
 */
static struct mem_block *search_start = NULL;

/**
 * Current search mode.
 */
static enum search_mode smode = FIRST_FIT;

/**
 * Reset the heap to the original position.
 */
void reset_heap()
{
    // Already reset.
    if (heap_start == NULL)
    {
        return;
    }

    // Roll back to the beginning.
    brk(heap_start);

    heap_start = NULL;
    top = NULL;
    search_start = NULL;
}

/**
 * Initializes the heap, and the search mode.
 */
void init(enum search_mode m)
{
    smode = m;
    reset_heap();
}

/**
 * you can’t split block of size 32 to two blocks of 16,
 * since you need to take into account the size of the object headers.
 * So I used block of size 64 for splitting use case.
 *
 * min split size is 16bytes (min aligned size is 8bytes x2) + size of object headers (1 header of  x2)
 *
 *
 * min size is 64
 * Whether this block can be split.
 */
bool can_split_block(struct mem_block *block, size_t size)
{
    return !block->used && (block->size - size >= sizeof(struct mem_block));
}

/**
 * if 64 -> total = 88
 * 88 - 24 = 64         / left for header1
 * 64 - 16 (size) = 48  / left for second block header and data
 * 48 - 24 = 24         / left for second block data
 *
 * Splits the block on two, returns the pointer to the smaller sub-block.
 */
struct mem_block *split_block(struct mem_block *block, size_t size)
{
    struct mem_block *second_block = (struct mem_block *) (((char *) block->data) + size);
    printf("splitting. block1: %p  data1: %p, block2: %p  data2: %p\n", block, block->data, second_block, second_block->data);

    second_block->size = block->size - (size + mem_block_header_size());
    second_block->used = false;
    second_block->next = block->next;

    block->size = size;
    block->next = second_block;

    printf("finished splitting. block1 next %p, block2 next: %p\n", block->next, second_block->next);
    return block;
}

/**
 * Whether this block can be merged together with the block->next.
 */
bool can_coalesce_block(struct mem_block *block)
{
    return block->next && !block->next->used;
}

/**
 *
 */
struct mem_block *coalesce_block(struct mem_block *block)
{
    struct mem_block *coalsc_block = block->next;
    block->size = block->size + coalsc_block->size;
    block->next = coalsc_block->next;
    return block;
}

/**
 * Allocates a block from the list, splitting if needed.
 */
struct mem_block *allocate_block(struct mem_block *block, size_t size)
{
    if (!block)
        return NULL;

    // Split the larger block, reusing the free part.
    if (can_split_block(block, size))
        block = split_block(block, size);

    block->used = true;
    block->size = size;

    return block;
}

/**
 * First-fit algorithm.
 *
 * Returns the first free block which fits the size.
 */
struct mem_block *first_fit_search(size_t size)
{
    struct mem_block * block = heap_start;

    while (block != NULL)
    {
        // O(n) search.
        if (block->used || block->size < size)
        {
            block = block->next;
            continue;
        }

        // Found the block:
        return block;
    }

    return NULL;
}

/**
 * Next-fit algorithm.
 *
 * Returns the next free block which fits the size.
 * Updates the `searchStart` of success.
 */
struct mem_block *next_fit_search(size_t size)
{
    if(!search_start)
        search_start = heap_start;

    struct mem_block * block = search_start;

    while (block != NULL)
    {
        // O(n) search.
        if (block->used || block->size < size)
        {
            block = block->next;
            continue;
        }

        // Found the block:
        search_start = block;
        return block;
    }

    return NULL;
}

/**
 * Best-fit algorithm.
 *
 * Returns a free block which size fits the best.
 */
struct mem_block *best_fit_search(size_t size)
{
    if(!search_start)
        search_start = heap_start;

    struct mem_block *block = search_start;

    struct mem_block *found = NULL;
    while (block != NULL)
    {
        // O(n) search.
        if (block->used || block->size < size)
        {
            block = block->next;
            continue;
        }

        // Found the block
        if (block->size == size)
        {
            found = block;
            break;
        }

        if (found == NULL || block->size < found->size)
            found = block;

        search_start = block;
        block = block->next;
    }

    return allocate_block(found, size);
}


/**
 * Segregated fit algorithm.
 */
struct mem_block *segregated_fit_search(size_t size)
{
    int bucket = get_bucket(size);
    struct mem_block *original_heap = heap_start;

    // Init the search.
    heap_start = segregated_lists[bucket];

    // Use first-fit here, but can be any:
    struct mem_block *block = best_fit_search(size);

    heap_start = original_heap;
    return block;
}

/**
 * Tries to find a block of a needed size.
 */
struct mem_block *find_free_block(size_t size)
{
    switch (smode)
    {
        case FIRST_FIT:
            return first_fit_search(size);
        case NEXT_FIT:
            return next_fit_search(size);
        case BEST_FIT:
            return best_fit_search(size);
        case SEGREGATED_LIST:
            return segregated_fit_search(size);
        default:
            return NULL;
    }
}

/**
 * Allocates a block of memory of (at least) `size` bytes.
 */
void *alloc(size_t size)
{
    size = align_mem(size);

    // Search for an available free block:
    struct mem_block *free_block = find_free_block(size);
    if (free_block)
        return free_block->data;

    // If block not found in the free list, request from OS:
    struct mem_block *block = request_mem(size);
    block->size = size;
    block->used = true;

    if (smode == SEGREGATED_LIST)
    {
        // init segregated list
        int bucket = get_bucket(size);

        if (!segregated_lists[bucket])
            segregated_lists[bucket] = block;

        if (segregated_tops[bucket] != NULL)
            segregated_tops[bucket]->next = block;
        segregated_tops[bucket] = block;
    }
    else
    {
        // Init heap.
        if (heap_start == NULL)
            heap_start = block;

        // Chain the blocks.
        if (top != NULL)
            top->next = block;
        top = block;
    }


    printf("finished allocation. block: %p, userData: %p\n", top, block->data);
    // User payload:
    return block->data;
}

void dealloc(void *ptr)
{
    struct mem_block *block = get_header(ptr);

    // excluding segregated list, cause after coalescence block will be in a wrong bucket
    // TODO: Implement bucketed coalescence
    if (can_coalesce_block(block) && smode != SEGREGATED_LIST)
        block = coalesce_block(block);

    block->used = false;
}

void debug_print_list()
{
    struct mem_block *ptr = heap_start;
    int cnt = 0;
    while (ptr != NULL)
    {
        // ptrdiff_t sizeDiff = (char*) ptr - (char*) prevAddr;
        printf("Block %d, "
               "headerAddr: %p, "
               "dataAddr: %p, "
               "dataSize: %zu, "
               //"totalSize: %zu, "
               "used: %s\n",
               cnt, ptr, ptr->data, ptr->size,
               //sizeDiff,
               ptr->used ? "true" : "false");
        ptr = ptr->next;
        cnt++;
    }
}

void debug_print_buckets(size_t size)
{
    struct mem_block *ptr = segregated_lists[get_bucket(size)];
    int cnt = 0;
    while (ptr != NULL)
    {
        // ptrdiff_t sizeDiff = (char*) ptr - (char*) prevAddr;
        printf("Block %d, "
               "headerAddr: %p, "
               "dataAddr: %p, "
               "dataSize: %zu, "
               //"totalSize: %zu, "
               "used: %s\n",
               cnt, ptr, ptr->data, ptr->size,
                //sizeDiff,
               ptr->used ? "true" : "false");
        ptr = ptr->next;
        cnt++;
    }
}

//#include <unistd.h>
//#include <assert.h>
//
//#include <stdio.h>
//#include <time.h>
//#include <mm_malloc.h>

#define INTERNAL_SIZE_T size_t
#define SIZE_SZ (sizeof (INTERNAL_SIZE_T))
#define chunk2mem(p)   ((void*)((char*)(p) + 2*SIZE_SZ))

int main()
{
    // Test case 1: Alignment. A request for 3 bytes is aligned to 8.
    printf("RUNNING Test case 1: Alignment. A request for 3 bytes is aligned to 8.\n");
    void* p1 = alloc(3);
    struct mem_block* p1b = get_header(p1);
    assert(p1b->size == sizeof(size_t));
    printf("FINISHED Test case 1.\n--------------------------------------\n");
    // --------------------------------------

    // Test case 2: Exact amount of aligned bytes
    printf("RUNNING Test case 2: Exact amount of aligned bytes.\n");
    void* p2 = alloc(8);
    struct mem_block* p2b = get_header(p2);
    assert(p2b->size == 8);
    printf("FINISHED Test case 2.\n--------------------------------------\n");
    // --------------------------------------

    assert(p1b->next == p2b);
    // --------------------------------------

    // Test case 3: Free the object
    printf("RUNNING Test case 3: Free the object.\n");
    dealloc(p2);
    assert(p2b->used == false);
    printf("FINISHED Test case 3.\n--------------------------------------\n");
    // --------------------------------------

    // Test case 4: The block is reused
    // A consequent allocation of the same size reuses the previously freed block.
    printf("RUNNING Test case 4: The block is reused. A consequent allocation of the same size reuses the previously freed block.\n");
    void* p3 = alloc(8);
    struct mem_block* p3b = get_header(p3);
    assert(p3b->size == 8);
    assert(p3b == p2b);  // Reused!
    printf("FINISHED Test case 4.\n--------------------------------------\n");
    // --------------------------------------

    // Init the heap, and the searching algorithm.
    init(NEXT_FIT);
    // --------------------------------------

    // Test case 5: Next search start position
    printf("RUNNING Test case 5: Next search start position.\n");
    // [[8, 1], [8, 1], [8, 1]]
    alloc(8);
    alloc(8);
    alloc(8);

    // [[8, 1], [8, 1], [8, 1], [16, 1], [16, 1]]
    void *o1 = alloc(16);
    void *o2 = alloc(16);

    // [[8, 1], [8, 1], [8, 1], [16, 0], [16, 0]]
    dealloc(o1);
    dealloc(o2);

    // [[8, 1], [8, 1], [8, 1], [16, 1], [16, 0]]
    void *o3 = alloc(16);

    // Start position from o3:
    assert(search_start == get_header(o3));

    // [[8, 1], [8, 1], [8, 1], [16, 1], [16, 1]]
    //                           ^ start here
    alloc(16);
    printf("FINISHED Test case 5.\n--------------------------------------\n");
    // --------------------------------------

    init(BEST_FIT);
    // --------------------------------------

    // Test case 6: Best-fit search
    printf("RUNNING Test case 6: Best-fit search. Block splitting\n");
    // [[8, 1], [64, 1], [8, 1], [16, 1]]
    alloc(8);
    void *z1 = alloc(64);
    alloc(8);
    void *z2 = alloc(16);

    // Free the last 16
    dealloc(z2);

    // Free 64:
    dealloc(z1);
    // [[8, 1], [64, 0], [8, 1], [16, 0]]

    // Reuse the last 16 block:
    void *z3 = alloc(16);
    assert(get_header(z3) == get_header(z2));
    // [[8, 1], [64, 0], [8, 1], [16, 1]]

    // Reuse 64, splitting it to 16, and 48
    z3 = alloc(16);
    assert(get_header(z3) == get_header(z1));
    // [[8, 1], [16, 1], [48, 0], [8, 1], [16, 1]]
    printf("FINISHED Test case 6.\n--------------------------------------\n");
    // --------------------------------------

    init(BEST_FIT);
    // --------------------------------------

    // Test case 7: Block coalescence
    printf("RUNNING Test case 7: Block coalescence.\n");
    // [[8, 1], [8, 1], [16, 1], [8, 1]]
    alloc(8);
    void *x1 = alloc(8);
    void *x2 = alloc(16);
    void *x3 = alloc(8);

    // [[8, 1], [8, 1], [16, 0], [8, 1]]
    dealloc(x2);

    // Coalesce 2nd and 3rd block
    // [[8, 1], [24, 0], [8, 1]]
    dealloc(x1);

    // Reuse the last 16 block:
    struct mem_block *x1_header = get_header(x1);
    struct mem_block *x3_header = get_header(x3);
    assert(x1_header->size == 24);
    assert(x1_header->used == false);
    assert(x1_header->next == x3_header);
    printf("FINISHED Test case 7.\n--------------------------------------\n");
    // --------------------------------------

    // Test case 8: Segregated lists
    printf("RUNNING Test case 8: Segregated lists\n");
    init(SEGREGATED_LIST);
    alloc(8);
    void *y1 = alloc(8);
    void *y2 = alloc(16);
    dealloc(y1);

    void *y3 = alloc(8);
    assert(get_header(y3) == get_header(y1));
    alloc(16);
    dealloc(y2);
    alloc(32);
    printf("FINISHED Test case 8.\n--------------------------------------\n");
    // --------------------------------------

    debug_print_buckets(8);
    printf("\n");
    debug_print_buckets(16);
    printf("\n");
    debug_print_buckets(32);
    printf("\n");


    printf("\n");
    debug_print_list();
}

