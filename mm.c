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
    size_t *heap = mem_sbrk(2*SIZE_T_SIZE);
    if (heap == (void *) -1)
        return -1;

    HEAD_SET(heap+1, SIZE_T_SIZE, 1);
    HEAD_SET(heap+2, SIZE_T_SIZE, 1);
    HEAD_SET(heap+3, 0, 1);

    /* DON'T MODIFY THIS STAGE AND LEAVE IT AS IT WAS */
    gl_ranges = ranges;

    return 0;
}

/*
 *  mm_malloc - Allocate a block by incrementing the brk pointer (example).
 *  Always allocate a block whose size is a multiple of the alignment.-
 */
void *mm_malloc(size_t size)
{
    if (size == 0)
        return NULL;

    size_t reqsz = ALIGN(size) + SIZE_T_SIZE, cursz;
    size_t *heap = mem_heap_lo();
    heap++;

    while ((cursz = HEAD_SIZE(heap)) < reqsz || HEAD_ALLOC(heap)) {
        if (cursz == 0)
            break;
        heap += cursz/sizeof(size_t);
    }

    if (cursz >= reqsz) {
        // allocate into existing blocks
        HEAD_SET(heap, cursz, 1);
        HEAD_SET(heap + cursz/sizeof(size_t) - 1, cursz, 1);
        return (void *)(heap+1);
    } else {
        // allocate new memory
        void *new_area = mem_sbrk(reqsz);
        if (new_area == (void *)-1)
            return NULL;

        HEAD_SET(heap, reqsz, 1);
        HEAD_SET(heap + reqsz/sizeof(size_t) - 1, reqsz, 1);
        HEAD_SET(heap + reqsz/sizeof(size_t), 0, 1);
        return (void *) new_area;
    }
}

/*
 *  mm_free - Frees a block. Does nothing (example)
 */
void mm_free(void *ptr)
{
    size_t *heap = ptr;
    heap--;

    size_t cursz = HEAD_SIZE(heap);
    HEAD_SET(heap, cursz, 0);
    HEAD_SET(heap + cursz/sizeof(size_t) -1, cursz, 0);

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
    size_t *heap = mem_heap_lo();
    heap++;

    heap += HEAD_SIZE(heap)/sizeof(size_t);

    size_t cursz;
    while ((cursz = HEAD_SIZE(heap)) != 0) {
        if (HEAD_ALLOC(heap))
            mm_free(heap+1);
        heap += cursz/sizeof(size_t);
    }
}

// vim: ts=4 sts=4 sw=4 et
