/**
 *
 * @author Sanzhar Kudaibergen
 * @created 11/10/2020
 */

#include <stdbool.h> // for bool

#include "allocatr.h"
#include "oscore.h"

// FIXME: TEMPORARY DEPS
#include <assert.h> // for assert
#include <stdio.h> // for printf


static size_t align_mem(size_t size) {
    size_t bound = sizeof(size_t) - 1;
    return (size + bound) & (~bound);
}

/**
 * 32-bytes sized structure. 24 bytes meta + 8 byte userData pointer
 */
struct mem_block
{
    size_t size;
    bool used;
    struct mem_block *next;

    /** user data pointer **/
    size_t data[1];
};

/**
 * heap start. Initialized on first allocation.
 */
static struct mem_block *heap_start = NULL;

/**
 * current top. updated on each allocation.
 */
static struct mem_block *top = NULL;

/**
 * segregated buckets by blocck size, each list is initialized on first allocation.
 */
static struct mem_block *buckets[] =
{
        NULL,   //   8       bytes
        NULL,   //  16       bytes
        NULL,   //  32       bytes
        NULL,   //  64       bytes
        NULL,   // 128       bytes
        NULL,   // any > 128 bytes
};

/**
 * bucket tops with segregatted heap start pointers
 */
static struct mem_block *bucket_tops[] =
{
        NULL,   //   8       bytes
        NULL,   //  16       bytes
        NULL,   //  32       bytes
        NULL,   //  64       bytes
        NULL,   // 128       bytes
        NULL,   // any > 128 bytes
};

/**
 * gets the bucket number from buckets based on the size.
 */
static int get_bucket(size_t size)
{
    return ((int) (size / sizeof(size_t))) -  1;
}

/**
 * @return size of object header minus user data pointer size
 */
static size_t mem_block_header_size()
{
    return sizeof(struct mem_block) - sizeof(size_t);
}

/**
 * @return total size to allocate including object header
 */
static size_t alloc_size(size_t size)
{
    return size + mem_block_header_size();
}

/**
 * @return address of object header from user data pointer
 */
static struct mem_block* get_header(size_t *data)
{
    return (struct mem_block *)
            (((char *) data) + sizeof(size_t) - sizeof(struct mem_block));
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
 * previously found block.
 */
static struct mem_block *search_start = NULL;

/**
 * current search mode.
 */
static enum search_mode smode = FIRST_FIT;

/**
 * you can’t split block of size 32 to two blocks of 16,
 * since you need to take into account the size of the object headers.
 * So I used block of size 64 for splitting use case.
 *
 * min split size is 16bytes (min aligned size is 8bytes x2) + size of object headers (1 header of  x2)
 *
 *
 * min size is 64
 * check whether this block can be split.
 */
static bool can_split_block(struct mem_block *block, size_t size)
{
    return !block->used && (block->size - size >= sizeof(struct mem_block));
}

/**
 * splits the block on two, returns the pointer to the smaller sub-block.
 */
static struct mem_block *split_block(struct mem_block *block, size_t size)
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
 * check whether this block can be merged together with the block->next.
 */
static bool can_coalesce_block(struct mem_block *block)
{
    return block->next && !block->next->used;
}

/**
 * merge block with the next one
 */
static struct mem_block *coalesce_block(struct mem_block *block)
{
    struct mem_block *coalsc_block = block->next;
    block->size = block->size + coalsc_block->size;
    block->next = coalsc_block->next;
    return block;
}

/**
 * allocates a block from the list, splitting if needed.
 */
static struct mem_block *allocate_block(struct mem_block *block, size_t size)
{
    if (!block)
        return NULL;

    if (can_split_block(block, size))
        block = split_block(block, size);

    block->used = true;
    block->size = size;

    return block;
}

/**
 * first-fit algorithm.
 *
 * returns the first free block which fits the size.
 */
static struct mem_block *first_fit_search(size_t size)
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

        // found the block:
        return block;
    }

    return NULL;
}

/**
 * next-fit algorithm.
 *
 * returns the next free block which fits the size.
 * updates the `searchStart` of success.
 */
static struct mem_block *next_fit_search(size_t size)
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

        // found the block
        search_start = block;
        return block;
    }

    return NULL;
}

