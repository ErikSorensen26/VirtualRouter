// OpaqueLsaV2.hpp

#ifndef OPAQUE_LSA_V2_HPP
#define OPAQUE_LSA_V2_HPP

#include <cstdint>
#include <vector>

#include "packet/HeaderHelpers.hpp"
#include "ospf/transmission/OspfFletcher.hpp"

namespace OSPF
{
struct OpaqueLsaV2
{
    uint8_t opaqueType;
    uint32_t opaqueId;
    std::vector<uint8_t> payload;

    static OpaqueLsaV2 build(uint32_t linkStateId, const uint8_t* buf, uint16_t len)
    {
        OpaqueLsaV2 lsa;

        lsa.opaqueType = static_cast<uint8_t>(linkStateId >> 24);
        lsa.opaqueId = linkStateId & 0x00FFFFFF;
        lsa.payload.assign(buf, buf + len);
        return lsa;
    }

    uint16_t size() const
    {
        return static_cast<uint16_t>(payload.size());
    }

    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len < payload.size()) return false;
        std::memcpy(buf, payload.data(), payload.size());
        return true;
    }

    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addBytes(payload.data(), payload.size());
    }
};
}


#endif // OPAQUE_LSA_V2_HPP
