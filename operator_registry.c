#include "operator.h"
#include <string.h>

// Forward declarations of all operators
int operator_add(…);
int operator_matmul(…);
int operator_relu(…);
// declare others as you implement them

typedef struct {
    const char *name;
    operator_func func;
} operator_entry;

static operator_entry registry[] = {
    {"Add", operator_add},
    {"MatMul", operator_matmul},
    {"Relu", operator_relu},
    {"Conv", operator_conv},
    {"AveragePool", operator_averagepool},
    {"Softmax", operator_softmax},
    {"Gemm", operator_gemm},
    // add all operators here
};

operator_func find_operator(const char *name) {
    for (size_t i = 0; i < sizeof(registry)/sizeof(registry[0]); i++) {
        if (strcmp(registry[i].name, name) == 0)
            return registry[i].func;
    }
    return NULL;
}