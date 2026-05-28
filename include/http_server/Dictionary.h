//
// Created by redonxharja on 8/24/25.
//

#ifndef HTTPSERVER_DICTIONARY_H_H
#define HTTPSERVER_DICTIONARY_H_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "linked_node_macro.h"

#define BUCKET 1024

typedef const char* Key;

typedef struct {
    Key key;
    void *value;
} Kvp;

linked_node(Kvp)

typedef struct {
    Kvp_node_t * bucket[BUCKET];
} Dictionary;

/**
 * @param c  NUL-terminated key
 * @return   64-bit FNV-style hash
 */
uint64_t key_hash(const char *c);

/**
 * @param d      target dictionary
 * @param key    NUL-terminated key (not copied; caller must keep it live)
 * @param value  value pointer to store under `key` (ownership rules are caller's choice)
 * @return       0 on success, non-zero on allocation failure
 */
int dict_insert(Dictionary* d, Key key, void* value);

/**
 * @param d  dictionary to print to stdout (debug aid)
 */
void dict_show(const Dictionary* d);

/**
 * @return  freshly-allocated empty dictionary; caller frees with dict_free()
 */
Dictionary* dict_init(void);

/**
 * @param d        dictionary to free
 * @param destroy  optional value-destructor invoked once per stored value; may be NULL
 */
void dict_free(Dictionary* d, void (*destroy)(void*));

/**
 * @param d    dictionary to search
 * @param key  NUL-terminated lookup key
 * @return     stored value pointer, or NULL if `key` is absent
 */
void* dict_find(const Dictionary *d, Key key);

/**
 * Destructor compatible with dict_free()'s `destroy` parameter: frees a Kvp's value.
 *
 * @param p  pointer to a Kvp's value slot
 */
void kvp_free(void* p);

#endif //HTTPSERVER_DICTIONARY_H_H