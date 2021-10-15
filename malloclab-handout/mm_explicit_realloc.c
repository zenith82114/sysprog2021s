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
#define MIN_BSIZE	2*DSIZE	/* Minimum block size (bytes) */

#define MAX(x, y)	((x)>(y)? (x) : (y))
#define MIN(x, y)	((x)>(y)? (y) : (x))
#define DIFF(x, y)	(MAX(x, y) - MIN(x, y))

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
#define SUCCP(bp)		((char *)(bp))
#define PREDP(bp)		((char *)(bp) + WSIZE)

/* Given block ptr bp, compute address of next/prev block */
#define NEXT_BLKP(bp)		((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)		((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))
#define NEXT_FREEBLKP(bp)	((char *)(GET(SUCCP(bp))))
#define PREV_FREEBLKP(bp)	((char *)(GET(PREDP(bp))))

/* Support functions */
static void *extend_heap(size_t words);
static void *find_fit(size_t bsize);
static void place(void *bp, size_t bsize);
static void *coalesce(void *bp);
static void *set_root(void *bp);
static void cut_out(void *bp);
#ifdef DEBUG
static void mm_check(void);
#endif
/* Points to the prologue block */
static char *prol_bp;
static char *root_bp;

/*
 * mm_init - initialize the explicit free list.
 */
int mm_init(void)
{
	/* Create the initial empty list */
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

#ifdef DEBUG
	printf("prol_bp: %p\n", prol_bp);
#endif
	
	/* Initially it has a free block of CHUNKSIZE bytes */
	if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
		return -1;
#ifdef DEBUG
	printf("init done\n");
	mm_check();
#endif
	return 0;
}

void *mm_malloc(size_t size)
{
#ifdef DEBUG
	printf("malloc(%zu)\n", size);
#endif
	size_t bsize;
	size_t extendsize;
	void *bp;

	if (size == 0) return NULL;

	bsize = ALIGN(size) + DSIZE;

	if ((bp = find_fit(bsize)) != NULL) {
		place(bp, bsize);
#ifdef DEBUG
		printf("malloc done\n");
		mm_check();
#endif
		return bp;
	}

	extendsize = MAX(bsize, CHUNKSIZE);
	if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
		return NULL;
	place(bp, bsize);
#ifdef DEBUG
	printf("malloc done\n");
	mm_check();
#endif
	return bp;
}

void mm_free(void *ptr)
{
#ifdef DEBUG
	printf("free(%p)\n", ptr);
#endif
	if (ptr == NULL) return;
	size_t size = GET_SIZE(HDRP(ptr));
	PUT(HDRP(ptr), PACK(size, 0));
	PUT(FTRP(ptr), PACK(size, 0));
	set_root(coalesce(ptr));
#ifdef DEBUG
	printf("free done\n");
	mm_check();
#endif
}

void *mm_realloc(void *ptr, size_t size)
{
#ifdef DEBUG
	printf("realloc(%p, %zu)\n", ptr, size);
#endif
	void *newptr;
	size_t oldbsize, newbsize, totalbsize,
			nextsize, prevsize, copysize;

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
#ifdef DEBUG
		printf("shrink\n");
#endif
		PUT(HDRP(ptr), PACK(newbsize, 1));
		PUT(FTRP(ptr), PACK(newbsize, 1));
		PUT(HDRP(NEXT_BLKP(ptr)), PACK(oldbsize - newbsize, 0));
		PUT(FTRP(NEXT_BLKP(ptr)), PACK(oldbsize - newbsize, 0));
		set_root(coalesce(NEXT_BLKP(ptr)));
		return ptr;
	}
	if (oldbsize == newbsize) {
#ifdef DEBUG
		printf("do nothing\n");
#endif
		return ptr;
	}
	if (!GET_ALLOC(FTRP(PREV_BLKP(ptr)))
		&& ((totalbsize = oldbsize + prevsize) >= newbsize + MIN_BSIZE)) {
#ifdef DEBUG
		printf("prev join\n");
#endif
		newptr = PREV_BLKP(ptr);
		cut_out(newptr);
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
#ifdef DEBUG
		printf("next join\n");
#endif
		cut_out(NEXT_BLKP(ptr));
		PUT(HDRP(ptr), PACK(newbsize, 1));
		PUT(FTRP(ptr), PACK(newbsize, 1));
		PUT(HDRP(NEXT_BLKP(ptr)), PACK(totalbsize - newbsize, 0));
		PUT(FTRP(NEXT_BLKP(ptr)), PACK(totalbsize - newbsize, 0));
		set_root(coalesce(NEXT_BLKP(ptr)));
		return ptr;
	}
#ifdef DEBUG
	printf("find new fit\n");
#endif
	if ((newptr = mm_malloc(size)) == NULL)
		return NULL;
	copysize = MIN(oldbsize, newbsize) - DSIZE;
	memcpy(newptr, ptr, copysize);
	mm_free(ptr);
#ifdef DEBUG
	printf("realloc done\n");
#endif
	return newptr;
}

/*
 * extend_heap - Extend the heap with a new free block
 */
