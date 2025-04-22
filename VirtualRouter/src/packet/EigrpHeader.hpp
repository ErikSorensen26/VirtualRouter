// EigrpHeader.hpp

#ifndef EIGRP_HEADER_HPP
#define EIGRP_HEADER_HPP

#include <ByteString.hpp>
#include <optional>
#include <vector>
#include <Functions.h>

/**
 * @struct EigrpHeader
 * @brief Represents an EIGRP (Enhanced Interior Gateway Routing Protocol) header.
 */
struct EigrpHeader
{
    ByteString version{};             ///< EIGRP version.
    ByteString opcode{};              ///< EIGRP opcode.
    ByteString checksum{};            ///< EIGRP checksum.
    ByteString sequence{};            ///< Sequence number.
    ByteString ack{};                 ///< Acknowledgment number.
    ByteString virtualRouterID{};     ///< Virtual Router ID.
    ByteString autonomousSystem{};    ///< Autonomous System number.

    /**
     * @struct flags
     * @brief Represents EIGRP flags.
     */
    struct flags
    {
        ByteString init{};                ///< INIT flag.
        ByteString conditionalRecieve{};  ///< Conditional Receive flag.
        ByteString restart{};             ///< Restart flag.
        ByteString endOfTable{};          ///< End Of Table flag.
    } flags;

    /**
     * @struct Option
     * @brief Represents an EIGRP option.
     */
    struct Option
    {
        ByteString option{};   ///< Option code.
        ByteString length{};   ///< Option length.
        ByteString value{};    ///< Option value.
    };

    std::vector<Option> options{}; ///< Vector of EIGRP options.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString eigrpString;
        if (version.size() != 1 || opcode.size() != 1 || checksum.size() != 2 || flags.endOfTable.size() != 1 || flags.conditionalRecieve.size() != 1 || flags.init.size() != 1 || flags.restart.size() != 1 ||
            sequence.size() != 4 || ack.size() != 4 || virtualRouterID.size() != 2 || autonomousSystem.size() != 2) return std::nullopt;

        eigrpString.reserve(20);
        eigrpString += version;
        eigrpString += opcode;
        eigrpString += ByteString(2, 0x00);
        eigrpString += Functions::binToByte(ByteString("0000000000000000000000000000") + flags.endOfTable + flags.restart + flags.conditionalRecieve + flags.init);
        eigrpString += sequence;
        eigrpString += ack;
        eigrpString += virtualRouterID;
        eigrpString += autonomousSystem;
        for (auto opt : options)
        {
            eigrpString += opt.option;
            eigrpString += opt.length;
            eigrpString += opt.value;
        }
            
        return eigrpString;
    }
    bool decapsulate(const ByteString eigrpHeader)
    {
        size_t eigrpStart;
        size_t eigrpEnd;
        if (eigrpHeader.size() < 20) return false;

        version = eigrpHeader.substr(0, 1);
        opcode = eigrpHeader.substr(1, 1);
        checksum = eigrpHeader.substr(2, 2);
        ByteString flag = Functions::byteToBin(eigrpHeader.substr(4, 4));
        flags.endOfTable = flag.substr(28, 1);
        flags.restart = flag.substr(29, 1);
        flags.conditionalRecieve = flag.substr(30, 1);
        flags.init = flag.substr(31, 1);
        sequence = eigrpHeader.substr(8, 4);
        ack = eigrpHeader.substr(12, 4);
        virtualRouterID = eigrpHeader.substr(16, 2);
        autonomousSystem = eigrpHeader.substr(18, 2);
        eigrpStart = 20;
        eigrpEnd = eigrpHeader.size();

        while (eigrpStart != eigrpEnd)
        {
            EigrpHeader::Option option;
            if (!validateSize(eigrpStart, 4, eigrpHeader)) return false;
            option.option = eigrpHeader.substr(eigrpStart, 2);
            eigrpStart += 2;
            option.length = eigrpHeader.substr(eigrpStart, 2);
            eigrpStart += 2;
            size_t eigrpADD = static_cast<size_t>(Functions::byteToNum(option.length) - 4);
            if (validateSize(eigrpStart, eigrpADD, eigrpHeader))
            {
                option.value = eigrpHeader.substr(eigrpStart, eigrpADD);
                eigrpStart += eigrpADD;
                options.push_back(option);
            }
        }
        return true;
    }
};

#endif // EIGRP_HEADER_HPP
