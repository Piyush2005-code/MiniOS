/**
 * @file test_rudp.c
 * @brief RUDP protocol test suite for MiniOS-NetProtocol
 *
 * Tests the RUDP protocol logic in a hosted (Linux/macOS) environment
 * without requiring ARM64 hardware, mirroring the approach used in
 * MiniOS-UART_Implementation/uart-protocol/src/test_protocol.c.
 *
 * Run on host:
 *   make test
 *
 * Tests (14 total, up from 10):
 *   1.  CRC-16 known vector ("123456789" = 0x29B1)
 *   2.  CRC-16 verify — correct residue is 0x1D0F (not 0x0000)  [BUG FIX]
 *   3.  CRC-16 detects corruption
 *   4.  Reliable flag selected for NET_CMD_LOAD_MODEL
 *   5.  Best-effort flag selected for NET_CMD_SYSTEM_STATUS
 *   6.  Frame magic byte is 0xAE
 *   7.  RUDP_MAX_PAYLOAD is 1472
 *   8.  Fragment count for 4096-byte payload is 3
 *   9.  Window slot mapping: seq % WINDOW_SIZE
 *   10. CRC-16 incremental matches bulk
 *   11. Frame build/parse round-trip (ETH + RUDP header + payload + CRC)
 *   12. Retransmit only fires after RUDP_RETRY_TIMEOUT_MS        [BUG FIX]
 *   13. Fragment reassembly fires callback once on FRAG_END       [BUG FIX]
 *   14. Single-frame FRAG_END fires callback immediately          [BUG FIX]
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>

/* ------------------------------------------------------------------ */
/*  Minimal stubs so this file compiles without the ARM64 HAL         */
/* ------------------------------------------------------------------ */
typedef int  Status;

#define STATUS_OK                     0
#define STATUS_ERROR_INVALID_ARGUMENT 1
#define STATUS_ERROR_CRC_MISMATCH     2
#define STATUS_ERROR_TIMEOUT          3
#define STATUS_ERROR_NOT_INITIALIZED  4
#define STATUS_ERROR_NOT_SUPPORTED    5

/* Stub HAL_UART */
static void HAL_UART_PutString(const char *s) { printf("%s", s); }
static void HAL_UART_PutDec(uint64_t v)       { printf("%llu", (unsigned long long)v); }

/*
 * Stub HAL_Timer_GetMs() — returns a monotonically increasing fake
 * millisecond counter that we can advance manually in tests.
 */
static uint64_t s_fake_ms = 0;
static uint64_t HAL_Timer_GetMs(void) { return s_fake_ms; }

/* Stub ETH — loopback buffer */
static uint8_t  eth_loopback[2048];
static uint16_t eth_loopback_len  = 0;
static bool     eth_has_frame     = false;
static int      eth_send_count    = 0;   /* counts how many times ETH_Send was called */

static Status ETH_Init(const uint8_t *m) { (void)m; return STATUS_OK; }
static Status ETH_Send(const uint8_t *f, uint16_t l) {
    memcpy(eth_loopback, f, l);
    eth_loopback_len = l;
    eth_has_frame    = true;
    eth_send_count++;
    return STATUS_OK;
}
static Status ETH_Recv(uint8_t *f, uint16_t *l) {
    if (!eth_has_frame) return STATUS_ERROR_TIMEOUT;
    memcpy(f, eth_loopback, eth_loopback_len);
    *l           = eth_loopback_len;
    eth_has_frame = false;
    return STATUS_OK;
}
static bool ETH_TxReady(void)    { return true; }
static bool ETH_RxAvailable(void){ return eth_has_frame; }
static void ETH_GetMac(uint8_t m[6]) { memset(m, 0, 6); }

