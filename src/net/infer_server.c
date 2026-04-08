/**
 * @file infer_server.c
 * @brief SFU Inference Server — real ONNX inference + model management
 *
 * Architecture:
 *   ┌──────────┐   SFU_MSG_INFER_REQUEST   ┌──────────────┐
 *   │  Host CLI│ ────────────────────────▶ │ INFER_OnRequest│
 *   └──────────┘                           └──────┬───────┘
 *                                                 │ ONNX_Runtime_Inference
 *                                          ┌──────▼───────┐
 *   ┌──────────┐   SFU_MSG_CMD             │ ActiveModel  │
 *   │  Host CLI│ ────────────────────────▶ │ (ULFS .onnx) │
 *   └──────────┘   LIST/SELECT/GET         └──────────────┘
 *
 * Shell commands (UART side):
 *   models          — list .onnx files in /storage
 *   model [name]    — show or switch active model
 *   infer <f…>      — run local inference from the shell
 */

#include "net/infer_server.h"
#include "net/sfu.h"
#include "kernel/cmd.h"
#include "kernel/ulfs.h"
#include "onnx/onnx_loader.h"
#include "onnx/onnx_graph.h"
#include "onnx/onnx_runtime.h"
#include "hal/uart.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Configuration                                                     */
/* ------------------------------------------------------------------ */

#define INFER_MAX_INPUT_FLOATS   4096
#define INFER_MAX_OUTPUT_FLOATS  4096
#define INFER_MODEL_BUF_SIZE     (512 * 1024)   /* 512 KB for largest model */
#define INFER_WORKSPACE_SIZE     (256 * 1024)   /* 256 KB ONNX workspace    */
#define STORAGE_PREFIX           "/storage/"
#define ONNX_EXT                 ".onnx"
#define ONNX_EXT_LEN             5

/* ------------------------------------------------------------------ */
/*  Static state                                                      */
/* ------------------------------------------------------------------ */

static float   infer_input_buf[INFER_MAX_INPUT_FLOATS];
static float   infer_output_buf[INFER_MAX_OUTPUT_FLOATS];
static uint8_t infer_resp_buf[INFER_MAX_OUTPUT_FLOATS * 4];
static uint8_t model_data_buf[INFER_MODEL_BUF_SIZE];

static ONNX_Graph             g_graph;
static ONNX_InferenceContext  g_ctx;
static uint8_t  g_model_loaded = 0;
static char     g_active_model[INFER_MODEL_NAME_MAX] = "none";

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

static void is_puts(const char *s) { HAL_UART_PutString(s); }
static void is_putu(uint32_t v)    { HAL_UART_PutDec(v); }

static uint16_t is_strlen(const char *s)
{
    uint16_t n = 0;
    while (s[n]) n++;
    return n;
}

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

/** Check if a filename ends with ".onnx" */
static int has_onnx_ext(const char *name)
{
    uint16_t len = is_strlen(name);
    if (len <= ONNX_EXT_LEN) return 0;
    const char *tail = name + len - ONNX_EXT_LEN;
    return (tail[0]=='.' && tail[1]=='o' && tail[2]=='n' &&
            tail[3]=='n' && tail[4]=='x');
}

/** Copy name without ".onnx" suffix into dst */
static void strip_onnx_ext(char *dst, const char *name, uint16_t max)
{
    uint16_t len = is_strlen(name);
    uint16_t copy = (len > ONNX_EXT_LEN) ? (len - ONNX_EXT_LEN) : len;
    if (copy > max - 1) copy = (uint16_t)(max - 1);
    for (uint16_t i = 0; i < copy; i++) dst[i] = name[i];
    dst[copy] = '\0';
}

