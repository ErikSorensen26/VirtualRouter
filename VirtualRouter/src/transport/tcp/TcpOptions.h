// TcpOptions.h

#ifndef TCP_OPTIONS_H
#define TCP_OPTIONS_H

#include <TLVOptions.hpp>
#include <cstdint>
#include <optional>
#include <vector>

namespace TCP
{
struct TcpSackBlock final
{
    uint32_t leftEdge{0};
    uint32_t rightEdge{0};
};

struct TcpTimestampOption final
{
    uint32_t tsVal{0};
    uint32_t tsEcr{0};
};

struct TcpOptions final
{
    std::optional<uint16_t> mss;
    std::optional<uint8_t> windowScale;
    bool sackPermitted{false};
    std::optional<TcpTimestampOption> timestamp;

    std::vector<TcpSackBlock> sackBlocks;
    std::vector<uint8_t> rawOptions;

    static TcpOptions parse(std::vector<TLV8Option>& opts);
    size_t encode(uint8_t* buf, size_t maxSize) const;

    bool empty() const noexcept;
};
}

#endif // TCP_OPTIONS_H
