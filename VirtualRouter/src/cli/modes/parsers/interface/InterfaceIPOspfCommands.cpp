// InterfaceIPOspfCommands.cpp

#include <VirtualRouter.h>
#include <ByteUtils.hpp>

#include "InterfaceIPOspfCommands.h"
#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "configs/registry/interface/InterfaceRegistry.h"
#include "cli/runtime/CliSession.h"
#include "cli/parser/CommandUtils.hpp"

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
    auto& area = ctx.configs.get<config::OspfInterfaceBase::AREA_ID>();
    if (!utils::handleValueReset(area, ctx))
        area.set(areaId.addr);
    auto& secondaries = ctx.configs.get<config::OspfInterfaceBase::INCLUDE_SECONDARIES>();
    if (!utils::handleValueReset(secondaries, ctx) && (segs >> 2))
        utils::setToggleValue(secondaries, ctx);
    return true;
}

bool InterfaceIPOspf_Authentication_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs;
    auto& authType = ospf.get<config::OspfInterfaceBase::AUTHENTICATION_TYPE>();

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
    auto& ospf = ctx.configs;
    auto& authKey = ospf.get<config::OspfInterfaceBase::AUTHENTICATION_KEY>();
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
    auto& ospf = ctx.configs;
    auto& lls = ospf.get<config::OspfInterfaceBase::LLS>();
    if (!utils::handleValueReset(lls, ctx))
	return true;
    bool disable = segs >> 1; // Disable
    lls.set(!disable);
    return true;
}

bool InterfaceIPOspf_MessageDigestKey_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs;
    auto& digestKeys = ospf.get<config::OspfInterfaceBase::MESSAGE_DIGEST_KEYS>();
    config::DefType<decltype(digestKeys)>::node tup;
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
    auto& ospf = ctx.configs;
    auto& ps = ospf.get<config::OspfInterfaceBase::PREFIX_SUPPRESSION>();
    if (!utils::handleValueReset(ps, ctx))
	return true;
    bool disable = segs >> 1; // Disable
    ps.set(!disable);
    return true;
}

bool InterfaceIPOspf_ResyncTimeout_Handler(OSPF_PARAMS)
{
    auto& resync = ctx.configs.get<config::OspfInterfaceBase::RESYNC_TIMEOUT>();
    return utils::setFieldValue(resync, ctx, segs >> 0 >> 1);
}

bool InterfaceIPOspf_Shutdown_Handler(OSPF_PARAMS)
{
    UNUSED(segs);
    auto& shut = ctx.configs.get<config::OspfInterfaceBase::SHUTDOWN>();
    utils::setToggleValue(shut, ctx);
    return true;
}

bool InterfaceIPOspf_TtlSecurity_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs;
    auto& ttlSec = ospf.get<config::OspfInterfaceBase::BASE>().get().get<config::OspfInterface::TTL_SEC>();
    auto& ttlSecHops = ospf.get<config::OspfInterfaceBase::BASE>().get().get<config::OspfInterface::TTL_SEC_HOPS>();
    if (utils::handleValueReset(ttlSec, ctx) && utils::handleValueReset(ttlSecHops, ctx))
	return true;
    ttlSec.set((segs >> 0 >> 0) != "disable"_tok);
    utils::setFieldValueWithFallback(ttlSecHops, ctx, segs >> 0 >> 1);
    return true;
}
}

#undef OSPF_PARAMS
