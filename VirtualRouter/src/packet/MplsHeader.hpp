// MplsHeader.hpp

#ifndef MPLS_HEADER_HPP
#define MPLS_HEADER_HPP

#include <ByteString.hpp>
#include <optional>
#include <Functions.h>

/**
 * @struct MplsHeader
 * @brief Represents an MPLS (Multiprotocol Label Switching) header.
 */
struct MplsHeader
{
    ByteString label{};           ///< MPLS label.
    ByteString expBit{};          ///< Experimental bits (EXP).
    ByteString bottomLabelStack{};///< Bottom of Stack bit.
    ByteString TTL{};             ///< Time-To-Live (TTL).

    const std::optional<ByteString> encapsulate() const
    {
        ByteString mplsString;
        if (/*label.size() != 3 || TTL.size() != 1*/false) return std::nullopt;

        mplsString.reserve(8);
        mplsString += label;
        //mplsString += Functions::binToHex(mpls.expBit + mpls.bottomLabelStack);
        mplsString += TTL;
        mplsString = Functions::hexToByte(mplsString);

        return mplsString;
    }
    bool decapsulate(const ByteString mplsHeader)
    {
        if (mplsHeader.size() != 8) return false;
            
        label = mplsHeader.substr(0, 5);
        expBit = (Functions::hexToBin(mplsHeader.substr(5, 1))).substr(0, 3);
        bottomLabelStack = (Functions::hexToBin(mplsHeader.substr(5, 1))).substr(3, 1);
        TTL = mplsHeader.substr(6, 2);

        return true;
    }
};

#endif // MPLS_HEADER_HPP
