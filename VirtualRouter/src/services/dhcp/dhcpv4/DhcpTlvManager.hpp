/**
 * @file DhcpTlvManager.hpp
 */

// DhcpTLVManager.hpp

#ifndef DHCP_TLV_MANAGER_HPP
#define DHCP_TLV_MANAGER_HPP

#include <ByteUtils.hpp>
#include "packet/TlvOptions.hpp"
#include "packet/headers/DhcpHeader.hpp"

namespace services::dhcp
{
struct DhcpTLVManager
{
    DhcpTLVManager(packet::DhcpHeader& dhcp, size_t size, bool isAuth, uint8_t* requests = nullptr, size_t reqSize = 0)
        : auth(isAuth),
          tlv(dhcp.getTrailData(), size),
          tlvFile(dhcp.raw->file, 128),
          tlvSname(dhcp.raw->serverName, 64),
          requestList(requests),
          requestSize(reqSize)
    {}

    bool auth = false;
    bool file = false;
    bool sname = true;
    packet::TLV8BufferManager tlv;
    packet::TLV8BufferManager tlvFile;
    packet::TLV8BufferManager tlvSname;

    uint8_t* requestList = nullptr;
    size_t requestSize = 0;
};

inline static bool appendTLV(DhcpTLVManager& tlv, uint8_t type, uint8_t size, const uint8_t* value)
{
    if (!tlv.file && !tlv.sname && tlv.tlv.hasRoom(size + 5 + (tlv.auth ? 34 : 0))) // space for override, auth, and end option
    {
        tlv.tlv.append(type, size, value, size);
        return true;
    }
    else if (!tlv.sname && tlv.tlvFile.hasRoom(size))
    {
        if (!tlv.file) tlv.file = true;
        tlv.tlvFile.append(type, size, value, size);
        return true;
    }
    else if (tlv.tlvSname.hasRoom(size))
    {
        if (!tlv.sname) tlv.sname = true;
        tlv.tlvSname.append(type, size, value, size);
        return true;
    }
    return false;
}

inline static bool appendTLV(DhcpTLVManager& tlv, uint8_t type, uint32_t value)
{
    auto zeroRequested = [&]() {
        if (!tlv.requestList) return;
        for (size_t i = 0; i < tlv.requestSize; ++i)
            if (tlv.requestList[i] == type)
                tlv.requestList[i] = 0;
    };

    uint8_t* buffer = nullptr;
    if (!tlv.file && !tlv.sname && tlv.tlv.hasRoom(9 + (tlv.auth ? 34 : 0))) // space for override, auth, and end option
    {
        buffer = tlv.tlv.getNextValBuf(9); // int32: 4, override/end: 5
        if (buffer)
        {
            utils::writeU32(buffer, value);
            tlv.tlv.append(type, 4, nullptr, 4);
            zeroRequested();
            return true;
        }
    }
    if (!tlv.sname)
    {
        if (!tlv.file) tlv.file = true;
        buffer = tlv.tlvFile.getNextValBuf(4);
        if (buffer)
        {
            utils::writeU32(buffer, value);
            tlv.tlvFile.append(type, 4, nullptr, 4);
            zeroRequested();
            return true;
        }
    }
    if (!tlv.sname) tlv.sname = true;
    buffer = tlv.tlvSname.getNextValBuf(4);
    if (buffer)
    {
        utils::writeU32(buffer, value);
        tlv.tlvSname.append(type, 4, nullptr, 4);
        zeroRequested();
        return true;
    }
    return false;
}
} // namespace services::dhcp

#endif // DHCP_TLV_MANAGER_HPP

