// FloodTypes.hpp

#ifndef OSPF_FLOOD_TYPES_HPP
#define OSPF_FLOOD_TYPES_HPP

#include <cstdint>
#include <LsaKey.hpp>
#include <LSDB.hpp>

namespace OSPF
{
enum class LsaCompareResult
{
    NEWER,    
    OLDER,
    SAME,
};

enum class InstallAction : uint8_t
{
    REJECT_INVALID, // Checksum invalid (or caller marks invalid).
    IGNORE_OLDER,   // Incoming older than installed.
    IGNORE_DUPLICATE,   // Same instance; may update stored age if incoming younger.
    INSTALL_NEWER,  // Incoming newer; store and flood.
    FLUSH_MAX_AGE,  // Incoming is newer and MaxAge; flood flush, do not keep
    FIGHT_BACK_SELF // self-originated key received as newer; store then originate newer instance
};

struct InstallResult final
{
    InstallAction action{InstallAction::IGNORE_OLDER};
    LsaCompareResult compare{LsaCompareResult::SAME};

    // Storage instructions:
    bool shouldStoreReplace{false}; // replace (header/body) with incoming instance
    bool shouldUpdateAgeOnly{false}; // same instance; update stored header.age only
    bool shouldRunSpf{false}; // Run spf on this specific lsa installation
    uint16_t newStoredAge{0};        // valid if shouldUpdateAgeOnly==true

    // Flooding/origination signals:
    bool shouldFlood{false};         // enqueue flooding of THIS incoming instance (or flush)
    bool shouldFightBack{false};     // originate newer self instance (caller triggers)
};
}

#endif // OSPF_FLOOD_TYPES_HPP
