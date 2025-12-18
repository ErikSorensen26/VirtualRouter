// FloodRequestKey.hpp

#ifndef FLOOD_REQUEST_KEY_HPP
#define FLOOD_REQUEST_KEY_HPP

#include <LsaKey.hpp>
#include <functional>

namespace OSPF
{
struct FloodRequestKey final
{
    LsaKey key{};
    uint32_t area{0};
    uint32_t incomingInterface{0};
    uint8_t excludeIncoming{1};

    bool operator==(const FloodRequestKey& o) const noexcept
    {
        return area == o.area &&
               incomingInterface == o.incomingInterface &&
               excludeIncoming == o.excludeIncoming &&
               key == o.key;
    }
};
}

namespace std
{
template <>
struct hash<OSPF::FloodRequestKey>
{
    size_t operator()(const OSPF::FloodRequestKey& k) const noexcept
    {
        size_t h = std::hash<OSPF::LsaKey>{}(k.key);

        auto mix = [&](size_t x)
        {
            x ^= x >> 33;
            x *= 0xff51afd7ed558ccdull;
            x ^= x >> 33;
            x *= 0xc4ceb9fe1a85ec53ull;
            x ^= x >> 33;
            return x;
        };

        h ^= mix(static_cast<size_t>(k.area) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
        h ^= mix(static_cast<size_t>(k.incomingInterface) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
        h ^= mix(static_cast<size_t>(k.excludeIncoming) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
        return h;
    }
};
}

#endif // FLOOD_REQUEST_KEY_HPP
