// TcpOptions.cpp

#include "TcpOptions.h"

#include <TcpHeader.hpp>
#include <cstddef>
#include <cstdint>

namespace TCP
{
TcpOptions TcpOptions::parse(std::vector<TLV8Option>& opts)
{
    TcpOptions out{};
    for (auto& opt : opts)
    {
        switch (opt.type)
        {
            case TCP_OPTION_MSS:
                if (opt.length == 4)
                    out.mss = readU16(opt.value);
                break;
            case TCP_OPTION_WINDOW_SCALE:
                if (opt.length == 3)
                    out.windowScale = opt.value[0];
                break;
            case TCP_OPTION_SACK_PERMITTED:
                if (opt.length == 2)
                    out.sackPermitted = true;
                break;
            case TCP_OPTION_SACK:
            {
                if (out.sackPermitted && (opt.valueSize % 8 == 0))
                {
                    out.sackBlocks.clear();
                    for (size_t j = 0; j < opt.valueSize; j += 8)
                    {
                        TcpSackBlock b{};
                        b.leftEdge = readU32(opt.value + j);
                        b.rightEdge = readU32(opt.value + j + 4);
                        out.sackBlocks.push_back(b);
                    }
                }
                break;
            }
            case TCP_OPTION_TIMESTAMP:
            {
                if (opt.length == 10)
                {
                    TcpTimestampOption ts{};
                    ts.tsVal = readU32(opt.value);
                    ts.tsEcr = readU32(opt.value + 4);
                    out.timestamp = ts;
                }
                break;
            }
            default:
                break;
        }
    }

    return out;
}

size_t TcpOptions::encode(uint8_t* buf, size_t maxSize) const
{
    TLV8BufferManager opts(buf, maxSize);
    if (mss.has_value())
    {
        auto val = opts.getNextValBuf(2);
        if (!val) return false;
        writeU16(val, mss.value());
        opts.append(TCP_OPTION_MSS, 4, nullptr, 2);
    }
    if (windowScale.has_value())
    {
        auto val = opts.getNextValBuf(1);
        if (!val) return false;
        val[0] = windowScale.value();
        opts.append(TCP_OPTION_WINDOW_SCALE, 3, nullptr, 1);
    }
    if (sackPermitted)
    {
        opts.append(TCP_OPTION_SACK_PERMITTED, 2, nullptr, 0);
    }

    if (timestamp.has_value())
    {
        const auto& ts = *timestamp;
        auto val = opts.getNextValBuf(8);
        if (!val) return false;
        writeU32(val, ts.tsVal);
        writeU32(val + 4, ts.tsEcr);
        opts.append(TCP_OPTION_TIMESTAMP, 10, nullptr, 8);
    }
    if (sackPermitted && !sackBlocks.empty())
    {
        uint8_t len = static_cast<uint8_t>(8 * sackBlocks.size());
        if (len < 255)
        {
            auto val = opts.getNextValBuf(len);
            if (!val) return false;
            uint8_t offset = 0;
            for (const auto& b : sackBlocks)
            {
                writeU32(val + offset, b.leftEdge);
                offset += 4;
                writeU32(val + offset, b.rightEdge);
                offset += 4;
            }
            opts.append(TCP_OPTION_SACK, len + 2, nullptr, len);
        }
        else return false;
    }

    while ((opts.size() % 4) != 0) opts.addLen(1);
    return true;
}

bool TcpOptions::empty() const noexcept
{
    return !mss.has_value() &&
           !windowScale.has_value() &&
           !timestamp.has_value() &&
           sackBlocks.empty() &&
           rawOptions.empty();
}
}