/**
 * best-fit algorithm.
 *
 * returns a free block which size fits the best.
 */
static struct mem_block *best_fit_search(size_t size)
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
 * segregated fit algorithm.
 */
static struct mem_block *segregated_fit_search(size_t size)
{
    int bucket = get_bucket(size);
    struct mem_block *original_heap = heap_start;

    heap_start = buckets[bucket];
    struct mem_block *block = best_fit_search(size);

    heap_start = original_heap;
    return block;
}

/**
 * tries to find a block of a needed size.
 */
static struct mem_block *find_free_block(size_t size)
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
 * allocates a block of memory of (at least) `size` bytes.
 */
void *alloc(size_t size)
{
    size = align_mem(size);
    alloc_lock_acquire();

    // search for an available free block
    struct mem_block *free_block = find_free_block(size);
    if (free_block)
    {
        printf("alloc(size = %zu). found free block %p with size = %zu\n", size, free_block, free_block->size);
        alloc_lock_release();
        return free_block->data;
    }

    size_t total_size = alloc_size(size);
    printf("alloc(size = %zu). requesting from OS. total_size = %zu\n", size, total_size);

    struct mem_block *block = request_mem(total_size);
    block->size = size;
    block->used = true;

    if (smode == SEGREGATED_LIST)
    {
        // init segregated list
        int bucket = get_bucket(size);

        if (!buckets[bucket])
            buckets[bucket] = block;

        if (bucket_tops[bucket] != NULL)
            bucket_tops[bucket]->next = block;
        bucket_tops[bucket] = block;
    }
    else
    {
        // init heap.
        if (heap_start == NULL)
            heap_start = block;

        // chain the blocks.
        if (top != NULL)
            top->next = block;
        top = block;
    }

    printf("finished allocation. block: %p, userData: %p\n", top, block->data);
    alloc_lock_release();
    // user payload pointer
    return block->data;
}

/**
 * frees a block by ptr
 */
void dealloc(void *ptr)
{
    struct mem_block *block = get_header(ptr);

    alloc_lock_acquire();
    // excluding segregated list, cause after coalescence block will be in a wrong bucket
    if (can_coalesce_block(block) && smode != SEGREGATED_LIST)
        block = coalesce_block(block);

    block->used = false;
    alloc_lock_release();
}

/**
 * reset the heap to the original position.
 */
static void debug_reset_heap()
{
    if (heap_start == NULL)
    {
        return;
    }

    // Roll back to the beginning.
    // brk(heap_start);

    heap_start = NULL;
    top = NULL;
    search_start = NULL;
}

/**
 * initializes the heap, and the search mode.
 */
static void debug_init_heap(enum search_mode m)
{
    smode = m;
    debug_reset_heap();
}

static void debug_print_list()
{
    struct mem_block *ptr = heap_start;
    int cnt = 0;
    while (ptr != NULL)
    {
        printf("Block %d, "
               "headerAddr: %p, "
               "dataAddr: %p, "
               "dataSize: %zu, "
               "used: %s\n",
               cnt, ptr, ptr->data, ptr->size,
               ptr->used ? "true" : "false");
        ptr = ptr->next;
        cnt++;
    }
}

static void debug_print_buckets(size_t size)
{
    struct mem_block *ptr = buckets[get_bucket(size)];
    int cnt = 0;
    while (ptr != NULL)
    {
        printf("Block %d, "
               "headerAddr: %p, "
               "dataAddr: %p, "
               "dataSize: %zu, "
               "used: %s\n",
               cnt, ptr, ptr->data, ptr->size,
               ptr->used ? "true" : "false");
        ptr = ptr->next;
        cnt++;
    }
}

int main()
{

//    int i1 = get_bucket(5);
//    int i2 = get_bucket(8);
//    int i3 = get_bucket(15);
//    int i4 = get_bucket(16);
//    int i5 = get_bucket(32);


    char *p = (char *) alloc(4 * 1024 * 1024 - 56);

    *p = 'F';
    char *pp = (char *) alloc(8);
    *pp = 'D';

    printf("\n");






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
    debug_init_heap(NEXT_FIT);
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

    debug_init_heap(BEST_FIT);
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

    debug_init_heap(BEST_FIT);
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
    debug_init_heap(SEGREGATED_LIST);
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

