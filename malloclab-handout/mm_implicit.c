/*
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

  /* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

//#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE		4		/* Word and hdr/ftr size (bytes) */
#define DSIZE		8		/* Double word size (bytes) */
#define CHUNKSIZE	(1<<12)	/* Extend by chunk (bytes) */
#define MIN_BSIZE	DSIZE	/* Minimum block size (bytes) */

#define MAX(x, y)	((x)>(y)? (x) : (y))

/* Pack size/alloc bit into a word */
#define PACK(size, alloc)	((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)			(*(unsigned int *)(p))
#define PUT(p, val)		(*(unsigned int *)(p) = (val))

/* Read size/alloc from hdr/ftr address p */
#define GET_SIZE(p)		(GET(p) & ~0x7)
#define GET_ALLOC(p)	(GET(p) & 0x1)

/* Given block ptr bp, compute address of its hdr/ftr */
#define HDRP(bp)		((char *)(bp) - WSIZE)
#define FTRP(bp)		((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define PREDP(bp)		((char *)(bp))
#define SUCCP(bp)		((char *)(bp) + WSIZE)
/* Given block ptr bp, compute address of next/prev block */
#define NEXT_BLKP(bp)		((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)		((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))
#define NEXT_FREEBLKP(bp)	((char *)SUCCP(bp))
#define PREV_FREEBLKP(bp)	((char *)PREDP(bp))

/* Points to the prologue block */
/* Optimize to point to the next block if you think you can */
static char *heap_listp;

/* Support functions */
static void *extend_heap(size_t words);
static void *find_fit(size_t bsize);
static void place(void *bp, size_t bsize);
static void *coalesce(void *bp);

/*
 * mm_init - initialize the explicit free list.
 */
int mm_init(void)
{
	/* Create the initial empty list */

	if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
		return -1;
	PUT(heap_listp, 0);
	PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));
	PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));
	PUT(heap_listp + (3 * WSIZE), PACK(0, 1));
	heap_listp += (2 * WSIZE);

	/* Initially it has a free block of CHUNKSIZE bytes */
	if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
		return -1;
	return 0;
}

void *mm_malloc(size_t size)
{
	size_t bsize;
	size_t extendsize;
	void *bp;

	if (size == 0) return NULL;

	bsize = ALIGN(size) + DSIZE;

	if ((bp = find_fit(bsize)) != NULL) {
		place(bp, bsize);
		return bp;
	}

	extendsize = MAX(bsize, CHUNKSIZE);
	if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
		return NULL;
	place(bp, bsize);
	return bp;
	
}

void mm_free(void *ptr)
{
	// maybe some safety code?
	void *bp;
	if ((bp = ptr) == NULL) return;
	size_t size = GET_SIZE(HDRP(bp));
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	coalesce(bp);
}

void *mm_realloc(void *ptr, size_t size)
{
	void *oldptr = ptr;
	void *newptr;
	size_t copysize;

	if ((newptr = mm_malloc(size)) == NULL)
		return NULL;

	copysize = GET_SIZE(HDRP(oldptr));
	if (size < copysize) copysize = size;
	memcpy(newptr, oldptr, copysize);
	mm_free(oldptr);
	return newptr;
}

/*
 * extend_heap - Extend the heap with a new free block
 */
static void *extend_heap(size_t words)
{
	char *bp;
	size_t size;

	/* Allocate an even number of words to maintain alignment */
	size = (words % 2) ? (words + 1)*WSIZE : words * WSIZE;
	if ((long)(bp = mem_sbrk(size)) == -1)
		return NULL;

	/* Initialize free block hdr/ftr and the epilogue hdr */
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

	/* Coalesce forward if possible */
	return coalesce(bp);
}

static void *find_fit(size_t bsize)
{
	// first-fit
	void *bp;
	for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
		if (!GET_ALLOC(HDRP(bp)) && bsize <= GET_SIZE(HDRP(bp)))
			return bp;
	}
	return NULL;
}

static void place(void *bp, size_t bsize)
{
	size_t initsize = GET_SIZE(HDRP(bp));

	if (initsize - bsize < 2*DSIZE) {
		// don't split, use the whole block
		PUT(HDRP(bp), PACK(initsize, 1));
		PUT(FTRP(bp), PACK(initsize, 1));
	}
	else {
		// split
		PUT(HDRP(bp), PACK(bsize, 1));
		PUT(FTRP(bp), PACK(bsize, 1));
		bp = NEXT_BLKP(bp);
		PUT(HDRP(bp), PACK(initsize - bsize, 0));
		PUT(FTRP(bp), PACK(initsize - bsize, 0));
	}
}

static void *coalesce(void *bp)
{
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

	if (!prev_alloc && !next_alloc) {
		/* both free */
		size += GET_SIZE(HDRP(PREV_BLKP(bp)))
			+ GET_SIZE(FTRP(NEXT_BLKP(bp)));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}
	else if (prev_alloc && !next_alloc) {
		/* next free */
		size += GET_SIZE(FTRP(NEXT_BLKP(bp)));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}
	else if (!prev_alloc && next_alloc) {
		/* prev free */
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		PUT(FTRP(bp), PACK(size, 0));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}
	return bp;
}