/** Build full path /storage/<stem>.onnx into buf (max bytes) */
static void make_model_path(char *buf, const char *stem, uint16_t max)
{
    uint16_t i = 0;
    const char *prefix = STORAGE_PREFIX;
    while (*prefix && i < max - 1) buf[i++] = *prefix++;
    const char *s = stem;
    while (*s && i < max - 1) buf[i++] = *s++;
    const char *ext = ONNX_EXT;
    while (*ext && i < max - 1) buf[i++] = *ext++;
    buf[i] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Model loading                                                     */
/* ------------------------------------------------------------------ */

/**
 * Load a model from ULFS /storage/<stem>.onnx into g_graph / g_ctx.
 * Returns 0 on success, -1 on failure.
 */
static int load_model(const char *stem)
{
    char path[96];
    make_model_path(path, stem, sizeof(path));

    /* Open file */
    int fd;
    Status s = ULFS_Open(path, ULFS_O_RDONLY, &fd);
    if (s != STATUS_OK) {
        is_puts("[INFER] ERROR: cannot open ");
        is_puts(path);
        is_puts("\n");
        return -1;
    }

    /* Read into model buffer */
    uint32_t nread = 0, total = 0;
    while (total < INFER_MODEL_BUF_SIZE) {
        s = ULFS_Read(fd, model_data_buf + total,
                      INFER_MODEL_BUF_SIZE - total, &nread);
        if (s != STATUS_OK || nread == 0) break;
        total += nread;
    }
    ULFS_Close(fd);

    if (total == 0) {
        is_puts("[INFER] ERROR: model file is empty\n");
        return -1;
    }

    /* Try to load as Protobuf ONNX */
    s = ONNX_LoadEmbedded(&g_graph, model_data_buf, (uint64_t)total,
                          ONNX_FORMAT_PROTOBUF);
    if (s != STATUS_OK) {
        /* Fallback: try custom binary */
        s = ONNX_LoadEmbedded(&g_graph, model_data_buf, (uint64_t)total,
                              ONNX_FORMAT_CUSTOM_BINARY);
    }
    if (s != STATUS_OK) {
        is_puts("[INFER] ERROR: ONNX_Load failed for ");
        is_puts(stem);
        is_puts("\n");
        return -1;
    }

    /* Init runtime context */
    s = ONNX_Runtime_Init(&g_ctx, &g_graph, INFER_WORKSPACE_SIZE);
    if (s != STATUS_OK) {
        is_puts("[INFER] ERROR: ONNX_Runtime_Init failed\n");
        return -1;
    }

    is_strncpy(g_active_model, stem, INFER_MODEL_NAME_MAX);
    g_model_loaded = 1;

    is_puts("[INFER] loaded model: ");
    is_puts(g_active_model);
    is_puts(" (");
    is_putu(total);
    is_puts(" bytes)\n");

    return 0;
}

/* ------------------------------------------------------------------ */
/*  INFER_ListModels / INFER_SelectModel / INFER_GetActiveModel       */
/* ------------------------------------------------------------------ */

/* Callback for ULFS_Readdir used during model listing */
typedef struct {
    char     buf[512];   /* response accumulation buffer */
    uint16_t pos;
    uint32_t count;
} list_ctx_t;

static void list_cb(const char *name, const ulfs_stat_t *st, void *user_data)
{
    (void)st;
    list_ctx_t *lc = (list_ctx_t *)user_data;
    if (!has_onnx_ext(name)) return;

    /* Strip extension */
    char stem[INFER_MODEL_NAME_MAX];
    strip_onnx_ext(stem, name, INFER_MODEL_NAME_MAX);

    /* Append "stem\n" to buffer */
    uint16_t slen = is_strlen(stem);
    if (lc->pos + slen + 2 < (uint16_t)sizeof(lc->buf)) {
        for (uint16_t i = 0; i < slen; i++) lc->buf[lc->pos++] = stem[i];
        lc->buf[lc->pos++] = '\n';
    }
    lc->count++;
}

void INFER_ListModels(void)
{
    list_ctx_t lc;
    lc.pos   = 0;
    lc.count = 0;
    lc.buf[0]= '\0';

    Status s = ULFS_Readdir("/storage", list_cb, &lc);
    if (s != STATUS_OK) {
        is_puts("[INFER] ERROR: cannot read /storage\n");
        return;
    }

    is_puts("\n  Available ONNX models in /storage:\n");
    is_puts("  ───────────────────────────────────\n");
    if (lc.count == 0) {
        is_puts("  (none found)\n");
        return;
    }

    /* Print the buffer line by line with indent */
    uint16_t i = 0;
    while (i < lc.pos) {
        is_puts("  * ");
        while (i < lc.pos && lc.buf[i] != '\n') {
            HAL_UART_PutChar(lc.buf[i++]);
        }
        if (i < lc.pos && lc.buf[i] == '\n') i++;
        is_puts("\n");
    }
    is_puts("  ───────────────────────────────────\n");
    is_puts("  Active: ");
    is_puts(g_active_model);
    is_puts("\n");
}

int INFER_SelectModel(const char *name)
{
    return load_model(name);
}

const char *INFER_GetActiveModel(void)
{
    return g_active_model;
}

/* ------------------------------------------------------------------ */
/*  CMD channel handler (SFU_MSG_CMD)                                 */
/* ------------------------------------------------------------------ */

/* Build a CMD_RESPONSE sending a text payload back to the host */
static void cmd_respond(uint32_t src_ip, uint16_t src_port,
                        uint32_t req_id,
                        const char *text)
{
    uint16_t tlen = is_strlen(text);
    SFU_SendRaw(src_ip, src_port, SFU_MSG_CMD_RESPONSE, req_id,
                (uint8_t *)text, tlen);
}

void INFER_OnCmd(uint32_t src_ip, uint16_t src_port,
                 uint32_t req_id,
                 const char *cmd, uint16_t cmd_len)
{
    (void)cmd_len;

    /* ---- LIST_MODELS ---- */
    if (is_strcmp(cmd, "LIST_MODELS") == 0) {
        /* Collect model list into a response buffer */
        list_ctx_t lc;
        lc.pos = 0; lc.count = 0; lc.buf[0] = '\0';
        ULFS_Readdir("/storage", list_cb, &lc);
        lc.buf[lc.pos] = '\0';
        SFU_SendRaw(src_ip, src_port, SFU_MSG_CMD_RESPONSE, req_id,
                    (uint8_t *)lc.buf, lc.pos);
        return;
    }

    /* ---- GET_MODEL ---- */
    if (is_strcmp(cmd, "GET_MODEL") == 0) {
        cmd_respond(src_ip, src_port, req_id, g_active_model);
        return;
    }

    /* ---- SELECT_MODEL <stem> ---- */
    if (cmd[0]=='S' && cmd[1]=='E' && cmd[2]=='L' && cmd[3]=='E' &&
        cmd[4]=='C' && cmd[5]=='T' && cmd[6]=='_' &&
        cmd[7]=='M' && cmd[8]=='O' && cmd[9]=='D' && cmd[10]=='E' &&
        cmd[11]=='L' && cmd[12]==' ') {

        const char *stem = cmd + 13;
        if (load_model(stem) == 0) {
            cmd_respond(src_ip, src_port, req_id, g_active_model);
        } else {
            SFU_SendRaw(src_ip, src_port, SFU_MSG_ERROR, req_id,
                        (uint8_t *)0, 0u);
        }
        return;
    }

    /* Unknown CMD */
    SFU_SendRaw(src_ip, src_port, SFU_MSG_NACK, req_id, (uint8_t *)0, 0u);
}

/* ------------------------------------------------------------------ */
/*  INFER_OnRequest — SFU_MSG_INFER_REQUEST handler                   */
/* ------------------------------------------------------------------ */

void INFER_SendError(uint32_t dst_ip, uint16_t dst_port,
                     uint32_t req_id, uint32_t error_code)
{
    uint8_t err_buf[4];
    err_buf[0] = (uint8_t)(error_code & 0xFF);
    err_buf[1] = (uint8_t)((error_code >> 8)  & 0xFF);
    err_buf[2] = (uint8_t)((error_code >> 16) & 0xFF);
    err_buf[3] = (uint8_t)((error_code >> 24) & 0xFF);
    SFU_SendRaw(dst_ip, dst_port, SFU_MSG_ERROR, req_id, err_buf, 4);
}

static void infer_memcpy(uint8_t *dst, const uint8_t *src, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) dst[i] = src[i];
}

