// InterfaceOspfv3Commands.cpp

#include <VirtualRouter.h>

#include "InterfaceOspfv3Commands.h"
#include "cli/execution/parser/CliModeParser.hpp"
#include "InterfaceOspfCommands.h"
#include "cli/session/CliSession.h"
#include "cli/execution/parser/CommandUtils.hpp"
#include "configs/FieldAccessor.hpp"

#define OSPF_PARAMS DEFINE_PARAMS(config::OspfGlobalInterfaceRegistry)
#define INTERFACE_SUB_PARAMS DEFINE_SUB_PARAMS(config::InterfaceRegistry)

namespace cli::execution
{
bool InterfaceOspfv3_Area_Handler(OSPF_PARAMS)
{
    types::IPv4Address areaId;
    auto idTok = segs >> 0 >> 1;
    if (!utils::setValue(areaId, idTok) && !utils::setValue(areaId.addr, idTok))
	return false;
    auto area = ctx.configs().get<config::OspfGlobalInterface::AREA_ID>();
    if (!utils::handleValueReset(area, ctx))
	area.set(areaId.addr);
    auto instance = ctx.configs().get<config::OspfGlobalInterface::GLOBAL_BASE>().get()
	.get<config::OspfGlobalInterfaceBase::INSTANCE_ID>();
    utils::setFieldValue(instance, ctx, segs >> 1 >> 1);
    return true;
}

bool InterfaceOspfv3_Neighbor_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs().get<config::OspfGlobalInterface::BASE>().get();
    auto neighbor = ospf.get<config::OspfInterface::NEIGHBOR>();
    config::DefType<decltype(neighbor)::Field>::node tup;

    // Start after neighbor address
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

    return utils::setListEntry(neighbor, ctx, tup);
}

bool InterfaceDefaultOspfv3_Authentication_Handler(OSPF_PARAMS)
{
    auto& configs = ctx.configs().get<config::OspfGlobalInterface::IPSEC>().get();
    auto authType = configs.get<config::OspfInterfaceIPSec::AUTHENTICATION_TYPE>();
    auto authSpi = configs.get<config::OspfInterfaceIPSec::SPI>();
    auto authKey = configs.get<config::OspfInterfaceIPSec::AUTHENTICATION_KEY>();

    if (utils::handleValueReset(authType, ctx) &&
	utils::handleValueReset(authSpi, ctx) &&
	utils::handleValueReset(authKey, ctx))
	return false;

    for (const auto& seg : segs)
    {
	switch (seg[0])
	{
	    case "spi"_tok:
	    {
		if (!utils::setFieldValue(authSpi, ctx, seg >> 1))
		    return false;
		break;
	    }
	    case "md5"_tok:
	    {
		Token* t = seg >> 1;
		if (t)
		{
		    authType.set(config::ospf::IPsecAuthType::MD5);
		    std::array<uint8_t, 40> arr;
		    size_t len = std::min<size_t>(t->value.size(), 32);
		    std::copy_n(t->value.data(), len, arr.data());
		    if (len < 40) std::fill(arr.data() + len, arr.data() + 40, '\0');
		    authKey.set(arr);
		    return true;
		}
		return false;
	    }
	    case "sha1"_tok:
	    {
		Token* t = seg >> 1;
		if (t)
		{
		    authType.set(config::ospf::IPsecAuthType::SHA1);
		    std::array<uint8_t, 40> arr;
		    size_t len = std::min<size_t>(t->value.size(), 32);
		    std::copy_n(t->value.data(), len, arr.data());
		    if (len < 40) std::fill(arr.data() + len, arr.data() + 40, '\0');
		    authKey.set(arr);
		    return true;
		}
		return false;
	    }
	}
    }
    return true;
}

bool InterfaceDefaultOspfv3_NullAuthentication_Handler(OSPF_PARAMS)
{
    UNUSED(segs);
    auto& configs = ctx.configs().get<config::OspfGlobalInterface::IPSEC>().get();
    auto authType = configs.get<config::OspfInterfaceIPSec::AUTHENTICATION_TYPE>();
    if (utils::handleValueReset(authType, ctx))
	return true;
    authType.set(config::ospf::IPsecAuthType::NULL_AUTH);
    return true;
}

