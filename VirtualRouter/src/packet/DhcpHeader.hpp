// DhcpHeader.hpp

#ifndef DHCP_HEADER_HPP
#define DHCP_HEADER_HPP

#include <HeaderHelpers.hpp>
#include <TLVOptions.hpp>
#include <vector>

/**
 * @struct DhcpHeaderRaw
 * @brief Represents a raw DHCP header
 */
#pragma pack(push, 1)
struct DhcpHeaderRaw
{
    uint8_t opcode;         ///< Boot Message Type.
    uint8_t hType;          ///< Hardware Type.
    uint8_t hLen;           ///< Hardware Address Length.
    uint8_t hops;           ///< Hops.
    uint8_t xId[4];         ///< Transaction ID.
    uint8_t secs[2];        ///< Seconds Elapsed.
    uint8_t flags[2];       ///< Flags
    uint8_t ciaddr[4];      ///< Client IP Address.
    uint8_t yiaddr[4];      ///< 'Your' (Client) IP Address.
    uint8_t siaddr[4];      ///< Next Server IP Address.
    uint8_t giaddr[4];      ///< Relay Agent IP Address.
    uint8_t chaddr[16];     ///< Client MAC Address + padding.
    uint8_t serverName[64]; ///< Server Hostname.
    uint8_t file[128];      ///< Boot File Name.
    uint8_t magicCookie[4]; ///< Magic Cookie.
};
#pragma pack(pop)


/**
 * @struct DhcpHeader
 * @brief Represents a DHCP (Dynamic Host Configuration Protocol) header.
 */
struct DhcpHeader
{
    DEFINE_PACKET_HEADER(DhcpHeaderRaw);

    uint8_t getOpcode() const { return raw->opcode; }
    uint8_t getHType() const { return raw->hType; }
    uint8_t getHLen() const { return raw->hLen; }
    uint8_t getHops() const { return raw->hops; }
    const uint8_t* getXid() const { return raw->xId; }
    uint16_t getSecs() const { return readU16(raw->secs); }
    uint16_t getFlags() const { return readU16(raw->flags); }
    const uint8_t* getClientIP() const { return raw->ciaddr; }
    uint32_t getClientIPInt() const { return readU32(raw->ciaddr); }
    const uint8_t* getYourIP() const { return raw->yiaddr; }
    const uint8_t* getNextServerIP() const { return raw->siaddr; }
    const uint8_t* getRelayAgentIP() const { return raw->giaddr; }
    const uint8_t* getClientMac() const { return raw->chaddr; }
    const uint8_t* getServerName() const { return raw->serverName; }
    const uint8_t* getBootFile() const { return raw->file; }
    uint32_t getMagicCookie() const { return readU32(raw->magicCookie); }

    void setOpcode(uint8_t val)
        { raw->opcode = val; }
    void setHType(uint8_t val)
        { raw->hType = val; }
    void setHLen(uint8_t val)
        { raw->hLen = val; }
    void setHops(uint8_t val)
        { raw->hops = val; }
    void const setXid(uint32_t val)
        { writeU32(raw->xId, val); }
    void const setXid(const uint8_t* val)
        { std::memcpy(raw->xId, val, 4); }
    void setSecs(uint16_t val)
        { writeU16(raw->secs, val); }
    void setFlags(const uint8_t* val)
        { std::memcpy(raw->flags, val, 2); }
    void setClientIp(const uint8_t* val)
        { std::memcpy(raw->ciaddr, val, 4); }
    void setYourIp(const uint8_t* val)
        { std::memcpy(raw->yiaddr, val, 4); }
    void setNextServerIP(const uint8_t* val)
        { std::memcpy(raw->siaddr, val, 4); }
    void setRelayAgentIp(const uint8_t* val)
        { std::memcpy(raw->giaddr, val, 4); }
    void setClientMac(const uint8_t* val)
        { std::memcpy(raw->chaddr, val, getHLen()); }
    void setServerName(const uint8_t* val)
        { std::memcpy(raw->serverName, val, 64); }
    void setBootFile(const uint8_t* val)
        { std::memcpy(raw->file, val, 128); }
    void setMagicCookie(uint32_t val)
        { writeU32(raw->magicCookie, val); }
};

inline bool parseDhcpOptions(uint8_t* data, size_t size, std::vector<TLV8Option>& outOptions, bool overload = false)
{
    size_t offset = 0;
    while (offset + 2 <= size)
    {
        if (overload && data[offset] == 0x00) return true; // end of options in overload
        const uint8_t type = data[offset];
        const uint8_t length = data[offset + 1];

        if (offset + 2 + length > size) return false;

        uint8_t* value = data + offset + 2;
        outOptions.emplace_back(type, length, value, length);
        
        offset += 2 + length;
    }
    return offset == size;
}

#endif // DHCP_HEADER_HPP
