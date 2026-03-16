#ifndef ATTR_UTILS_H
#define ATTR_UTILS_H

#include "operator.h"
#include <string.h>

// Find an integer attribute by name
static inline int find_int_attr(const attribute_t attrs[], int num_attrs,
                                 const char *name, int64_t *out) {
    for (int i = 0; i < num_attrs; i++) {
        if (strcmp(attrs[i].name, name) == 0 && attrs[i].type == ATTR_INT) {
            *out = attrs[i].value.i;
            return 1;
        }
    }
    return 0;
}

// Find a list of integers attribute by name
static inline int find_ints_attr(const attribute_t attrs[], int num_attrs,
                                  const char *name, const int64_t **data, uint32_t *count) {
    for (int i = 0; i < num_attrs; i++) {
        if (strcmp(attrs[i].name, name) == 0 && attrs[i].type == ATTR_INTS) {
            *data = attrs[i].value.ints.data;
            *count = attrs[i].value.ints.count;
            return 1;
        }
    }
    return 0;
}

// Find a float attribute by name
static inline int find_float_attr(const attribute_t attrs[], int num_attrs,
                                   const char *name, double *out) {
    for (int i = 0; i < num_attrs; i++) {
        if (strcmp(attrs[i].name, name) == 0 && attrs[i].type == ATTR_FLOAT) {
            *out = attrs[i].value.f;
            return 1;
        }
    }
    return 0;
}

// Find a string attribute by name
static inline const char* find_string_attr(const attribute_t attrs[], int num_attrs,
                                            const char *name) {
    for (int i = 0; i < num_attrs; i++) {
        if (strcmp(attrs[i].name, name) == 0 && attrs[i].type == ATTR_STRING) {
            return attrs[i].value.str;
        }
    }
    return NULL;
}

#endif // ATTR_UTILS_H