bool InterfaceDefaultOspfv3_Encryption_Handler(OSPF_PARAMS)
{
    auto& configs = ctx.configs().get<config::OspfGlobalInterface::IPSEC>().get();
    auto espSpi = configs.get<config::OspfInterfaceIPSec::SPI>();
    auto authType = configs.get<config::OspfInterfaceIPSec::AUTHENTICATION_TYPE>();
    auto authKey = configs.get<config::OspfInterfaceIPSec::AUTHENTICATION_KEY>();
    auto encryptType = configs.get<config::OspfInterfaceIPSec::ENCRYPTION_TYPE>();
    auto encryptkey = configs.get<config::OspfInterfaceIPSec::ENCRYPTION_KEY>();

    for (const auto& seg : segs)
    {
	switch (seg[0])
	{
	    case "spi"_tok:
	    {
		if (!utils::setFieldValue(espSpi, ctx, seg >> 1))
		    return false;
		break;
	    }
	    case "3des"_tok:
	    {
		Token* t = seg >> 1;
		if (t)
		{
		    encryptType.set(config::ospf::IPsecEncryptType::_3DES);
		    std::array<uint8_t, 64> arr;
		    size_t len = std::min<size_t>(t->value.size(), 48);
		    std::copy_n(t->value.data(), len, arr.data());
		    if (len < 48) std::fill(arr.data() + len, arr.data() + 64, '\0');
		    encryptkey.set(arr);
		    return true;
		}
		return false;
	    }
	    case "128"_tok:
	    {
		Token* t = seg >> 1;
		if (t)
		{
		    encryptType.set(config::ospf::IPsecEncryptType::AES_CBC_128);
		    std::array<uint8_t, 64> arr;
		    size_t len = std::min<size_t>(t->value.size(), 32);
		    std::copy_n(t->value.data(), len, arr.data());
		    if (len < 32) std::fill(arr.data() + len, arr.data() + 64, '\0');
		    encryptkey.set(arr);
		    return true;
		}
		return false;
	    }
	    case "192"_tok:
	    {
		Token* t = seg >> 1;
		if (t)
		{
		    encryptType.set(config::ospf::IPsecEncryptType::AES_CBC_192);
		    std::array<uint8_t, 64> arr;
		    size_t len = std::min<size_t>(t->value.size(), 48);
		    std::copy_n(t->value.data(), len, arr.data());
		    if (len < 48) std::fill(arr.data() + len, arr.data() + 64, '\0');
		    encryptkey.set(arr);
		    return true;
		}
		return false;
	    }
	    case "256"_tok:
	    {
		Token* t = seg >> 1;
		if (t)
		{
		    encryptType.set(config::ospf::IPsecEncryptType::AES_CBC_256);
		    std::array<uint8_t, 64> arr;
		    size_t len = std::min<size_t>(t->value.size(), 64);
		    std::copy_n(t->value.data(), len, arr.data());
		    if (len < 64) std::fill(arr.data() + len, arr.data() + 64, '\0');
		    encryptkey.set(arr);
		    return true;
		}
		return false;
	    }
	    case "des"_tok:
	    {
		Token* t = seg >> 1;
		if (t)
		{
		    encryptType.set(config::ospf::IPsecEncryptType::DES);
		    std::array<uint8_t, 64> arr;
		    size_t len = std::min<size_t>(t->value.size(), 16);
		    std::copy_n(t->value.data(), len, arr.data());
		    if (len < 16) std::fill(arr.data() + len, arr.data() + 64, '\0');
		    encryptkey.set(arr);
		    return true;
		}
		return false;
	    }
	    case "md5"_tok:
	    {
		Token* t = seg >> 1;
		if (t)
		{
		    authType.set(config::ospf::IPsecAuthType::MD5);
		    std::array<uint8_t, 40> arr;
		    size_t len = std::min<size_t>(t->value.size(), 32);
		    std::copy_n(t->value.data(), len, arr.data());
		    if (len < 40) std::fill(arr.data() + len, arr.data() + 40, '\0');
		    authKey.set(arr);
		    return true;
		}
		return false;
	    }
	    case "sha1"_tok:
	    {
		Token* t = seg >> 1;
		if (t)
		{
		    authType.set(config::ospf::IPsecAuthType::SHA1);
		    std::array<uint8_t, 40> arr;
		    size_t len = std::min<size_t>(t->value.size(), 32);
		    std::copy_n(t->value.data(), len, arr.data());
		    if (len < 40) std::fill(arr.data() + len, arr.data() + 40, '\0');
		    authKey.set(arr);
		    return true;
		}
		return false;
	    }
	}
    }
    return true;
}

bool InterfaceDefaultOspfv3_NullEncryption_Handler(OSPF_PARAMS)
{
    UNUSED(segs);
    auto& configs = ctx.configs().get<config::OspfGlobalInterface::IPSEC>().get();
    auto type = configs.get<config::OspfInterfaceIPSec::ENCRYPTION_TYPE>();
    if (utils::handleValueReset(type, ctx))
	return true;
    type.set(config::ospf::IPsecEncryptType::NULL_TYPE);
    return true;
}

