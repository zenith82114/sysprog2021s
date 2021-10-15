//------------------------------------------------------------------------------
//
// memtrace
//
// trace calls to the dynamic memory manager
//
#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <memlog.h>
#include <memlist.h>

//
// function pointers to stdlib's memory management functions
//
static void *(*mallocp)(size_t size) = NULL;
static void (*freep)(void *ptr) = NULL;
static void *(*callocp)(size_t nmemb, size_t size);
static void *(*reallocp)(void *ptr, size_t size);

//
// statistics & other global variables
//
static unsigned long n_malloc  = 0;
static unsigned long n_calloc  = 0;
static unsigned long n_realloc = 0;
static unsigned long n_allocb  = 0;
static unsigned long n_freeb   = 0;
static item *list = NULL;
//
// init - this function is called once when the shared library is loaded
//
__attribute__((constructor))
void init(void)
{
  char *error;

  LOG_START();

  // initialize a new list to keep track of all memory (de-)allocations
  // (not needed for part 1)
  list = new_list();

  // ...
}

//
// fini - this function is called once when the shared library is unloaded
//
__attribute__((destructor))
void fini(void)
{
  // ...

  unsigned long alloc_total = n_allocb;
  unsigned long alloc_avg = alloc_total / (n_malloc + n_calloc + n_realloc);
  unsigned long free_total = n_freeb;

  LOG_STATISTICS(alloc_total, alloc_avg, free_total);

  if(list->next != NULL){
	
	item *cur = list;
	int firstblock = 1;

	while(cur->next != NULL){
		cur = cur->next;
		if(cur->cnt > 0){
			if(firstblock){
				LOG_NONFREED_START();
				firstblock = 0;
			}
			LOG_BLOCK(cur->ptr, cur->size, cur->cnt);
		}
	}
  }

  LOG_STOP();

  // free list (not needed for part 1)
  free_list(list);
}

void *malloc(size_t size)
{
	char *error;
	char *res;

	if(mallocp == NULL){
		mallocp = dlsym(RTLD_NEXT, "malloc");
		if( (error = dlerror()) != NULL){
			fputs(error, stderr);
			exit(1);
		}
	}

	res = mallocp(size);

	n_malloc++;
	n_allocb += size;
	
	if(res != NULL) alloc(list, res, size);

	LOG_MALLOC(size, res);

	return res;
}

void *calloc(size_t nmemb, size_t size)
{
	char *error;
	char *res;

	if(callocp == NULL){
		callocp = dlsym(RTLD_NEXT, "calloc");
		if( (error = dlerror()) != NULL){
			fputs(error, stderr);
			exit(1);
		}
	}

	res = callocp(nmemb, size);

	n_calloc++;
	n_allocb += nmemb * size;
	if(res != NULL) alloc(list, res, nmemb * size);
	
	LOG_CALLOC(nmemb, size, res);

	return res;
}

void *realloc(void *ptr, size_t size)
{
	char* error;
	char* res;
	item* i;

	if(reallocp == NULL){
		reallocp = dlsym(RTLD_NEXT, "realloc");
		if( (error = dlerror()) != NULL){
			fputs(error, stderr);
			exit(1);
		}
	}

	if(ptr == NULL){
		res = reallocp(NULL, size);
		LOG_REALLOC(ptr, size, res);

		n_allocb += size;
		if(size > 0){
			n_realloc++;
			if(res != NULL) alloc(list, res, size);
		}
	}
	else{
		i = find(list, ptr);

		if(i == NULL || i->cnt == 0){
			res = reallocp(NULL, size);
			LOG_REALLOC(ptr, size, res);

			if(i == NULL) LOG_ILL_FREE();
			else if(i->cnt == 0) LOG_DOUBLE_FREE();

			n_allocb += size;
			n_realloc++;
			if(res != NULL) alloc(list, res, size);

		} else if(size == 0){
			res = reallocp(ptr, 0);
			LOG_REALLOC(ptr, size, res);

			n_freeb += i->size;
			dealloc(list, ptr);

		} else if(size <= i->size){
			res = reallocp(ptr, size);
			LOG_REALLOC(ptr, size, res);

		} else if(size > i->size){
			res = reallocp(ptr, size);
			LOG_REALLOC(ptr, size, res);

			n_freeb += i->size;
			n_allocb += size;
			n_realloc++;
			dealloc(list, ptr);
			if(res != NULL) alloc(list, res, size);
		}
	}

	return res;
}

void free(void *ptr)
{
	char* error;
	item* i;

	LOG_FREE(ptr);

	if(freep == NULL){
		freep = dlsym(RTLD_NEXT, "free");
		if( (error = dlerror()) != NULL){
			fputs(error, stderr);
			exit(1);
		}
	}

	if(ptr == NULL) return;

	i = find(list, ptr);
	if(i == NULL){
		LOG_ILL_FREE();
		return;
	} else if(i->cnt == 0){
		LOG_DOUBLE_FREE();
		return;
	}

	dealloc(list, ptr);
	
	n_freeb += i->size;

	freep(ptr);
}