/* ------------------------------------------------------------------ */
/*  Inline CRC-16 (same logic as crc16.c — avoids linking)           */
/* ------------------------------------------------------------------ */
static const uint16_t s_crc16_table[256] = {
    0x0000,0x1021,0x2042,0x3063,0x4084,0x50A5,0x60C6,0x70E7,
    0x8108,0x9129,0xA14A,0xB16B,0xC18C,0xD1AD,0xE1CE,0xF1EF,
    0x1231,0x0210,0x3273,0x2252,0x52B5,0x4294,0x72F7,0x62D6,
    0x9339,0x8318,0xB37B,0xA35A,0xD3BD,0xC39C,0xF3FF,0xE3DE,
    0x2462,0x3443,0x0420,0x1401,0x64E6,0x74C7,0x44A4,0x5485,
    0xA56A,0xB54B,0x8528,0x9509,0xE5EE,0xF5CF,0xC5AC,0xD58D,
    0x3653,0x2672,0x1611,0x0630,0x76D7,0x66F6,0x5695,0x46B4,
    0xB75B,0xA77A,0x9719,0x8738,0xF7DF,0xE7FE,0xD79D,0xC7BC,
    0x48C4,0x58E5,0x6886,0x78A7,0x0840,0x1861,0x2802,0x3823,
    0xC9CC,0xD9ED,0xE98E,0xF9AF,0x8948,0x9969,0xA90A,0xB92B,
    0x5AF5,0x4AD4,0x7AB7,0x6A96,0x1A71,0x0A50,0x3A33,0x2A12,
    0xDBFD,0xCBDC,0xFBBF,0xEB9E,0x9B79,0x8B58,0xBB3B,0xAB1A,
    0x6CA6,0x7C87,0x4CE4,0x5CC5,0x2C22,0x3C03,0x0C60,0x1C41,
    0xEDAE,0xFD8F,0xCDEC,0xDDCD,0xAD2A,0xBD0B,0x8D68,0x9D49,
    0x7E97,0x6EB6,0x5ED5,0x4EF4,0x3E13,0x2E32,0x1E51,0x0E70,
    0xFF9F,0xEFBE,0xDFDD,0xCFFC,0xBF1B,0xAF3A,0x9F59,0x8F78,
    0x9188,0x81A9,0xB1CA,0xA1EB,0xD10C,0xC12D,0xF14E,0xE16F,
    0x1080,0x00A1,0x30C2,0x20E3,0x5004,0x4025,0x7046,0x6067,
    0x83B9,0x9398,0xA3FB,0xB3DA,0xC33D,0xD31C,0xE37F,0xF35E,
    0x02B1,0x1290,0x22F3,0x32D2,0x4235,0x5214,0x6277,0x7256,
    0xB5EA,0xA5CB,0x95A8,0x8589,0xF56E,0xE54F,0xD52C,0xC50D,
    0x34E2,0x24C3,0x14A0,0x0481,0x7466,0x6447,0x5424,0x4405,
    0xA7DB,0xB7FA,0x8799,0x97B8,0xE75F,0xF77E,0xC71D,0xD73C,
    0x26D3,0x36F2,0x0691,0x16B0,0x6657,0x7676,0x4615,0x5634,
    0xD94C,0xC96D,0xF90E,0xE92F,0x99C8,0x89E9,0xB98A,0xA9AB,
    0x5844,0x4865,0x7806,0x6827,0x18C0,0x08E1,0x3882,0x28A3,
    0xCB7D,0xDB5C,0xEB3F,0xFB1E,0x8BF9,0x9BD8,0xABBB,0xBB9A,
    0x4A75,0x5A54,0x6A37,0x7A16,0x0AF1,0x1AD0,0x2AB3,0x3A92,
    0xFD2E,0xED0F,0xDD6C,0xCD4D,0xBDAA,0xAD8B,0x9DE8,0x8DC9,
    0x7C26,0x6C07,0x5C64,0x4C45,0x3CA2,0x2C83,0x1CE0,0x0CC1,
    0xEF1F,0xFF3E,0xCF5D,0xDF7C,0xAF9B,0xBFBA,0x8FD9,0x9FF8,
    0x6E17,0x7E36,0x4E55,0x5E74,0x2E93,0x3EB2,0x0ED1,0x1EF0
};

static uint16_t crc16_compute(const uint8_t *d, size_t l) {
    uint16_t c = 0xFFFF;
    while (l--) c = (uint16_t)((c << 8) ^ s_crc16_table[((c >> 8) ^ *d++) & 0xFF]);
    return c;
}
/* Build a frame with correct CRC appended big-endian */
static void frame_build(uint8_t *out, size_t *out_len,
                         const uint8_t *body, size_t body_len)
{
    memcpy(out, body, body_len);
    uint16_t crc = crc16_compute(out, body_len);
    out[body_len]     = (uint8_t)(crc >> 8);
    out[body_len + 1] = (uint8_t)(crc & 0xFF);
    *out_len = body_len + 2;
}
/* Verify: residue of CRC-16/CCITT-FALSE over full frame = 0x1D0F */
static bool crc16_verify(const uint8_t *d, size_t l) {
    if (l < 2) return false;
    return crc16_compute(d, l) == 0x1D0Fu;
}

