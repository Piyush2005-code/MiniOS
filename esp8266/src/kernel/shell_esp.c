/**
 * @file shell_esp.c
 * @brief Minimal UART Interactive Shell for MiniOS-ESP8266
 *
 * Polled via os_timer every 20ms. Accumulates characters into a line
 * buffer and dispatches commands on CR/LF. Supports the same core
 * commands as the ARM64 shell plus Wi-Fi-specific commands.
 *
 * Commands:
 *   help          List all commands
 *   models        List available ONNX models
 *   model [name]  Show or switch active model
 *   infer f1 f2   Run local inference from shell
 *   status        Show system status (RAM, uptime, Wi-Fi, model)
 *   wifi          Show Wi-Fi status (IP, RSSI)
 *   reconnect     Reconnect Wi-Fi
 *   reset         Software-reset the ESP8266
 */

#include "kernel/shell.h"
#include "net/infer_server.h"
#include "hal/uart.h"
#include "hal/timer.h"
#include "hal/wifi.h"
#include "types.h"

/* ESP8266 SDK */
#include "osapi.h"
#include "os_type.h"
#include "user_interface.h"

/* ------------------------------------------------------------------ */
/*  Shell state                                                       */
/* ------------------------------------------------------------------ */

#define SHELL_LINE_MAX   128
#define SHELL_ARGC_MAX   16

static char     g_line_buf[SHELL_LINE_MAX];
static uint8_t  g_line_pos = 0;
static os_timer_t g_shell_timer;

/* ------------------------------------------------------------------ */
/*  Internal: minimal strtok-style tokenizer                          */
/* ------------------------------------------------------------------ */

static ICACHE_FLASH_ATTR uint8_t tokenize(char *line, char *argv[], uint8_t max_args)
{
    uint8_t argc = 0;
    char *p = line;
    while (*p && argc < max_args) {
        while (*p == ' ') p++; /* skip spaces */
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

/* ------------------------------------------------------------------ */
/*  Internal: parse float from string                                 */
/* ------------------------------------------------------------------ */

static ICACHE_FLASH_ATTR float parse_float(const char *s)
{
    float sign = 1.0f, result = 0.0f, frac = 0.0f, fdiv = 10.0f;
    int after_dot = 0;
    if (*s == '-') { sign = -1.0f; s++; }
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            if (after_dot) { frac += (float)(*s-'0') / fdiv; fdiv *= 10.0f; }
            else           { result = result * 10.0f + (float)(*s-'0'); }
        } else if (*s == '.') {
            after_dot = 1;
        } else break;
        s++;
    }
    return sign * (result + frac);
}

/* ------------------------------------------------------------------ */
/*  Command handlers                                                  */
/* ------------------------------------------------------------------ */

static ICACHE_FLASH_ATTR void cmd_help(uint8_t argc, char *argv[])
{
    (void)argc; (void)argv;
    HAL_UART_PutString(
        "\nMiniOS-ESP8266 shell commands:\n"
        "  help              This help\n"
        "  models            List embedded ONNX models\n"
        "  model [name]      Show/switch active model\n"
        "  infer f1 f2 ...   Run inference from shell\n"
        "  status            System status (RAM, uptime, Wi-Fi)\n"
        "  wifi              Wi-Fi status\n"
        "  reconnect         Reconnect Wi-Fi\n"
        "  reset             Software reset\n"
    );
}

static ICACHE_FLASH_ATTR void cmd_models(uint8_t argc, char *argv[])
{
    (void)argc; (void)argv;
    INFER_ListModels();
}

static ICACHE_FLASH_ATTR void cmd_model(uint8_t argc, char *argv[])
{
    if (argc < 2) {
        HAL_UART_PutString("Active model: ");
        HAL_UART_PutString(INFER_GetActiveModel());
        HAL_UART_PutString("\n  usage: model <name>\n");
        return;
    }
    if (INFER_SelectModel(argv[1]) == 0) {
        HAL_UART_PutString("Model set: ");
        HAL_UART_PutString(INFER_GetActiveModel());
        HAL_UART_PutString("\n");
    } else {
        HAL_UART_PutString("Error: model '");
        HAL_UART_PutString(argv[1]);
        HAL_UART_PutString("' not found\n");
    }
}

