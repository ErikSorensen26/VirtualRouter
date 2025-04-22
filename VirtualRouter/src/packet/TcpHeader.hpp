// TcpHeader.hpp

#ifndef TCP_HEADER_HPP
#define TCP_HEADER_HPP

#include <ByteString.hpp>
#include <optional>
#include <Functions.h>

/**
 * @struct TcpHeader
 * @brief Represents a TCP (Transmission Control Protocol) header.
 */
struct TcpHeader
{
    ByteString sourcePort{};        ///< Source port number.
    ByteString destinationPort{};   ///< Destination port number.
    ByteString sequenceNumber{};    ///< Sequence number.
    ByteString ackNumber{};         ///< Acknowledgment number.
    ByteString headerLength{};      ///< Data offset (header length).
    ByteString windowSize{};        ///< Window size.
    ByteString checksum{};          ///< Checksum.
    ByteString urgentPointer{};     ///< Urgent pointer.

    /**
     * @struct Flags
     * @brief Represents TCP flags.
     */
    struct Flags
    {
        ByteString congestionWindowReduced{}; ///< Congestion Window Reduced (CWR) flag.
        ByteString ecnEcho{};                  ///< ECN Echo flag.
        ByteString urgent{};                   ///< Urgent flag.
        ByteString acknowledgement{};          ///< Acknowledgment flag.
        ByteString push{};                     ///< Push flag.
        ByteString reset{};                    ///< Reset flag.
        ByteString syn{};                      ///< SYN flag.
        ByteString fin{};                      ///< FIN flag.
    } flags;

    /**
     * @struct Option
     * @brief Represents a TCP option.
     */
    struct Option
    {
        ByteString type{};   ///< Option type.
        ByteString length{}; ///< Option length.
        ByteString value{};  ///< Option value.
    };

    std::vector<Option> options{}; ///< Vector of TCP options

    const std::optional<ByteString> encapsulate() const
    {
        ByteString tcpString;
        if (sourcePort.size() != 2 || destinationPort.size() != 2 || sequenceNumber.size() != 4 || ackNumber.size() != 4 ||
            headerLength.size() != 1 || windowSize.size() != 2 || checksum.size() != 2 || urgentPointer.size() != 2 ||
            flags.congestionWindowReduced.size() != 1 || flags.urgent.size() != 1 || flags.acknowledgement.size() != 1 || flags.ecnEcho.size() != 1 ||
            flags.fin.size() != 1 || flags.push.size() != 1 || flags.reset.size() != 1 || flags.syn.size() != 1) return std::nullopt;
        
        tcpString.reserve(20);
        tcpString += sourcePort;
        tcpString += destinationPort;
        tcpString += sequenceNumber;
        tcpString += ackNumber;
        tcpString += headerLength;
        tcpString += Functions::binToByte(flags.congestionWindowReduced + flags.ecnEcho + flags.urgent + flags.acknowledgement + flags.push + flags.reset + flags.syn + flags.fin);
        tcpString += windowSize;
        tcpString += ByteString(2, 0x00);
        tcpString += urgentPointer;
        // Add TCP options.
        for (auto opt : options)
        {
            tcpString += opt.type;
            tcpString += opt.length;
            tcpString += opt.value;
        }

        return tcpString;
    }
    bool decapsulate(const ByteString tcpHeader)
    {
        if (tcpHeader.size() < 20) return false;

        sourcePort = tcpHeader.substr(0, 2);
        destinationPort = tcpHeader.substr(2, 2);
        sequenceNumber = tcpHeader.substr(4, 4);
        ackNumber = tcpHeader.substr(8, 4);
        headerLength = tcpHeader.substr(12, 1);
        windowSize = tcpHeader.substr(14, 2);
        checksum = tcpHeader.substr(16, 2);
        urgentPointer = tcpHeader.substr(18, 2);

        ByteString flagOpts = Functions::byteToBin(tcpHeader.substr(13, 1));

        flags.congestionWindowReduced = flagOpts.substr(0, 1);
        flags.ecnEcho = flagOpts.substr(1, 1);
        flags.urgent = flagOpts.substr(2, 1);
        flags.acknowledgement = flagOpts.substr(3, 1);
        flags.push = flagOpts.substr(4, 1);
        flags.reset = flagOpts.substr(5, 1);
        flags.syn = flagOpts.substr(6, 1);
        flags.fin = flagOpts.substr(7, 1);

        if (tcpHeader.size() > 20)
        {
            ByteString tcpOptions = tcpHeader.substr(20);
            size_t optionStart = 0;
            while (optionStart != tcpHeader.size() - 20)
            {
                TcpHeader::Option option;
                if (!validateSize(optionStart, 2, tcpOptions)) return false;

                option.type = tcpOptions.substr(optionStart, 1);
                optionStart += 1;
                if (option.type != ByteString("\x01", 1))
                {
                    option.length = tcpOptions.substr(optionStart, 1);
                    optionStart += 1;
                    size_t valueLength = static_cast<size_t>(Functions::byteToNum(option.length) - 2);
                    if (!validateSize(optionStart, valueLength, tcpOptions)) return false;
                    option.value = tcpOptions.substr(optionStart, valueLength);
                    optionStart += valueLength;
                }
                options.push_back(option);
            }
        }
        return true;
    }
};

#endif // TCP_HEADER_HPP