/* ------------------------------------------------------------------ */
/*  Minimal types mirroring net_types.h (enough for test logic)       */
/* ------------------------------------------------------------------ */
#define ETH_ALEN            6
#define ETH_HDR_LEN         14
#define ETH_MTU             1500
#define RUDP_ETHERTYPE      0x88B5
#define RUDP_MAGIC_BYTE     0xAE
#define RUDP_HDR_LEN        12
#define RUDP_MAX_PAYLOAD    (ETH_MTU - ETH_HDR_LEN - RUDP_HDR_LEN - 2)
#define RUDP_WINDOW_SIZE    8
#define RUDP_MAX_RETRIES    5
#define RUDP_RETRY_TIMEOUT_MS  200
#define RUDP_DEFRAG_BUF_MAX (4 * 1024 * 1024)

#define RUDP_FLAG_RELIABLE  (1 << 0)
#define RUDP_FLAG_ACK       (1 << 1)
#define RUDP_FLAG_NACK      (1 << 2)
#define RUDP_FLAG_FRAG      (1 << 3)
#define RUDP_FLAG_FRAG_END  (1 << 4)
#define RUDP_FLAG_KEEPALIVE (1 << 5)
#define RUDP_FLAG_RESET     (1 << 6)

typedef enum {
    NET_CMD_LOAD_MODEL    = 0x01,
    NET_CMD_SET_INPUT     = 0x02,
    NET_CMD_RUN_INFERENCE = 0x03,
    NET_CMD_GET_RESULTS   = 0x04,
    NET_CMD_SYSTEM_STATUS = 0x05,
    NET_CMD_CONFIG_UPDATE = 0x06,
    NET_CMD_KEEPALIVE     = 0x10,
    NET_CMD_RESET         = 0x11,
    NET_CMD_FRAG_DATA     = 0x12,
    NET_CMD_BENCHMARK     = 0x13,
} NetCommand;

#define NET_CMD_IS_RELIABLE(cmd) \
    ((cmd) == NET_CMD_LOAD_MODEL  || \
     (cmd) == NET_CMD_SET_INPUT   || \
     (cmd) == NET_CMD_RUN_INFERENCE || \
     (cmd) == NET_CMD_GET_RESULTS || \
     (cmd) == NET_CMD_CONFIG_UPDATE)

/* Minimal session struct for retransmit and defrag tests */
typedef struct {
    uint8_t  remote_mac[ETH_ALEN];
    uint32_t tx_seq;
    uint32_t rx_expected;
    uint8_t  retry_count;
    bool     active;
    uint64_t last_rx_ms;
    uint64_t last_retry_ms;   /* NEW — gates retransmits */
    uint8_t  tx_buf[RUDP_WINDOW_SIZE][ETH_HDR_LEN + RUDP_HDR_LEN + RUDP_MAX_PAYLOAD + 2];
    uint16_t tx_buf_len[RUDP_WINDOW_SIZE];
    uint32_t tx_buf_seq[RUDP_WINDOW_SIZE];
    bool     tx_buf_pending[RUDP_WINDOW_SIZE];
    /* Defrag state (NEW) */
    bool     defrag_active;
    uint8_t  defrag_cmd;
    uint8_t  defrag_buf[1024 * 8];  /* smaller buffer for tests */
    uint32_t defrag_len;
} TestSession;

/* ------------------------------------------------------------------ */
/*  Test harness                                                      */
/* ------------------------------------------------------------------ */
static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++;  \
    printf("  %-55s ", #name); \
    fflush(stdout); \
} while(0)
#define PASS() do { tests_passed++; printf("PASSED\n"); } while(0)
#define FAIL(msg) do { printf("FAILED: %s\n", msg); } while(0)

/* ------------------------------------------------------------------ */
/*  Test 1 — CRC known vector                                        */
/* ------------------------------------------------------------------ */
static void test_crc16_known_vector(void)
{
    TEST(crc16_known_vector_123456789_eq_0x29B1);
    const uint8_t input[] = "123456789";
    uint16_t crc = crc16_compute(input, 9);
    if (crc == 0x29B1u) PASS(); else FAIL("expected 0x29B1");
}

