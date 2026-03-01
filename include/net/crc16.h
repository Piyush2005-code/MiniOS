/**
 * @file crc16.h
 * @brief CRC-16/CCITT-FALSE checksum for MiniOS-NetProtocol
 *
 * Provides a stronger integrity check than the CRC-8 XOR used in
 * MiniOS-UART_Implementation, appropriate for Ethernet frame sizes.
 *
 * CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF, refin=false, refout=false
 * This variant is commonly used in embedded networking (X.25, HDLC).
 *
 * The table is generated at compile time (const) — no runtime init needed,
 * compatible with static ROM placement (DC-003: code size ≤ 256KB).
 * Table size: 256 × 2 bytes = 512 bytes.
 */

#ifndef MINIOS_CRC16_H
#define MINIOS_CRC16_H

#include "../../include/types.h"

/**
 * @brief Compute CRC-16/CCITT-FALSE over a data buffer.
 *
 * @param[in] data  Pointer to input bytes
 * @param[in] len   Number of bytes
 * @return 16-bit CRC value
 *
 * @complexity O(n), constant per byte, no dynamic allocation
 */
uint16_t CRC16_Compute(const uint8_t *data, size_t len);

/**
 * @brief Update a running CRC-16 with additional data.
 *
 * Allows incremental CRC calculation over discontiguous buffers
 * (e.g., Ethernet header + RUDP header + payload separately).
 *
 * @param[in] crc   Running CRC (initialize with 0xFFFF)
 * @param[in] data  Additional bytes
 * @param[in] len   Length of additional bytes
 * @return Updated CRC value
 */
uint16_t CRC16_Update(uint16_t crc, const uint8_t *data, size_t len);

/**
 * @brief Verify CRC-16 of a complete buffer (including the 2-byte CRC at end).
 *
 * The last 2 bytes of the buffer must be the big-endian CRC.
 * A valid frame verifies to 0x0000 (residue of this CRC variant).
 *
 * @param[in] data  Complete buffer including CRC bytes at end
 * @param[in] len   Total buffer length (including 2 CRC bytes)
 * @return true if CRC valid, false otherwise
 */
bool CRC16_Verify(const uint8_t *data, size_t len);

#endif /* MINIOS_CRC16_H */