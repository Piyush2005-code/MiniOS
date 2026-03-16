#ifndef TENSOR_H
#define TENSOR_H

#include <stddef.h>   // for size_t
#include <stdint.h>   // for uint32_t, etc.

// Data types supported by tensors (start with float, extend later)
typedef enum {
    DT_FLOAT = 0,
    DT_UINT8,
    DT_INT32,
    DT_INT64,
    // add others as needed
} data_type_t;

// Tensor structure – holds a multi‑dimensional array
typedef struct {
    void *data;                 // pointer to raw data (aligned)
    uint32_t *dims;              // array of dimension sizes
    uint32_t rank;                // number of dimensions
    data_type_t dtype;            // data type
    // optional: strides if needed for non‑contiguous tensors, skip for now
} tensor_t;

// Helper: compute total number of elements from dimensions
static inline size_t tensor_num_elements(const tensor_t *t) {
    size_t n = 1;
    for (uint32_t i = 0; i < t->rank; i++)
        n *= t->dims[i];
    return n;
}

// Helper: check if two tensors have the same shape
static inline int tensor_shape_equal(const tensor_t *a, const tensor_t *b) {
    if (a->rank != b->rank) return 0;
    for (uint32_t i = 0; i < a->rank; i++)
        if (a->dims[i] != b->dims[i]) return 0;
    return 1;
}

// Optional: allocate/free tensor functions (if using static arena, you might not need these)
// but you can define them if you move to dynamic allocation later.

#endif // TENSOR_H