/*
  Dynamic memory allcator
	with explicit LIFO free list & enhanced realloc
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

#define WSIZE		4			/* Word and hdr/ftr size (bytes) */
#define DSIZE		8			/* Double word size (bytes) */
#define CHUNKSIZE	(1<<12)		/* Extend by chunk (bytes) */
#define MIN_BSIZE	2*DSIZE		/* Minimum block size (bytes) */

#define MAX(x, y)	((x)>(y)? (x) : (y))
#define MIN(x, y)	((x)>(y)? (y) : (x))

/* Pack size/alloc bit into a word */
#define PACK(size, alloc)	((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)			(*(unsigned int *)(p))
#define PUT(p, val)		(*(unsigned int *)(p) = (unsigned int)(val))

/* Read size/alloc from hdr/ftr address p */
#define GET_SIZE(p)		(GET(p) & ~0x7)
#define GET_ALLOC(p)	(GET(p) & 0x1)

/* Given block ptr bp, compute address of its hdr/ftr */
#define HDRP(bp)		((char *)(bp) - WSIZE)
#define FTRP(bp)		((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next/prev block */
#define NEXT_BLKP(bp)		((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)		((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Given free block ptr bp, compute address of its succ/pred field */
#define SUCCP(bp)		((char *)(bp))
#define PREDP(bp)		((char *)(bp) + WSIZE)

/* Given free block ptr bp, compute address of next/prev free block */
#define NEXT_FREEBLKP(bp)	((char *)(GET(SUCCP(bp))))
#define PREV_FREEBLKP(bp)	((char *)(GET(PREDP(bp))))

/* Support functions */
static void *extend_heap(size_t words);
static void *find_fit(size_t bsize);
static void place(void *bp, size_t bsize);
static void *coalesce(void *bp);
static void *set_root(void *bp);
static void cut_out(void *bp);

/* Checker function */
static void mm_check(void);

/* Pointer to prologue block (marked allocated, end of free list) */
static char *prol_bp;
/* Pointer to root (first block) of free list) */
static char *root_bp;

/*
  mm_init
	Set up the explicit free list.
 	A prologue block and a CHUNKSIZE byte free block are created;
	prol_bp and root_bp are set to point to them respectively.
 */