bool InterfaceOspfv3Base_ProcessIP_SubHandler(INTERFACE_SUB_PARAMS)
{
    uint16_t id;
    if (!utils::setValue(id, &toks[1]))
	return false;
    auto& ospf = ctx.configs().get<config::Interface::OSPFV3>()
	.emplaceBack(id).get<config::OspfInterfaceAf::IPV4>().get();
    Context<config::OspfGlobalInterfaceRegistry> newCtx(ctx.terminal, ospf);
    newCtx.negate = ctx.negate;
    newCtx.defaulted = ctx.defaulted;
    return InterfaceOspfv3Commands::execute(newCtx, toks, idx);
}

bool InterfaceOspfv3Base_ProcessIPv6_SubHandler(INTERFACE_SUB_PARAMS)
{
    uint16_t id;
    if (!utils::setValue(id, &toks[1]))
	return false;
    auto& ospf = ctx.configs().get<config::Interface::OSPFV3>()
	.emplaceBack(id).get<config::OspfInterfaceAf::IPV6>().get();
    Context<config::OspfGlobalInterfaceRegistry> newCtx(ctx.terminal, ospf);
    newCtx.negate = ctx.negate;
    newCtx.defaulted = ctx.defaulted;
    return InterfaceOspfv3Commands::execute(newCtx, toks, idx);
}

bool InterfaceOspfv3Base_Process_SubHandler(INTERFACE_SUB_PARAMS)
{
    uint16_t id;
    if (!utils::setValue(id, &toks[1]))
	return false;
    auto& ospf = ctx.configs().get<config::Interface::OSPFV3>()
	.emplaceBack(id).get<config::OspfInterfaceAf::DEFAULT>().get();
    Context<config::OspfGlobalInterfaceRegistry> newCtx(ctx.terminal, ospf);
    newCtx.negate = ctx.negate;
    newCtx.defaulted = ctx.defaulted;
    return InterfaceOspfv3Commands::execute(newCtx, toks, idx);
}

bool InterfaceOspfv3Base_Default_SubHandler(INTERFACE_SUB_PARAMS)
{
    auto& ospf = ctx.configs().get<config::Interface::OSPFV3_DEFAULT>().get();
    Context<config::OspfGlobalInterfaceRegistry> newCtx(ctx.terminal, ospf);
    newCtx.negate = ctx.negate;
    newCtx.defaulted = ctx.defaulted;
    return InterfaceDefaultOspfv3Commands::execute(newCtx, toks, idx);
}

#define OSPFV3_LIST(X, Y) \
    X(Y, (CMD_INHERIT, InterfaceOspfCommands)) \
    X(Y, (COMMAND, Neighbor, "neighbor"_tok))

DEFINE_CMD_MODE(InterfaceOspfv3, config::OspfGlobalInterfaceRegistry, OSPFV3_LIST);

#define DEFAULT_OSPFV3_LIST(X, Y) \
    X(Y, (CMD_INHERIT, InterfaceOspfCommands)) \
    X(Y, (COMMAND, Authentication, "authentication"_tok, "ipsec"_tok)) \
    X(Y, (COMMAND, NullAuthentication, "authentication"_tok, "null"_tok)) \
    X(Y, (COMMAND, Encryption, "encryption"_tok, "ipsec"_tok)) \
    X(Y, (COMMAND, NullEncryption, "encryption"_tok, "null"_tok)) \

DEFINE_CMD_MODE(InterfaceDefaultOspfv3, config::OspfGlobalInterfaceRegistry, DEFAULT_OSPFV3_LIST);

#define INTERFACE_OSPFV3_LIST(X, Y) \
    X(Y, (SUBPRSR, ProcessIP, P_NUMRNG, "ipv4"_tok)) \
    X(Y, (SUBPRSR, ProcessIPv6, P_NUMRNG, "ipv6"_tok)) \
    X(Y, (SUBPRSR, Process, P_NUMRNG)) \
    X(Y, (SUBPRSR, Default))

/**
 * @brief Parser for OSPFv3 commands in Interface Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Aggregates area, authentication, encryption, and neighbor commands
 * for OSPFv3 interfaces under `CliMode::Interface` with `InterfaceContext`.
 */
DEFINE_CMD_MODE(InterfaceOspfv3Base, config::InterfaceRegistry, INTERFACE_OSPFV3_LIST);
}