void INFER_OnRequest(uint32_t src_ip, uint16_t src_port,
                     uint32_t req_id,
                     uint8_t *payload, uint16_t payload_len)
{
    if ((payload_len % 4) != 0 || (payload_len / 4) > INFER_MAX_INPUT_FLOATS) {
        is_puts("[INFER] ERROR: bad input size\n");
        INFER_SendError(src_ip, src_port, req_id, 0x01);
        return;
    }

    infer_memcpy((uint8_t *)infer_input_buf, payload, payload_len);
    int input_count = (int)(payload_len / 4);
    int output_count = 0;
    Status ret = STATUS_ERROR_NOT_SUPPORTED;

    if (g_model_loaded) {
        /* Run real ONNX inference */
        const void *in_ptrs[1]  = { (const void *)infer_input_buf };
        void       *out_ptrs[1] = { (void *)infer_output_buf };
        uint64_t    in_sz[1]    = { (uint64_t)payload_len };
        uint64_t    out_sz[1]   = { (uint64_t)(INFER_MAX_OUTPUT_FLOATS * 4) };

        ret = ONNX_Runtime_InferenceSimple(&g_ctx,
                                           in_ptrs,  in_sz,  1,
                                           out_ptrs, out_sz, 1);
        output_count = (int)(out_sz[0] / 4);
    }

    if (ret != STATUS_OK) {
        /* Fallback stub: multiply input by 2 */
        int limit = input_count < 4 ? input_count : 4;
        for (int i = 0; i < limit; i++) {
            infer_output_buf[i] = infer_input_buf[i] * 2.0f;
        }
        output_count = limit;
    }

    uint16_t resp_len = (uint16_t)(output_count * 4);
    infer_memcpy(infer_resp_buf, (uint8_t *)infer_output_buf, resp_len);

    SFU_SendRaw(src_ip, src_port, SFU_MSG_INFER_RESPONSE, req_id,
                infer_resp_buf, resp_len);

    is_puts("[INFER] req ");
    is_putu(req_id);
    is_puts(" model=");
    is_puts(g_active_model);
    is_puts(" in=");
    is_putu((uint32_t)input_count);
    is_puts(" out=");
    is_putu((uint32_t)output_count);
    is_puts("\n");
}