int mm_init(void)
{
	/* Set up the empty list */
	if ((prol_bp = mem_sbrk(6 * WSIZE)) == (void *)-1)
		return -1;
	PUT(prol_bp, 0);
	prol_bp += (2 * WSIZE);
	PUT(HDRP(prol_bp), PACK(MIN_BSIZE, 1));
	PUT(FTRP(prol_bp), PACK(MIN_BSIZE, 1));
	PUT(SUCCP(prol_bp), 0);
	PUT(PREDP(prol_bp), NEXT_BLKP(prol_bp));
	PUT(HDRP(NEXT_BLKP(prol_bp)), PACK(0, 1));
	root_bp = prol_bp;

	/* Initial free block of CHUNKSIZE bytes */
	if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
		return -1;
	return 0;
}
/*
  mm_malloc
	Calculate block size for the requested payload and allocate the new block
	on a free block, or on the extended heap area if failed.
*/
void *mm_malloc(size_t size)
{
	size_t bsize;
	size_t extendsize;
	void *bp;

	/* Ignore malloc(0) */
	if (size == 0) return NULL;

	/* Required block size including hdr/ftr/padding */
	bsize = ALIGN(size) + DSIZE;

	/* Find a spot for the new block */
	if ((bp = find_fit(bsize)) != NULL) {
		place(bp, bsize);
		return bp;
	}

	/* No spot found: extend heap */
	extendsize = MAX(bsize, CHUNKSIZE);
	if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
		return NULL;
	place(bp, bsize);
	return bp;
}
/*
  mm_free
	Mark off the alloc bits on hdr/ftr, coalesce the block if possible,
	and bring it to the start of the free list.
*/
void mm_free(void *ptr)
{
	if (ptr == NULL) return;
	size_t size = GET_SIZE(HDRP(ptr));
	PUT(HDRP(ptr), PACK(size, 0));
	PUT(FTRP(ptr), PACK(size, 0));
	set_root(coalesce(ptr));
}
/*
  mm_realloc
	See if the reallocation can be done within the old block
	or by absorbing a bordering free block if any.
	If not, allocate a new block, copy contents and free the old block.
*/
void *mm_realloc(void *ptr, size_t size)
{
	void *newptr;
	size_t oldbsize, newbsize, totalbsize,
			nextsize, prevsize, copysize;

	/* Exceptional cases */
	if (ptr == NULL) return mm_malloc(size);
	if (size == 0) {
		mm_free(ptr);
		return NULL;
	}

	oldbsize = GET_SIZE(HDRP(ptr));
	newbsize = ALIGN(size) + DSIZE;
	prevsize = GET_SIZE(FTRP(PREV_BLKP(ptr)));
	nextsize = GET_SIZE(FTRP(NEXT_BLKP(ptr)));

	if (oldbsize >= newbsize + MIN_BSIZE) {
		/* New block size is small enough to split the old block */
		PUT(HDRP(ptr), PACK(newbsize, 1));
		PUT(FTRP(ptr), PACK(newbsize, 1));
		PUT(HDRP(NEXT_BLKP(ptr)), PACK(oldbsize - newbsize, 0));
		PUT(FTRP(NEXT_BLKP(ptr)), PACK(oldbsize - newbsize, 0));

		/* The remainder free block is sent to the start of free list */
		set_root(coalesce(NEXT_BLKP(ptr)));
		return ptr;
	}
	if (oldbsize == newbsize) {
		/* New block size equals old; do nothing */
		return ptr;
	}
	if (!GET_ALLOC(FTRP(PREV_BLKP(ptr)))
		&& ((totalbsize = oldbsize + prevsize) >= newbsize + MIN_BSIZE)) {
		/* Prev block is free and large enough to fit new size if absorbed */
		newptr = PREV_BLKP(ptr);
		cut_out(newptr);
		/* Shift payload using memmove because new and old payloads may overlap */
		memmove(newptr, ptr, newbsize - DSIZE);
		PUT(HDRP(newptr), PACK(newbsize, 1));
		PUT(FTRP(newptr), PACK(newbsize, 1));
		PUT(HDRP(NEXT_BLKP(newptr)), PACK(totalbsize - newbsize, 0));
		PUT(FTRP(NEXT_BLKP(newptr)), PACK(totalbsize - newbsize, 0));
		set_root(coalesce(NEXT_BLKP(newptr)));
		return newptr;
	}
	if (!GET_ALLOC(HDRP(NEXT_BLKP(ptr)))
		&& (totalbsize = oldbsize + nextsize) >= newbsize + MIN_BSIZE) {
		/* Next block is free and large enough to fit new size if absorbed */
		cut_out(NEXT_BLKP(ptr));
		PUT(HDRP(ptr), PACK(newbsize, 1));
		PUT(FTRP(ptr), PACK(newbsize, 1));
		PUT(HDRP(NEXT_BLKP(ptr)), PACK(totalbsize - newbsize, 0));
		PUT(FTRP(NEXT_BLKP(ptr)), PACK(totalbsize - newbsize, 0));
		set_root(coalesce(NEXT_BLKP(ptr)));
		return ptr;
	}
	/* All above tests failed; make a new block */
	if ((newptr = mm_malloc(size)) == NULL)
		return NULL;
	copysize = MIN(oldbsize, newbsize) - DSIZE;
	memcpy(newptr, ptr, copysize);
	mm_free(ptr);
	return newptr;
}
/*
  extend_heap
	Extend heap by minimum alignment units (double-words)
	to accommodate requested number of words
	and return the start of the extended area.
 */
static void *extend_heap(size_t words)
{
	char *bp;
	size_t size;

	/* Allocate an even number of words to maintain alignment */
	size = (words % 2) ? (words + 1)*WSIZE : words*WSIZE;
	if ((long)(bp = mem_sbrk(size)) == -1)
		return NULL;

	/* Write free block hdr/ftr and the epilogue (dummy) hdr */
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

	/* Coalesce forward if possible */
	return set_root(coalesce(bp));
}
/*
  find_fit
	Iterate the free list and return the first free block
	large enough for allocating a new bsize-byte block, or NULL if none.
 */
