/**
 *
 * @author Sanzhar Kudaibergen
 * @created 11/15/2020
 */

#include <assert.h> // for assert
#include <stdio.h> // for printf

#include "../src/allocatr.c"


int main(int argc, char** argv)
{
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
