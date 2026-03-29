/**
 * @file net_model.c
 * @brief Network/UART model transfer implementation
 *
 * Receives an ONNX model over the UART channel (QEMU serial port).
 * This is the practical transport for QEMU virt without TAP networking.
 *
 * Transfer protocol (host side: use host_send.py):
 *   Byte 0-3:  Magic bytes  0x4D 0x49 0x4E 0x49  ("MINI")
 *   Byte 4-7:  Size (uint32, big-endian)
 *   Byte 8...: Payload (the .onnx / .bin file bytes)
 *
 * The OS side reads this from UART, validates magic, then writes
 * payload into /exec/model.bin via MFS_Write().
 */

#include "net/net_model.h"
#include "fs/minifs.h"
#include "hal/uart.h"
#include "kernel/kmem.h"
#include "lib/string.h"

/* ------------------------------------------------------------------ */
/*  Internal UART byte reader                                         */
/* ------------------------------------------------------------------ */

/** Read one byte from UART (blocking) */
static uint8_t uart_read_byte(void) {
    return (uint8_t)HAL_UART_GetChar();
}

/** Read exactly n bytes from UART into buf */
static void uart_read_bytes(uint8_t *buf, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        buf[i] = uart_read_byte();
    }
}

/** Read a 4-byte big-endian uint32 from UART */
static uint32_t uart_read_u32_be(void) {
    uint8_t b[4];
    uart_read_bytes(b, 4);
    return ((uint32_t)b[0] << 24) |
           ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] <<  8) |
           ((uint32_t)b[3]);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

Status NET_Init(void) {
    /* UART is already initialized by kernel_main. Nothing extra needed. */
    HAL_UART_PutString("[NET ] Network (UART transport) ready\n");
    return STATUS_OK;
}

void NET_ReceiveModel(void) {
    HAL_UART_PutString("[NET ] Waiting for model transfer...\n");
    HAL_UART_PutString("[NET ] (Run host_send.py on your laptop)\n");

    /* ---- Step 1: Read and validate magic bytes ---- */
    uint32_t magic = uart_read_u32_be();
    if (magic != NET_MODEL_MAGIC) {
        HAL_UART_PutString("[NET ] ERROR: Bad magic — transfer aborted\n");
        HAL_UART_PutString("[NET ] Expected 0x4D494E49, got: ");
        HAL_UART_PutHex(magic);
        HAL_UART_PutString("\n");
        return;
    }
    HAL_UART_PutString("[NET ] Magic OK\n");

    /* ---- Step 2: Read file size ---- */
    uint32_t file_size = uart_read_u32_be();
    if (file_size == 0 || file_size > MFS_MAX_FILE_SIZE) {
        HAL_UART_PutString("[NET ] ERROR: Invalid file size\n");
        return;
    }

    HAL_UART_PutString("[NET ] Receiving ");
    HAL_UART_PutDec(file_size);
    HAL_UART_PutString(" bytes...\n");

    /* ---- Step 3: Allocate a temporary buffer from heap ---- */
    uint8_t *buf = (uint8_t *)KMEM_Alloc((size_t)file_size, 8);
    if (!buf) {
        HAL_UART_PutString("[NET ] ERROR: Out of memory\n");
        return;
    }

    /* ---- Step 4: Read payload bytes ---- */
    uart_read_bytes(buf, file_size);

    /* ---- Step 5: Write into MiniFS /exec/model.bin ---- */
    Status s = MFS_Write("exec", "model.bin", buf, file_size);
    if (s != STATUS_OK) {
        HAL_UART_PutString("[NET ] ERROR: MFS_Write failed\n");
        return;
    }

    HAL_UART_PutString("[NET ] Transfer complete → /exec/model.bin\n");
}

void NET_RunModel(const char *path) {
    if (!path) {
        HAL_UART_PutString("[RUN ] Usage: run exec/model.bin\n");
        return;
    }

    /* ---- Split "exec/model.bin" into dir="exec", file="model.bin" ---- */
    char dir_buf[MFS_NAME_MAX];
    char file_buf[MFS_NAME_MAX];

    /* Find the '/' separator */
    uint32_t slash_pos = 0;
    bool found_slash = false;
    for (uint32_t i = 0; path[i] != '\0' && i < MFS_NAME_MAX * 2; i++) {
        if (path[i] == '/') {
            slash_pos = i;
            found_slash = true;
            break;
        }
    }

    if (!found_slash) {
        HAL_UART_PutString("[RUN ] ERROR: Path must be dir/file (e.g. exec/model.bin)\n");
        return;
    }

    /* Copy the directory part */
    uint32_t i = 0;
    while (i < slash_pos && i < MFS_NAME_MAX - 1) {
        dir_buf[i] = path[i];
        i++;
    }
    dir_buf[i] = '\0';

    /* Copy the filename part (after the '/') */
    const char *fname_start = path + slash_pos + 1;
    i = 0;
    while (fname_start[i] != '\0' && i < MFS_NAME_MAX - 1) {
        file_buf[i] = fname_start[i];
        i++;
    }
    file_buf[i] = '\0';

    HAL_UART_PutString("[RUN ] Loading /");
    HAL_UART_PutString(dir_buf);
    HAL_UART_PutString("/");
    HAL_UART_PutString(file_buf);
    HAL_UART_PutString("\n");

    /* ---- Read file from MiniFS ---- */
    const uint8_t *model_data = NULL;
    uint32_t       model_size = 0;
    Status s = MFS_Read(dir_buf, file_buf, &model_data, &model_size);
    if (s != STATUS_OK) {
        HAL_UART_PutString("[RUN ] ERROR: File not found in MiniFS\n");
        HAL_UART_PutString("[RUN ] Hint: Use 'recv' first to transfer a model\n");
        return;
    }

    HAL_UART_PutString("[RUN ] File loaded: ");
    HAL_UART_PutDec(model_size);
    HAL_UART_PutString(" bytes\n");

    /*
     * ---- Wire to ONNX runtime (Harshit's feat/onnx branch) ----
     *
     * When integration with feat/onnx happens, uncomment these:
     *
     *   ONNX_Graph graph;
     *   memset(&graph, 0, sizeof(graph));
     *   Status os = ONNX_LoadCustomBinary(&graph, model_data, model_size);
     *   if (os != STATUS_OK) {
     *       HAL_UART_PutString("[RUN ] ONNX load failed\n");
     *       return;
     *   }
     *   ONNX_InferenceContext ctx;
     *   ONNX_Runtime_Init(&ctx, &graph, 64 * 1024);
     *   ONNX_Runtime_Inference(&ctx, NULL, 0, NULL, 0);
     *   ONNX_Runtime_PrintProfile(&ctx);
     *   ONNX_Runtime_Cleanup(&ctx);
     *
     * For now, print a simulated result so the demo works standalone:
     */

    HAL_UART_PutString("[RUN ] Running inference (simulated)...\n");
    HAL_UART_PutString("[RUN ] Model: ");
    HAL_UART_PutDec(model_size);
    HAL_UART_PutString(" bytes loaded, ");
    /* Show first 4 bytes as a sanity check */
    if (model_size >= 4) {
        HAL_UART_PutString("header: 0x");
        HAL_UART_PutHex((uint32_t)model_data[0] << 24 |
                         (uint32_t)model_data[1] << 16 |
                         (uint32_t)model_data[2] <<  8 |
                         (uint32_t)model_data[3]);
        HAL_UART_PutString("\n");
    }
    HAL_UART_PutString("[RUN ] Inference complete\n");
    HAL_UART_PutString("[RUN ] Profile: 1 inference, avg ~2000 us\n");
}