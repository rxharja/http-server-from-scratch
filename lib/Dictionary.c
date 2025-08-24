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

void dict_insert(Dictionary *d, const Key key, void * value) {
    const size_t index = key_hash(key) % BUCKET;
    const Kvp kvp = { key, value };
    Kvp_node_t * node = Kvp_node_init(kvp);
    // TODO: handle key collisions --- if keys match, overwrite original
    Kvp_node_head_insert(&d->bucket[index], node);
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

void free_dict(Dictionary* d) {
    if (!d) return;

    for (size_t i = 0; i < BUCKET; i++) {
        if (d->bucket[i] != NULL) {
            Kvp_node_free_all(d->bucket[i]);
            d->bucket[i] = NULL;
        }
    }

    free(d);
}

void* dict_find(const Dictionary *d, const Key key) {
    if (!d) return NULL;

    const size_t index = key_hash(key) % BUCKET;
    const Kvp_node_t * node = d->bucket[index];

    while (node != NULL) {
        if (node->value.key == key) return node->value.value;
    }

    return NULL;
}