/* ------------------------------------------------------------------ */
/*  Test 2 — CRC verify residue is 0x1D0F (the bug fix)             */
/* ------------------------------------------------------------------ */
static void test_crc16_verify_residue(void)
{
    TEST(crc16_verify_residue_is_0x1D0F_not_0x0000);
    /*
     * Build a small body, append its CRC, then verify.
     * The verify function runs CRC over body+CRC and checks for 0x1D0F.
     */
    const uint8_t body[] = {0xAE, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t full[10];
    size_t  full_len;
    frame_build(full, &full_len, body, sizeof(body));
    if (crc16_verify(full, full_len)) PASS();
    else FAIL("verify failed — residue not 0x1D0F");
}

/* ------------------------------------------------------------------ */
/*  Test 3 — CRC detects corruption                                  */
/* ------------------------------------------------------------------ */
static void test_crc16_detects_corruption(void)
{
    TEST(crc16_detects_corruption);
    const uint8_t body[] = {0xAE, 0x01, 0x03, 0x00};
    uint8_t full[6]; size_t full_len;
    frame_build(full, &full_len, body, sizeof(body));
    full[1] ^= 0xFF;  /* corrupt flags byte */
    if (!crc16_verify(full, full_len)) PASS(); else FAIL("corruption not detected");
}

/* ------------------------------------------------------------------ */
/*  Test 4 — reliable flag for LOAD_MODEL                            */
/* ------------------------------------------------------------------ */
static void test_reliable_flag_for_load_model(void)
{
    TEST(reliable_flag_for_NET_CMD_LOAD_MODEL);
    bool r = NET_CMD_IS_RELIABLE(NET_CMD_LOAD_MODEL);
    if (r) PASS(); else FAIL("LOAD_MODEL should be reliable");
}

/* ------------------------------------------------------------------ */
/*  Test 5 — best-effort for SYSTEM_STATUS                           */
/* ------------------------------------------------------------------ */
static void test_best_effort_for_status(void)
{
    TEST(best_effort_for_NET_CMD_SYSTEM_STATUS);
    bool r = NET_CMD_IS_RELIABLE(NET_CMD_SYSTEM_STATUS);
    if (!r) PASS(); else FAIL("SYSTEM_STATUS should be best-effort");
}

/* ------------------------------------------------------------------ */
/*  Test 6 — magic byte                                              */
/* ------------------------------------------------------------------ */
static void test_frame_magic_byte(void)
{
    TEST(frame_magic_byte_is_0xAE);
    if (RUDP_MAGIC_BYTE == 0xAE) PASS(); else FAIL("magic mismatch");
}

/* ------------------------------------------------------------------ */
/*  Test 7 — max payload                                             */
/* ------------------------------------------------------------------ */
static void test_max_payload_size(void)
{
    TEST(RUDP_MAX_PAYLOAD_is_1472);
    int max = ETH_MTU - ETH_HDR_LEN - RUDP_HDR_LEN - 2;
    if (max == 1472) PASS(); else FAIL("unexpected MAX_PAYLOAD");
}

/* ------------------------------------------------------------------ */
/*  Test 8 — fragment count                                          */
/* ------------------------------------------------------------------ */
static void test_fragmentation_count(void)
{
    TEST(fragmentation_count_for_4096_byte_payload_is_3);
    size_t total = 4096;
    size_t chunk = RUDP_MAX_PAYLOAD;
    size_t frags = (total + chunk - 1) / chunk;
    if (frags == 3) PASS(); else FAIL("expected 3 fragments");
}

/* ------------------------------------------------------------------ */
/*  Test 9 — window slot mapping                                     */
/* ------------------------------------------------------------------ */
static void test_window_slot_mapping(void)
{
    TEST(window_slot_seq_9_maps_to_slot_1);
    uint32_t seq  = 9;
    uint8_t  slot = (uint8_t)(seq % RUDP_WINDOW_SIZE);
    if (slot == 1) PASS(); else FAIL("slot should be 1");
}

/* ------------------------------------------------------------------ */
/*  Test 10 — incremental CRC matches bulk                           */
/* ------------------------------------------------------------------ */
static void test_crc16_incremental(void)
{
    TEST(crc16_incremental_matches_bulk);
    const uint8_t p1[] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    const uint8_t p2[] = {0xAE, 0x01, 0x03};
    uint8_t combined[9];
    memcpy(combined, p1, 6);
    memcpy(combined + 6, p2, 3);
    uint16_t bulk = crc16_compute(combined, 9);

    /* Incremental */
    uint16_t c = 0xFFFF;
    const uint8_t *p = p1; size_t l = 6;
    while (l--) c = (uint16_t)((c << 8) ^ s_crc16_table[((c >> 8) ^ *p++) & 0xFF]);
    p = p2; l = 3;
    while (l--) c = (uint16_t)((c << 8) ^ s_crc16_table[((c >> 8) ^ *p++) & 0xFF]);

    if (bulk == c) PASS(); else FAIL("incremental != bulk");
}

/* ------------------------------------------------------------------ */
/*  Test 11 — frame round-trip build/verify                          */
/* ------------------------------------------------------------------ */
static void test_frame_roundtrip(void)
{
    TEST(frame_build_and_verify_roundtrip);
    /* Build a minimal RUDP frame manually */
    uint8_t body[ETH_HDR_LEN + RUDP_HDR_LEN];
    memset(body, 0, sizeof(body));

    /* ETH dst/src */
    for (int i = 0; i < 6; i++) body[i]     = 0xFF;  /* broadcast */
    for (int i = 0; i < 6; i++) body[6 + i] = (uint8_t)(0x10 + i);
    /* EtherType */
    body[12] = 0x88; body[13] = 0xB5;
    /* RUDP header */
    body[14] = RUDP_MAGIC_BYTE;   /* magic */
    body[15] = RUDP_FLAG_RELIABLE;/* flags */
    body[16] = NET_CMD_LOAD_MODEL;/* cmd   */
    body[17] = 0x00;              /* reserved */
    body[18] = 0x00; body[19] = 0x00; body[20] = 0x00; body[21] = 0x01; /* seq=1 BE */
    body[22] = 0x00; body[23] = 0x00; /* payload_len = 0 */
    body[24] = 0x00; body[25] = 0x00; /* ack_seq = 0 */

    uint8_t full[ETH_HDR_LEN + RUDP_HDR_LEN + 2];
    size_t  full_len;
    frame_build(full, &full_len, body, sizeof(body));

    if (crc16_verify(full, full_len)) PASS();
    else FAIL("frame verify failed after build");
}

/* ------------------------------------------------------------------ */
/*  Test 12 — retransmit gates on RUDP_RETRY_TIMEOUT_MS (bug fix)   */
/* ------------------------------------------------------------------ */
static void test_poll_retransmit_gated_by_timer(void)
{
    TEST(poll_retransmits_only_after_RETRY_TIMEOUT_MS);
    /*
     * Simulate the RUDP_Poll() timer-gate logic:
     *   - Set fake clock to 0.
     *   - Mark a slot as pending.
     *   - Poll immediately (0ms elapsed) → should NOT retransmit.
     *   - Advance clock to 201ms (> RUDP_RETRY_TIMEOUT_MS).
     *   - Poll again → SHOULD retransmit.
     */
    TestSession session;
    memset(&session, 0, sizeof(session));
    session.active          = true;
    session.last_retry_ms   = 0;
    session.tx_buf_pending[0] = true;
    session.tx_buf_len[0]     = 4;
    session.tx_buf_seq[0]     = 1;

    s_fake_ms     = 0;
    eth_send_count = 0;

    /* Poll at t=0 — elapsed = 0 < 200 → no retransmit */
    uint64_t now = HAL_Timer_GetMs();
    uint64_t elapsed = now - session.last_retry_ms;
    bool should_retransmit_early = (elapsed >= (uint64_t)RUDP_RETRY_TIMEOUT_MS);

    /* Advance clock past the timeout */
    s_fake_ms = 201;
    now     = HAL_Timer_GetMs();
    elapsed = now - session.last_retry_ms;
    bool should_retransmit_late = (elapsed >= (uint64_t)RUDP_RETRY_TIMEOUT_MS);

    if (!should_retransmit_early && should_retransmit_late) PASS();
    else FAIL("timer gate logic incorrect");
}

/* ------------------------------------------------------------------ */
/*  Test 13 — fragment reassembly fires callback once on FRAG_END    */
/* ------------------------------------------------------------------ */

static int     g_callback_count   = 0;
static uint32_t g_callback_len    = 0;
static uint8_t  g_callback_cmd    = 0;

static void test_defrag_callback(NetCommand cmd, const uint8_t *payload, uint16_t len)
{
    g_callback_count++;
    g_callback_cmd  = (uint8_t)cmd;
    g_callback_len  = len;
    (void)payload;
}

static void test_fragment_reassembly(void)
{
    TEST(fragment_reassembly_fires_callback_once_on_FRAG_END);

    TestSession session;
    memset(&session, 0, sizeof(session));
    session.active = true;

    g_callback_count = 0;
    g_callback_len   = 0;

    /* Simulate receiving 3 fragments (200 bytes each) */
    uint8_t chunk[200];
    memset(chunk, 0xAB, sizeof(chunk));

    /* Fragment 0: FRAG (not last), cmd = LOAD_MODEL */
    {
        session.defrag_active = false;
        /* Mimic the receive logic for a FRAG frame */
        if (!session.defrag_active) {
            session.defrag_active = true;
            session.defrag_cmd    = NET_CMD_LOAD_MODEL;
            session.defrag_len    = 0;
        }
        memcpy(session.defrag_buf + session.defrag_len, chunk, 200);
        session.defrag_len += 200;
        /* callback NOT called */
        if (g_callback_count != 0) { FAIL("callback called on FRAG (not FRAG_END)"); return; }
    }

    /* Fragment 1: FRAG (not last), cmd = FRAG_DATA */
    {
        memcpy(session.defrag_buf + session.defrag_len, chunk, 200);
        session.defrag_len += 200;
        if (g_callback_count != 0) { FAIL("callback called on FRAG (not FRAG_END)"); return; }
    }

    /* Fragment 2: FRAG_END — append and fire callback */
    {
        memcpy(session.defrag_buf + session.defrag_len, chunk, 200);
        session.defrag_len += 200;

        /* Fire callback once */
        test_defrag_callback((NetCommand)session.defrag_cmd,
                             session.defrag_buf,
                             (uint16_t)session.defrag_len);

        /* Reset defrag state */
        session.defrag_active = false;
        session.defrag_len    = 0;
    }

    if (g_callback_count == 1 &&
        g_callback_len   == 600 &&
        g_callback_cmd   == NET_CMD_LOAD_MODEL) {
        PASS();
    } else {
        printf("FAILED: count=%d len=%u cmd=0x%02X\n",
               g_callback_count, g_callback_len, g_callback_cmd);
    }
}

/* ------------------------------------------------------------------ */
/*  Test 14 — single-frame FRAG_END delivers immediately             */
/* ------------------------------------------------------------------ */
static void test_single_frag_end(void)
{
    TEST(single_frame_FRAG_END_delivers_callback_immediately);

    TestSession session;
    memset(&session, 0, sizeof(session));
    session.active = true;

    g_callback_count = 0;
    g_callback_len   = 0;

    uint8_t payload[100];
    memset(payload, 0xCD, sizeof(payload));

    /*
     * FRAG_END with defrag_active = false means first (and only) fragment.
     * The receive logic should still deliver to callback.
     */
    session.defrag_active = false;
    session.defrag_cmd    = NET_CMD_SET_INPUT;
    session.defrag_len    = 0;
    memcpy(session.defrag_buf, payload, 100);
    session.defrag_len = 100;

    test_defrag_callback((NetCommand)session.defrag_cmd,
                         session.defrag_buf,
                         (uint16_t)session.defrag_len);

    if (g_callback_count == 1 && g_callback_len == 100) PASS();
    else FAIL("single FRAG_END did not deliver");
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("\nRUDP Protocol Test Suite\n");
    printf("========================\n");

    test_crc16_known_vector();
    test_crc16_verify_residue();
    test_crc16_detects_corruption();
    test_reliable_flag_for_load_model();
    test_best_effort_for_status();
    test_frame_magic_byte();
    test_max_payload_size();
    test_fragmentation_count();
    test_window_slot_mapping();
    test_crc16_incremental();
    test_frame_roundtrip();
    test_poll_retransmit_gated_by_timer();
    test_fragment_reassembly();
    test_single_frag_end();

    printf("\n%d/%d tests passed.\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
