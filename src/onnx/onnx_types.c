/**
 * @file onnx_types.c
 * @brief ONNX type utilities implementation
 */

#include "onnx/onnx_types.h"

/**
 * @brief Get operator name string
 */
const char* ONNX_GetOperatorName(ONNX_OperatorType op_type)
{
    switch (op_type) {
        case ONNX_OP_ADD:        return "Add";
        case ONNX_OP_SUB:        return "Sub";
        case ONNX_OP_MUL:        return "Mul";
        case ONNX_OP_DIV:        return "Div";
        case ONNX_OP_MATMUL:     return "MatMul";
        case ONNX_OP_RELU:       return "Relu";
        case ONNX_OP_SIGMOID:    return "Sigmoid";
        case ONNX_OP_TANH:       return "Tanh";
        case ONNX_OP_SOFTMAX:    return "Softmax";
        case ONNX_OP_CONV:       return "Conv";
        case ONNX_OP_MAXPOOL:    return "MaxPool";
        case ONNX_OP_AVGPOOL:    return "AveragePool";
        case ONNX_OP_RESHAPE:    return "Reshape";
        case ONNX_OP_TRANSPOSE:  return "Transpose";
        case ONNX_OP_FLATTEN:    return "Flatten";
        case ONNX_OP_BATCHNORM:  return "BatchNormalization";
        case ONNX_OP_GEMM:       return "Gemm";
        case ONNX_OP_CONCAT:     return "Concat";
        default:                 return "Unknown";
    }
}

/**
 * @brief Get data type name string
 */
const char* ONNX_GetDataTypeName(ONNX_DataType dtype)
{
    switch (dtype) {
        case ONNX_DTYPE_FLOAT32:  return "float32";
        case ONNX_DTYPE_FLOAT64:  return "float64";
        case ONNX_DTYPE_INT8:     return "int8";
        case ONNX_DTYPE_UINT8:    return "uint8";
        case ONNX_DTYPE_INT16:    return "int16";
        case ONNX_DTYPE_UINT16:   return "uint16";
        case ONNX_DTYPE_INT32:    return "int32";
        case ONNX_DTYPE_INT64:    return "int64";
        default:                  return "undefined";
    }
}
