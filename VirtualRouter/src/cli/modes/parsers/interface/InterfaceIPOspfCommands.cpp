// InterfaceIPOspfCommands.cpp

#include <VirtualRouter.h>
#include <ByteUtils.hpp>

#include "InterfaceIPOspfCommands.h"
#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/CommandUtils.hpp"
#include "cli/runtime/CliSession.h"
#include "InterfaceOspfCommands.h"

#define OSPF_PARAMS DEFINE_PARAMS(config::OspfInterfaceBaseRegistry)

namespace cli
{

bool InterfaceIPOspf_Area_Handler(OSPF_PARAMS)
{
    uint16_t id;
    if (!utils::setValue(id, segs >> 0 >> 1))
        return false;

    types::IPv4Address areaId;
    auto idTok = segs >> 1 >> 1;
    if (!utils::setValue(areaId, idTok) && !utils::setValue(areaId.addr, idTok))
        return false;
    auto area = ctx.configs().reg.get<config::OspfInterfaceBase::AREA_ID>();
    if (!utils::handleValueReset(area, ctx))
        area.set(areaId.addr);
    auto secondaries = ctx.configs().reg.get<config::OspfInterfaceBase::INCLUDE_SECONDARIES>();
    if (!utils::handleValueReset(secondaries, ctx) && (segs >> 2))
        utils::setToggleValue(secondaries, ctx);
    return true;
}

bool InterfaceIPOspf_Authentication_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs();
    auto authType = ospf.reg.get<config::OspfInterfaceBase::AUTHENTICATION_TYPE>();

    if (segs.empty())
    {
	if (utils::handleValueReset(authType, ctx))
	    return true;
	authType.set(config::ospf::AuthType::SIMPLE);
	return true;
    }
    else
    {
	switch (segs[0][0])
	{
	    case "message-digest"_tok:
	    {
		authType.set(config::ospf::AuthType::CRYPTO);
		return true;
	    }
	    case "null"_tok:
	    {
		authType.set(config::ospf::AuthType::NULL_AUTH);
		return true;
	    }
	}
    }
    return false;
}

bool InterfaceIPOspf_AuthenticationKey_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs();
    auto authKey = ospf.reg.get<config::OspfInterfaceBase::AUTHENTICATION_KEY>();
    if (utils::handleValueReset(authKey, ctx))
	return true;
    Token* t = segs >> 0 >> 1;
    if (t)
    {
	std::string keyStr = std::string(t->value).append(std::min<size_t>(0, 8 - t->value.size()), '\0');
	uint64_t key = ::utils::readU64(reinterpret_cast<uint8_t*>(keyStr.data()));
	authKey.set(key);
	return true;
    }
    return false;
}

bool InterfaceIPOspf_LLS_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs();
    auto lls = ospf.reg.get<config::OspfInterfaceBase::LLS>();
    if (!utils::handleValueReset(lls, ctx))
	return true;
    bool disable = segs >> 1; // Disable
    lls.set(!disable);
    return true;
}

bool InterfaceIPOspf_MessageDigestKey_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs();
    auto digestKeys = ospf.reg.get<config::OspfInterfaceBase::MESSAGE_DIGEST_KEYS>();
    config::DefType<typename decltype(digestKeys)::Field>::node tup;
    for (const auto& seg : segs)
    {
	switch (seg[0])
	{
	    case "message-digest-key"_tok:
	    {
		if (!utils::setTupleElement(std::get<0>(tup), segs >> 0 >> 1))
		    return false;
		break;
	    }
	    case "md5"_tok:
	    {
		std::string value;
		utils::setValue(value, seg >> 1);
		value.append(16 - value.size(), '\0');
		auto& arr = std::get<1>(tup);
		std::memcpy(arr.value.data(), value.data(), 16);
		break;
	    }
	}
    }

    return utils::setListEntry(digestKeys, ctx, tup);
}

bool InterfaceIPOspf_PrefixSuppression_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs();
    auto ps = ospf.reg.get<config::OspfInterfaceBase::PREFIX_SUPPRESSION>();
    if (!utils::handleValueReset(ps, ctx))
	return true;
    bool disable = segs >> 1; // Disable
    ps.set(!disable);
    return true;
}

bool InterfaceIPOspf_ResyncTimeout_Handler(OSPF_PARAMS)
{
    auto resync = ctx.configs().reg.get<config::OspfInterfaceBase::RESYNC_TIMEOUT>();
    return utils::setFieldValue(resync, ctx, segs >> 0 >> 1);
}

bool InterfaceIPOspf_Shutdown_Handler(OSPF_PARAMS)
{
    UNUSED(segs);
    auto shut = ctx.configs().reg.get<config::OspfInterfaceBase::SHUTDOWN>();
    utils::setToggleValue(shut, ctx);
    return true;
}

bool InterfaceIPOspf_TtlSecurity_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs();
    auto ttlSec = ospf.reg.get<config::OspfInterfaceBase::BASE>().get().reg.get<config::OspfInterface::TTL_SEC>();
    auto ttlSecHops = ospf.reg.get<config::OspfInterfaceBase::BASE>().get().reg.get<config::OspfInterface::TTL_SEC_HOPS>();
    if (utils::handleValueReset(ttlSec, ctx) && utils::handleValueReset(ttlSecHops, ctx))
	return true;
    ttlSec.set((segs >> 0 >> 0) != "disable"_tok);
    utils::setFieldValueWithFallback(ttlSecHops, ctx, segs >> 0 >> 1);
    return true;
}

#define INTERFACE_IP_OSPF_LIST(X, Y) \
    X(Y, (CMD_INHERIT, InterfaceOspfCommands)) \
    X(Y, (COMMAND, Area, P_ARG, "Area"_tok)) \
    X(Y, (COMMAND, Authentication, "authentication"_tok)) \
    X(Y, (COMMAND, AuthenticationKey, "authentication-key"_tok)) \
    X(Y, (COMMAND, LLS, "lls"_tok)) \
    X(Y, (COMMAND, MessageDigestKey, "message-digest-key"_tok)) \
    X(Y, (COMMAND, PrefixSuppression, "prefix-suppression"_tok)) \
    X(Y, (COMMAND, ResyncTimeout, "resync-timeout"_tok)) \
    X(Y, (COMMAND, Shutdown, "shutdown"_tok)) \
    X(Y, (COMMAND, TtlSecurity, "ttl-security"_tok))

/**
 * @brief Parser for the `ip ospf` sub-tree in Interface Configuration mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Extends `InterfaceOspfCommands` (shared OSPFv2/v3 base) with OSPFv2-specific
 * interface commands.  Covers `CliMode::Interface` with `InterfaceContext`.
 */
DEFINE_CMD_MODE(InterfaceIPOspf, config::OspfInterfaceBaseRegistry, INTERFACE_IP_OSPF_LIST);
}

#undef OSPF_PARAMS
