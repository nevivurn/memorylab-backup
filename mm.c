/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc isn't implemented.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
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
 */
int mm_init(range_t **ranges)
{
    /* Initialize heap */
    size_t *heap = mem_sbrk(3*SIZE_T_SIZE);
    if (heap == (void *) -1)
        return -1;
    heap++;

    HEAD_SET(heap, 2*SIZE_T_SIZE, 1);
    heap[1] = NULL;
    heap[2] = 0;
    HEAD_SET(&heap[3], 2*SIZE_T_SIZE, 1);
    HEAD_SET(&heap[4], 0, 1);

    /* DON'T MODIFY THIS STAGE AND LEAVE IT AS IT WAS */
    gl_ranges = ranges;

    return 0;
}

/*
 * Allocates a brand-new portion of the heap, obtained through a call to
 * mem_sbrk().
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
 * Adds a new free block to the free list.
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
 *  mm_malloc - Allocate a block by incrementing the brk pointer (example).
 *  Always allocate a block whose size is a multiple of the alignment.-
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

    // scan free list
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

    // reset known free size if necessary
    if (cursz == heap[1])
        heap[1] = 0;

    // remove from free list
    mm_malloc_rm_free(cur_head);

    // splitting logic
    if (cursz - reqsz >= 16) {
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
 *  mm_free - Frees a block. Does nothing (example)
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
    if (heap[1] && heap[1] < freesz)
        heap[1] = freesz;

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
 *  mm_exit - finalize the malloc package.
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
