/*
 * mm.c - an explicit free list allocator.
 *
 * This is mostly a traditional explicit free list allocator, where we keep
 * track of a linked list containing only the free blocks.
 *
 * Eack block is as follows:
 * struct block {
 *     size_t header;
 *     block *prev;
 *     block *next;
 *     char payload[N];
 *     size_t footer;
 * }
 * where N is the size of the payload.
 * The header and footer both contain the same data, the size of the block
 * (including both header and footer) in bytes, and the allocated flag at its
 * least-significant bit.
 *
 * The heap itself is 8-byte aligned, so the footer of the previous block will
 * share a double-word with the header of the following word.
 *
 * The first block (the "dummy" block) and the last block (the "end" block) have
 * a slightly different structure and usage.
 *
 * The first block is structured just like a regular block, but because its
 * "prev" field will always be NULL (and is never accessed), it can be used for
 * other uses, and in this case, it was used for the "short-circuit" mechanism.
 *
 * The last block is designed to mark the end of the heap (so if a heap scan
 * reaches this block, it should grow the heap to fit in the new block). It only
 * needs to have a header, of a special size 0 and marked as allocated.
 *
 * The malloc() function acts as usual in an explicit free list allocator,
 * choosing first-fit block to insert and splitting a block if and only if the
 * split size produces two blocks that are larger than the minimum size of a
 * block (which happens to be 24 bytes, the size of teh above block struct
 * rounded up to a multiple of 8).
 *
 * The "short-circuit" mechanism mentioned above works as follows: because the
 * slowest case in an explicit free list allocator is when a full traversal of
 * the free list is done, due to none of the blocks being large enough to fit
 * the new incoming block. In order to prevent a full traversal, we keep track
 * of the smallest block size that was unable to be allocated, and skip the heap
 * traversal in its entirety when the incoming block has a block size larger
 * than this threshold. This threshold is adjusted downwards when a small block
 * fails to be allocated using the existing heap, and is adjusted upwards when a
 * sufficiently large block is free()'d.
 * This mechanism degrades to a regular explicit free list behavior when the
 * allocations fail inside the heap in reverse size order.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/**********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please  *
 * provide  your  information  in the  following  struct. *
 **********************************************************/
team_t team = {
    /* Your full name */
    "Yongun Seong",
    /* Your student ID */
    "2017-19937"
};

/* DON'T MODIFY THIS VALUE AND LEAVE IT AS IT WAS */
static range_t **gl_ranges;

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/*
 * remove_range - manipulate range lists
 * DON'T MODIFY THIS FUNCTION AND LEAVE IT AS IT WAS
 */
static void remove_range(range_t **ranges, char *lo)
{
    range_t *p;
    range_t **prevpp = ranges;

    if (!ranges)
      return;

    for (p = *ranges;  p != NULL; p = p->next) {
      if (p->lo == lo) {
        *prevpp = p->next;
        free(p);
        break;
      }
      prevpp = &(p->next);
    }
}

#define HEAD_SIZE_MASK (~0x7)
#define HEAD_ALLOC_MASK (0x1)
#define HEAD_DATA(head) (*(size_t *)(head))
#define HEAD_SIZE(head) (HEAD_DATA(head) & HEAD_SIZE_MASK)
#define HEAD_ALLOC(head) (HEAD_DATA(head) & HEAD_ALLOC_MASK)
#define HEAD_SET(head, size, alloc) (HEAD_DATA(head) = (size & HEAD_SIZE_MASK) | (alloc & HEAD_ALLOC_MASK))

/*
 *  mm_init - initialize the malloc package.
 *  it initializes the heap with a dummy prologue and epilogue. The prologue
 *  is a block that seems to be 24 bytes long, that holds the short-circuit
 *  threshold in the "prev" field ([1]) and the beginning of the free list in
 *  the "next" field ([2]). The prologue holds a dummy "allocated" block of zie
 *  0, to mark the end of the heap.
 */
int mm_init(range_t **ranges)
{
    /* Initialize heap */
    size_t *heap = mem_sbrk(3*SIZE_T_SIZE);
    if (heap == (void *) -1)
        return -1;
    heap++;

    HEAD_SET(heap, 2*SIZE_T_SIZE, 1);
    heap[1] = 0;
    heap[2] = NULL;
    HEAD_SET(&heap[3], 2*SIZE_T_SIZE, 1);
    HEAD_SET(&heap[4], 0, 1);

    /* DON'T MODIFY THIS STAGE AND LEAVE IT AS IT WAS */
    gl_ranges = ranges;

    return 0;
}

/*
 * mm_malloc_new - allocate a brand-new portion of the heap.
 * Allocates a new portion of the heap, obtained through a call to mem_sbrk().
 * It then updates the former epilogue to be the new header, and adds a footer
 * as well as the new epilogue.
 */
static void *mm_malloc_new(size_t reqsz) {
    size_t *new_area = mem_sbrk(reqsz);
    if (new_area == (void *)-1)
        return NULL;

    // cur block header
    HEAD_SET(&new_area[-1], reqsz, 1);
    // cur block footer
    HEAD_SET(&new_area[reqsz/sizeof(size_t)-2], reqsz, 1);
    // next block header
    HEAD_SET(&new_area[reqsz/sizeof(size_t)-1], 0, 1);

    return (void *)new_area;
}

