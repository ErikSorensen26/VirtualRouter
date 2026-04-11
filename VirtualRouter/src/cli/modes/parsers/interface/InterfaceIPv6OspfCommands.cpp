// InterfaceIPv6OspfCommands.cpp

#include <VirtualRouter.h>

#include "InterfaceIPv6OspfCommands.h"
#include "cli/parser/CliModeParser.hpp"
#include "cli/runtime/CliSession.h"
#include "cli/parser/CommandUtils.hpp"
#include "InterfaceOspfCommands.h"

#define OSPF_PARAMS DEFINE_PARAMS(config::OspfInterfaceBaseRegistry)

namespace cli
{
bool InterfaceIPv6Ospf_Area_Handler(OSPF_PARAMS)
{
    config::OspfInterfaceBaseRegistry* ospf = nullptr;

    uint16_t id;
    if (!utils::setValue(id, segs >> 0 >> 1))
	return false;

    types::IPv4Address areaId;
    auto idTok = segs >> 1 >> 1;
    if (!utils::setValue(areaId, idTok) && !utils::setValue(areaId.addr, idTok))
	    return false;
    auto& area = ospf->get<config::OspfInterfaceBase::AREA_ID>();
    if (!utils::handleValueReset(area, ctx))
	area.set(areaId.addr);
    auto& instance = ospf->get<config::OspfInterfaceBase::INSTANCE_ID>();
    utils::setFieldValueWithFallback(instance, ctx, segs >> 2 >> 1);
    return true;
}

bool InterfaceIPv6Ospf_Authentication_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs.get<config::OspfInterfaceBase::IPSEC>().get();

    if (auto& t = ospf.get<config::OspfInterfaceIPSec::ENCRYPTION_TYPE>();
	t.hasValue() || t.load() != config::ospf::IPsecEncryptType::NULL_TYPE)
    {
	ctx.terminal.controller.print("\r\n% Ospfv3: Interface is already configured with Encryption.");
	return false;
    }
	
    auto& authSpi = ospf.get<config::OspfInterfaceIPSec::SPI>();
    auto& authType = ospf.get<config::OspfInterfaceIPSec::AUTHENTICATION_TYPE>();
    auto& authKey = ospf.get<config::OspfInterfaceIPSec::AUTHENTICATION_KEY>();

    if (utils::handleValueReset(authType, ctx))
	return true;

    for (const auto& seg : segs)
    {
	switch (seg[0])
	{
	    case "spi"_tok:
	    {
		if (!utils::setFieldValue(authSpi, ctx, seg >> 1))
		    return false;
	    }
	    case "md5"_tok:
	    {
		std::string keyStr = std::string(seg[1].value).append(std::min<size_t>(0, 32 - seg[1].value.size()), '\0');
		std::array<uint8_t, 40> arr{0};
		std::memcpy(arr.data(), keyStr.data(), 32);
		authKey.set(arr);
		authType.set(config::ospf::IPsecAuthType::MD5);
		return true;	
	    }
	    case "sha1"_tok:
	    {
		std::string keyStr = std::string(seg[1].value).append(std::min<size_t>(0, 40 - seg[1].value.size()), '\0');
		std::array<uint8_t, 40> arr{0};
		std::memcpy(arr.data(), keyStr.data(), 40);
		authKey.set(arr);
		authType.set(config::ospf::IPsecAuthType::SHA1);
		return true;	
	    }
	    case "null"_tok:
	    {
		authType.set(config::ospf::IPsecAuthType::NULL_AUTH);
		return true;
	    }
	}
    }
    return false;
}

