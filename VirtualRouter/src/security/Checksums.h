/**
 * @file Checksums.h
 * @brief Packet checksum calculation utilities for protocol header validation.
 * @ingroup SECURITY
 */

/**
 * @defgroup SECURITY Security
 * @brief Cryptographic primitives, checksum utilities, and key-chain management.
 */

#ifndef CHECKSUMS_H
#define CHECKSUMS_H

#include <cstdint>
#include <cstddef>

namespace security { enum class HeaderType : uint8_t; }

/**
 * @namespace security::checksum
 * @brief Checksum computation routines used for protocol header integrity verification.
 *
 * Provides a CRC32 implementation and a generic one's-complement Internet
 * checksum writer that handles optional pseudo-header inclusion and byte-swap
 * control. These are used by protocol TX paths to stamp outgoing packets before
 * they are handed to the egress pipeline.
 */
namespace security::checksum
{

/**
 * @brief Computes a CRC32 checksum over the given data.
 *
 * Uses a standard CRC32 polynomial table. The 4-byte result is written to
 * @p out in little-endian order.
 *
 * @param out      Destination for the 4-byte CRC32 result; must not overlap @p data.
 * @param data     Input data to checksum.
 * @param dataSize Length of @p data in bytes.
 * @return True on success, false if @p out or @p data is null.
 *
 * @note CRC32 provides error detection, not authentication; use HMAC for
 *       cryptographic integrity.
 */
bool crc32(uint8_t* out, const uint8_t* data, size_t dataSize);

/**
 * @brief Computes and inserts an Internet (one's-complement) checksum into a packet header.
 *
 * Calculates a standard RFC 791 / RFC 768 one's-complement checksum over the
 * header bytes, optionally prepending a pseudo-header (e.g. for TCP/UDP/ICMPv6
 * checksum calculations that cover source/destination address fields). The
 * checksum is written directly into the packet buffer at @p checksumStartIndex.
 *
 * The checksum field within @p packet must be zeroed before calling this
 * function; the calculation treats the two-byte field as zero when it
 * covers the header region.
 *
 * @param packet              Pointer to the start of the header to checksum; also
 *                            the destination for the result.
 * @param headerSize          Number of bytes of @p packet to include in the calculation.
 * @param checksumStartIndex  Byte offset inside @p packet where the 2-byte checksum
 *                            result will be written.
 * @param checksumSize        Width of the checksum field in bytes (typically 2).
 * @param pseudoHeader        Optional pseudo-header prepended before the packet data
 *                            during calculation (e.g. IPv4/IPv6 pseudo-header for
 *                            transport-layer checksums). May be nullptr.
 * @param pseudoHeaderSize    Length of @p pseudoHeader in bytes; ignored when
 *                            @p pseudoHeader is nullptr.
 * @param swap                When true, the computed checksum is byte-swapped before
 *                            being written, converting between host and network byte order.
 *
 * @warning The checksum field at [@p checksumStartIndex, @p checksumStartIndex +
 *          @p checksumSize) must be zeroed by the caller before this function is
 *          invoked; otherwise the calculation will be incorrect.
 */
void calculateChecksum(uint8_t* packet, size_t headerSize, size_t checksumStartIndex, size_t checksumSize, const uint8_t* pseudoHeader = nullptr, size_t pseudoHeaderSize = 0, bool swap = false);

} // namespace security::checksum

#endif // CHECKSUMS_H
