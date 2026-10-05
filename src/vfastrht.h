#ifndef VFASTRHT_H_INCLUDED
#define VFASTRHT_H_INCLUDED

// vfastrht is a fast data structure like bsts or b-trees. it is faster than both, however, when considering massive amounts of entries.
// this is because it indexes into an array once per byte for at most however many bytes are in the key.
// search, insert & deletion of individual elements are all O(logn).
// the algorithm is most efficient for when all values inserted have large, random keys. 
// it is most inefficient when all keys are very similar except for a couple of bits towards the end of the key.

// configurable parameters are those that should be defined before the file to customise vfastrht.
//  VFASTRHT_KEY_UNDERLYING_TYPE    ~ type one wants the rht to use.
//  VFASTRHT_KEY_LENGTH             ~ length of key.

// stdlib includes.
#include "stddef.h"                     // for size_t, NULL.
#include "stdlib.h"                     // for calloc, free.
#include "string.h"                     // for memcmp.
#include "stdbool.h"                    // for boolean type.

#ifndef VFASTRHT_KEY_UNDERLYING_TYPE
# define VFASTRHT_KEY_UNDERLYING_TYPE int
#endif

#ifndef VFASTRHT_KEY_LENGTH
# define VFASTRHT_KEY_LENGTH sizeof(int)
#endif

typedef struct vfastrhtS vfastrhtT;
typedef VFASTRHT_KEY_UNDERLYING_TYPE vfastrht_keyT;

vfastrhtT* vfastrht_table_init();                                                                                 // initialises table.
void vfastrht_table_destroy(vfastrhtT* const restrict table);                                                     // destroys table.
bool vfastrht_del(vfastrhtT* restrict table, const vfastrht_keyT key);                            // deletes entry from table, returns true if successful.
void* vfastrht_del_elem(vfastrhtT* restrict table, const vfastrht_keyT key);                      // deletes entry from table, returns obj if successful.
bool vfastrht_put(vfastrhtT* restrict table, const vfastrht_keyT key, void* const restrict obj);  // puts entry in table, returns true if successful.
bool vfastrht_search(vfastrhtT* restrict table, const vfastrht_keyT key);                         // searches entry within table, returns true if successful.
void* vfastrht_search_elem(vfastrhtT* restrict table, const vfastrht_keyT key);                   // searches entry within table, returns obj if successful.

#endif

#ifdef VFASTRHT_IMPLEMENTATION

struct vfastrhtS{
    struct vfastrhtS* restrict under;
    vfastrht_keyT* restrict key;
    void* obj;
};

// 256 elements/table so that the key can be indexed as a byte array.
#define VFASTRHT_TABLE_SIZE 256

vfastrhtT* vfastrht_table_init(){
    return (vfastrhtT*)calloc(VFASTRHT_TABLE_SIZE, sizeof(vfastrhtT));
}

void vfastrht_table_destroy(vfastrhtT* const restrict table){
    for(size_t i = 0; i < VFASTRHT_TABLE_SIZE; i++){
        if(table[i].key) free(table[i].key);
        if(table[i].under) vfastrht_table_destroy(table[i].under);
    }
    free(table);
}

static size_t vfastrht__idx(const vfastrht_keyT key, unsigned short r){
    return ((unsigned char*)key)[r];
}

bool vfastrht_del(vfastrhtT* restrict table, const vfastrht_keyT key){
    unsigned short r = 0;       // why short? because we don't need int! really both short & int work.

    do{
        const size_t idx = vfastrht__idx(key, r);
        if(table[idx].key && !memcmp(table[idx].key, key, VFASTRHT_KEY_LENGTH)){
            free(table[idx].key);
            table[idx].key = NULL;
            return true;
        }
        if(!table[idx].under)
            return false;
        table = table[idx].under;

        r++;
    } while(r < VFASTRHT_KEY_LENGTH);

    return false;
}

void* vfastrht_del_elem(vfastrhtT* restrict table, const vfastrht_keyT key){
    unsigned short r = 0;       // why short? because we don't need int! really both short & int work.

    do{
        const size_t idx = vfastrht__idx(key, r);
        if(table[idx].key && !memcmp(table[idx].key, key, VFASTRHT_KEY_LENGTH)){
            free(table[idx].key);
            table[idx].key = NULL;
            return table[idx].obj;
        }
        if(!table[idx].under)
            return NULL;
        table = table[idx].under;

        r++;
    } while(r < VFASTRHT_KEY_LENGTH);
    return NULL;
}

bool vfastrht_put(vfastrhtT* restrict table, const vfastrht_keyT key, void* const restrict obj){
    unsigned short r = 0;
    vfastrhtT* lastfree = NULL;
// goes all the way down.
    do{
        const size_t idx = vfastrht__idx(key, r);
        if(!table[idx].key)
            lastfree = &table[idx];
        else if(!memcmp(table[idx].key, key, VFASTRHT_KEY_LENGTH))
            return false;

        if(!table[idx].under && !lastfree)
            table[idx].under = vfastrht_table_init();

        table = table[idx].under;
        r++;
    } while(r < VFASTRHT_KEY_LENGTH && table);

    if(!lastfree) return false;

    lastfree->key = malloc(VFASTRHT_KEY_LENGTH);
    memcpy(lastfree->key, key, VFASTRHT_KEY_LENGTH);
    lastfree->obj = obj;

    return true;
}

bool vfastrht_search(vfastrhtT* restrict table, const vfastrht_keyT key){
    unsigned short r = 0;

    do{
        const size_t idx = vfastrht__idx(key, r);
        if(table[idx].key && !memcmp(table[idx].key, key, VFASTRHT_KEY_LENGTH))
            return true;
        if(!table[idx].under)
            return false;
        table = table[idx].under;

        r++;
    } while(r < VFASTRHT_KEY_LENGTH);

    return false;
}

void* vfastrht_search_elem(vfastrhtT* restrict table, const vfastrht_keyT key){
    unsigned short r = 0;

    do{
        const size_t idx = vfastrht__idx(key, r);
        if(table[idx].key && !memcmp(table[idx].key, key, VFASTRHT_KEY_LENGTH))
            return table[idx].obj;
        if(!table[idx].under)
            return NULL;
        table = table[idx].under;

        r++;
    } while(r < VFASTRHT_KEY_LENGTH);

    return NULL;
}

#endif
