// EthernetHeader.hpp

#ifndef ETHERNET_HEADER_HPP
#define ETHERNET_HEADER_HPP

#include <ByteString.hpp>
#include <optional>

/**
 * @struct EthernetHeader
 * @brief Represents an Ethernet frame header.
 */
struct EthernetHeader
{
    ByteString sourceMac{};         ///< Source MAC address
    ByteString destinationMac{};    ///< Destination MAC address
    ByteString type{};              ///< Next header type

    const std::optional<ByteString> encapsulate() const
    {
        //Profiler::getInstance().notify("ethernet encap start");
        ByteString ethernetString;
        ethernetString += destinationMac.size() == 6 ? destinationMac : ByteString(6, '\x00');
        if (sourceMac.size() != 6 || type.size() != 2) return std::nullopt;

        ethernetString.reserve(14);
        ethernetString.append(sourceMac);
        ethernetString.append(type);

        return ethernetString;
        //Profiler::getInstance().notify("ethernet encap end");
    }
    bool decapsulate(const ByteString ethernetHeader)
    {
        //Profiler::getInstance().notify("ethernet decap start");
        if (ethernetHeader.size() != 14) 
            return false;
        destinationMac = ethernetHeader.substr(0, 6);
        sourceMac = ethernetHeader.substr(6, 6);
        type = ethernetHeader.substr(12, 2);
        //Profiler::getInstance().notify("ethernet decap end");
        return true;
    }
};

#endif // ETHERNET_HEADER_HPP
