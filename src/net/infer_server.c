/**
 * @file infer_server.c
 * @brief SFU Inference Server implementation
 */

#include "net/infer_server.h"
#include "net/sfu.h"
#include "hal/uart.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Configuration & Static Buffers                                   */
/* ------------------------------------------------------------------ */

#define INFER_MAX_INPUT_FLOATS   4096
#define INFER_MAX_OUTPUT_FLOATS  4096

static float    infer_input_buf[INFER_MAX_INPUT_FLOATS];
static float    infer_output_buf[INFER_MAX_OUTPUT_FLOATS];
static uint8_t  infer_resp_buf[INFER_MAX_OUTPUT_FLOATS * 4];

/* ------------------------------------------------------------------ */
/*  Internal Helpers                                                 */
/* ------------------------------------------------------------------ */

static void infer_memcpy(uint8_t *dst, const uint8_t *src, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

/**
 * @brief Stub for the ONNX inference function
 *
 * NOTE: The user's template "[paste the function signature of your main
 * inference call]" was left empty in the prompt!
 * Using this stub so the code compiles. Replace this with the actual
 * ONNX_Runtime_InferenceSimple wrapper later.
 */
static int ONNX_RunInference_Stub(float* input, int input_count,
                                  float* output, int* output_count)
{
    /* Echo back up to 4 floats to prove the pathway works */
    int limit = (input_count < 4) ? input_count : 4;
    for (int i = 0; i < limit; i++) {
        output[i] = input[i] * 2.0f; /* Dummy operation: multiply by 2 */
    }
    *output_count = limit;
    return 0; /* STATUS_OK */
}

/* ------------------------------------------------------------------ */
/*  Public API                                                       */
/* ------------------------------------------------------------------ */

void INFER_SendError(uint32_t dst_ip, uint16_t dst_port,
                     uint32_t req_id, uint32_t error_code)
{
    uint8_t err_buf[4];
    err_buf[0] = (uint8_t)(error_code & 0xFF);
    err_buf[1] = (uint8_t)((error_code >> 8) & 0xFF);
    err_buf[2] = (uint8_t)((error_code >> 16) & 0xFF);
    err_buf[3] = (uint8_t)((error_code >> 24) & 0xFF);

    SFU_SendRaw(dst_ip, dst_port, SFU_MSG_ERROR, req_id, err_buf, 4);
}

void INFER_OnRequest(uint32_t src_ip, uint16_t src_port,
                     uint32_t req_id,
                     uint8_t *payload, uint16_t payload_len)
{
    /* Validate payload length */
    if ((payload_len % 4) != 0 || (payload_len / 4) > INFER_MAX_INPUT_FLOATS) {
        HAL_UART_PutString("[INFER] ERROR: bad input size\n");
        INFER_SendError(src_ip, src_port, req_id, 0x01); /* Error code 1: Bad input size */
        return;
    }

    /* Copy payload correctly into float buffer */
    infer_memcpy((uint8_t *)infer_input_buf, payload, payload_len);
    int input_count = payload_len / 4;

    /* Execute the Inference via the ONNX helper function */
    int output_count = INFER_MAX_OUTPUT_FLOATS;
    int ret = ONNX_RunInference_Stub(infer_input_buf, input_count,
                                     infer_output_buf, &output_count);

    if (ret != 0) {
        HAL_UART_PutString("[INFER] ERROR: inference failed\n");
        INFER_SendError(src_ip, src_port, req_id, (uint32_t)ret);
        return;
    }

    /* Serialize the output buffer into response array */
    uint16_t resp_len = (uint16_t)(output_count * 4);
    infer_memcpy(infer_resp_buf, (uint8_t *)infer_output_buf, resp_len);

    /* Send response */
    SFU_SendRaw(src_ip, src_port, SFU_MSG_INFER_RESPONSE, req_id,
                infer_resp_buf, resp_len);

    HAL_UART_PutString("[INFER] req ");
    HAL_UART_PutDec(req_id);
    HAL_UART_PutString(" done, ");
    HAL_UART_PutDec((uint32_t)input_count);
    HAL_UART_PutString(" floats in, ");
    HAL_UART_PutDec((uint32_t)output_count);
    HAL_UART_PutString(" floats out\n");
}

void INFER_Init(void)
{
    SFU_SetInferHandler(INFER_OnRequest);
    HAL_UART_PutString("[INFER] server ready\n");
}
