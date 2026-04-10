/**
 * @file infer_server_esp.c
 * @brief SFU Inference Server for MiniOS-ESP8266
 *
 * Adapts the ARM64 infer_server.c for ESP8266 constraints:
 *   - Models loaded from compiled-in C arrays (not ULFS filesystem)
 *   - Input: float32 from sfu_client.py → quantized to int8 internally
 *   - Output: int8 inference result → dequantized to float32 → SFU response
 *   - Max input/output: 64 floats each (not 4096)
 *   - Static buffers only — no heap allocation during request handling
 *
 * Supported SFU CMD strings (same as original, sfu_client.py compatible):
 *   "LIST_MODELS"          → list available embedded models
 *   "GET_MODEL"            → return active model name
 *   "SELECT_MODEL <name>"  → switch active model
 */

#include "net/infer_server.h"
#include "net/sfu.h"
#include "onnx/onnx_runtime.h"
#include "onnx/onnx_loader.h"
#include "onnx/onnx_types.h"
#include "hal/uart.h"
#include "hal/timer.h"
#include "types.h"
#include "../../user_config.h"

/* ------------------------------------------------------------------ */
/*  Static state                                                      */
/* ------------------------------------------------------------------ */

static ONNX_Graph            g_graph;
static ONNX_InferenceContext g_ctx;
static uint8_t               g_model_loaded = 0;
static char                  g_active_model[INFER_MODEL_NAME_MAX] = "none";

/* Static I/O buffers — float32 from/to sfu_client.py */
static float   g_input_f32[INFER_MAX_INPUT_FLOATS];
static float   g_output_f32[INFER_MAX_OUTPUT_FLOATS];
static uint8_t g_resp_buf[INFER_MAX_OUTPUT_FLOATS * 4]; /* float32 bytes */

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

static void is_puts(const char *s) { HAL_UART_PutString(s); }
static void is_putu(uint32_t v)    { HAL_UART_PutDec(v); }

static int is_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (*a == *b) ? 0 : 1;
}

static void is_strncpy(char *dst, const char *src, uint16_t max)
{
    uint16_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void infer_memcpy(uint8_t *dst, const uint8_t *src, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) dst[i] = src[i];
}

/* ------------------------------------------------------------------ */
/*  INFER_SelectModel / INFER_GetActiveModel                          */
/* ------------------------------------------------------------------ */

int INFER_SelectModel(const char *name)
{
    Status s = ONNX_LoadEmbedded(&g_graph, name);
    if (s != STATUS_OK) return -1;

    ONNX_Runtime_Cleanup(&g_ctx);
    s = ONNX_Runtime_Init(&g_ctx, &g_graph, 0);
    if (s != STATUS_OK) return -1;

    is_strncpy(g_active_model, name, INFER_MODEL_NAME_MAX);
    g_model_loaded = 1;

    is_puts("[INFER] active model: ");
    is_puts(g_active_model);
    is_puts("\n");
    return 0;
}

const char *INFER_GetActiveModel(void) { return g_active_model; }

/* ------------------------------------------------------------------ */
/*  INFER_ListModels                                                  */
/* ------------------------------------------------------------------ */

void INFER_ListModels(void)
{
    is_puts("\n  Available embedded models:\n");
    is_puts("  * tiny_mlp  (4->8->4, int8, ReLU+Softmax)\n");
    is_puts("  Active: ");
    is_puts(g_active_model);
    is_puts("\n");
}

/* ------------------------------------------------------------------ */
/*  INFER_OnCmd — handles SFU_MSG_CMD packets from sfu_client.py     */
/* ------------------------------------------------------------------ */