bool InterfaceIPv6Ospf_Encryption_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs.get<config::OspfInterfaceBase::IPSEC>().get();
	
    auto& spi = ospf.get<config::OspfInterfaceIPSec::SPI>();
    auto& encryptType = ospf.get<config::OspfInterfaceIPSec::ENCRYPTION_TYPE>();
    auto& encryptKey = ospf.get<config::OspfInterfaceIPSec::ENCRYPTION_KEY>();
    auto& authType = ospf.get<config::OspfInterfaceIPSec::AUTHENTICATION_TYPE>();
    auto& authKey = ospf.get<config::OspfInterfaceIPSec::AUTHENTICATION_KEY>();

    if (utils::handleValueReset(authType, ctx))
	    return true;

    auto handleEncryptKey = [&](std::string_view k, size_t siz)
    {
	    std::string keyStr = std::string(k).append(std::min<size_t>(0, siz - k.size()), '\0');
	    std::array<uint8_t, 64> arr;
	    std::memcpy(arr.data(), keyStr.data(), 64);
	    encryptKey.set(arr);
    };

    for (const auto& seg : segs)
    {
	switch (seg[0])
	{
	    case "spi"_tok:
	    {
		if (!utils::setFieldValue(spi, ctx, seg >> 1))
		    return false;
	    }
	    case "3des"_tok:
	    {
		handleEncryptKey(seg[1].value, 48);
		break;
	    }
	    case "128"_tok:
	    {
		handleEncryptKey(seg[1].value, 32);
		break;
	    }
	    case "192"_tok:
	    {
		handleEncryptKey(seg[1].value, 48);
		break;
	    }
	    case "256"_tok:
	    {
		handleEncryptKey(seg[1].value, 64);
		break;
	    }
	    case "md5"_tok:
	    {
		std::string keyStr = std::string(seg[1].value).append(std::min<size_t>(0, 32 - seg[1].value.size()), '\0');
		std::array<uint8_t, 40> arr{0};
		std::memcpy(arr.data(), keyStr.data(), 32);
		authKey.set(arr);
		authType.set(config::ospf::IPsecAuthType::MD5);
		return true;	
	    }
	    case "sha1"_tok:
	    {
		std::string keyStr = std::string(seg[1].value).append(std::min<size_t>(0, 40 - seg[1].value.size()), '\0');
		std::array<uint8_t, 40> arr{0};
		std::memcpy(arr.data(), keyStr.data(), 40);
		authKey.set(arr);
		authType.set(config::ospf::IPsecAuthType::SHA1);
		return true;	
	    }
	    case "null"_tok:
	    {
		encryptType.set(config::ospf::IPsecEncryptType::NULL_TYPE);
		return true;
	    }
	}
    }
    return false;
}

bool InterfaceIPv6Ospf_Neighbor_Handler(OSPF_PARAMS)
{
    auto& neighbors = ctx.configs.get<config::OspfInterfaceBase::BASE>().get().get<config::OspfInterface::NEIGHBOR>();
    config::DefType<decltype(neighbors)>::node tup;

    for (const auto& seg : segs)
    {
	switch (seg[0])
	{
	    case "neighbor"_tok:
	    {
		if (!utils::setTupleElement(std::get<0>(tup), seg >> 1))
		    return false;
		break;
	    }
	    case "cost"_tok:
	    {
		if (!utils::setTupleElement(std::get<1>(tup), seg >> 1))
		    return false;
		break;
	    }
	    case "database-filter"_tok:
	    {
		std::get<2>(tup) = true;
		break;
	    }
	    case "poll-interval"_tok:
	    {
		if (!utils::setTupleElement(std::get<3>(tup), seg >> 1))
		    return false;
		break;
	    }
	    case "priority"_tok:
	    {
		if (!utils::setTupleElement(std::get<4>(tup), seg >> 1))
		    return false;
		break;
	    }
	}
    }

    return utils::setListEntry(neighbors, ctx, tup);
}

#define INTERFACE_IPV6_OSPF_LIST(X, Y) \
    X(Y, (INHERIT, InterfaceOspfCommands)) \
    X(Y, (COMMAND, Area, P_ARG, "area"_tok)) \
    X(Y, (COMMAND, Authentication, "authentication"_tok)) \
    X(Y, (COMMAND, Encryption, "encryption"_tok)) \
    X(Y, (COMMAND, Neighbor, "neighbor"_tok))

/**
 * @brief Parser for the `ipv6 ospf` sub-tree in Interface Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Extends `InterfaceOspfCommands` (shared OSPFv2/v3 base) with OSPFv3-specific
 * interface commands.  Covers `CliMode::Interface` with `InterfaceContext`.
 */
DEFINE_CMD_MODE(InterfaceIPv6Ospf, config::OspfInterfaceBaseRegistry, INTERFACE_IPV6_OSPF_LIST);
}

#undef OSPF_PARAMS