static ICACHE_FLASH_ATTR void cmd_infer(uint8_t argc, char *argv[])
{
    if (argc < 2) {
        HAL_UART_PutString("usage: infer <f1> <f2> ...\n");
        return;
    }

    static float input_buf[64];
    static float output_buf[64];
    uint8_t count = argc - 1;
    if (count > 64) count = 64;

    for (uint8_t i = 0; i < count; i++) input_buf[i] = parse_float(argv[i+1]);

    /* Direct call to inference runtime */
    extern ONNX_InferenceContext g_ctx;
    const void *in_ptrs[1]  = { input_buf };
    void       *out_ptrs[1] = { output_buf };
    uint32_t    in_sz[1]    = { (uint32_t)(count * 4) };
    uint32_t    out_sz[1]   = { 256 };

    Status ret = ONNX_Runtime_InferenceSimple(&g_ctx,
                                              in_ptrs, in_sz, 1,
                                              out_ptrs, out_sz, 1);
    uint8_t out_count = (uint8_t)(out_sz[0] / 4);

    HAL_UART_PutString("inputs=[");
    for (uint8_t i = 0; i < count; i++) {
        float v = input_buf[i];
        if (v < 0.0f) { HAL_UART_PutChar('-'); v = -v; }
        HAL_UART_PutDec((uint32_t)v);
        HAL_UART_PutChar('.');
        HAL_UART_PutDec((uint32_t)((v-(float)(uint32_t)v)*100.0f));
        if (i < count-1) HAL_UART_PutChar(' ');
    }
    HAL_UART_PutString("] outputs=[");

    if (ret == STATUS_OK) {
        for (uint8_t i = 0; i < out_count; i++) {
            float v = output_buf[i];
            if (v < 0.0f) { HAL_UART_PutChar('-'); v = -v; }
            HAL_UART_PutDec((uint32_t)v);
            HAL_UART_PutChar('.');
            HAL_UART_PutDec((uint32_t)((v-(float)(uint32_t)v)*100.0f));
            if (i < out_count-1) HAL_UART_PutChar(' ');
        }
    } else {
        HAL_UART_PutString("ERROR");
    }
    HAL_UART_PutString("]\n");
}

static ICACHE_FLASH_ATTR void cmd_status(uint8_t argc, char *argv[])
{
    (void)argc; (void)argv;
    uint32_t free_heap = system_get_free_heap_size();
    uint32_t uptime_s  = HAL_Timer_GetSystemTicks() * HAL_Timer_GetTickPeriodMs() / 1000;

    HAL_UART_PutString("\n=== MiniOS-ESP8266 Status ===\n");
    HAL_UART_PutString("  Free heap:  ");  HAL_UART_PutDec(free_heap); HAL_UART_PutString(" bytes\n");
    HAL_UART_PutString("  Uptime:     ");  HAL_UART_PutDec(uptime_s);  HAL_UART_PutString(" s\n");
    HAL_UART_PutString("  Model:      ");  HAL_UART_PutString(INFER_GetActiveModel()); HAL_UART_PutString("\n");

    char ip[16];
    HAL_WiFi_GetIPString(ip);
    HAL_UART_PutString("  IP:         ");  HAL_UART_PutString(ip); HAL_UART_PutString("\n");
    HAL_UART_PutString("  SFU port:   9000\n");
    HAL_UART_PutString("=============================\n");
}

