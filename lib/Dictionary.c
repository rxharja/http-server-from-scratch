//
// Created by redonxharja on 8/23/25.
//
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "linked_node_macro.h"
#include "Dictionary.h"

uint64_t key_hash(const char *c) {
    uint64_t hash = 1469598103934665603ULL; // FNV offset basis
    while (*c) {
        hash ^= (unsigned char)*c++;
        hash *= 1099511628211ULL; // FNV prime
    }

    return hash;
}

int dict_insert(Dictionary* d, const Key key, void* value) {
    const size_t index = key_hash(key) % BUCKET;

    // check if the key exists
    Kvp_node_t * curr = d->bucket[index];
    while (curr != NULL) {
        if (strcmp(curr->value.key, key) == 0) {
           if (curr->value.value) free(curr->value.value);
           curr->value.value = value;
           return 1;
        }
        curr = curr->next;
    }

    // if it does not exist, it is a new key
    const Key persistent_key = strdup(key);
    const Kvp kvp = { persistent_key, value };
    Kvp_node_t * node = Kvp_node_init(kvp);

    const Kvp_node_t * n = Kvp_node_head_insert(&d->bucket[index], node);
    return n ? 1 : -1;
}

void print_dict(const Dictionary* d) {
    for (size_t i = 0; i < BUCKET; i++) {
        if (d->bucket[i] != NULL) {
            for (Kvp_node_t * node = d->bucket[i]; node != NULL; node = node->next) {
                printf("%s -> %s\n", node->value.key, (char*)node->value.value);
            }
        }
        else {
            printf("---\n");
        }
    }
}

Dictionary* dict_init(void) {
    Dictionary* d = malloc(sizeof(Dictionary));
    if (!d) return NULL;
    for (size_t i = 0; i < BUCKET; i++) {
        d->bucket[i] = NULL;
    }
    return d;
}

void free_dict(Dictionary* d, void (*destroy)(void*)) {
    if (!d) return;

    for (size_t i = 0; i < BUCKET; i++) {
        if (d->bucket[i] != NULL) {
            Kvp_node_free_all(d->bucket[i], destroy);
            d->bucket[i] = NULL;
        }
    }

    free(d);
}

void* dict_find(const Dictionary *d, const Key key) {
    if (!d || !key) return NULL;

    const size_t index = key_hash(key) % BUCKET;
    const Kvp_node_t * node = d->bucket[index];

    while (node != NULL) {
        if (strcmp(node->value.key, key) == 0) return node->value.value;
        node = node->next;
    }

    return NULL;
}

void free_kpv(void* p) {
    const Kvp* kvp = (Kvp*)p;

    // 1. Free the CachedFile (the void* value)
    if (kvp->value) free(kvp->value);

    // 2. Free the key if it's a heap-allocated string
    if (kvp->key) free((void*)kvp->key);
}