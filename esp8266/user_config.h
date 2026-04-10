/**
 * @file user_config.h
 * @brief MiniOS-ESP8266 Compile-Time Configuration
 *
 * Edit this file to configure Wi-Fi credentials, SFU parameters,
 * and model selection before building and flashing.
 *
 * All settings can also be overridden via UART shell at runtime.
 */

#ifndef MINIOS_ESP8266_USER_CONFIG_H
#define MINIOS_ESP8266_USER_CONFIG_H

/* ------------------------------------------------------------------ */
/*  Wi-Fi Configuration                                               */
/* ------------------------------------------------------------------ */

/** Wi-Fi network SSID to connect to */
#define WIFI_SSID           "YourNetworkSSID"

/** Wi-Fi password (WPA/WPA2 passphrase) */
#define WIFI_PASSWORD       "YourNetworkPassword"

/** Static IP configuration (set WIFI_USE_DHCP=1 to use DHCP instead) */
#define WIFI_USE_DHCP       1

/** Static IP address (only used if WIFI_USE_DHCP == 0) */
#define WIFI_STATIC_IP      "192.168.1.200"
#define WIFI_STATIC_GW      "192.168.1.1"
#define WIFI_STATIC_MASK    "255.255.255.0"

/* ------------------------------------------------------------------ */
/*  SFU Protocol Configuration                                        */
/* ------------------------------------------------------------------ */

/** UDP port for SFU inference server — must match sfu_client.py */
#define SFU_LISTEN_PORT     9000

/** Maximum SFU retransmission intervals */
#define SFU_TICK_INTERVAL_MS  100

/* ------------------------------------------------------------------ */
/*  ONNX Runtime Configuration                                        */
/* ------------------------------------------------------------------ */

/**
 * Default model to load on boot.
 * Options: "tiny_add" | "tiny_mlp"
 */
#define DEFAULT_MODEL       "tiny_mlp"

/**
 * Enable float32 → int8 quantization on inference input.
 * sfu_client.py always sends float32; this converts it to int8 internally.
 * Disabling this uses software float (very slow, ~100x slower).
 */
#define INFER_USE_INT8      1

/**
 * Inference input/output float buffer sizes.
 * Maximum floats the SFU server will accept/return per request.
 * Keep small — each float = 4 bytes.
 */
#define INFER_MAX_INPUT_FLOATS   64
#define INFER_MAX_OUTPUT_FLOATS  64

/* ------------------------------------------------------------------ */
/*  Debug Configuration                                               */
/* ------------------------------------------------------------------ */

/** Enable verbose SFU packet logging over UART */
#define DEBUG_SFU_VERBOSE   0

/** Enable ONNX inference timing output over UART */
#define DEBUG_INFER_TIMING  1

/** Enable Wi-Fi event logging */
#define DEBUG_WIFI_EVENTS   1

/** UART baud rate */
#define UART_BAUD_RATE      115200

/* ------------------------------------------------------------------ */
/*  Memory Configuration                                              */
/* ------------------------------------------------------------------ */

/** Heap size carved from dRAM for the bump allocator (bytes) */
#define KMEM_HEAP_SIZE      (24 * 1024)   /* 24 KB */

/** Maximum number of ONNX nodes in the tiny graph */
#define ONNX_TINY_MAX_NODES      16

/** Maximum number of ONNX tensors in the tiny graph */
#define ONNX_TINY_MAX_TENSORS    32

#endif /* MINIOS_ESP8266_USER_CONFIG_H */
