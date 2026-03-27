/**
 * @file InterfaceId.hpp
 * @brief Composite key identifying an OSPF interface by index and area.
 */

#ifndef OSPF_INTERFACE_ID_HPP
#define OSPF_INTERFACE_ID_HPP

#include <functional>
#include <cstdint>

namespace routing::ospf
{

/**
 * @brief Composite key that uniquely identifies an OSPF interface within a process.
 * @ingroup OSPF_INTERFACE
 *
 * An OSPF process may attach the same physical interface to different areas,
 * so the interface index alone is not sufficient to distinguish entries in the
 * interface map. This struct pairs the hardware interface index with the OSPF
 * area ID to form a fully-qualified key.
 *
 * ## Architectural Role
 * Used as the key type for `InterfaceManager::ospfInterfaceList` and as the
 * immutable identity carried by every `OspfInterface` instance. No two
 * `OspfInterface` objects within the same `OspfProcess` may share the same
 * `OspfInterfaceId`.
 *
 * ## Lifecycle & Ownership
 * Constructed at interface creation time and stored as a `const` member inside
 * `OspfInterface`. The value never changes for the lifetime of the interface.
 *
 * @see OspfInterface
 * @see InterfaceManager
 */
struct OspfInterfaceId
{
    OspfInterfaceId() = default;

    /**
     * @brief Constructs an interface ID from a hardware index and an area.
     *
     * @param id  Hardware interface index as assigned by the system.
     * @param a   OSPF area ID this interface participates in.
     */
    OspfInterfaceId(uint32_t id, uint32_t a)
        : interfaceId(id), area(a) {}

    const uint32_t interfaceId = 0; ///< Hardware interface index.
    const uint32_t area = 0;        ///< OSPF area this interface belongs to.

    bool operator==(const OspfInterfaceId& other) const {
        return interfaceId == other.interfaceId && area == other.area;
    }
};

} // namespace routing::ospf

namespace std
{
/**
 * @brief `std::hash` specialisation so `OspfInterfaceId` can be used as an
 *        unordered container key.
 * @ingroup OSPF_INTERFACE
 */
template <>
struct hash<routing::ospf::OspfInterfaceId>
{
    size_t operator()(const routing::ospf::OspfInterfaceId& k) const
    {
        return hash<uint32_t>{}(k.interfaceId) ^ (hash<uint32_t>{}(k.area));
    }
};
}

#endif // OSPF_INTERFACE_ID_HPP
