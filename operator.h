#ifndef OPERATOR_H
#define OPERATOR_H

#include "tensor.h"
#include <stdint.h>

// Attribute types supported (simplified – extend as needed)
typedef enum {
    ATTR_INT,
    ATTR_INTS,       // list of integers
    ATTR_FLOAT,
    ATTR_FLOATS,
    ATTR_STRING,
    // … more
} attr_type_t;

// Attribute value union
typedef union {
    int64_t i;
    double f;
    struct { int64_t *data; uint32_t count; } ints;
    struct { double *data; uint32_t count; } floats;
    const char *str;
} attr_value_t;

// Attribute structure (name + type + value)
typedef struct {
    const char *name;
    attr_type_t type;
    attr_value_t value;
} attribute_t;

// Operator function signature
typedef int (*operator_func)(const tensor_t inputs[], int num_inputs,
                              tensor_t outputs[], int num_outputs,
                              const attribute_t attrs[], int num_attrs);

// Operator registry – you'll implement this in a separate .c file
operator_func find_operator(const char *name);

#endif // OPERATOR_H