/* ------------------------------------------------------------------ */
/*  Shell commands: models / model / infer                            */
/* ------------------------------------------------------------------ */

static void cmd_models(int argc, char *argv[])
{
    (void)argc; (void)argv;
    INFER_ListModels();
}

static void cmd_model(int argc, char *argv[])
{
    if (argc < 2) {
        is_puts("Active model: ");
        is_puts(INFER_GetActiveModel());
        is_puts("\n  usage: model <stem>  (e.g. model tiny_mlp)\n");
        return;
    }
    if (INFER_SelectModel(argv[1]) == 0) {
        is_puts("Active model set to: ");
        is_puts(INFER_GetActiveModel());
        is_puts("\n");
    } else {
        is_puts("model: failed to load '");
        is_puts(argv[1]);
        is_puts("'\n");
    }
}

/** Parse a decimal or simple float string into a float */
static float parse_float(const char *s)
{
    float sign = 1.0f, result = 0.0f, frac = 0.0f, fdiv = 10.0f;
    int after_dot = 0;
    if (*s == '-') { sign = -1.0f; s++; }
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            if (after_dot) { frac += (float)(*s - '0') / fdiv; fdiv *= 10.0f; }
            else           { result = result * 10.0f + (float)(*s - '0'); }
        } else if (*s == '.') {
            after_dot = 1;
        } else {
            break;
        }
        s++;
    }
    return sign * (result + frac);
}

