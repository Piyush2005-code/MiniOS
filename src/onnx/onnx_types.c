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
        case ONNX_OP_SPLIT:      return "Split";
        case ONNX_OP_CONSTANT:   return "Constant";
        case ONNX_OP_LEAKYRELU:  return "LeakyRelu";
        case ONNX_OP_GLOBALAVERAGEPOOL: return "GlobalAveragePool";
        case ONNX_OP_SQUEEZE:    return "Squeeze";
        case ONNX_OP_UNSQUEEZE:  return "Unsqueeze";
        case ONNX_OP_CAST:       return "Cast";
        case ONNX_OP_ABS:        return "Abs";
        case ONNX_OP_NEG:        return "Neg";
        case ONNX_OP_EXP:        return "Exp";
        case ONNX_OP_LOG:        return "Log";
        case ONNX_OP_SQRT:       return "Sqrt";
        case ONNX_OP_CEIL:       return "Ceil";
        case ONNX_OP_FLOOR:      return "Floor";
        case ONNX_OP_SIN:        return "Sin";
        case ONNX_OP_COS:        return "Cos";
        case ONNX_OP_REDUCESUM:  return "ReduceSum";
        case ONNX_OP_REDUCEMEAN: return "ReduceMean";
        case ONNX_OP_REDUCEMAX:  return "ReduceMax";
        case ONNX_OP_REDUCEMIN:  return "ReduceMin";
        case ONNX_OP_CLIP:       return "Clip";
        case ONNX_OP_IDENTITY:   return "Identity";
        case ONNX_OP_LRN:        return "LRN";
        case ONNX_OP_DROPOUT:    return "Dropout";
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
