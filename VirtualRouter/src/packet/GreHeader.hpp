// GreHeader.hpp

#ifndef GRE_HEADER_HPP
#define GRE_HEADER_HPP

#include <ByteString.hpp>
#include <optional>
#include <Functions.h>

/**
 * @struct GreHeade
 * @brief Represents a GRE (Generic Routing Encapsulation) header.
 */
struct GreHeade
{
    /**
     * @struct Flags
     * @brief Represents GRE flags.
     */
    struct Flags
    {
        ByteString checksum{};              ///< Checksum flag.
        ByteString routing{};                ///< Routing flag.
        ByteString key{};                    ///< Key flag.
        ByteString seqNum{};                 ///< Sequence Number flag.
        ByteString strictSourceRoute{};      ///< Strict Source Route flag.
        ByteString acknowledgment{};         ///< Acknowledgment flag.
        ByteString recursion{};              ///< Recursion flags.
        ByteString reserved{};               ///< Reserved flags.
        ByteString version{};                ///< GRE version.
    } flags;

    ByteString protocol{};        ///< GRE Protocol Type.
    ByteString length{};          ///< GRE Length.
    ByteString callID{};          ///< GRE Call ID.
    ByteString seqNum{};          ///< GRE Sequence Number.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString greString;
        if (flags.checksum.size() != 1 || flags.routing.size() != 1 || flags.key.size() != 1 || flags.seqNum.size() != 1 ||
            flags.strictSourceRoute.size() != 1 || flags.recursion.size() != 3 || flags.acknowledgment.size() != 1 || flags.reserved.size() != 4 ||
            flags.version.size() != 3 || protocol.size() != 2 || length.size() != 2 || callID.size() != 2 || seqNum.size() != 4) return std::nullopt;

        greString.reserve(12);
        greString += Functions::binToByte(flags.checksum + flags.routing + flags.key + flags.seqNum + flags.strictSourceRoute + flags.recursion + flags.acknowledgment + flags.recursion + flags.version);
        greString += protocol;
        greString += length;
        greString += callID;
        greString += seqNum;

        return greString;
    }
    bool decapsulate(const ByteString greHeader)
    {
        if (greHeader.size() != 12) return false;

        ByteString flagOpts = Functions::byteToBin(greHeader.substr(0, 2));
        flags.checksum = flagOpts.substr(0, 1);
        flags.routing = flagOpts.substr(1, 1);
        flags.key = flagOpts.substr(2, 1);
        flags.seqNum = flagOpts.substr(3, 1);
        flags.strictSourceRoute = flagOpts.substr(4, 1);
        flags.recursion = (Functions::byteToBin(greHeader.substr(0, 2))).substr(5, 3);
        flags.acknowledgment = flagOpts.substr(8, 1);
        flags.reserved = (Functions::byteToBin(greHeader.substr(0, 2))).substr(9, 4);
        flags.version = (Functions::byteToBin(greHeader.substr(0, 2))).substr(13, 3);
        protocol = greHeader.substr(2, 2);
        length = greHeader.substr(4, 2);
        callID = greHeader.substr(6, 2);
        seqNum = greHeader.substr(8, 4);

        return true;
    }
};

#endif // GRE_HEADER_HPP