void INFER_OnCmd(uint32_t src_ip, uint16_t src_port,
                 uint32_t req_id, const char *cmd, uint16_t cmd_len)
{
    (void)cmd_len;

    /* LIST_MODELS */
    if (is_strcmp(cmd, "LIST_MODELS") == 0) {
        static const char model_list[] = "tiny_mlp\n";
        SFU_SendRaw(src_ip, src_port, SFU_MSG_CMD_RESPONSE, req_id,
                    (uint8_t *)model_list,
                    (uint16_t)(sizeof(model_list) - 1));
        return;
    }

    /* GET_MODEL */
    if (is_strcmp(cmd, "GET_MODEL") == 0) {
        uint16_t nlen = 0;
        const char *n = g_active_model;
        while (n[nlen]) nlen++;
        SFU_SendRaw(src_ip, src_port, SFU_MSG_CMD_RESPONSE, req_id,
                    (uint8_t *)g_active_model, nlen);
        return;
    }

    /* SELECT_MODEL <name> — 13-char prefix "SELECT_MODEL " */
    if (cmd[0]=='S' && cmd[1]=='E' && cmd[2]=='L' && cmd[3]=='E' &&
        cmd[4]=='C' && cmd[5]=='T' && cmd[6]=='_' && cmd[7]=='M' &&
        cmd[8]=='O' && cmd[9]=='D' && cmd[10]=='E' && cmd[11]=='L' &&
        cmd[12]==' ') {
        const char *stem = cmd + 13;
        if (INFER_SelectModel(stem) == 0) {
            uint16_t nlen = 0;
            while (g_active_model[nlen]) nlen++;
            SFU_SendRaw(src_ip, src_port, SFU_MSG_CMD_RESPONSE, req_id,
                        (uint8_t *)g_active_model, nlen);
        } else {
            SFU_SendRaw(src_ip, src_port, SFU_MSG_ERROR, req_id,
                        (uint8_t *)0, 0u);
        }
        return;
    }

    /* Unknown command */
    SFU_SendRaw(src_ip, src_port, SFU_MSG_NACK, req_id, (uint8_t *)0, 0u);
}

/* ------------------------------------------------------------------ */
/*  INFER_OnRequest — handles SFU_MSG_INFER_REQUEST                   */
/* ------------------------------------------------------------------ */

void INFER_OnRequest(uint32_t src_ip, uint16_t src_port,
                     uint32_t req_id, uint8_t *payload, uint16_t payload_len)
{
    /* Validate: payload must be N×4 bytes of float32 */
    if ((payload_len % 4) != 0) {
        is_puts("[INFER] bad payload alignment\n");
        SFU_SendRaw(src_ip, src_port, SFU_MSG_ERROR, req_id,
                    (uint8_t *)0, 0u);
        return;
    }

    uint16_t input_count = payload_len / 4;
    if (input_count > INFER_MAX_INPUT_FLOATS) {
        is_puts("[INFER] input too large\n");
        SFU_SendRaw(src_ip, src_port, SFU_MSG_ERROR, req_id,
                    (uint8_t *)0, 0u);
        return;
    }

    /* Copy float32 LE bytes → g_input_f32[] */
    infer_memcpy((uint8_t *)g_input_f32, payload, payload_len);

    uint16_t output_count = 0;
    Status   ret          = STATUS_ERROR_NOT_SUPPORTED;

    if (g_model_loaded) {
        uint32_t t0 = HAL_Timer_GetTicks();

        const void *in_ptrs[1]  = { (const void *)g_input_f32 };
        void       *out_ptrs[1] = { (void *)g_output_f32 };
        uint32_t    in_sz[1]    = { (uint32_t)payload_len };
        uint32_t    out_sz[1]   = { (uint32_t)(INFER_MAX_OUTPUT_FLOATS * 4) };

        ret = ONNX_Runtime_InferenceSimple(&g_ctx,
                                           in_ptrs, in_sz, 1,
                                           out_ptrs, out_sz, 1);
        output_count = (uint16_t)(out_sz[0] / 4);

        uint32_t elapsed = HAL_Timer_GetElapsedUs(t0);
        is_puts("[INFER] req ");
        is_putu(req_id);
        is_puts(" model=");
        is_puts(g_active_model);
        is_puts(" in=");
        is_putu(input_count);
        is_puts(" out=");
        is_putu(output_count);
        is_puts(" ");
        is_putu(elapsed);
        is_puts("us\n");
    }

    if (ret != STATUS_OK) {
        /* Fallback stub: multiply input by 2.0 (same as ARM64 fallback) */
        uint16_t lim = input_count < 4 ? input_count : 4;
        for (uint16_t i = 0; i < lim; i++) g_output_f32[i] = g_input_f32[i] * 2.0f;
        output_count = lim;
    }

    /* Serialize float32 output into response buffer */
    uint16_t resp_len = (uint16_t)(output_count * 4);
    infer_memcpy(g_resp_buf, (uint8_t *)g_output_f32, resp_len);

    SFU_SendRaw(src_ip, src_port, SFU_MSG_INFER_RESPONSE, req_id,
                g_resp_buf, resp_len);
}

/* ------------------------------------------------------------------ */
/*  INFER_Init                                                        */
/* ------------------------------------------------------------------ */

void INFER_Init(void)
{
    /* Load the default model from user_config.h */
    if (INFER_SelectModel(DEFAULT_MODEL) != 0) {
        is_puts("[INFER] default model not found; using stub (input*2)\n");
    }

    SFU_SetInferHandler(INFER_OnRequest);
    SFU_SetCmdHandler(INFER_OnCmd);

    is_puts("[INFER] server ready\n");
}