static ICACHE_FLASH_ATTR void cmd_wifi(uint8_t argc, char *argv[])
{
    (void)argc; (void)argv;
    char ip[16];
    HAL_WiFi_GetIPString(ip);
    wifi_state_t state = HAL_WiFi_GetState();

    HAL_UART_PutString("Wi-Fi: ");
    switch (state) {
        case WIFI_STATE_GOT_IP:      HAL_UART_PutString("connected"); break;
        case WIFI_STATE_CONNECTING:  HAL_UART_PutString("connecting"); break;
        case WIFI_STATE_DISCONNECTED:HAL_UART_PutString("disconnected"); break;
        default:                     HAL_UART_PutString("unknown"); break;
    }
    HAL_UART_PutString("  IP: ");
    HAL_UART_PutString(ip);
    HAL_UART_PutString("  RSSI: ");
    HAL_UART_PutDec((uint32_t)(int32_t)HAL_WiFi_GetRSSI());
    HAL_UART_PutString(" dBm\n");
}

static ICACHE_FLASH_ATTR void cmd_reconnect(uint8_t argc, char *argv[])
{
    (void)argc; (void)argv;
    HAL_WiFi_Reconnect();
}

static ICACHE_FLASH_ATTR void cmd_reset(uint8_t argc, char *argv[])
{
    (void)argc; (void)argv;
    HAL_UART_PutString("Resetting...\n");
    HAL_Timer_DelayMs(100);
    system_restart();
}

/* ------------------------------------------------------------------ */
/*  Command dispatch table                                            */
/* ------------------------------------------------------------------ */

typedef struct { const char *name; void (*fn)(uint8_t, char **); } shell_cmd_t;

static const shell_cmd_t g_cmds[] = {
    { "help",      cmd_help      },
    { "models",    cmd_models    },
    { "model",     cmd_model     },
    { "infer",     cmd_infer     },
    { "status",    cmd_status    },
    { "wifi",      cmd_wifi      },
    { "reconnect", cmd_reconnect },
    { "reset",     cmd_reset     },
};

#define SHELL_NUM_CMDS (sizeof(g_cmds)/sizeof(g_cmds[0]))

/* ------------------------------------------------------------------ */
/*  Internal: dispatch line                                           */
/* ------------------------------------------------------------------ */

static ICACHE_FLASH_ATTR void shell_dispatch(char *line)
{
    char *argv[SHELL_ARGC_MAX];
    uint8_t argc = tokenize(line, argv, SHELL_ARGC_MAX);
    if (argc == 0) return;

    for (uint8_t i = 0; i < SHELL_NUM_CMDS; i++) {
        const char *a = g_cmds[i].name;
        const char *b = argv[0];
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == '\0' && *b == '\0') {
            g_cmds[i].fn(argc, argv);
            return;
        }
    }
    HAL_UART_PutString("Unknown command: '");
    HAL_UART_PutString(argv[0]);
    HAL_UART_PutString("' (type 'help')\n");
}

/* ------------------------------------------------------------------ */
/*  Shell poll callback (os_timer, 20ms)                              */
/* ------------------------------------------------------------------ */

static void ICACHE_FLASH_ATTR shell_poll_cb(void *arg)
{
    (void)arg;
    while (HAL_UART_HasChar()) {
        char c = HAL_UART_GetChar();
        if (c == '\r' || c == '\n') {
            if (g_line_pos > 0) {
                g_line_buf[g_line_pos] = '\0';
                HAL_UART_PutString("\n");
                shell_dispatch(g_line_buf);
                g_line_pos = 0;
            }
            HAL_UART_PutString("minios> ");
        } else if (c == '\b' || c == 127) {
            if (g_line_pos > 0) {
                g_line_pos--;
                HAL_UART_PutString("\b \b");
            }
        } else if (g_line_pos < SHELL_LINE_MAX - 1) {
            g_line_buf[g_line_pos++] = c;
            HAL_UART_PutChar(c);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public: SHELL_Init                                                */
/* ------------------------------------------------------------------ */

void ICACHE_FLASH_ATTR SHELL_Init(void)
{
    g_line_pos = 0;
    os_timer_disarm(&g_shell_timer);
    os_timer_setfn(&g_shell_timer, (os_timer_func_t)shell_poll_cb, NULL);
    os_timer_arm(&g_shell_timer, 20, 1); /* poll every 20ms */
    HAL_UART_PutString("\nMiniOS-ESP8266 shell ready. Type 'help'.\n");
    HAL_UART_PutString("minios> ");
}
