#include "cache.h"

CacheList *list;
pthread_rwlock_t lock;

void cache_init()
{
    list = (CacheList *)Malloc(sizeof(CacheList));
    list->head = list->tail = NULL;
    list->remainlen = MAX_CACHE_SIZE;
    pthread_rwlock_init(&lock, NULL);
}

void cache_deinit()
{
    // printf("********deinit\n");
    pthread_rwlock_wrlock(&lock);
    CacheItem *item2;
    for(CacheItem *item = list->head; item; ){
        free(item->tag);
        free(item->object);
        item2 = item;
        item = item->next;
        free(item2);
    }
    free(list);
    pthread_rwlock_unlock(&lock);
    pthread_rwlock_destroy(&lock);
}

void move_to_head(CacheItem *item)
{
    // printf("move to head\n");
    if(list->head == item)
        return;
    if(!list->head)
        list->head = list->tail = item;
    else{
        if(list->tail == item) list->tail = item->prev;
        if(item->prev) item->prev->next = item->next;
        if(item->next) item->next->prev = item->prev;

        item->prev = NULL;
        item->next = list->head;
        list->head = item;
    }
}

void cache_add(char* url, char* object, int objectlen)
{
    // printf("add\n");
    pthread_rwlock_wrlock(&lock);
    while(list->remainlen < objectlen)
        cache_evict();

    CacheItem *item = (CacheItem *)Malloc(sizeof(CacheItem));
    size_t len = strlen(url);
    item->tag = (char *)Malloc(len+1);
    strcpy(item->tag, url);
    item->object = (char *)Malloc(objectlen+1);
    strcpy(item->object, object);
    item->prev = item->next = NULL;
    item->objectlen = objectlen;

    list->remainlen -= objectlen;
    move_to_head(item);
    pthread_rwlock_unlock(&lock);
}

void cache_evict()
{
    // printf("evict\n");
    CacheItem *temp = list->tail;
    list->tail = temp->prev;
    list->remainlen += temp->objectlen;

    free(temp->tag);
    free(temp->object);
    free(temp);
}

size_t cache_lookup(char* url, char* buf)
{
    // printf("lookup\n");
    CacheItem *item;
    size_t len;
    pthread_rwlock_rdlock(&lock);
    for(item = list->head; item; item = item->next){
        if(!strcmp(item->tag, url)) break;
    }
    if(!item){
        pthread_rwlock_unlock(&lock);
        return 0;
    }
    len = item->objectlen;
    memcpy(buf, item->object, len);
    pthread_rwlock_unlock(&lock);
    pthread_rwlock_wrlock(&lock);
    move_to_head(item);
    pthread_rwlock_unlock(&lock);
    return len;
}