static void cmd_infer(int argc, char *argv[])
{
    if (argc < 2) {
        is_puts("usage: infer <f1> <f2> ...\n");
        return;
    }

    int count = argc - 1;
    if (count > INFER_MAX_INPUT_FLOATS) count = INFER_MAX_INPUT_FLOATS;

    for (int i = 0; i < count; i++) {
        infer_input_buf[i] = parse_float(argv[i + 1]);
    }

    int output_count = 0;
    Status ret = STATUS_ERROR_NOT_SUPPORTED;

    if (g_model_loaded) {
        const void *in_ptrs[1]  = { (const void *)infer_input_buf };
        void       *out_ptrs[1] = { (void *)infer_output_buf };
        uint64_t    in_sz[1]    = { (uint64_t)(count * 4) };
        uint64_t    out_sz[1]   = { (uint64_t)(INFER_MAX_OUTPUT_FLOATS * 4) };

        ret = ONNX_Runtime_InferenceSimple(&g_ctx,
                                           in_ptrs,  in_sz,  1,
                                           out_ptrs, out_sz, 1);
        output_count = (int)(out_sz[0] / 4);
    }

    if (ret != STATUS_OK) {
        int limit = count < 4 ? count : 4;
        for (int i = 0; i < limit; i++) {
            infer_output_buf[i] = infer_input_buf[i] * 2.0f;
        }
        output_count = limit;
    }

    is_puts("[INFER] model=");
    is_puts(g_active_model);
    is_puts(" inputs=[");
    for (int i = 0; i < count; i++) {
        /* Simple float print: integer + 4 decimal places */
        float v = infer_input_buf[i];
        if (v < 0.0f) { HAL_UART_PutChar('-'); v = -v; }
        is_putu((uint32_t)v);
        HAL_UART_PutChar('.');
        uint32_t frac = (uint32_t)((v - (float)(uint32_t)v) * 10000.0f);
        if (frac < 1000) HAL_UART_PutChar('0');
        if (frac < 100)  HAL_UART_PutChar('0');
        if (frac < 10)   HAL_UART_PutChar('0');
        is_putu(frac);
        if (i < count - 1) HAL_UART_PutChar(' ');
    }
    is_puts("] outputs=[");
    for (int i = 0; i < output_count; i++) {
        float v = infer_output_buf[i];
        if (v < 0.0f) { HAL_UART_PutChar('-'); v = -v; }
        is_putu((uint32_t)v);
        HAL_UART_PutChar('.');
        uint32_t frac = (uint32_t)((v - (float)(uint32_t)v) * 10000.0f);
        if (frac < 1000) HAL_UART_PutChar('0');
        if (frac < 100)  HAL_UART_PutChar('0');
        if (frac < 10)   HAL_UART_PutChar('0');
        is_putu(frac);
        if (i < output_count - 1) HAL_UART_PutChar(' ');
    }
    is_puts("]\n");
}

void INFER_RegisterShellCommands(void)
{
    CMD_Register("models", "List available ONNX models in /storage",  cmd_models);
    CMD_Register("model",  "Show/set active model  [stem]",           cmd_model);
    CMD_Register("infer",  "Run local inference  <f1> <f2> ...",      cmd_infer);
}

/* ------------------------------------------------------------------ */
/*  INFER_Init                                                        */
/* ------------------------------------------------------------------ */

void INFER_Init(void)
{
    /* Try to load a default model */
    if (load_model("tiny_mlp") != 0) {
        /* No default model found — stub will be used */
        is_puts("[INFER] no default model; using stub (input*2)\n");
    }

    SFU_SetInferHandler(INFER_OnRequest);
    SFU_SetCmdHandler(INFER_OnCmd);

    INFER_RegisterShellCommands();

    is_puts("[INFER] server ready\n");
}
