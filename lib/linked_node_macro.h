#pragma once

#include <stdlib.h>
#include <string.h>

#define linked_node_struct(T)\
    typedef struct T##_linked_node T##_node_t;\
    struct T##_linked_node {\
      T value;\
      T##_node_t *next;\
      T##_node_t *previous;\
    };

#define linked_node_init(T)\
    static inline T##_node_t* T##_node_init(const T value) {\
        T##_node_t *result = malloc(sizeof(T##_node_t));\
        result->value=value;\
        result->next=NULL;\
        result->previous=NULL;\
        return result;\
    }\

#define linked_node_head_insert(T)\
    static inline T##_node_t* T##_node_head_insert(T##_node_t** current_head, T##_node_t* node_to_insert) {\
        node_to_insert->next = *current_head;\
        if (*current_head != NULL) {\
            (*current_head)->previous = node_to_insert;\
        }\
        *current_head = node_to_insert;\
        return node_to_insert;\
    }\

#define linked_node_insert_after(T)\
    static inline T##_node_t* T##_node_insert_after(T##_node_t* node_to_insert_after, T##_node_t* new_node) {\
        if (node_to_insert_after == NULL) return NULL;\
        new_node->next = node_to_insert_after->next;\
        if (node_to_insert_after->next != NULL) {\
            node_to_insert_after->next->previous = new_node;\
        }\
        new_node->previous = node_to_insert_after;\
        node_to_insert_after->next=new_node;\
        return new_node;\
    }\

#define linked_node_find(T)\
   static inline T##_node_t* T##_find_node(T##_node_t* head, const T value, int (*cmp)(const T*, const T*)) {\
      T##_node_t* temporary = head;\
      while(temporary != NULL) {\
        if (cmp(&temporary->value, &value) == 0) return temporary;\
        temporary = temporary->next;\
      }\
      return NULL;\
   }\

#define linked_node_free_all(T)\
    static inline void T##_node_free_all(T##_node_t* head) {\
      T##_node_t* temporary = head;\
      while(temporary != NULL) {\
        T##_node_t* next = temporary->next;\
        free(temporary);\
        temporary = next;\
      }\
    }\

#define linked_node_print(T)\
    static inline void T##_print_nodes(T##_node_t *head, const char* format) {\
        T##_node_t *temporary = head;\
        while(temporary != NULL) {\
            printf(format,temporary->value);\
            temporary = temporary->next;\
        }\
        printf("End!\n");\
    }\

#define EXPAND(x) x
#define CONCAT(a, b) a##b
#define linked_node_map(T, U)\
    static inline U##_node_t* EXPAND(CONCAT(T##_to_, U##_node_map))(EXPAND(T##_node_t) *head, EXPAND(U) (*f)(const EXPAND(T))) {\
        if (head == NULL) return NULL;\
        U##_node_t *new_head = NULL;\
        U##_node_head_insert(&new_head, U##_node_init(f(head->value)));\
        T##_node_t *temporary = head->next;\
        U##_node_t *next = new_head;\
        while(temporary != NULL) {\
            U##_node_t* mapped_node = U##_node_init(f(temporary->value));\
            next = U##_node_insert_after(next, mapped_node);\
            temporary = temporary->next;\
        }\
        return new_head;\
    }\

#define linked_node(T)\
   linked_node_struct(T);\
   linked_node_init(T)\
   linked_node_head_insert(T)\
   linked_node_insert_after(T)\
   linked_node_find(T)\
   linked_node_free_all(T)\
   linked_node_print(T)
