//
// Created by redonxharja on 8/24/25.
//

#ifndef HTTPSERVER_DICTIONARY_H_H
#define HTTPSERVER_DICTIONARY_H_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "linked_node_macro.h"

#define BUCKET 32

typedef const char* Key;

typedef struct {
    Key key;
    void *value;
} Kvp;

linked_node(Kvp)

typedef struct {
    Kvp_node_t * bucket[BUCKET];
} Dictionary;

uint64_t key_hash(const char *c);

void dict_insert(Dictionary *d, Key key, void * value);

void print_dict(const Dictionary* d);

Dictionary* dict_init(void);

void free_dict(Dictionary* d);

void* dict_find(const Dictionary *d, Key key);


#endif //HTTPSERVER_DICTIONARY_H_H