static void *find_fit(size_t bsize)
{
	// first-fit
	void *bp;
	for (bp = root_bp; bp != prol_bp; bp = NEXT_FREEBLKP(bp)) {
		if (bsize <= GET_SIZE(HDRP(bp)))
			return bp;
	}
	return NULL;
}
/*
  place
	Allocate a new bsize-byte block on a free block bp.
	If block bp is split, the remainder block is sent to the start of free list.
 */
static void place(void *bp, size_t bsize)
{
	size_t initsize = GET_SIZE(HDRP(bp));

	cut_out(bp);
	if (initsize < bsize + MIN_BSIZE) {
		// don't split, use the whole block
		PUT(HDRP(bp), PACK(initsize, 1));
		PUT(FTRP(bp), PACK(initsize, 1));
	}
	else {
		// split, then set the remainder to root
		PUT(HDRP(bp), PACK(bsize, 1));
		PUT(FTRP(bp), PACK(bsize, 1));
		PUT(HDRP(NEXT_BLKP(bp)), PACK(initsize - bsize, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(initsize - bsize, 0));
		if (root_bp == bp) set_root(NEXT_BLKP(bp));
		else set_root(NEXT_BLKP(bp));
	}
}
/*
  coalesce
	Merge with the given free block bp
	if next and/or prev bordering block are/is also free. 
	and return the resulting free block.
 */
static void *coalesce(void *bp)
{
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

	if (prev_alloc && next_alloc) {
		/* neither is free; do nothing */
		return bp;
	}
	else if (prev_alloc && !next_alloc) {
		/* only next block is free; merge forward */
		cut_out(NEXT_BLKP(bp));
		size += GET_SIZE(FTRP(NEXT_BLKP(bp)));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}
	else if (!prev_alloc && next_alloc) {
		/* only prev block is free; merge backward */
		cut_out(PREV_BLKP(bp));
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		PUT(FTRP(bp), PACK(size, 0));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}
	else {
		/* both blocks are free; merge both */
		cut_out(PREV_BLKP(bp));
		cut_out(NEXT_BLKP(bp));
		size += GET_SIZE(HDRP(PREV_BLKP(bp)))
			+ GET_SIZE(FTRP(NEXT_BLKP(bp)));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}
	return bp;
}
/*
  set_root
	Connect a free block not in the free list to its root
	i.e., make it the new root of the list
 */
static void *set_root(void *bp)
{
	if (bp == root_bp) return bp;
	PUT(PREDP(root_bp), bp);
	PUT(SUCCP(bp), root_bp);
	PUT(PREDP(bp), 0);
	return (root_bp = bp);
}
/*
  cut_out
	Remove a block from the free list.
 */
static void cut_out(void *bp)
{
	if (bp == root_bp) {
		/* This is the root; its next block is the new root */
		root_bp = NEXT_FREEBLKP(bp);
		PUT(PREDP(root_bp), 0);
	}
	else {
		/* This is not the root; link next and prev free blocks to each other */
		PUT(SUCCP(PREV_FREEBLKP(bp)), GET(SUCCP(bp)));
		PUT(PREDP(NEXT_FREEBLKP(bp)), GET(PREDP(bp)));
	}
}
/*
  mm_check
	A check routine for debugging.
	Prints current heap range, heap status (address, size and status of all blocks)
	and the free list.
 */
static void mm_check(void)
{
	void *bp;

	printf("\n******* Heap Range *******\n");
	printf("[  %p  ~  %p  ]\n", mem_heap_lo(), mem_heap_hi());

	printf("******* Heap status *******\n");
	for (bp = prol_bp; GET_SIZE(HDRP(bp)); bp = NEXT_BLKP(bp)) {
		printf("(%p)[%zu]\t", bp, GET_SIZE(HDRP(bp)));
		if (GET_ALLOC(HDRP(bp))) printf("A\n");
		else printf("F\n");
	}

	printf("******* Free list *******\n");
	printf("[PROL] -> ");
	for (bp = PREV_FREEBLKP(prol_bp); bp; bp = PREV_FREEBLKP(bp)) {
		printf("(%p)[%zu] -> ", bp, GET_SIZE(HDRP(bp)));
	}
	printf("NULL\n\n");
}