/**
 * @file OpaqueLsaV2.hpp
 * @brief OSPFv2 Opaque LSA body (Types 9, 10, 11) — RFC 2370.
 */

#ifndef OPAQUE_LSA_V2_HPP
#define OPAQUE_LSA_V2_HPP

#include <cstdint>
#include <vector>

#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{
/**
 * @brief Wire-format body of an OSPFv2 Opaque LSA (Types 9 / 10 / 11), as defined in RFC 2370.
 * @ingroup OSPF_V2_DATABASE
 *
 * Opaque LSAs carry application-specific data whose format is determined by
 * @ref opaqueType (e.g., Traffic Engineering extensions use type 1).  The
 * Link-State ID encodes both the opaque type (high byte) and a per-type
 * opaque identifier (low 24 bits).
 *
 * Scoping:
 * - Type 9 — link-local scope.
 * - Type 10 — area scope.
 * - Type 11 — AS scope.
 *
 * The payload is treated as an opaque byte sequence by OSPF; higher-layer
 * consumers are responsible for interpreting its contents.
 *
 * Use @ref build to parse a received buffer, and @ref buildBody to serialise
 * for transmission.
 */
struct OpaqueLsaV2
{
    uint8_t opaqueType;              ///< Opaque LSA type (top 8 bits of the Link-State ID).
    uint32_t opaqueId;               ///< Opaque identifier (low 24 bits of the Link-State ID).
    std::vector<uint8_t> payload;    ///< Opaque application payload bytes.

    /**
     * @brief Constructs an OpaqueLsaV2 from a raw buffer and the originating Link-State ID.
     *
     * The opaque type and identifier are extracted from @p linkStateId; the
     * entire @p buf is copied verbatim into @ref payload.
     *
     * @param linkStateId Full 32-bit Link-State ID from the LSA header.
     * @param buf         Pointer to the start of the LSA body payload.
     * @param len         Length of @p buf in bytes.
     * @return Fully populated OpaqueLsaV2 struct.
     */
    static OpaqueLsaV2 build(uint32_t linkStateId, const uint8_t* buf, uint16_t len)
    {
        OpaqueLsaV2 lsa;

        lsa.opaqueType = static_cast<uint8_t>(linkStateId >> 24);
        lsa.opaqueId = linkStateId & 0x00FFFFFF;
        lsa.payload.assign(buf, buf + len);
        return lsa;
    }

    /**
     * @brief Returns the serialised size of this LSA body in bytes.
     * @return Length of @ref payload.
     */
    uint16_t size() const
    {
        return static_cast<uint16_t>(payload.size());
    }

    /**
     * @brief Serialises this LSA body into a wire buffer.
     * @param buf Destination buffer; must be at least @ref size() bytes.
     * @param len Available bytes in @p buf; must be at least @c payload.size().
     * @return True on success; false if @p len is smaller than the payload.
     */
    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len < payload.size()) return false;
        std::memcpy(buf, payload.data(), payload.size());
        return true;
    }

    /**
     * @brief Feeds this LSA body's payload into a Fletcher checksum accumulator.
     * @param check Checksum accumulator to update.
     */
    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addBytes(payload.data(), payload.size());
    }
};
} // namespace routing::ospf

#endif // OPAQUE_LSA_V2_HPP
