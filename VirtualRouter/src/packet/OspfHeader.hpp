// OspfHeader.hpp

#ifndef OSPF_HEADER_HPP
#define OSPF_HEADER_HPP

#include <ByteString.hpp>
#include <optional>
#include <vector>
#include <Functions.h>

/**
 * @struct OspfPacket::ospfHeader
 * @brief Represents an OSPF (Open Shortest Path First) packet header.
 */
namespace OspfPacket
{
    struct ospfHeader
    {
        ByteString version{};       ///< OSPF version.
        ByteString type{};          ///< OSPF packet type.
        ByteString packetLength{};  ///< OSPF packet length.
        ByteString sourceRouter{};  ///< OSPF source router ID.
        ByteString areaID{};         ///< OSPF Area ID.
        ByteString checksum{};       ///< OSPF checksum.
        ByteString authType{};       ///< OSPF authentication type.
        ByteString authData{};       ///< OSPF authentication data.

        std::optional<ByteString> encapsulate()
        {
            return std::nullopt;
        }
        bool decapsulate(const ByteString Header)
        {
            return false;
        }
    };

    struct ospfHelloHeader
    {
        ByteString mask{};             ///< OSPF Hello mask.
        ByteString helloInterval{};    ///< OSPF Hello interval.
        ByteString routerPriority{};   ///< OSPF Router Priority.
        ByteString routerDeadInterval{}; ///< OSPF Router Dead Interval.
        ByteString designatedRouter{}; ///< OSPF Designated Router.
        ByteString backupDesignatedRouter{}; ///< OSPF Backup Designated Router.
        ByteString activeNeighbor{};    ///< OSPF Active Neighbor.

        /**
         * @struct options
         * @brief Represents OSPF Hello options.
         */
        struct options
        {
            ByteString notSet{};           ///< Not Set option.
            ByteString opaque{};            ///< Opaque option.
            ByteString demand{};            ///< Demand option.
            ByteString llsPresent{};        ///< Link-Layer Signaling Present option.
            ByteString nssa{};              ///< NSSA option.
            ByteString multicast{};         ///< Multicast option.
            ByteString externalRouting{};   ///< External Routing option.
            ByteString multiTopology{};     ///< Multi-Topology Routing option.
        } options;

        std::optional<ByteString> encapsulate()
        {
            return std::nullopt;
        }
        bool decapsulate(const ByteString Header)
        {
            return false;
        }
    };

    struct ospfDescriptionHeader
    {
        ByteString interfaceMtu{};      ///< OSPF Interface MTU.
        ByteString sequence{};          ///< OSPF Sequence number.

        /**
         * @struct options
         * @brief Represents OSPF Description options.
         */
        struct options
        {
            ByteString notSet{};           ///< Not Set option.
            ByteString opaque{};            ///< Opaque option.
            ByteString demand{};            ///< Demand option.
            ByteString llsPresent{};        ///< Link-Layer Signaling Present option.
            ByteString nssa{};              ///< NSSA option.
            ByteString multicast{};         ///< Multicast option.
            ByteString externalRouting{};   ///< External Routing option.
            ByteString multiTopology{};     ///< Multi-Topology Routing option.
        } options;

        /**
         * @struct description
         * @brief Represents OSPF Description flags.
         */
        struct description
        {
            ByteString OOBResync{};      ///< Out-of-Band Resynchronization flag.
            ByteString init{};            ///< INIT flag.
            ByteString more{};            ///< MORE flag.
            ByteString master{};          ///< MASTER flag.
        } description;

        std::optional<ByteString> encapsulate()
        {
            return std::nullopt;
        }
        bool decapsulate(const ByteString Header)
        {
            return false;
        }
    };

    struct ospfRequest
    {
        ByteString lsType{};           ///< Link State Type.
        ByteString linkStatID{};       ///< Link State ID.
        ByteString advertisingRouter{}; ///< Advertising Router.

        std::optional<ByteString> encapsulate()
        {
            return std::nullopt;
        }
        bool decapsulate(const ByteString Header)
        {
            return false;
        }
    };

    struct ospfLLSHeader
    {
        ByteString checksum{};     ///< OSPF Link-Layer Signaling checksum.
        ByteString dataLength{};   ///< OSPF Link-Layer Signaling data length.
        ByteString options{};      ///< OSPF Link-Layer Signaling options.

        std::optional<ByteString> encapsulate()
        {
            return std::nullopt;
        }
        bool decapsulate(const ByteString Header)
        {
            return false;
        }
    };

    struct LSA
    {
        ByteString lsAge{};         ///< Link State Advertisement age.
        ByteString doNotAge{};      ///< Do Not Age flag.
        ByteString lsType{};        ///< Link State type.
        ByteString linkStateID{};   ///< Link State ID.
        ByteString advertisingRouter{}; ///< Advertising Router ID.
        ByteString sequenceNumber{}; ///< Sequence Number.
        ByteString checksum{};       ///< Link State Advertisement checksum.
        ByteString length{};         ///< Link State Advertisement length.

        /**
         * @struct options
         * @brief Represents LSA options.
         */
        struct options
        {
            ByteString notSet{};           ///< Not Set option.
            ByteString opaque{};            ///< Opaque option.
            ByteString demand{};            ///< Demand option.
            ByteString llsPresent{};        ///< Link-Layer Signaling Present option.
            ByteString nssa{};              ///< NSSA option.
            ByteString multicast{};         ///< Multicast option.
            ByteString externalRouting{};   ///< External Routing option.
            ByteString multiTopology{};     ///< Multi-Topology Routing option.
        } options;

        std::optional<ByteString> encapsulate()
        {
            return std::nullopt;
        }
        bool decapsulate(const ByteString Header)
        {
            return false;
        }
    };

    struct ospfUpdateheader
    {
        ByteString numOfLsa{}; ///< Number of LSAs in the Update.
        std::vector<LSA> lsa{}; ///< Vector of LSAs.

        std::optional<ByteString> encapsulate()
        {
            return std::nullopt;
        }
        bool decapsulate(const ByteString Header)
        {
            return false;
        }
    };
}

#endif // OSPF_HEADER_HPP
