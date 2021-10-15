#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct CacheItem {
    char *tag;
    char *object;
    struct CacheItem *prev;
    struct CacheItem *next;
    size_t objectlen;
} CacheItem;

typedef struct CacheList {
    CacheItem *head;
    CacheItem *tail;
    size_t remainlen;
} CacheList;

void cache_init();
void cache_deinit();
void move_to_head(CacheItem *item);
void cache_add(char* url, char* object, int objectlen);
void cache_evict();
size_t cache_lookup(char* url, char* buf);

#endif