/*
 * mm_malloc_new_free - add a new free block to the free list.
 * This inserts the new block at the beginning of the free list, by setting its
 * "prev" field to the first block in the heap and the "next" node to the former
 * first block. It also updates the former first block's "next" field if it
 * if it exists.
 */
static void mm_malloc_new_free(size_t *heap, size_t *block) {
    if (heap[2] != NULL) {
        size_t *first = (size_t *)heap[2];
        first[1] = block;
    }
    block[1] = heap;
    block[2] = heap[2];
    heap[2] = block;
}

/*
 * mm_malloc_rm_free - remove a block from the free list.
 * Removes the block by setting the "next" field of the block before this (which
 * is guaranteed to never be null) to its own "next", and vice versa.
 */
static void mm_malloc_rm_free(size_t *block) {
    // prev is never null
    size_t *prev = (size_t *)block[1];
    prev[2] = block[2];

    if (block[2] != NULL) {
        size_t *next = (size_t *)block[2];
        next[1] = block[1];
    }
}
/*
 * mm_malloc - allocate a block.
 * First check the short-circuit threshold, and allocate a new block with
 * mm_malloc_new if this block cannot be inserted in the current heap.
 * Otherwise, traverse the free list to see if there is any block where we can
 * insert this new block, and insert it as needed. If this block could not be
 * inserted, adjust the threshold downwards (as appropriate) and use
 * mm_malloc_new.
 */
void *mm_malloc(size_t size)
{
    if (size == 0)
        return NULL;

    size_t reqsz = ALIGN(size) + SIZE_T_SIZE;
    size_t *heap = mem_heap_lo();
    heap++;

    // short-circuit large blocks
    if (heap[1] && reqsz >= heap[1])
        return mm_malloc_new(reqsz);

    // scan free list, first fit
    size_t *cur_head = (size_t *)heap[2], cursz;
    while (cur_head != NULL) {
        if (!HEAD_ALLOC(cur_head) && (cursz = HEAD_SIZE(cur_head)) >= reqsz)
            break;
        cur_head = (size_t *)cur_head[2];
    }

    // no appropriate block found
    if (cur_head == NULL) {
        // update short-circuit
        if (!heap[1] || heap[1] > reqsz)
            heap[1] = reqsz;
        return mm_malloc_new(reqsz);
    }
    // else, reuse existing block at cur_head

    // remove from free list
    mm_malloc_rm_free(cur_head);

    // splitting logic, only split if the other part is large enough
    if (cursz - reqsz >= 24) {
        size_t restsz = cursz - reqsz;

        HEAD_SET(&cur_head[reqsz/sizeof(size_t)], restsz, 0);
        HEAD_SET(&cur_head[cursz/sizeof(size_t)-1], restsz, 0);

        mm_malloc_new_free(heap, &cur_head[reqsz/sizeof(size_t)]);
        cursz = reqsz;
    }

    // set cur header
    HEAD_SET(cur_head, cursz, 1);
    // set cur footer
    HEAD_SET(&cur_head[cursz/sizeof(size_t)-1], cursz, 1);

    return (void *)(cur_head+1);
}

/*
 * mm_free - free a block.
 * First adjust the free beginning and end by coalescing with its neighboring
 * blocks, then adjusts the free list as appropriate. We also upwardly adjust
 * the short-circuit threshold if needed.
 */
void mm_free(void *ptr)
{
    size_t *block = ((size_t *)ptr)-1;

    // error on double-free
    if (!HEAD_ALLOC(block)) {
        fprintf(stderr, "double-free detected\n");
        exit(1);
    }

    size_t *start = block, *end = &block[HEAD_SIZE(block)/sizeof(size_t)];

    if (!HEAD_ALLOC(&start[-1])) {
        start -= HEAD_SIZE(&start[-1])/sizeof(size_t);
        mm_malloc_rm_free(start);
    }

    if (!HEAD_ALLOC(end)) {
        mm_malloc_rm_free(end);
        end += HEAD_SIZE(end)/sizeof(size_t);
    }

    size_t freesz = (end-start)*sizeof(size_t);
    HEAD_SET(start, freesz, 0);
    HEAD_SET(end-1, freesz, 0);

    size_t *heap = mem_heap_lo();
    heap++;

    mm_malloc_new_free(heap, start);
    // update short-circuit upper bound if needed
    if (heap[1] && heap[1] < freesz+1)
        heap[1] = freesz+1;

    /* DON'T MODIFY THIS STAGE AND LEAVE IT AS IT WAS */
    if (gl_ranges)
      remove_range(gl_ranges, ptr);
}

/*
 *  mm_realloc - empty implementation; YOU DO NOT NEED TO IMPLEMENT THIS
 */
void *mm_realloc(void *ptr, size_t t)
{
    return NULL;
}

/*
 *  mm_exit - free all blocks by traversing the entire heap.
 */
void mm_exit(void)
{
    size_t *heap = mem_heap_lo(), cursz;
    heap++;

    heap = &heap[HEAD_SIZE(heap)/sizeof(size_t)];
    while ((cursz = HEAD_SIZE(heap)) != 0) {
        if (HEAD_ALLOC(heap))
            mm_free(heap+1);
        heap = &heap[cursz/sizeof(size_t)];
    }
}

// vim: ts=4 sts=4 sw=4 et