static void *extend_heap(size_t words)
{
#ifdef DEBUG
	printf("extend heap(%zu)\n", words);
#endif
	char *bp;
	size_t size;

	/* Allocate an even number of words to maintain alignment */
	size = (words % 2) ? (words + 1)*WSIZE : words*WSIZE;
	if ((long)(bp = mem_sbrk(size)) == -1)
		return NULL;

	/* Initialize free block hdr/ftr and the epilogue hdr */
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

	/* Coalesce forward if possible */
	return set_root(coalesce(bp));
}

static void *find_fit(size_t bsize)
{
#ifdef DEBUG
	printf("finding fit (%zu)\n", bsize);
#endif
	// first-fit
	void *bp;
	for (bp = root_bp; bp != prol_bp; bp = NEXT_FREEBLKP(bp)) {
#ifdef DEBUG
		printf("looking at %p...\n", bp);
#endif
		if (bsize <= GET_SIZE(HDRP(bp))) {
#ifdef DEBUG
			printf("found fit at %p\n", bp);
#endif
			return bp;
		}
	}
#ifdef DEBUG
	printf("fit not found\n");
#endif
	return NULL;
}

static void place(void *bp, size_t bsize)
{
#ifdef DEBUG
	printf("place %zu at %p\n", bsize, bp);
#endif
	size_t initsize = GET_SIZE(HDRP(bp));

	cut_out(bp);
	if (initsize - bsize < MIN_BSIZE) {
		// don't split, use the whole block
#ifdef DEBUG
		printf("no split\n");
#endif
		PUT(HDRP(bp), PACK(initsize, 1));
		PUT(FTRP(bp), PACK(initsize, 1));
	}
	else {
		// split, then set the remainder to root
#ifdef DEBUG
		printf("split\n");
#endif
		PUT(HDRP(bp), PACK(bsize, 1));
		PUT(FTRP(bp), PACK(bsize, 1));
		PUT(HDRP(NEXT_BLKP(bp)), PACK(initsize - bsize, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(initsize - bsize, 0));
		if (root_bp == bp) set_root(NEXT_BLKP(bp));
		else set_root(NEXT_BLKP(bp));
	}
#ifdef DEBUG
	printf("place done\n");
	mm_check();
#endif
}

static void *coalesce(void *bp)
{
#ifdef DEBUG
	printf("coalesce(%p)\n", bp);
#endif
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

	if (prev_alloc && next_alloc) {
		/* neither free */
#ifdef DEBUG
		printf("neither free\n");
#endif
		return bp;
	}
	else if (prev_alloc && !next_alloc) {
		/* next free */
#ifdef DEBUG
		printf("next free\n");
#endif
		cut_out(NEXT_BLKP(bp));
		size += GET_SIZE(FTRP(NEXT_BLKP(bp)));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}
	else if (!prev_alloc && next_alloc) {
		/* prev free */
#ifdef DEBUG
		printf("prev free\n");
#endif
		cut_out(PREV_BLKP(bp));
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		PUT(FTRP(bp), PACK(size, 0));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}
	else {
		/* both free */
#ifdef DEBUG
		printf("both free\n");
#endif
		cut_out(PREV_BLKP(bp));
		cut_out(NEXT_BLKP(bp));
		size += GET_SIZE(HDRP(PREV_BLKP(bp)))
			+ GET_SIZE(FTRP(NEXT_BLKP(bp)));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}
#ifdef DEBUG
	printf("coalesce done (%p)\n", bp);
#endif
	return bp;
}

static void *set_root(void *bp)
{
#ifdef DEBUG
	printf("set root (%p)[%zu]\n", bp, GET_SIZE(HDRP(bp)));
#endif
	if (bp == root_bp) return bp;
	PUT(PREDP(root_bp), bp);
	PUT(SUCCP(bp), root_bp);
	PUT(PREDP(bp), 0);
#ifdef DEBUG
	printf("set root done\n");
	mm_check();
#endif
	return (root_bp = bp);
}

static void cut_out(void *bp)
{
	if (bp == root_bp) {
#ifdef DEBUG
		printf("cut out %p(root)\n", bp);
#endif
		root_bp = NEXT_FREEBLKP(bp);
		PUT(PREDP(root_bp), 0);
	}
	else {
#ifdef DEBUG
		printf("cut out %p\n", bp);
#endif
		PUT(SUCCP(PREV_FREEBLKP(bp)), GET(SUCCP(bp)));
		PUT(PREDP(NEXT_FREEBLKP(bp)), GET(PREDP(bp)));
	}
#ifdef DEBUG
	printf("cut out done\n");
	mm_check();
#endif
}
#ifdef DEBUG
static void mm_check(void)
{
	void *bp;

	printf("\n******* Free list *******\n");
	printf("[PROL] -> ");
	for (bp = PREV_FREEBLKP(prol_bp); bp; bp = PREV_FREEBLKP(bp)) {
		printf("(%p)[%zu] -> ", bp, GET_SIZE(HDRP(bp)));
	}
	printf("NULL\n");

	printf("******* Heap list *******\n");
	for (bp = prol_bp; GET_SIZE(HDRP(bp)); bp = NEXT_BLKP(bp)) {
		printf("(%p)[%zu]\t", bp, GET_SIZE(HDRP(bp)));
		if (GET_ALLOC(HDRP(bp))) printf("A\n");
		else printf("F\n");
	}
	printf("\n");
}
#endif