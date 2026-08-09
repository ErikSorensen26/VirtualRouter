// OspfInterfaceBase.cpp

#include <VirtualRouter.h>

#include "OspfInterfaceBase.h"
#include "ospf/OspfProcess.h"
#include "ospf/ospfv2/transmission/PacketDispatcherV2.h"
#include "ospf/ospfv3/transmission/PacketDispatcherV3.h"
#include "ospf/area/Area.h"
#include "ospf/area/IntraOriginator.h"
#include "ospf/OspfTypes.hpp"

namespace routing::ospf
{
template <typename T>
OspfInterfaceBase::OspfInterfaceBase(OspfProcess& proc, const OspfInterfaceId& id, const T& cfgs)
    : area(proc.insureArea(id.area)),
      id(id),
      interfaceId(id.interfaceId),
      process(proc),
      graceManager(*this),
      dispatcher(proc.isV3
          ? *static_cast<PacketDispatcher*>(new PacketDispatcherV3(*this))
          : *static_cast<PacketDispatcher*>(new PacketDispatcherV2(*this))),
      tmgr(*this, proc.scheduler.ref()),
      ntable(*this, tmgr),
      flags(area.flags),
      lsaFlags(area.flags),
      globalConfigsBase([&cfgs]() -> config::OspfGlobalInterfaceBaseRegistry& {
          if constexpr (std::is_same_v<T, config::OspfVirtualLinkRegistry>)
              return cfgs.template get<config::OspfVirtualLink::GLOBAL_BASE>().get();
          else if constexpr (std::is_same_v<T, config::OspfGlobalInterfaceRegistry>)
              return cfgs.template get<config::OspfGlobalInterface::GLOBAL_BASE>().get();
      }()),
      configsBase([&cfgs]() -> config::OspfInterfaceBaseRegistry& {
          if constexpr (std::is_same_v<T, config::OspfVirtualLinkRegistry>)
              return cfgs.template get<config::OspfVirtualLink::BASE>().get();
          else if constexpr (std::is_same_v<T, config::OspfGlobalInterfaceRegistry>)
              return cfgs.template get<config::OspfGlobalInterface::BASE>().get().template get<config::OspfInterface::BASE>().get();
      }())
{}

OspfInterfaceBase::~OspfInterfaceBase()
{
    // Tear down all neighbors and expire originated LSAs
    tmgr.stopHello();

    ntable.forEach([this](uint32_t, Neighbor& nbr) {
        tmgr.cancelRetransmissionTimers(nbr);
        tmgr.cancleInactiveTimer(nbr);
    });

    // Tell the originator to withdraw this interface's contributions
    // (removes the network LSA if DR, removes router link, rebuilds router LSA)
    updateOriginations();

    delete &dispatcher;
}

void OspfInterfaceBase::enqueueSyncTimers()
{
    getScheduler().post([this] {
        syncTimers();
    });
}

void OspfInterfaceBase::enqueueSyncDigestKey()
{
    getScheduler().post([this] {
        syncDigestKey();
    });
}

void OspfInterfaceBase::syncDigestKey()
{
    globalConfigsBase.get<config::OspfGlobalInterfaceBase::MESSAGE_DIGEST_KEYS>().withRead([this](const auto& keys)
    {
        if (!keys.empty())
        {
            const auto& last = keys.back();
            priv.authKey = utils::read<__uint128_t>(reinterpret_cast<const uint8_t*>(std::get<1>(last).value.data()));
            priv.authKeyId = std::get<0>(last);
        }
        else
        {
            priv.authKey.reset();
            priv.authKeyId.reset();
        }
    });
}

void OspfInterfaceBase::syncConfigs()
{
    opaqueEnabled.store(true, std::memory_order_release);
    syncTimers();
}

void OspfInterfaceBase::syncTimers()
{
    auto helloTimer = configsBase.get<config::OspfInterfaceBase::HELLO_INTERVAL>();
    auto helloMultiplier = configsBase.get<config::OspfInterfaceBase::HELLO_MULTIPLIER>();
    auto deadTimer = configsBase.get<config::OspfInterfaceBase::DEAD_INTERVAL>();

    if (helloMultiplier.hasValue())
    {
        priv.helloTime = std::chrono::seconds(1) / helloMultiplier.load();
        priv.deadTime = std::chrono::seconds(1);
        return;
    }
    else
    {
        uint16_t ht;
        uint16_t dt;

        if (helloTimer.hasValue())
        {
            ht = helloTimer.load();
        }
        else
        {
            auto net = getNetworkType();
            if (net == config::ospf::NetworkType::NON_BROADCAST || net == config::ospf::NetworkType::POINT_TO_MULTIPOINT_BROADCAST || net == config::ospf::NetworkType::POINT_TO_MULTIPOINT)
                ht = OSPF_MU_HELLO_TIME;
            else
                ht = OSPF_HELLO_TIME;
            helloTimer.set(ht);
        }

        if (deadTimer.hasValue())
            dt = deadTimer.load();
        else
        {
            dt = ht * 4;
            deadTimer.set(dt);
        }

        priv.helloTime = std::chrono::seconds(ht);
        priv.deadTime = std::chrono::seconds(dt);
    }
}
void OspfInterfaceBase::updateOriginations()
{
    area.originator.updateInterface(interfaceId) ;
}

void OspfInterfaceBase::flushNeighborLsas(Neighbor& nbr)
{
    area.flushNeighborLsas(nbr.routerID);
}

void OspfInterfaceBase::resetNeighbors()
{
    ntable.resetNeighbors();
}

void OspfInterfaceBase::handleGraceLsaReceived(uint32_t advertisingRouter, const GraceLsaTlv& tlv)
{
    Neighbor* nbr = ntable.lookup(advertisingRouter);
    if (!nbr) return;

    nbr->helperDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(tlv.gracePeriodSeconds);
    nbr->helpingRestart.store(true, std::memory_order_release);
}

bool OspfInterfaceBase::compareLSASummary(const LsaHeader& hdr, const LsaKey& key) const
{
    return area.compareLSASummary(hdr, key);
}

template <typename Policy>
std::optional<Area::Result> OspfInterfaceBase::processLsa(IncomingLsaContext& ctx, LsaBody& body)
{
    return area.processLsa<Policy>(ctx, body);
}

template std::optional<Area::Result> OspfInterfaceBase::processLsa<PolicyV2>(IncomingLsaContext&, LsaBody&);
template std::optional<Area::Result> OspfInterfaceBase::processLsa<PolicyV3>(IncomingLsaContext&, LsaBody&);

void OspfInterfaceBase::runAreaDCIntegrityScan()
{
    area.runDCIntegrityScan();
}

void OspfInterfaceBase::beginGracefulRestart(uint32_t gracePeriodSeconds, GraceRestartReason reason)
{
    graceDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(gracePeriodSeconds);
    gracefulRestartInProgress.store(true, std::memory_order_release);
    graceManager.originateGraceLsa(gracePeriodSeconds, reason);
}

void OspfInterfaceBase::endGracefulRestart()
{
    gracefulRestartInProgress.store(false, std::memory_order_release);
    graceManager.flushGraceLsa();
}

const LsdbTable& OspfInterfaceBase::getLsdb() const
{
    return area.lsdb;
}

const config::OspfRegistry& OspfInterfaceBase::getProcessConfigs() const
{
    return process.configs;
}

const config::OspfAreaRegistry& OspfInterfaceBase::getAreaConfigs() const
{
    return area.configs;
}

core::ProcessQueue& OspfInterfaceBase::getScheduler() const
{
    return area.process.scheduler;
}

template OspfInterfaceBase::OspfInterfaceBase(OspfProcess& proc, const OspfInterfaceId& id, const config::OspfVirtualLinkRegistry& cfgs);
template OspfInterfaceBase::OspfInterfaceBase(OspfProcess& proc, const OspfInterfaceId& id, const config::OspfGlobalInterfaceRegistry& cfgs);
}
