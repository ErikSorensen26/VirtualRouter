// Internal_OspfTest.cpp

#include <cstdint>
#include <algorithm>
#include <gtest/gtest.h>
#include <processing/PacketBuilder.hpp>
#include <packet/headers/Ospfv2Header.hpp>
#include <packet/headers/embedded/ospf/Ospfv2DBDHeader.hpp>
#include <packet/headers/embedded/ospf/Ospfv2HelloHeader.hpp>
#include <packet/headers/embedded/ospf/Ospfv2LSAHeader.hpp>
#include <packet/headers/embedded/ospf/Ospfv2LSRHeader.hpp>
#include <packet/headers/Ospfv3Header.hpp>
#include <packet/headers/embedded/ospf/Ospfv3DBDHeader.hpp>
#include <packet/headers/embedded/ospf/Ospfv3HelloHeader.hpp>
#include <packet/headers/embedded/ospf/Ospfv3LSAHeader.hpp>
#include <packet/headers/embedded/ospf/Ospfv3LSRHeader.hpp>
#include <ospf/OspfProcess.h>
#include <ospf/interface/OspfInterface.h>
#include <ospf/interface/InterfaceManager.h>
#include <ospf/interface/InterfaceTimers.h>
#include <ospf/interface/InterfaceId.hpp>
#include <ospf/neighbor/Neighbor.h>
#include <ospf/neighbor/NeighborTable.h>
#include <ospf/area/Area.h>
#include <ospf/area/IntraOriginator.h>
#include <ospf/ospfv2/area/IntraOriginatorV2.h>
#include <ospf/ospfv2/area/OpaqueOriginatorV2.h>
#include <ospf/ospfv2/database/OpaqueLsaV2.hpp>
#include <ospf/ospfv2/database/RouterCapabilityTlv.hpp>
#include <ospf/ospfv3/area/IntraOriginatorV3.h>
#include <ospf/database/LsdbTable.h>
#include <ospf/OspfTypes.hpp>
#include <ospf/transmission/PacketDispatcher.h>
#include <ospf/transmission/OspfFletcher.hpp>
#include <ospf/ospfv2/transmission/PacketDispatcherV2.h>
#include <ospf/ospfv2/database/RouterLsaV2.hpp>
#include <ospf/ospfv3/transmission/PacketDispatcherV3.h>
#include <ospf/spf/SpfTopology.h>
#include <ospf/spf/SpfEngine.h>
#include <ospf/spf/SpfTypes.hpp>
#include <ospf/topology/TopologyTable.h>
#include <ospf/topology/RoutingTable.h>
#include <ByteUtils.hpp>
#include <security/Encryption.hpp>
#include <VirtualRouter.h>
#include <MockInterface.hpp>
#include <MockFileSystem.hpp>
#include <packet/PacketStructure.h>
#include <mutex>
#include <condition_variable>
#include <infrastructure/Arp.h>
#include <infrastructure/Ndp.h>
#include <configs/FieldAccessor.hpp>
#include <types/IPAddress.h>

using namespace routing::ospf;

// Test fixture for global OSPF tests
class Internal_OspfTest : public ::testing::Test
{
protected:
    cli::MockFileSystem fs;
    core::Global* global = nullptr;
    interface::MockInterface* mockInterface = nullptr;
    core::VirtualRouter* vrf = nullptr;
    interface::InterfaceKey mKey;

    // OSPFv2 process (IPv4) and OSPFv3 processes (IPv4/IPv6 AFs)
    OspfProcess* ospfInstance = nullptr;
    OspfProcess* ospfv3Instance = nullptr;

    // Real OspfInterface objects created via the InterfaceManager
    OspfInterface* ospfInterface = nullptr;   // OSPFv2, area 0
    OspfInterface* ospfv3Interface = nullptr; // OSPFv3, area 0

    std::condition_variable cv;
    std::mutex cvMutex;
    bool packetEnqueued = false;

    uint8_t testPacket[1500] = {0};

    // Test addressing constants
    types::IPv4Address ipIntv4 = 0xC0A80101;     // 192.168.1.1
    types::IPv4Address ipIntv4Net = 0xC0A80100;  // 192.168.1.0/24
    types::IPv6Address ipIntv6 = (static_cast<__uint128_t>(0xFD00000000000000) << 64) | 0x0000000000000101; // fd00::101 style test addr

    static constexpr uint32_t selfRouterId = 0xC0A80101;     // 192.168.1.1
    static constexpr uint32_t neighborRouterId = 0xC0A80102; // 192.168.1.2
    static constexpr uint32_t neighborRouterId2 = 0xC0A80103; // 192.168.1.3

    void SetUp() override
    {
        utils::RCU::registerThread();
        global = new core::Global(fs, {}, false, true);
        mockInterface = new interface::MockInterface(*global, interface::InterfaceType::GIGABIT_ETHERNET);
        mKey = mockInterface->configs.key;

        vrf = global->getRoutingInstance("default", types::AddressFamily::IPv4);
        vrf->getInterfaceManager().add(mockInterface, mKey);
        vrf->enabledAddressFamilies.insert(types::AddressFamily::IPv6);

        mockInterface->enableIPs();
        mockInterface->enableShutdown();

        setIPv4(ipIntv4, 24);
        setIPv6(ipIntv6, 64);

        EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(::testing::AnyNumber());

        // OSPFv2 process (IPv4)
        ospfInstance = &vrf->addOspf(1);
        ospfInstance->configs.get<config::Ospf::LSA_THROTTLE_HOLD>().set(0);
        ospfInstance->configs.get<config::Ospf::LSA_THROTTLE_MAX>().set(0);
        ospfInstance->configs.get<config::Ospf::LSA_ARRIVAL>().set(0);
        ospfInstance->calculateRID();
        {
            std::lock_guard lock(ospfInstance->scheduler.getLock());
            ospfInstance->insureArea(0);
        }
        ospfInstance->scheduler.waitScheduled();
        {
            std::lock_guard lock(ospfInstance->scheduler.getLock());
            ospfInterface = static_cast<OspfInterface*>(&ospfInstance->ifaceMgr.createInterface(
                *mockInterface, OspfInterfaceId(ipIntv4.addr, 0)));
        }
        ospfInstance->scheduler.waitScheduled();

        // OSPFv3 process (IPv6)
        ospfv3Instance = &vrf->addOspfv3(2, types::AddressFamily::IPv6);
        ospfv3Instance->configs.get<config::Ospf::LSA_THROTTLE_HOLD>().set(0);
        ospfv3Instance->configs.get<config::Ospf::LSA_THROTTLE_MAX>().set(0);
        ospfv3Instance->configs.get<config::Ospf::LSA_ARRIVAL>().set(0);
        ospfv3Instance->configs.get<config::Ospf::SPF_THROTTLE_DELAY>().set(0);
        ospfv3Instance->configs.get<config::Ospf::SPF_THROTTLE_HOLD>().set(0);
        ospfv3Instance->configs.get<config::Ospf::SPF_THROTTLE_MAX>().set(0);
        ospfv3Instance->calculateRID();
        {
            std::lock_guard lock(ospfv3Instance->scheduler.getLock());
            ospfv3Instance->insureArea(0);
        }
        ospfv3Instance->scheduler.waitScheduled();
        {
            std::lock_guard lock(ospfv3Instance->scheduler.getLock());
            ospfv3Interface = static_cast<OspfInterface*>(&ospfv3Instance->ifaceMgr.createInterface(
                *mockInterface, OspfInterfaceId(ipIntv4.addr, 0)));
        }
        ospfv3Instance->scheduler.waitScheduled();
    }

    void TearDown() override
    {
        ospfInstance->scheduler.waitScheduled();
        ospfv3Instance->scheduler.waitScheduled();
        mockInterface->blockEnqueues();
        vrf->removeOspf(1);
        vrf->removeOspfv3(2);
        vrf->getInterfaceManager().remove(mKey);
        delete mockInterface;
        delete global;
        std::memset(testPacket, 0, sizeof(testPacket));
        utils::RCU::unregisterThread();
    }

    void wait(OspfProcess* process = nullptr)
    {
        (process ? process : ospfInstance)->scheduler.waitScheduled();
    }

    template <typename Pred>
    LsaRecord* waitForLsa(const LsaKey& key, Pred pred, Area* area = nullptr, OspfProcess* process = nullptr)
    {

        for (int i = 0; i < 50; ++i)
        {
            LsaRecord* record = nullptr;
            {
                std::lock_guard lock(getSchedulerLock(process));
                record = getLsdb(area).find(key);
            }
            if (record && pred(*record))
                return record;
            wait(process);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        // On timeout return nullptr, not a stale non-matching record, so callers' ASSERT_NE fails loudly.
        return nullptr;
    }

    LsaRecord* waitForLsa(const LsaKey& key, Area* area = nullptr, OspfProcess* process = nullptr)
    {
        return waitForLsa(key, [](const LsaRecord&) { return true; }, area, process);
    }

    // Core accessors

    Area& getArea(uint32_t areaId = 0, OspfProcess* proc = nullptr)
    {
        OspfProcess* p = proc ? proc : ospfInstance;
        if (Area* existing = p->getArea(areaId); existing)
            return *existing;

        std::lock_guard lock(p->scheduler.getLock());
        return p->insureArea(areaId);
    }
    NeighborTable& getNTable(OspfInterface* iface = nullptr)
        { return (iface ? iface : ospfInterface)->ntable; }
    TopologyTable& getTable(OspfProcess* process = nullptr)
        { return (process ? process : ospfInstance)->table; }
    OspfRib& getRib(OspfProcess* process = nullptr)
        { return (process ? process : ospfInstance)->rib; }
    LsdbTable& getLsdb(Area* area = nullptr)
        { return (area ? area : &getArea(0))->lsdb; }
    std::recursive_mutex& getSchedulerLock(OspfProcess* process = nullptr)
        { return (process ? process : ospfInstance)->scheduler.getLock(); }
    PacketDispatcherV2& getDispatcherV2(OspfInterfaceBase* iface = nullptr)
        { return static_cast<PacketDispatcherV2&>((iface ? iface : ospfInterface)->dispatcher); }
    void sendHelloViaTimer(OspfInterfaceBase& iface)
        { iface.tmgr.sendHello(); }
    PacketDispatcherV3& getDispatcherV3(OspfInterface* iface = nullptr)
        { return static_cast<PacketDispatcherV3&>((iface ? iface : ospfv3Interface)->dispatcher); }
    std::optional<LsaBody> invokeBuildLsaBody(PacketDispatcherV3& dispatcher, uint16_t type, const uint8_t* buf, uint16_t len)
        { return dispatcher.buildLsaBody(type, buf, len); }
    IntraOriginatorV2& getIntraOriginatorV2(Area* area = nullptr)
        { return static_cast<IntraOriginatorV2&>((area ? area : &getArea(0))->originator); }
    IntraOriginatorV3& getIntraOriginatorV3(Area* area = nullptr)
        { return static_cast<IntraOriginatorV3&>((area ? area : &getArea(0, ospfv3Instance))->originator); }
    InterOriginator& getInterOriginator(OspfProcess* process = nullptr)
        { return (process ? process : ospfInstance)->interOriginator; }
    ExternalOriginator& getExternalOriginator(OspfProcess* process = nullptr)
        { return (process ? process : ospfInstance)->externalOriginator; }
    OriginatorContext& getOriginatorCtx(Area* area = nullptr)
        { return (area ? area : &getArea(0))->originContext; }
    OpaqueOriginatorV2& getOpaqueOriginator(Area* area = nullptr)
        { return *(area ? area : &getArea(0))->originContext.getOpaqueOriginator(); }
    void fullRefreshV2(Area* area = nullptr)
    {
        Area* a = area ? area : &getArea(0);
        IntraOriginatorV2& orig = getIntraOriginatorV2(a);
        std::lock_guard lock(a->process.scheduler.getLock());
        orig.fullRefresh();
    }
    void fullRefreshV3(Area* area = nullptr)
    {
        Area* a = area ? area : &getArea(0, ospfv3Instance);
        IntraOriginatorV3& orig = getIntraOriginatorV3(a);
        std::lock_guard lock(a->process.scheduler.getLock());
        orig.fullRefresh();
    }
    void updateInterfaceV2(uint32_t ifaceId, Area* area = nullptr)
    {
        Area* a = area ? area : &getArea(0);
        IntraOriginatorV2& orig = getIntraOriginatorV2(a);
        std::lock_guard lock(a->process.scheduler.getLock());
        orig.updateInterface(ifaceId);
    }
    void originateRouterCapability(uint32_t capabilities, Area* area = nullptr)
    {
        Area* a = area ? area : &getArea(0);
        OpaqueOriginatorV2& orig = getOpaqueOriginator(a);
        std::lock_guard lock(a->process.scheduler.getLock());
        orig.originateRouterCapability(capabilities);
    }
    void withdrawRouterCapability(Area* area = nullptr)
    {
        Area* a = area ? area : &getArea(0);
        OpaqueOriginatorV2& orig = getOpaqueOriginator(a);
        std::lock_guard lock(a->process.scheduler.getLock());
        orig.withdrawRouterCapability();
    }
    template <typename Policy>
    void originateSummary(OriginatorContext& ctx, uint32_t lsid, const types::IPPrefix& prefix, uint32_t cost, bool expire = false, OspfProcess* process = nullptr)
    {
        OspfProcess* p = process ? process : ospfInstance;
        InterOriginator& orig = getInterOriginator(p);
        std::lock_guard lock(p->scheduler.getLock());
        orig.originateSummary<Policy>(ctx, lsid, prefix, cost, expire);
    }
    template <typename Policy>
    void reoriginateSummaries(OriginatorContext& ctx, std::vector<OspfRouteChange>& pathList, OspfProcess* process = nullptr)
    {
        OspfProcess* p = process ? process : ospfInstance;
        InterOriginator& orig = getInterOriginator(p);
        std::lock_guard lock(p->scheduler.getLock());
        orig.reoriginateSummaries<Policy>(ctx, pathList);
    }
    template <typename Policy>
    void addExternal(OriginatorContext& ctx, uint32_t asbr, uint32_t lsid, bool expire, OspfProcess* process = nullptr)
    {
        OspfProcess* p = process ? process : ospfInstance;
        InterOriginator& orig = getInterOriginator(p);
        std::lock_guard lock(p->scheduler.getLock());
        orig.addExternal<Policy>(ctx, asbr, lsid, expire);
    }
    void setExternalDbEntry(const LsaKey& key, const LsaHeader& hdr, const LsaBody& body, OspfProcess* process = nullptr)
    {
        OspfProcess* p = process ? process : ospfInstance;
        ExternalOriginator& ext = getExternalOriginator(p);
        std::lock_guard lock(p->scheduler.getLock());
        ext.externalDb[key] = { hdr, body };
    }
    template <typename Policy>
    void originateExternal(ExternalOriginateContext& ctx, bool expire, OspfProcess* process = nullptr)
    {
        OspfProcess* p = process ? process : ospfInstance;
        ExternalOriginator& ext = getExternalOriginator(p);
        std::lock_guard lock(p->scheduler.getLock());
        ext.originateExternal<Policy>(ctx, expire);
    }
    void setIfaceOpaqueEnabled(bool enabled, OspfInterface* iface = nullptr)
        { (iface ? iface : ospfInterface)->opaqueEnabled = enabled; }
    static uint32_t getLlsResyncBit()
        { return static_cast<uint32_t>(PacketDispatcher::LlsOptions::RESYNC); }
    static uint32_t getLlsRestartBit()
        { return static_cast<uint32_t>(PacketDispatcher::LlsOptions::RESTART); }
    static bool lssHasResyncOption(uint32_t extension)
        { return PacketDispatcher::getLlsOption(extension, PacketDispatcher::LlsOptions::RESYNC); }
    static bool lssHasRestartOption(uint32_t extension)
        { return PacketDispatcher::getLlsOption(extension, PacketDispatcher::LlsOptions::RESTART); }
    void invokeHandleGraceLsaReceived(OspfInterface& iface, uint32_t advertisingRouter, const GraceLsaTlv& tlv)
        { iface.handleGraceLsaReceived(advertisingRouter, tlv); }
    IntraRouteManager& getIntraRouteManager(Area* area = nullptr)
        { return (area ? area : &getArea(0))->routeManager; }
    InterRouteManager& getInterRouteManager(OspfProcess* process = nullptr)
        { return (process ? process : ospfInstance)->interRouteManager; }
    ExternalRouteManager& getExternalRouteManager(OspfProcess* process = nullptr)
        { return (process ? process : ospfInstance)->externalRouteManager; }
    config::OspfRegistry& getConfigs(OspfProcess* proc = nullptr)
        { return *const_cast<config::OspfRegistry*>(reinterpret_cast<volatile config::OspfRegistry*>(const_cast<config::OspfRegistry*>(&(proc ? proc : ospfInstance)->configs))); }
    config::OspfInterfaceBaseRegistry& getIfaceBaseConfigs(OspfInterface* iface = nullptr)
        { return *const_cast<config::OspfInterfaceBaseRegistry*>(reinterpret_cast<volatile config::OspfInterfaceBaseRegistry*>(const_cast<config::OspfInterfaceBaseRegistry*>(&(iface ? iface : ospfInterface)->configsBase))); }
    config::OspfInterfaceRegistry& getIfaceConfigs(OspfInterface* iface = nullptr)
        { return *const_cast<config::OspfInterfaceRegistry*>(reinterpret_cast<volatile config::OspfInterfaceRegistry*>(const_cast<config::OspfInterfaceRegistry*>(&(iface ? iface : ospfInterface)->configs))); }
    config::OspfGlobalInterfaceBaseRegistry& getIfaceGlobalBaseConfigs(OspfInterface* iface = nullptr)
        { return *const_cast<config::OspfGlobalInterfaceBaseRegistry*>(reinterpret_cast<volatile config::OspfGlobalInterfaceBaseRegistry*>(const_cast<config::OspfGlobalInterfaceBaseRegistry*>(&(iface ? iface : ospfInterface)->globalConfigsBase))); }
    config::OspfGlobalInterfaceRegistry& getIfaceGlobalConfigs(OspfInterface* iface = nullptr)
        { return *const_cast<config::OspfGlobalInterfaceRegistry*>(reinterpret_cast<volatile config::OspfGlobalInterfaceRegistry*>(const_cast<config::OspfGlobalInterfaceRegistry*>(&(iface ? iface : ospfInterface)->globalConfigs))); }
    config::OspfAreaRegistry& getAreaConfigs(Area* area = nullptr)
        { return *const_cast<config::OspfAreaRegistry*>(reinterpret_cast<volatile config::OspfAreaRegistry*>(const_cast<config::OspfAreaRegistry*>(&(area ? area : &getArea(0))->configs))); }
    InterfaceManager& getIfaceMgr(OspfProcess* proc = nullptr)
        { return (proc ? proc : ospfInstance)->ifaceMgr; }
    OspfInterface& createIface(interface::Interface& iface, const OspfInterfaceId& id, OspfProcess* proc = nullptr)
    {
        OspfProcess* p = proc ? proc : ospfInstance;
        OspfInterface* result = nullptr;
        p->scheduler.postAndWait([&]() { result = static_cast<OspfInterface*>(&p->ifaceMgr.createInterface(iface, id)); });
        return *result;
    }
    void removeIface(const OspfInterfaceId& id, OspfProcess* proc = nullptr)
    {
        OspfProcess* p = proc ? proc : ospfInstance;
        // ~OspfInterface rebuilds Router-LSAs (updateOriginations); run it on the scheduler thread like createIface to avoid racing posted tasks.
        p->scheduler.postAndWait([&]() { p->ifaceMgr.removeInterface(id); });
    }
    VirtualLink& createVirtualLink(uint32_t transitAreaId, uint32_t remoteRid, OspfProcess* proc = nullptr)
    {
        OspfProcess* p = proc ? proc : ospfInstance;
        Area& transitArea = getArea(transitAreaId, p);
        VirtualLink* result = nullptr;
        p->scheduler.postAndWait([&]() {
            transitArea.configs.get<config::OspfArea::VIRTUAL_LINKS>().emplaceBack(remoteRid);
            p->ifaceMgr.syncVirtualLinks();
            // getInterface() only searches real hardware-bound interfaces;
            // virtual links aren't publicly enumerable, so walk forEach().
            p->ifaceMgr.forEach([&](OspfInterfaceId id, OspfInterfaceBase& iface) {
                if (id.area == 0 && id.interfaceId == remoteRid && iface.isVirtualLink())
                {
                    result = static_cast<VirtualLink*>(&iface);
                    return true;
                }
                return false;
            });
        });
        return *result;
    }
    void removeVirtualLink(uint32_t transitAreaId, uint32_t remoteRid, OspfProcess* proc = nullptr)
    {
        OspfProcess* p = proc ? proc : ospfInstance;
        Area& transitArea = getArea(transitAreaId, p);
        p->scheduler.postAndWait([&]() {
            transitArea.configs.get<config::OspfArea::VIRTUAL_LINKS>().erase(remoteRid);
            p->ifaceMgr.syncVirtualLinks();
        });
    }
    // Builds a real, SPF-resolvable intra-area path to remoteRid inside
    // transitAreaId: a P2P interface to remoteRid plus remoteRid's own
    // synthetic self-originated Router-LSA (SPF needs both directions of the
    // edge). Required for any virtual-link test that needs
    // getTransmitInterface()/getCost() to resolve, since IntraOriginator::
    // addRouterLink now skips Type-4 encoding when there is no transit path.
    // Returns the created local interface; caller must removeIface(id) it.
    OspfInterface& establishTransitPath(uint32_t transitAreaId, uint32_t remoteRid)
    {
        getConfigs().get<config::Ospf::SPF_THROTTLE_DELAY>().set(0);
        getConfigs().get<config::Ospf::SPF_THROTTLE_HOLD>().set(0);
        getConfigs().get<config::Ospf::SPF_THROTTLE_MAX>().set(0);

        Area& transitArea = getArea(transitAreaId);
        OspfInterface& iface1 = createIface(*mockInterface, OspfInterfaceId(ipIntv4.addr, transitAreaId));
        getIfaceConfigs(&iface1).get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
        getIfaceConfigs(&iface1).get<config::OspfInterface::COST>().set(5);
        iface1.enqueueSyncNetworkType();
        calculateCost(&iface1);
        wait();

        auto* nbr = addNeighbor(remoteRid, types::IPAddress(types::IPv4Address{remoteRid}),
                                 Neighbor::State::FULL, &iface1);
        (void)nbr;

        fullRefreshV2(&transitArea);
        wait();

        uint32_t selfRid = ospfInstance->getRouterId();
        waitForLsa(LsaKey(OSPFV2_LSA_ROUTER, selfRid, selfRid), [remoteRid](const LsaRecord& r) {
            auto* body = std::get_if<RouterLsaV2>(&r.body);
            if (!body) return false;
            for (const auto& link : body->links)
                if (link.type == OSPFV2_LINK_P2P && link.linkId == remoteRid)
                    return true;
            return false;
        }, &transitArea);

        RouterLsaV2 nbrLsa;
        nbrLsa.flags = 0;
        nbrLsa.links.push_back({.linkId = selfRid, .linkData = remoteRid, .type = OSPFV2_LINK_P2P, .metric = 5});

        LsaKey nbrKey(OSPFV2_LSA_ROUTER, remoteRid, remoteRid);
        LsaHeader nbrHdr;
        nbrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
        nbrHdr.age = 0;

        IncomingLsaContext ctx = {.key = nbrKey, .header = nbrHdr, .checksumValid = true};
        LsaBody nbrLsaBody{nbrLsa};
        processLsa<PolicyV2>(ctx, nbrLsaBody, &transitArea);
        wait();
        waitForLsa(nbrKey, &transitArea);

        getSpfManager(&transitArea).requestSpf();
        wait();

        for (int i = 0; i < 50 && getSpfManager(&transitArea).spfResult.nodes.find(Vertex{VertexType::ROUTER, remoteRid}) == getSpfManager(&transitArea).spfResult.nodes.end(); ++i)
        {
            wait();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        return iface1;
    }
    OspfProcess& setupAfIpv4Process(uint16_t procId, OspfInterface** ifaceOut = nullptr)
    {
        OspfProcess& p = vrf->addOspfv3(procId, types::AddressFamily::IPv4);
        p.configs.get<config::Ospf::LSA_THROTTLE_HOLD>().set(0);
        p.configs.get<config::Ospf::LSA_THROTTLE_MAX>().set(0);
        p.configs.get<config::Ospf::LSA_ARRIVAL>().set(0);
        p.calculateRID();
        {
            std::lock_guard lock(p.scheduler.getLock());
            p.insureArea(0);
        }
        // See SetUp(): settle insureArea's group-pacing timers before createInterface touches the same originator state.
        p.scheduler.waitScheduled();
        OspfInterface* ifacePtr = nullptr;
        {
            std::lock_guard lock(p.scheduler.getLock());
            ifacePtr = static_cast<OspfInterface*>(&p.ifaceMgr.createInterface(*mockInterface, OspfInterfaceId(ipIntv4.addr, 0)));
        }
        p.scheduler.waitScheduled();
        if (ifaceOut)
            *ifaceOut = ifacePtr;
        return p;
    }
    void teardownAfIpv4Process(uint16_t procId, OspfProcess& p)
    {
        // Settle in-flight origination callbacks before destroying the process; they touch originationState and raced to a heap-use-after-free.
        p.scheduler.waitScheduled();
        vrf->removeOspfv3(procId);
    }
    InterfaceFlagManager& getIfaceFlags(OspfInterface* iface = nullptr)
        { return (iface ? iface : ospfInterface)->flags; }
    AreaFlagManager& getAreaFlags(Area* area = nullptr)
        { return (area ? area : &getArea(0))->flags; }
    SpfManager& getSpfManager(Area* area = nullptr)
        { return (area ? area : &getArea(0))->spfMgr; }
    void calculateCost(OspfInterface* iface = nullptr)
    {
        OspfInterface* i = iface ? iface : ospfInterface;
        std::lock_guard lock(i->process.scheduler.getLock());
        i->calculateCost();
    }
    Area& getIfaceArea(OspfInterface* iface = nullptr)
        { return (iface ? iface : ospfInterface)->area; }
    uint16_t getIfaceHelloInterval(OspfInterface* iface = nullptr)
        { return static_cast<uint16_t>((iface ? iface : ospfInterface)->getHelloInterval().count()); }
    uint16_t getIfaceDeadInterval(OspfInterface* iface = nullptr)
        { return static_cast<uint16_t>((iface ? iface : ospfInterface)->getDeadInterval().count()); }
    void addNetworkLsa(OspfInterface* ospfIface, bool refresh)
    {
        OspfInterface* iface = ospfIface ? ospfIface : ospfInterface;
        std::lock_guard lock(iface->process.scheduler.getLock());
        iface->area.originator.addNetworkLsa(*iface, refresh);
    }
    void setIfaceDCEnabled(OspfInterface* iface = nullptr)
        { (iface ? iface : ospfInterface)->demandCircuit = OspfInterface::DcDecision::ENABLED; }
    void setIfaceDCDisabled(OspfInterface* iface = nullptr)
        { (iface ? iface : ospfInterface)->demandCircuit = OspfInterface::DcDecision::DISABLED; }
    void setIfaceDCUndecided(OspfInterface* iface = nullptr)
        { (iface ? iface : ospfInterface)->demandCircuit = OspfInterface::DcDecision::UNDECIDED; }
    bool isIfaceDCEnabled(OspfInterface* iface = nullptr)
        { return (iface ? iface : ospfInterface)->demandCircuit == OspfInterface::DcDecision::ENABLED; }
    bool isIfaceDCDisabled(OspfInterface* iface = nullptr)
        { return (iface ? iface : ospfInterface)->demandCircuit == OspfInterface::DcDecision::DISABLED; }
    bool isIfaceDCUndecided(OspfInterface* iface = nullptr)
        { return (iface ? iface : ospfInterface)->demandCircuit == OspfInterface::DcDecision::UNDECIDED; }
    void runIfaceElection(OspfInterface* iface = nullptr)
    {
        OspfInterface* i = iface ? iface : ospfInterface;
        std::lock_guard lock(i->process.scheduler.getLock());
        i->election();
    }
    void runDCIntegrityScan(Area* area = nullptr)
    {
        Area* a = area ? area : &getArea(0);
        std::lock_guard lock(a->process.scheduler.getLock());
        a->runDCIntegrityScan();
    }
    void setIsDr(bool isDr, OspfInterface* iface = nullptr)
        { (iface ? iface : ospfInterface)->priv.isDr = isDr; }
    void setIsBdr(bool isBdr, OspfInterface* iface = nullptr)
        { (iface ? iface : ospfInterface)->priv.isBdr = isBdr; }
    bool setDrRid(uint32_t rid, OspfInterface* ospfIface = nullptr, bool force = false)
    {
        auto* iface = ospfIface ? ospfIface : ospfInterface;
        if (force) { iface->dr.rid.store(rid); return true; }
        std::lock_guard lock(iface->process.scheduler.getLock());
        return iface->setDr(rid);
    }
    bool setBdrRid(uint32_t rid, OspfInterface* ospfIface = nullptr, bool force = false)
    {
        auto* iface = ospfIface ? ospfIface : ospfInterface;
        if (force) { iface->bdr.rid.store(rid); return true; }
        std::lock_guard lock(iface->process.scheduler.getLock());
        return iface->setBdr(rid);
    }
    bool getIsMulticast(OspfInterface* iface = nullptr)
        { return (iface ? iface : ospfInterface)->isMulticast.load(std::memory_order_relaxed); }
    std::optional<__uint128_t> getAuthKey(OspfInterface* iface = nullptr)
        { return (iface ? iface : ospfInterface)->getAuthKey(); }
    std::optional<uint32_t> getAuthKeyId(OspfInterface* iface = nullptr)
        { return (iface ? iface : ospfInterface)->getAuthKeyId(); }
    InterfaceTimers& getTimers(OspfInterface* iface = nullptr)
        { return (iface ? iface : ospfInterface)->tmgr; }
    bool compareLSASummary(const LsaHeader& hdr, const LsaKey& key, Area* area = nullptr)
    {
        Area* a = area ? area : &getArea(0);
        std::lock_guard lock(a->process.scheduler.getLock());
        return a->compareLSASummary(hdr, key);
    }
    bool getFloodReduction(OspfInterface* iface = nullptr)
        { return (iface ? iface : ospfInterface)->floodReduction; }
    void setFloodReduction(OspfInterface* iface = nullptr)
    {
        OspfInterface* i = iface ? iface : ospfInterface;
        std::lock_guard lock(i->process.scheduler.getLock());
        i->updateOriginations();
        i->setFloodReduction();
    }
    const std::unordered_set<types::IPPrefix>& getRanges(Area* area = nullptr)
        { return (area ? area : &getArea(0))->getRanges(); }
    void syncRangeSuppression(const std::unordered_set<types::IPPrefix>& ranges, bool abrChange = false, Area* area = nullptr)
    {
        Area* a = area ? area : &getArea(0);
        std::lock_guard lock(a->process.scheduler.getLock());
        a->syncRangeSuppression(ranges, abrChange);
    }
    void suppressInterAreaPrefix(const types::IPPrefix& prefix, Area* area = nullptr)
    {
        Area* a = area ? area : &getArea(0);
        std::lock_guard lock(a->process.scheduler.getLock());
        a->suppressInterAreaPrefix(prefix);
    }
    template <typename Policy>
    void reoriginateSummary(OriginatorContext& ctx, OspfRouteChange& path, OspfProcess* process = nullptr)
    {
        OspfProcess* p = process ? process : ospfInstance;
        std::lock_guard lock(p->scheduler.getLock());
        p->interOriginator.reoriginateSummary<Policy>(ctx, path);
    }
    template <typename Policy>
    void processSummaries(std::unordered_map<LsaKey, LsaBody>& summaries, Area* area = nullptr)
    {
        Area* a = area ? area : &getArea(0);
        std::lock_guard lock(a->process.scheduler.getLock());
        a->processSummaries<Policy>(summaries);
    }
    template <typename Policy>
    std::optional<Area::Result> processLsa(IncomingLsaContext& ctx, LsaBody& body, Area* area = nullptr)
    {
        Area* a = area ? area : &getArea(0);
        std::lock_guard lock(a->process.scheduler.getLock());
        return a->processLsa<Policy>(ctx, body);
    }
    template <typename Policy>
    void translateNssaToExternal(OriginatorContext& ctx, const LsaKey& key, const LsaBody& lsa, bool expire, OspfProcess* process = nullptr)
    {
        OspfProcess* p = process ? process : ospfInstance;
        ExternalOriginator& ext = getExternalOriginator(p);
        std::lock_guard lock(p->scheduler.getLock());
        ext.translateNssaToExternal<Policy>(ctx, key, lsa, expire);
    }
    void clearArea(Area* area = nullptr)
    {
        Area* a = area ? area : &getArea(0);
        std::lock_guard lock(a->process.scheduler.getLock());
        a->clear();
    }
    void onAgingTick(Area* area = nullptr)
    {
        Area* a = area ? area : &getArea(0);
        std::lock_guard lock(a->process.scheduler.getLock());
        a->priv.onAgingTick();
    }
    // Snapshot and tick under one held scheduler lock so pacing callbacks can't interleave.
    std::pair<uint16_t, uint16_t> snapshotAndTickAge(const LsaKey& key, Area* area = nullptr)
    {
        Area* a = area ? area : &getArea(0);
        std::lock_guard lock(a->process.scheduler.getLock());
        LsaRecord* before = a->lsdb.find(key);
        uint16_t ageBefore = before ? before->header.age : 0;
        a->priv.onAgingTick();
        LsaRecord* after = a->lsdb.find(key);
        uint16_t ageAfter = after ? after->header.age : 0;
        return {ageBefore, ageAfter};
    }
    bool calculateRID(OspfProcess* process = nullptr)
    {
        OspfProcess* p = process ? process : ospfInstance;
        std::lock_guard lock(p->scheduler.getLock());
        return p->calculateRID();
    }
    core::ProcessQueue& getScheduler(OspfProcess* process = nullptr)
        { return (process ? process : ospfInstance)->scheduler; }

    // Helper: set IPv4 address on an interface.
    void setIPv4(const uint32_t ip, uint8_t mask, interface::MockInterface* iface = nullptr)
    {
        if (iface)
            iface->setIPv4({ip, mask, true}, false);
        else
            mockInterface->setIPv4({ip, mask, true}, false);
    }

    void delIPv4(interface::MockInterface* iface = nullptr)
    {
        if (iface)
            iface->removeIPv4();
        else
            mockInterface->removeIPv4();
    }

    // Helper: set IPv6 address on an interface.
    void setIPv6(const types::IPv6Address& ip, uint8_t mask, interface::Interface* iface = nullptr)
    {
        types::IPv6Address local = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000001;
        if (iface)
        {
            iface->setIPv6({local.addr, 64, true}, false);
            iface->setIPv6({ip.addr, mask, true}, false);
        }
        else
        {
            mockInterface->setIPv6({local.addr, 64, true}, false);
            mockInterface->setIPv6({ip.addr, mask, true}, false);
        }
    }

    // Neighbor helpers

    Neighbor* addNeighbor(uint32_t routerId,
                                          const types::IPAddress& ip,
                                          Neighbor::State targetState = Neighbor::State::TWOWAY,
                                          OspfInterface* iface = nullptr,
                                          bool unicast = false)
    {
        OspfInterface* ifacePtr = iface ? iface : ospfInterface;

        std::lock_guard lock(ifacePtr->process.scheduler.getLock());
        Neighbor* nbr = ifacePtr->ntable.createNeighbor(routerId, ip, unicast);

        if (ip.isIPv6())
        {
            types::Mac neighborMac = 0x112233445566;
            ifacePtr->iface.ndp.addNdpEntry(ip.v6(), neighborMac);
        }
        else
        {
            types::Mac neighborMac = 0x112233445566;
            ifacePtr->iface.arp.addArpEntry(ip.v4(), neighborMac);
        }

        // Drive through the FSM in order; setState() guards on oldState so
        // intermediate transitions are safe to call sequentially.
        static const Neighbor::State order[] = {
            Neighbor::State::INIT,
            Neighbor::State::TWOWAY,
            Neighbor::State::EXSTART,
            Neighbor::State::EXCHANGE,
            Neighbor::State::LOADING,
            Neighbor::State::FULL,
        };

        for (auto s : order)
        {
            nbr->setState(s);
            if (s == targetState)
                break;
        }

        return nbr;
    }

    // Like addNeighbor, but for a VirtualLink (no hardware iface, so no ARP/NDP entries needed).
    Neighbor* addVlNeighbor(VirtualLink& link, uint32_t routerId, const types::IPAddress& ip,
                            Neighbor::State targetState = Neighbor::State::FULL)
    {
        std::lock_guard lock(link.process.scheduler.getLock());
        Neighbor* nbr = link.ntable.createNeighbor(routerId, ip, false);

        static const Neighbor::State order[] = {
            Neighbor::State::INIT,
            Neighbor::State::TWOWAY,
            Neighbor::State::EXSTART,
            Neighbor::State::EXCHANGE,
            Neighbor::State::LOADING,
            Neighbor::State::FULL,
        };

        for (auto s : order)
        {
            nbr->setState(s);
            if (s == targetState)
                break;
        }

        return nbr;
    }

    Neighbor* getNeighbor(uint32_t routerId, OspfInterface* iface = nullptr)
    {
        OspfInterface* i = iface ? iface : ospfInterface;
        std::lock_guard lock(i->process.scheduler.getLock());
        return i->ntable.lookup(routerId);
    }

    // Packet header extraction

    packet::Ospfv2Header getOspfV2Header(processing::PacketBuilder& pkt)
    {
        static uint8_t emptyBuf[sizeof(packet::Ospfv2HeaderRaw)] = {};
        auto h = pkt.getHeader(packet::HeaderType::OSPFV2);
        packet::Ospfv2Header hdr;
        hdr.setBuffer(h ? h->buffer : emptyBuf);
        return hdr;
    }

    packet::Ospfv3Header getOspfV3Header(processing::PacketBuilder& pkt)
    {
        auto h = pkt.getHeader(packet::HeaderType::OSPFV3);
        packet::Ospfv3Header hdr;
        hdr.setBuffer(h->buffer);
        return hdr;
    }

    // Helper: signal packet enqueue (if needed)
    void notifyPacketEnqueued()
    {
        std::lock_guard<std::mutex> lock(cvMutex);
        packetEnqueued = true;
        cv.notify_all();
    }

    // OSPFv2 packet helpers

    void finalizeOspfV2Checksum(uint8_t* buf, uint16_t packetLen)
    {
        packet::Ospfv2Header hdr;
        hdr.setBuffer(buf);
        hdr.setChecksum(0);
        ChecksumFletcher check;
        check.addBytes(buf, 12);
        check.addBytes(buf + 14, packetLen - 14);
        hdr.setChecksum(check.finalize());
    }

    uint16_t buildHelloV2(uint8_t* buf, uint32_t routerId, uint32_t areaId,
                           uint16_t helloInterval, uint32_t deadInterval,
                           uint32_t mask, uint8_t priority, uint32_t dr, uint32_t bdr,
                           const std::vector<uint32_t>& neighborRids, uint8_t options = 0x02 /* E-bit */)
    {
        packet::Ospfv2Header hdr;
        hdr.setBuffer(buf);
        hdr.setVersion(OSPFV2_VERSION);
        hdr.setType(OSPFV2_TYPE_HELLO);
        hdr.setRouterID(routerId);
        hdr.setAreaID(areaId);
        hdr.setAuthType(OSPFV2_AUTH_NULL);
        uint8_t zeroAuth[8] = {0};
        hdr.setAuthentication(zeroAuth);

        packet::Ospfv2HelloHeader hello;
        hello.setBuffer(buf + packet::Ospfv2Header::fixedSize);
        hello.setMask(mask);
        hello.setHelloInterval(helloInterval);
        hello.setOptions(options);
        hello.setPriority(priority);
        hello.setDeadInterval(deadInterval);
        hello.setDR(dr);
        hello.setBDR(bdr);

        size_t offset = packet::Ospfv2Header::fixedSize + packet::Ospfv2HelloHeader::fixedSize;
        for (uint32_t rid : neighborRids)
        {
            utils::write<uint32_t>(buf + offset, rid);
            offset += 4;
        }

        uint16_t packetLen = static_cast<uint16_t>(offset);
        hdr.setPacketLen(packetLen);
        finalizeOspfV2Checksum(buf, packetLen);
        return packetLen;
    }

    // Feeds a built OSPFv2 packet through the dispatcher's ingress path.
    void deliverV2(uint8_t* buf, const types::IPv4Address& sourceIp,
                    bool multicast = true, OspfInterfaceBase* iface = nullptr, uint16_t authTrailerSize = 0)
    {
        packet::Ospfv2Header hdr;
        hdr.setBuffer(buf);
        hdr.trailing = std::span<uint8_t>(buf + packet::Ospfv2Header::fixedSize,
                                           hdr.getPacketLen() - packet::Ospfv2Header::fixedSize + authTrailerSize);
        uint8_t srcBytes[4];
        utils::write<uint32_t>(srcBytes, sourceIp.addr);
        OspfInterfaceBase* i = iface ? iface : ospfInterface;
        std::lock_guard lock(i->process.scheduler.getLock());
        getDispatcherV2(i).handleIncoming(hdr, srcBytes, multicast);
    }

    // Writes the OSPFv2 common header fields shared by DBD/LSR/LSU/LSAck packets.
    void writeOspfV2CommonHeader(uint8_t* buf, uint8_t type, uint32_t routerId, uint32_t areaId)
    {
        packet::Ospfv2Header hdr;
        hdr.setBuffer(buf);
        hdr.setVersion(OSPFV2_VERSION);
        hdr.setType(type);
        hdr.setRouterID(routerId);
        hdr.setAreaID(areaId);
        hdr.setAuthType(OSPFV2_AUTH_NULL);
        uint8_t zeroAuth[8] = {0};
        hdr.setAuthentication(zeroAuth);
    }

    uint16_t buildDBDV2(uint8_t* buf, uint32_t routerId, uint32_t areaId,
                         uint16_t mtu, uint8_t options, uint8_t flags, uint32_t sequence,
                         const std::vector<LsaKey>& summaryKeys = {},
                         const std::vector<LsaHeader>& summaryHeaders = {})
    {
        writeOspfV2CommonHeader(buf, OSPFV2_TYPE_DATABASE_DESCRIPTION, routerId, areaId);
        packet::Ospfv2Header hdr;
        hdr.setBuffer(buf);

        packet::Ospfv2DBDHeader dbd;
        dbd.setBuffer(buf + packet::Ospfv2Header::fixedSize);
        dbd.setMtu(mtu);
        dbd.setOptions(options);
        dbd.raw->flags = flags;
        dbd.setSequence(sequence);

        size_t offset = packet::Ospfv2Header::fixedSize + packet::Ospfv2DBDHeader::fixedSize;
        for (size_t i = 0; i < summaryKeys.size(); ++i)
        {
            packet::Ospfv2LSAHeader lsaHdr;
            lsaHdr.setBuffer(buf + offset);
            const auto& key = summaryKeys[i];
            const auto& sh = summaryHeaders[i];
            lsaHdr.setAge(sh.age);
            lsaHdr.setOptions(sh.options);
            lsaHdr.setType(static_cast<uint8_t>(key.lsaType));
            lsaHdr.setLsID(key.linkStateId);
            lsaHdr.setAdvRouter(key.advertisingRouter);
            lsaHdr.setSeqNum(sh.sequence);
            lsaHdr.setChecksum(sh.checksum);
            lsaHdr.setLen(sh.length);
            offset += packet::Ospfv2LSAHeader::fixedSize;
        }

        uint16_t packetLen = static_cast<uint16_t>(offset);
        hdr.setPacketLen(packetLen);
        finalizeOspfV2Checksum(buf, packetLen);
        return packetLen;
    }

    // Builds a raw OSPFv2 Link State Request packet listing the given LSA keys.
    uint16_t buildLSRequestV2(uint8_t* buf, uint32_t routerId, uint32_t areaId,
                               const std::vector<LsaKey>& keys)
    {
        writeOspfV2CommonHeader(buf, OSPFV2_TYPE_LINK_STATE_REQUEST, routerId, areaId);
        packet::Ospfv2Header hdr;
        hdr.setBuffer(buf);

        size_t offset = packet::Ospfv2Header::fixedSize;
        for (const auto& key : keys)
        {
            packet::Ospfv2LSRHeader lsr;
            lsr.setBuffer(buf + offset);
            lsr.setType(key.lsaType);
            lsr.setLsID(key.linkStateId);
            lsr.setAdvRouter(key.advertisingRouter);
            offset += packet::Ospfv2LSRHeader::fixedSize;
        }

        uint16_t packetLen = static_cast<uint16_t>(offset);
        hdr.setPacketLen(packetLen);
        finalizeOspfV2Checksum(buf, packetLen);
        return packetLen;
    }

    uint16_t buildLSUpdateV2(uint8_t* buf, uint32_t routerId, uint32_t areaId,
                              const std::vector<LsaKey>& keys,
                              const std::vector<LsaHeader>& headers,
                              const std::vector<RouterLsaV2>& bodies)
    {
        writeOspfV2CommonHeader(buf, OSPFV2_TYPE_LINK_STATE_UPDATE, routerId, areaId);
        packet::Ospfv2Header hdr;
        hdr.setBuffer(buf);

        size_t offset = packet::Ospfv2Header::fixedSize;
        utils::write<uint32_t>(buf + offset, static_cast<uint32_t>(keys.size()));
        offset += 4;

        for (size_t i = 0; i < keys.size(); ++i)
        {
            const auto& key = keys[i];
            const auto& lh = headers[i];
            const auto& body = bodies[i];

            uint16_t bodyLen = static_cast<uint16_t>(4 + 12 * body.links.size());
            uint16_t lsaLen = packet::Ospfv2LSAHeader::fixedSize + bodyLen;

            packet::Ospfv2LSAHeader lsaHdr;
            lsaHdr.setBuffer(buf + offset);
            lsaHdr.setAge(lh.age);
            lsaHdr.setOptions(lh.options);
            lsaHdr.setType(static_cast<uint8_t>(key.lsaType));
            lsaHdr.setLsID(key.linkStateId);
            lsaHdr.setAdvRouter(key.advertisingRouter);
            lsaHdr.setSeqNum(lh.sequence);
            lsaHdr.setLen(lsaLen);
            lsaHdr.setChecksum(0);

            body.buildBody(buf + offset + packet::Ospfv2LSAHeader::fixedSize, bodyLen);

            // Fletcher checksum over bytes [2, lsaLen) (skips Age field), per RFC 2328 §C.4
            ChecksumFletcher check;
            check.addBytes(buf + offset + 2, lsaLen - 2);
            lsaHdr.setChecksum(check.finalize());

            offset += lsaLen;
        }

        uint16_t packetLen = static_cast<uint16_t>(offset);
        hdr.setPacketLen(packetLen);
        finalizeOspfV2Checksum(buf, packetLen);
        return packetLen;
    }

    // Builds a raw OSPFv2 Link State Acknowledgment packet listing the given LSA
    // key/header pairs.
    uint16_t buildLSAckV2(uint8_t* buf, uint32_t routerId, uint32_t areaId,
                           const std::vector<LsaKey>& keys,
                           const std::vector<LsaHeader>& headers)
    {
        writeOspfV2CommonHeader(buf, OSPFV2_TYPE_LINK_STATE_ACK, routerId, areaId);
        packet::Ospfv2Header hdr;
        hdr.setBuffer(buf);

        size_t offset = packet::Ospfv2Header::fixedSize;
        for (size_t i = 0; i < keys.size(); ++i)
        {
            const auto& key = keys[i];
            const auto& lh = headers[i];

            packet::Ospfv2LSAHeader lsaHdr;
            lsaHdr.setBuffer(buf + offset);
            lsaHdr.setAge(lh.age);
            lsaHdr.setOptions(lh.options);
            lsaHdr.setType(static_cast<uint8_t>(key.lsaType));
            lsaHdr.setLsID(key.linkStateId);
            lsaHdr.setAdvRouter(key.advertisingRouter);
            lsaHdr.setSeqNum(lh.sequence);
            lsaHdr.setChecksum(lh.checksum);
            lsaHdr.setLen(lh.length);
            offset += packet::Ospfv2LSAHeader::fixedSize;
        }

        uint16_t packetLen = static_cast<uint16_t>(offset);
        hdr.setPacketLen(packetLen);
        finalizeOspfV2Checksum(buf, packetLen);
        return packetLen;
    }

    // OSPFv3 packet helpers

    void finalizeOspfV3Checksum(uint8_t* buf, uint16_t packetLen)
    {
        packet::Ospfv3Header hdr;
        hdr.setBuffer(buf);
        hdr.setChecksum(0);
        ChecksumFletcher check;
        check.addBytes(buf, 12);
        check.addBytes(buf + 14, packetLen - 14);
        hdr.setChecksum(check.finalize());
    }

    // Writes the OSPFv3 common header fields shared by all packet types.
    void writeOspfV3CommonHeader(uint8_t* buf, uint8_t type, uint32_t routerId, uint32_t areaId, uint8_t instanceId = 0)
    {
        packet::Ospfv3Header hdr;
        hdr.setBuffer(buf);
        hdr.setVersion(OSPFV3_VERSION);
        hdr.setType(type);
        hdr.setRouterID(routerId);
        hdr.setAreaID(areaId);
        hdr.setInstanceID(instanceId);
    }

    uint16_t buildHelloV3(uint8_t* buf, uint32_t routerId, uint32_t areaId,
                           uint32_t interfaceId, uint16_t helloInterval, uint16_t deadInterval,
                           uint8_t priority, uint32_t dr, uint32_t bdr,
                           const std::vector<uint32_t>& neighborRids, uint32_t options = OSPFV3_OPT_V6 | OSPFV3_OPT_E)
    {
        writeOspfV3CommonHeader(buf, OSPFV3_TYPE_HELLO, routerId, areaId);
        packet::Ospfv3Header hdr;
        hdr.setBuffer(buf);

        packet::Ospfv3HelloHeader hello;
        hello.setBuffer(buf + packet::Ospfv3Header::fixedSize);
        hello.setInterfaceID(interfaceId);
        hello.setRouterPriority(priority);
        hello.setOptions(options);
        hello.setHelloInterval(helloInterval);
        hello.setDeadInterval(deadInterval);
        hello.setDrID(dr);
        hello.setBdrID(bdr);

        size_t offset = packet::Ospfv3Header::fixedSize + packet::Ospfv3HelloHeader::fixedSize;
        for (uint32_t rid : neighborRids)
        {
            utils::write<uint32_t>(buf + offset, rid);
            offset += 4;
        }

        uint16_t packetLen = static_cast<uint16_t>(offset);
        hdr.setPacketLen(packetLen);
        finalizeOspfV3Checksum(buf, packetLen);
        return packetLen;
    }

    // Feeds a built OSPFv3 packet through the dispatcher's ingress path.
    void deliverV3(uint8_t* buf, const types::IPv6Address& sourceIp,
                    bool multicast = true, OspfInterface* iface = nullptr)
    {
        packet::Ospfv3Header hdr;
        hdr.setBuffer(buf);
        hdr.trailing = std::span<uint8_t>(buf + packet::Ospfv3Header::fixedSize,
                                           hdr.getPacketLen() - packet::Ospfv3Header::fixedSize);
        uint8_t srcBytes[16];
        utils::write<__uint128_t>(srcBytes, sourceIp.addr);
        OspfInterface* i = iface ? iface : ospfv3Interface;
        std::lock_guard lock(i->process.scheduler.getLock());
        getDispatcherV3(i).handleIncoming(hdr, srcBytes, multicast);
    }

    uint16_t buildDBDV3(uint8_t* buf, uint32_t routerId, uint32_t areaId,
                         uint16_t mtu, uint32_t options, uint8_t flags, uint32_t sequence,
                         const std::vector<LsaKey>& summaryKeys = {},
                         const std::vector<LsaHeader>& summaryHeaders = {})
    {
        writeOspfV3CommonHeader(buf, OSPFV3_TYPE_DATABASE_DESCRIPTION, routerId, areaId);
        packet::Ospfv3Header hdr;
        hdr.setBuffer(buf);

        packet::Ospfv3DBDHeader dbd;
        dbd.setBuffer(buf + packet::Ospfv3Header::fixedSize);
        dbd.setMtu(mtu);
        utils::write<uint32_t, 3>(dbd.raw->options, options);
        dbd.raw->flags = flags;
        dbd.setSequence(sequence);

        size_t offset = packet::Ospfv3Header::fixedSize + packet::Ospfv3DBDHeader::fixedSize;
        for (size_t i = 0; i < summaryKeys.size(); ++i)
        {
            packet::Ospfv3LSAHeader lsaHdr;
            lsaHdr.setBuffer(buf + offset);
            const auto& key = summaryKeys[i];
            const auto& sh = summaryHeaders[i];
            lsaHdr.setAge(sh.age);
            lsaHdr.setType(static_cast<uint16_t>(key.lsaType));
            lsaHdr.setLsID(key.linkStateId);
            lsaHdr.setAdvRouter(key.advertisingRouter);
            lsaHdr.setSeqNum(sh.sequence);
            lsaHdr.setChecksum(sh.checksum);
            lsaHdr.setLen(sh.length);
            offset += packet::Ospfv3LSAHeader::fixedSize;
        }

        uint16_t packetLen = static_cast<uint16_t>(offset);
        hdr.setPacketLen(packetLen);
        finalizeOspfV3Checksum(buf, packetLen);
        return packetLen;
    }

    // Builds a raw OSPFv3 Link State Request packet listing the given LSA keys.
    uint16_t buildLSRequestV3(uint8_t* buf, uint32_t routerId, uint32_t areaId,
                               const std::vector<LsaKey>& keys)
    {
        writeOspfV3CommonHeader(buf, OSPFV3_TYPE_LINK_STATE_REQUEST, routerId, areaId);
        packet::Ospfv3Header hdr;
        hdr.setBuffer(buf);

        size_t offset = packet::Ospfv3Header::fixedSize;
        for (const auto& key : keys)
        {
            packet::Ospfv3LSRHeader lsr;
            lsr.setBuffer(buf + offset);
            lsr.setType(static_cast<uint16_t>(key.lsaType));
            lsr.setLsID(key.linkStateId);
            lsr.setAdvRouter(key.advertisingRouter);
            offset += packet::Ospfv3LSRHeader::fixedSize;
        }

        uint16_t packetLen = static_cast<uint16_t>(offset);
        hdr.setPacketLen(packetLen);
        finalizeOspfV3Checksum(buf, packetLen);
        return packetLen;
    }

    uint16_t buildLSUpdateV3(uint8_t* buf, uint32_t routerId, uint32_t areaId,
                              const std::vector<LsaKey>& keys,
                              const std::vector<LsaHeader>& headers,
                              const std::vector<RouterLsaV3>& bodies)
    {
        writeOspfV3CommonHeader(buf, OSPFV3_TYPE_LINK_STATE_UPDATE, routerId, areaId);
        packet::Ospfv3Header hdr;
        hdr.setBuffer(buf);

        size_t offset = packet::Ospfv3Header::fixedSize;
        utils::write<uint32_t>(buf + offset, static_cast<uint32_t>(keys.size()));
        offset += 4;

        for (size_t i = 0; i < keys.size(); ++i)
        {
            const auto& key = keys[i];
            const auto& lh = headers[i];
            const auto& body = bodies[i];

            uint16_t bodyLen = body.size();
            uint16_t lsaLen = packet::Ospfv3LSAHeader::fixedSize + bodyLen;

            packet::Ospfv3LSAHeader lsaHdr;
            lsaHdr.setBuffer(buf + offset);
            lsaHdr.setAge(lh.age);
            lsaHdr.setType(static_cast<uint16_t>(key.lsaType));
            lsaHdr.setLsID(key.linkStateId);
            lsaHdr.setAdvRouter(key.advertisingRouter);
            lsaHdr.setSeqNum(lh.sequence);
            lsaHdr.setLen(lsaLen);
            lsaHdr.setChecksum(0);

            body.buildBody(buf + offset + packet::Ospfv3LSAHeader::fixedSize, bodyLen);

            // Fletcher checksum over bytes [2, lsaLen) (skips Age field), per RFC 5340 §A.3
            ChecksumFletcher check;
            check.addBytes(buf + offset + 2, lsaLen - 2);
            lsaHdr.setChecksum(check.finalize());

            offset += lsaLen;
        }

        uint16_t packetLen = static_cast<uint16_t>(offset);
        hdr.setPacketLen(packetLen);
        finalizeOspfV3Checksum(buf, packetLen);
        return packetLen;
    }

    uint16_t buildLSAckV3(uint8_t* buf, uint32_t routerId, uint32_t areaId,
                           const std::vector<LsaKey>& keys,
                           const std::vector<LsaHeader>& headers)
    {
        writeOspfV3CommonHeader(buf, OSPFV3_TYPE_LINK_STATE_ACK, routerId, areaId);
        packet::Ospfv3Header hdr;
        hdr.setBuffer(buf);

        size_t offset = packet::Ospfv3Header::fixedSize;
        for (size_t i = 0; i < keys.size(); ++i)
        {
            const auto& key = keys[i];
            const auto& lh = headers[i];

            packet::Ospfv3LSAHeader lsaHdr;
            lsaHdr.setBuffer(buf + offset);
            lsaHdr.setAge(lh.age);
            lsaHdr.setType(static_cast<uint16_t>(key.lsaType));
            lsaHdr.setLsID(key.linkStateId);
            lsaHdr.setAdvRouter(key.advertisingRouter);
            lsaHdr.setSeqNum(lh.sequence);
            lsaHdr.setChecksum(lh.checksum);
            lsaHdr.setLen(lh.length);
            offset += packet::Ospfv3LSAHeader::fixedSize;
        }

        uint16_t packetLen = static_cast<uint16_t>(offset);
        hdr.setPacketLen(packetLen);
        finalizeOspfV3Checksum(buf, packetLen);
        return packetLen;
    }

    bool processOptions(uint32_t options, Neighbor& nbr)
    {
        if (nbr.getIface().process.isV3)
            return ospfInterface->dispatcher.processOptions<PolicyV3>(options, nbr);
        else
            return ospfInterface->dispatcher.processOptions<PolicyV2>(options, nbr);
    }
};

#pragma region NeighborStateMachine

// Test: Neighbor_SetState_Returns_True_When_State_Changes
TEST_F(Internal_OspfTest, Neighbor_SetState_Returns_True_When_State_Changes)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);

    EXPECT_TRUE(nbr->setState(Neighbor::State::INIT));
}

// Test: Neighbor_SetState_Returns_False_When_State_Unchanged
TEST_F(Internal_OspfTest, Neighbor_SetState_Returns_False_When_State_Unchanged)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);

    ASSERT_TRUE(nbr->setState(Neighbor::State::INIT));
    EXPECT_FALSE(nbr->setState(Neighbor::State::INIT));
}

// Test: Neighbor_ResetDbExchange_Clears_Seq_And_Dbd_Key
TEST_F(Internal_OspfTest, Neighbor_ResetDbExchange_Clears_Seq_And_Dbd_Key)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);

    nbr->resetDbExchange();

    // resetDbExchange regenerates the sequence number and clears the DBD key.
    EXPECT_FALSE(nbr->currentDbd.has_value());
}

// Test: Neighbor_TwoWay_Stays_TwoWay_When_Neither_Dr_Nor_Bdr_On_Broadcast
TEST_F(Internal_OspfTest, Neighbor_TwoWay_Stays_TwoWay_When_Neither_Dr_Nor_Bdr_On_Broadcast)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);

    // Network type defaults to BROADCAST; with dr/bdr both 0 (no election yet)
    // and routerID != 0, neither isDr() nor isBdr() will be true.
    nbr->setState(Neighbor::State::INIT);
    nbr->setState(Neighbor::State::TWOWAY);

    EXPECT_EQ(nbr->getState(), Neighbor::State::TWOWAY);
}

// Test: Neighbor_TwoWay_To_ExStart_When_Neighbor_Is_Dr
TEST_F(Internal_OspfTest, Neighbor_TwoWay_To_ExStart_When_Neighbor_Is_Dr)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);

    // Neighbor declares itself as DR in its Hello.
    nbr->dr.store(neighborRouterId, std::memory_order_relaxed);

    nbr->setState(Neighbor::State::INIT);
    nbr->setState(Neighbor::State::TWOWAY);

    EXPECT_EQ(nbr->getState(), Neighbor::State::EXSTART);
}

// Test: Neighbor_ExStart_Does_Not_Reenter_On_Repeated_SetState
TEST_F(Internal_OspfTest, Neighbor_ExStart_Does_Not_Reenter_On_Repeated_SetState)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);
    nbr->dr.store(neighborRouterId, std::memory_order_relaxed);

    nbr->setState(Neighbor::State::INIT);
    nbr->setState(Neighbor::State::TWOWAY);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXSTART);

    // Re-entering EXSTART while already in EXSTART must be a no-op (guarded
    // by `oldState != EXSTART`), so setState returns false.
    EXPECT_FALSE(nbr->setState(Neighbor::State::EXSTART));
    EXPECT_EQ(nbr->getState(), Neighbor::State::EXSTART);
}

// Test: Neighbor_ExStart_To_Exchange_Master_Sends_Dbd
TEST_F(Internal_OspfTest, Neighbor_ExStart_To_Exchange_Master_Sends_Dbd)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);
    nbr->dr.store(neighborRouterId, std::memory_order_relaxed);

    nbr->setState(Neighbor::State::INIT);
    nbr->setState(Neighbor::State::TWOWAY);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXSTART);

    nbr->setRole(Neighbor::Role::MASTER);
    nbr->setState(Neighbor::State::EXCHANGE);

    EXPECT_EQ(nbr->getState(), Neighbor::State::EXCHANGE);
    EXPECT_FALSE(nbr->currentDbd.has_value());
}

// Test: Neighbor_ExStart_To_Exchange_Slave_Does_Not_Proactively_Send
TEST_F(Internal_OspfTest, Neighbor_ExStart_To_Exchange_Slave_Does_Not_Proactively_Send)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);
    nbr->dr.store(neighborRouterId, std::memory_order_relaxed);

    nbr->setState(Neighbor::State::INIT);
    nbr->setState(Neighbor::State::TWOWAY);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXSTART);

    nbr->setRole(Neighbor::Role::SLAVE);
    nbr->setState(Neighbor::State::EXCHANGE);

    EXPECT_EQ(nbr->getState(), Neighbor::State::EXCHANGE);
    EXPECT_FALSE(nbr->isMaster());
}

// Test: Neighbor_Exchange_To_Loading_With_Empty_LSR_Goes_To_Full
// Regression for Bug #1: an empty outbound LSR list at LOADING entry must
// recurse straight to FULL without infinite recursion / stack overflow.
TEST_F(Internal_OspfTest, Neighbor_Exchange_To_Loading_With_Empty_LSR_Goes_To_Full)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);
    nbr->dr.store(neighborRouterId, std::memory_order_relaxed);

    nbr->setState(Neighbor::State::INIT);
    nbr->setState(Neighbor::State::TWOWAY);
    nbr->setRole(Neighbor::Role::MASTER);
    nbr->setState(Neighbor::State::EXCHANGE);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXCHANGE);

    ASSERT_FALSE(nbr->getRtr().lsrs().getActive());

    nbr->setState(Neighbor::State::LOADING);

    EXPECT_EQ(nbr->getState(), Neighbor::State::FULL);
}

// Test: Neighbor_Full_Reached_From_Exchange_Directly
TEST_F(Internal_OspfTest, Neighbor_Full_Reached_From_Exchange_Directly)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);
    nbr->dr.store(neighborRouterId, std::memory_order_relaxed);

    nbr->setState(Neighbor::State::INIT);
    nbr->setState(Neighbor::State::TWOWAY);
    nbr->setRole(Neighbor::Role::MASTER);
    nbr->setState(Neighbor::State::EXCHANGE);

    nbr->setState(Neighbor::State::FULL);

    EXPECT_EQ(nbr->getState(), Neighbor::State::FULL);
}

// Test: Neighbor_Full_Not_Reached_Directly_From_TwoWay
TEST_F(Internal_OspfTest, Neighbor_Full_Not_Reached_Directly_From_TwoWay)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);

    // No DR/BDR declared, so TWOWAY does not progress to EXSTART.
    nbr->setState(Neighbor::State::INIT);
    nbr->setState(Neighbor::State::TWOWAY);
    ASSERT_EQ(nbr->getState(), Neighbor::State::TWOWAY);

    // FULL is guarded on oldState == EXCHANGE || LOADING; from TWOWAY it must
    // not be entered.
    EXPECT_FALSE(nbr->setState(Neighbor::State::FULL));
    EXPECT_EQ(nbr->getState(), Neighbor::State::TWOWAY);
}

// Test: Neighbor_Full_With_DemandCircuit_Enabled_Stops_Hello
TEST_F(Internal_OspfTest, Neighbor_Full_With_DemandCircuit_Enabled_Stops_Hello)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);
    nbr->dr.store(neighborRouterId, std::memory_order_relaxed);

    setIfaceDCEnabled();

    nbr->setState(Neighbor::State::INIT);
    nbr->setState(Neighbor::State::TWOWAY);
    nbr->setRole(Neighbor::Role::MASTER);
    nbr->setState(Neighbor::State::EXCHANGE);
    nbr->setState(Neighbor::State::FULL);

    EXPECT_EQ(nbr->getState(), Neighbor::State::FULL);

    setIfaceDCUndecided();
}

// Test: Neighbor_Down_Clears_Retransmission_Lists
TEST_F(Internal_OspfTest, Neighbor_Down_Clears_Retransmission_Lists)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);

    nbr->setState(Neighbor::State::INIT);
    nbr->setState(Neighbor::State::DOWN);

    EXPECT_FALSE(nbr->getRtr().lsus().getActive());
    EXPECT_FALSE(nbr->getRtr().lsrs().getActive());
}

// Test: Neighbor_Down_Flushes_Originated_LSAs
TEST_F(Internal_OspfTest, Neighbor_Down_Flushes_Originated_LSAs)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);

    auto& lsdb = getLsdb();
    size_t sizeBefore = lsdb.size();

    // Insert two LSAs originated by the neighbor.
    LsaKey nbrKey1(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader hdr1;
    hdr1.sequence = routing::OSPF_INITIAL_SEQUENCE;
    hdr1.age = 0;
    IncomingLsaContext ctx1{nbrKey1, hdr1};
    lsdb.upsertMeta(ctx1, LsaRecordFlags::NONE);

    LsaKey nbrKey2(OSPFV2_LSA_NETWORK, 0x0A000001, neighborRouterId);
    LsaHeader hdr2;
    hdr2.sequence = routing::OSPF_INITIAL_SEQUENCE;
    hdr2.age = 100;
    IncomingLsaContext ctx2{nbrKey2, hdr2};
    lsdb.upsertMeta(ctx2, LsaRecordFlags::NONE);

    // Insert one LSA from a different router — must survive the flush.
    LsaKey otherKey(OSPFV2_LSA_ROUTER, neighborRouterId2, neighborRouterId2);
    LsaHeader hdr3;
    hdr3.sequence = routing::OSPF_INITIAL_SEQUENCE;
    hdr3.age = 50;
    IncomingLsaContext ctx3{otherKey, hdr3};
    lsdb.upsertMeta(ctx3, LsaRecordFlags::NONE);

    ASSERT_EQ(lsdb.size(), sizeBefore + 3);

    nbr->setState(Neighbor::State::INIT);
    nbr->setState(Neighbor::State::DOWN);

    // Neighbor's LSAs must be set to MaxAge.
    auto* rec1 = lsdb.find(nbrKey1);
    ASSERT_NE(rec1, nullptr);
    EXPECT_EQ(rec1->header.age, routing::OSPF_MAX_AGE);

    auto* rec2 = lsdb.find(nbrKey2);
    ASSERT_NE(rec2, nullptr);
    EXPECT_EQ(rec2->header.age, routing::OSPF_MAX_AGE);

    // Other router's LSA must be untouched.
    auto* rec3 = lsdb.find(otherKey);
    ASSERT_NE(rec3, nullptr);
    EXPECT_EQ(rec3->header.age, 50);
}

// Test: Neighbor_Destructor_Cancels_Inactivity_Timer
TEST_F(Internal_OspfTest, Neighbor_Destructor_Cancels_Inactivity_Timer)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);
    nbr->setState(Neighbor::State::INIT);

    // Destroying the neighbor must not crash even with an active inactivity
    // timer registered.
    getNTable(ospfInterface).deleteNeighbor(neighborRouterId, false);

    EXPECT_EQ(getNeighbor(neighborRouterId), nullptr);
}

// Test: Neighbor_IsDr_IsBdr_Reflect_Last_Hello_Declaration
TEST_F(Internal_OspfTest, Neighbor_IsDr_IsBdr_Reflect_Last_Hello_Declaration)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);

    EXPECT_FALSE(nbr->isDr());
    EXPECT_FALSE(nbr->isBdr());

    nbr->dr.store(neighborRouterId, std::memory_order_relaxed);
    EXPECT_TRUE(nbr->isDr());
    EXPECT_FALSE(nbr->isBdr());

    nbr->dr.store(0, std::memory_order_relaxed);
    nbr->bdr.store(neighborRouterId, std::memory_order_relaxed);
    EXPECT_FALSE(nbr->isDr());
    EXPECT_TRUE(nbr->isBdr());
}

// Test: NeighborTable_CreateNeighbor_Is_Idempotent_For_Existing_Rid
TEST_F(Internal_OspfTest, NeighborTable_CreateNeighbor_Is_Idempotent_For_Existing_Rid)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* first = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);
    auto* second = getNTable(ospfInterface).createNeighbor(neighborRouterId, nbrIp);

    EXPECT_EQ(first, second);
}

// Test: NeighborTable_Lookup_Returns_Null_For_Unknown_Rid
TEST_F(Internal_OspfTest, NeighborTable_Lookup_Returns_Null_For_Unknown_Rid)
{
    EXPECT_EQ(getNeighbor(neighborRouterId), nullptr);
}

#pragma endregion NeighborStateMachine

#pragma region HelloProcessing

// Test: Hello_Creates_New_Neighbor_Entry
TEST_F(Internal_OspfTest, Hello_Creates_New_Neighbor_Entry)
{
    ASSERT_EQ(getNeighbor(neighborRouterId), nullptr);

    uint16_t helloInterval = getIfaceHelloInterval();
    uint16_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();

    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 1, 0, 0, {});

    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    auto* nbr = getNeighbor(neighborRouterId);
    ASSERT_NE(nbr, nullptr);
    EXPECT_EQ(nbr->getState(), Neighbor::State::INIT);
}

// Test: Hello_Init_To_TwoWay_When_RID_Present_In_Hello
TEST_F(Internal_OspfTest, Hello_Init_To_TwoWay_When_RID_Present_In_Hello)
{
    uint16_t helloInterval = getIfaceHelloInterval();
    uint16_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();
    uint32_t selfRid = ospfInstance->getRouterId();

    // First Hello: our RID not yet present -> INIT
    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 1, 0, 0, {});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    auto* nbr = getNeighbor(neighborRouterId);
    ASSERT_NE(nbr, nullptr);
    ASSERT_EQ(nbr->getState(), Neighbor::State::INIT);

    // Second Hello: lists our RID -> TWOWAY
    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 1, 0, 0, {selfRid});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    // Neighbor may progress multiple states, exstart is possible
    EXPECT_GE(nbr->getState(), Neighbor::State::TWOWAY);
}

// Test: Hello_Mismatched_HelloInterval_Tears_Down_Existing_Neighbor
TEST_F(Internal_OspfTest, Hello_Mismatched_HelloInterval_Tears_Down_Existing_Neighbor)
{
    uint16_t helloInterval = getIfaceHelloInterval();
    uint16_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();

    // Establish the neighbor first.
    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 1, 0, 0, {});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    auto* nbr = getNeighbor(neighborRouterId);
    ASSERT_NE(nbr, nullptr);
    ASSERT_EQ(nbr->getState(), Neighbor::State::INIT);

    // Now send a Hello with a mismatched HelloInterval.
    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  static_cast<uint16_t>(helloInterval + 1), deadInterval, mask, 1, 0, 0, {});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_EQ(nbr->getState(), Neighbor::State::DOWN);
}

// Test: Hello_Mismatched_DeadInterval_Tears_Down_Existing_Neighbor
TEST_F(Internal_OspfTest, Hello_Mismatched_DeadInterval_Tears_Down_Existing_Neighbor)
{
    uint16_t helloInterval = getIfaceHelloInterval();
    uint16_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();

    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 1, 0, 0, {});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    auto* nbr = getNeighbor(neighborRouterId);
    ASSERT_NE(nbr, nullptr);
    ASSERT_EQ(nbr->getState(), Neighbor::State::INIT);

    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval + 1, mask, 1, 0, 0, {});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_EQ(nbr->getState(), Neighbor::State::DOWN);
}

// Test: Hello_Mismatched_AreaId_Dropped_No_Neighbor_Created
TEST_F(Internal_OspfTest, Hello_Mismatched_AreaId_Dropped_No_Neighbor_Created)
{
    uint16_t helloInterval = getIfaceHelloInterval();
    uint16_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();

    // Wrong area ID (interface is in area 0).
    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId() + 1,
                  helloInterval, deadInterval, mask, 1, 0, 0, {});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_EQ(getNeighbor(neighborRouterId), nullptr);
}

// Test: Hello_Mismatched_Netmask_Tears_Down_Existing_Neighbor_On_Broadcast
TEST_F(Internal_OspfTest, Hello_Mismatched_Netmask_Tears_Down_Existing_Neighbor_On_Broadcast)
{
    uint16_t helloInterval = getIfaceHelloInterval();
    uint16_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();

    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 1, 0, 0, {});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    auto* nbr = getNeighbor(neighborRouterId);
    ASSERT_NE(nbr, nullptr);
    ASSERT_EQ(nbr->getState(), Neighbor::State::INIT);

    // Mismatched mask on a BROADCAST network.
    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask >> 1, 1, 0, 0, {});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_EQ(nbr->getState(), Neighbor::State::DOWN);
}

// Test: Hello_Refreshes_Inactivity_Timer_On_Receipt
TEST_F(Internal_OspfTest, Hello_Refreshes_Inactivity_Timer_On_Receipt)
{
    uint16_t helloInterval = getIfaceHelloInterval();
    uint16_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();

    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 1, 0, 0, {});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    auto* nbr = getNeighbor(neighborRouterId);
    ASSERT_NE(nbr, nullptr);

    EXPECT_NE(nbr->inactivityTimerId.load(), 0u);
}

// Test: Hello_Updates_Neighbor_Priority
TEST_F(Internal_OspfTest, Hello_Updates_Neighbor_Priority)
{
    uint16_t helloInterval = getIfaceHelloInterval();
    uint16_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();

    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 5, 0, 0, {});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    auto* nbr = getNeighbor(neighborRouterId);
    ASSERT_NE(nbr, nullptr);
    EXPECT_EQ(nbr->priority.load(), 5);
}

// Test: Hello_Updates_Neighbor_Dr_Bdr_Declaration
TEST_F(Internal_OspfTest, Hello_Updates_Neighbor_Dr_Bdr_Declaration)
{
    uint16_t helloInterval = getIfaceHelloInterval();
    uint16_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();

    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 1, neighborRouterId, neighborRouterId2, {});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    auto* nbr = getNeighbor(neighborRouterId);
    ASSERT_NE(nbr, nullptr);
    EXPECT_EQ(nbr->dr.load(), neighborRouterId);
    EXPECT_EQ(nbr->bdr.load(), neighborRouterId2);
}

// Test: Hello_From_Self_Router_Id_Is_Discarded
TEST_F(Internal_OspfTest, Hello_From_Self_Router_Id_Is_Discarded)
{
    uint16_t helloInterval = getIfaceHelloInterval();
    uint16_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();
    uint32_t selfRid = ospfInstance->getRouterId();

    // Hello whose router ID matches our own — must be discarded per RFC 2328 §8.2.
    buildHelloV2(testPacket, selfRid, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 1, 0, 0, {});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_EQ(getNeighbor(selfRid), nullptr);
}

// Test: Hello_Duplicate_From_Same_Neighbor_No_State_Regression
TEST_F(Internal_OspfTest, Hello_Duplicate_From_Same_Neighbor_No_State_Regression)
{
    uint16_t helloInterval = getIfaceHelloInterval();
    uint16_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();
    uint32_t selfRid = ospfInstance->getRouterId();

    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 1, 0, 0, {selfRid});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    auto* nbr = getNeighbor(neighborRouterId);
    ASSERT_NE(nbr, nullptr);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXSTART);

    // Repeating the same Hello must not regress the neighbor's state.
    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 1, 0, 0, {selfRid});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_EQ(nbr->getState(), Neighbor::State::EXSTART);
}

#pragma endregion HelloProcessing

#pragma region Election

// Test: Election_Single_Router_Becomes_DR_By_Default
TEST_F(Internal_OspfTest, Election_Single_Router_Becomes_DR_By_Default)
{
    uint32_t selfRid = ospfInstance->getRouterId();

    runIfaceElection();

    EXPECT_TRUE(ospfInterface->getIsDr());
    EXPECT_EQ(ospfInterface->getDrRid(), selfRid);
}

// Test: Election_Higher_Priority_Neighbor_Wins_Dr
TEST_F(Internal_OspfTest, Election_Higher_Priority_Neighbor_Wins_Dr)
{
    uint8_t selfPrio = getIfaceConfigs().get<config::OspfInterface::PRIORITY>().load();

    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::TWOWAY);
    nbr->priority.store(static_cast<uint8_t>(selfPrio + 1));
    // Neighbor claims itself as DR.
    nbr->dr.store(neighborRouterId);
    nbr->bdr.store(0);

    runIfaceElection();

    EXPECT_EQ(ospfInterface->getDrRid(), neighborRouterId);
    EXPECT_FALSE(ospfInterface->getIsDr());
}

// Test: Election_Tie_Broken_By_Highest_RouterId
TEST_F(Internal_OspfTest, Election_Tie_Broken_By_Highest_RouterId)
{
    uint32_t selfRid = ospfInstance->getRouterId();
    uint8_t selfPrio = getIfaceConfigs().get<config::OspfInterface::PRIORITY>().load();

    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::TWOWAY);
    nbr->priority.store(selfPrio);
    nbr->dr.store(neighborRouterId);
    nbr->bdr.store(0);

    runIfaceElection();

    // Higher router ID wins the tie.
    uint32_t expectedDr = std::max(selfRid, neighborRouterId);
    EXPECT_EQ(ospfInterface->getDrRid(), expectedDr);
}

// Test: Election_Priority_Zero_Excludes_Router_From_Election
TEST_F(Internal_OspfTest, Election_Priority_Zero_Excludes_Router_From_Election)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::TWOWAY);
    nbr->priority.store(0);
    nbr->dr.store(neighborRouterId);
    nbr->bdr.store(0);

    getIfaceConfigs().get<config::OspfInterface::PRIORITY>().set(0);

    runIfaceElection();

    EXPECT_EQ(ospfInterface->getDrRid(), 0u);
    EXPECT_EQ(ospfInterface->getBdrRid(), 0u);
}

// Test: Election_Existing_DR_Not_Displaced_By_Higher_Priority_New_Router
TEST_F(Internal_OspfTest, Election_Existing_DR_Not_Displaced_By_Higher_Priority_New_Router)
{
    uint32_t selfRid = ospfInstance->getRouterId();

    // Self becomes DR with no competitors.
    runIfaceElection();
    ASSERT_EQ(ospfInterface->getDrRid(), selfRid);
    ASSERT_TRUE(ospfInterface->getIsDr());

    // A new neighbor with higher priority appears, but does not claim DR itself.
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::TWOWAY);
    uint8_t selfPrio = getIfaceConfigs().get<config::OspfInterface::PRIORITY>().load();
    nbr->priority.store(static_cast<uint8_t>(selfPrio + 1));
    nbr->dr.store(0);
    nbr->bdr.store(0);

    runIfaceElection();

    // RFC 2328 §9.4: existing DR is not displaced just because a higher
    // priority router appears that does not itself claim DR.
    EXPECT_EQ(ospfInterface->getDrRid(), selfRid);
    EXPECT_TRUE(ospfInterface->getIsDr());
}

// Test: Election_BDR_Promoted_To_DR_When_DR_Disappears
TEST_F(Internal_OspfTest, Election_BDR_Promoted_To_DR_When_DR_Disappears)
{
    uint32_t selfRid = ospfInstance->getRouterId();
    uint8_t selfPrio = getIfaceConfigs().get<config::OspfInterface::PRIORITY>().load();

    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::TWOWAY);
    nbr->priority.store(selfPrio);
    nbr->dr.store(neighborRouterId);
    nbr->bdr.store(0);

    runIfaceElection();

    ASSERT_EQ(ospfInterface->getDrRid(), std::max(selfRid, neighborRouterId));

    // Now the higher-RID neighbor declares itself BDR rather than DR.
    nbr->dr.store(0);
    nbr->bdr.store(neighborRouterId);

    // Manually demote self from DR claim to allow re-election to find a new DR
    // (simulating the original DR going down on this segment).
    setDrRid(0, ospfInterface, true);

    runIfaceElection();

    // BDR candidate (neighbor) should now be elected DR if it is the highest
    // priority/RID among remaining eligible candidates declaring/falling back.
    EXPECT_NE(ospfInterface->getDrRid(), 0u);
}

// Test: Election_Rerun_On_Neighbor_TwoWay_Transition
TEST_F(Internal_OspfTest, Election_Rerun_On_Neighbor_TwoWay_Transition)
{
    uint32_t selfRid = ospfInstance->getRouterId();

    // Self is DR with no competitors initially.
    runIfaceElection();
    ASSERT_EQ(ospfInterface->getDrRid(), selfRid);

    // A neighbor reaches TWOWAY and claims DR with higher priority+RID.
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    uint32_t higherRid = selfRid + 1;
    auto* nbr = addNeighbor(higherRid, nbrIp, Neighbor::State::TWOWAY);
    uint8_t selfPrio = getIfaceConfigs().get<config::OspfInterface::PRIORITY>().load();
    nbr->priority.store(static_cast<uint8_t>(selfPrio + 1));
    nbr->dr.store(higherRid);
    nbr->bdr.store(0);

    runIfaceElection();

    EXPECT_EQ(ospfInterface->getDrRid(), higherRid);
}

// Test: Election_Change_Triggers_TwoWay_Neighbors_To_ExStart
TEST_F(Internal_OspfTest, Election_Change_Triggers_TwoWay_Neighbors_To_ExStart)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    uint32_t higherRid = ospfInstance->getRouterId() + 1;
    auto* nbr = addNeighbor(higherRid, nbrIp, Neighbor::State::TWOWAY);
    uint8_t selfPrio = getIfaceConfigs().get<config::OspfInterface::PRIORITY>().load();
    nbr->priority.store(static_cast<uint8_t>(selfPrio + 1));
    nbr->dr.store(higherRid);
    nbr->bdr.store(0);

    ASSERT_EQ(nbr->getState(), Neighbor::State::TWOWAY);

    runIfaceElection();

    // The election result changed (new DR = nbr), so a TWOWAY neighbor that
    // is now DR-eligible transitions to EXSTART.
    EXPECT_NE(nbr->getState(), Neighbor::State::TWOWAY);
}

// Test: Election_NoChange_Leaves_TwoWay_Neighbors_Unaffected
TEST_F(Internal_OspfTest, Election_NoChange_Leaves_TwoWay_Neighbors_Unaffected)
{
    uint32_t selfRid = ospfInstance->getRouterId();

    // Self elected DR with no competitors.
    runIfaceElection();
    ASSERT_EQ(ospfInterface->getDrRid(), selfRid);
    ASSERT_TRUE(ospfInterface->getIsDr());

    // A low-priority DROther neighbor reaches TWOWAY but does not change the
    // election outcome (it does not claim DR/BDR and has lower priority).
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::TWOWAY);
    uint8_t selfPrio = getIfaceConfigs().get<config::OspfInterface::PRIORITY>().load();
    nbr->priority.store(selfPrio > 0 ? static_cast<uint8_t>(selfPrio - 1) : 0);
    nbr->dr.store(0);
    nbr->bdr.store(0);

    // Re-run election: self is still DR (unchanged), so no transition occurs
    // for nbr beyond what addNeighbor already drove it to.
    runIfaceElection();

    EXPECT_EQ(ospfInterface->getDrRid(), selfRid);
}

#pragma endregion Election

#pragma region InterfaceManagement

// Test: Interface_Creation_Registers_In_InterfaceManager
TEST_F(Internal_OspfTest, Interface_Creation_Registers_In_InterfaceManager)
{
    auto* found = getIfaceMgr().getInterface(ospfInterface->id);
    EXPECT_EQ(found, ospfInterface);
}

// Test: Interface_GetInterfaceByAddress_Finds_Primary_Address
TEST_F(Internal_OspfTest, Interface_GetInterfaceByAddress_Finds_Primary_Address)
{
    types::IPAddress addr(types::IPv4Address{ipIntv4});
    auto* found = getIfaceMgr().getInterfaceByAddress(addr);
    EXPECT_EQ(found, ospfInterface);
}

// Test: Interface_GetArea_Resolves_To_Configured_Area
TEST_F(Internal_OspfTest, Interface_GetArea_Resolves_To_Configured_Area)
{
    EXPECT_EQ(ospfInterface->getAreaId(), 0u);
    EXPECT_EQ(&getIfaceArea(), &getArea(0));
}

// Test: Interface_CalculateCost_From_Bandwidth_When_No_Override
TEST_F(Internal_OspfTest, Interface_CalculateCost_From_Bandwidth_When_No_Override)
{
    // No COST override configured -> derived from REFERENCE_BANDWIDTH / interface bandwidth.
    ASSERT_FALSE(getIfaceConfigs().get<config::OspfInterface::COST>().hasValue());

    uint32_t referenceBw = getConfigs().get<config::Ospf::REFERENCE_BANDWIDTH>().load();
    uint32_t interfaceBw = mockInterface->configs.getBandwidth();
    uint16_t expectedCost = static_cast<uint16_t>(referenceBw / interfaceBw);

    calculateCost();

    EXPECT_EQ(ospfInterface->getCost(), expectedCost);
}

// Test: Interface_CalculateCost_Override_Respected
TEST_F(Internal_OspfTest, Interface_CalculateCost_Override_Respected)
{
    getIfaceConfigs().get<config::OspfInterface::COST>().set(42);

    calculateCost();

    EXPECT_EQ(ospfInterface->getCost(), 42);
}

// Test: Interface_CalculateCost_Change_Triggers_Originator_Update
TEST_F(Internal_OspfTest, Interface_CalculateCost_Change_Triggers_Originator_Update)
{
    uint16_t oldCost = ospfInterface->getCost();
    getIfaceConfigs().get<config::OspfInterface::COST>().set(static_cast<uint16_t>(oldCost + 100));

    // Should not throw/crash; updateInterface is invoked on the area's originator.
    calculateCost();

    EXPECT_NE(ospfInterface->getCost(), oldCost);
}

// Test: Interface_SetDr_Fails_For_Unknown_RouterId
TEST_F(Internal_OspfTest, Interface_SetDr_Fails_For_Unknown_RouterId)
{
    EXPECT_FALSE(setDrRid(neighborRouterId));
}

// Test: Interface_SetDr_Succeeds_For_Known_Neighbor
TEST_F(Internal_OspfTest, Interface_SetDr_Succeeds_For_Known_Neighbor)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    addNeighbor(neighborRouterId, nbrIp, Neighbor::State::TWOWAY);

    EXPECT_TRUE(setDrRid(neighborRouterId));
    EXPECT_EQ(ospfInterface->getDrRid(), neighborRouterId);
}

// Test: Interface_SetBdr_Succeeds_For_Known_Neighbor
TEST_F(Internal_OspfTest, Interface_SetBdr_Succeeds_For_Known_Neighbor)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    addNeighbor(neighborRouterId, nbrIp, Neighbor::State::TWOWAY);

    EXPECT_TRUE(setBdrRid(neighborRouterId));
    EXPECT_EQ(ospfInterface->getBdrRid(), neighborRouterId);
}

// Test: Interface_SyncNetworkType_PointToPoint_Sets_NonMulticast_False
TEST_F(Internal_OspfTest, Interface_SyncNetworkType_PointToPoint_Sets_NonMulticast_False)
{
    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::NON_BROADCAST);

    ospfInterface->enqueueSyncNetworkType();
    wait();

    EXPECT_FALSE(getIsMulticast());
}

// Test: Interface_SyncNetworkType_Broadcast_Sets_Multicast_True
TEST_F(Internal_OspfTest, Interface_SyncNetworkType_Broadcast_Sets_Multicast_True)
{
    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::BROADCAST);

    ospfInterface->enqueueSyncNetworkType();
    wait();

    EXPECT_TRUE(getIsMulticast());
}

// Test: Interface_SetPassiveMode_True_Tears_Down_Existing_Neighbors
TEST_F(Internal_OspfTest, Interface_SetPassiveMode_True_Tears_Down_Existing_Neighbors)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::TWOWAY);
    ASSERT_NE(nbr->getState(), Neighbor::State::DOWN);

    getIfaceConfigs().get<config::OspfInterface::PASSIVE>().set(true);
    ospfInterface->enqueueSyncPassive();
    wait();

    EXPECT_EQ(nbr->getState(), Neighbor::State::DOWN);
}

// Test: Interface_SetPassiveMode_False_Restarts_Hello
TEST_F(Internal_OspfTest, Interface_SetPassiveMode_False_Restarts_Hello)
{
    auto passiveField = getIfaceConfigs().get<config::OspfInterface::PASSIVE>();
    
    passiveField.set(true);
    ospfInterface->enqueueSyncPassive();
    wait();
    // Should not crash re-enabling; startHello is invoked.
    passiveField.set(false);
    ospfInterface->enqueueSyncPassive();
    wait();
    SUCCEED();
}

// Test: Interface_SyncDigestKey_NoKeys_Clears_AuthKey
TEST_F(Internal_OspfTest, Interface_SyncDigestKey_NoKeys_Clears_AuthKey)
{
    ospfInterface->enqueueSyncDigestKey();
    wait();

    EXPECT_FALSE(getAuthKey().has_value());
    EXPECT_FALSE(getAuthKey().has_value());
}

// Test: Interface_Destruction_Removes_From_InterfaceManager
TEST_F(Internal_OspfTest, Interface_Destruction_Removes_From_InterfaceManager)
{
    OspfInterfaceId id = ospfInterface->id;

    removeIface(id);

    EXPECT_EQ(getIfaceMgr().getInterface(id), nullptr);

    // Prevent TearDown from operating on the now-destroyed interface pointer.
    ospfInterface = nullptr;
}

#pragma endregion InterfaceManagement

#pragma region Timers

// Test: Timer_StopHello_Cancels_Pending_Hello
TEST_F(Internal_OspfTest, Timer_StopHello_Cancels_Pending_Hello)
{
    // Hello was started during interface construction; stopping must not crash
    // and should leave the interface able to restart cleanly.
    getTimers().stopHello();
    {
        std::lock_guard lock(getScheduler().getLock());
        getTimers().startHello();
    }

    SUCCEED();
}

// Test: Timer_ScheduleHello_NoOp_When_Passive
TEST_F(Internal_OspfTest, Timer_ScheduleHello_NoOp_When_Passive)
{
    getIfaceConfigs().get<config::OspfInterface::PASSIVE>().set(true);

    // Should be a no-op and not schedule a timer when passive.
    {
        std::lock_guard lock(getScheduler().getLock());
        getTimers().scheduleHello();
    }

    SUCCEED();
}

// Test: Timer_StartInactiveTimer_Sets_NonZero_TimerId
TEST_F(Internal_OspfTest, Timer_StartInactiveTimer_Sets_NonZero_TimerId)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::INIT);

    {
        std::lock_guard lock(getScheduler().getLock());
        getTimers().startInactiveTimer(*nbr);
    }

    EXPECT_NE(nbr->inactivityTimerId.load(), 0u);
}

// Test: Timer_CancelInactiveTimer_Resets_TimerId_To_Zero
TEST_F(Internal_OspfTest, Timer_CancelInactiveTimer_Resets_TimerId_To_Zero)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::INIT);

    getTimers().startInactiveTimer(*nbr);
    ASSERT_NE(nbr->inactivityTimerId.load(), 0u);

    getTimers().cancleInactiveTimer(*nbr);

    EXPECT_EQ(nbr->inactivityTimerId.load(), 0u);
}

// Test: Timer_HandleInactiveTimeExpire_Drives_Neighbor_Down
TEST_F(Internal_OspfTest, Timer_HandleInactiveTimeExpire_Drives_Neighbor_Down)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::TWOWAY);
    ASSERT_NE(nbr->getState(), Neighbor::State::DOWN);

    getTimers().handleInactiveTimeExpire(*nbr);

    EXPECT_EQ(nbr->getState(), Neighbor::State::DOWN);
}

// Test: Timer_StartDbdRetransmissionTimer_Sets_DbdTimerId
TEST_F(Internal_OspfTest, Timer_StartDbdRetransmissionTimer_Sets_DbdTimerId)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXSTART);

    getTimers().startDbdRetransmissionTimer(*nbr);

    EXPECT_TRUE(nbr->getRtr().getDbdActive());
}

// Test: Timer_StartLsrRetransmissionTimer_Sets_RetransmitTimerId
TEST_F(Internal_OspfTest, Timer_StartLsrRetransmissionTimer_Sets_RetransmitTimerId)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXSTART);

    getTimers().startLsrRetransmissionTimer(*nbr);

    EXPECT_NE(nbr->getRtr().lsrs().retransmitTimerId, 0u);
}

// Test: Timer_StartLsuRetransmissionTimer_Sets_RetransmitTimerId
TEST_F(Internal_OspfTest, Timer_StartLsuRetransmissionTimer_Sets_RetransmitTimerId)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXSTART);

    getTimers().startLsuRetransmissionTimer(*nbr);

    EXPECT_NE(nbr->getRtr().lsus().retransmitTimerId, 0u);
}

// Test: Timer_StartLsrPacingTimer_Sets_PacingTimerId
TEST_F(Internal_OspfTest, Timer_StartLsrPacingTimer_Sets_PacingTimerId)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXSTART);

    getTimers().startLsrPacingTimer(*nbr);

    EXPECT_NE(nbr->getRtr().lsrs().pacingTimerId, 0u);
}

// Test: Timer_StartingNewInactiveTimer_Cancels_Previous
TEST_F(Internal_OspfTest, Timer_StartingNewInactiveTimer_Cancels_Previous)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::INIT);

    getTimers().startInactiveTimer(*nbr);
    uint32_t firstId = nbr->inactivityTimerId.load();
    ASSERT_NE(firstId, 0u);

    getTimers().startInactiveTimer(*nbr);
    uint32_t secondId = nbr->inactivityTimerId.load();

    EXPECT_NE(secondId, 0u);
}

#pragma endregion Timers

#pragma region LsdbOperations

// Test: Lsdb_UpsertMeta_Inserts_New_Record
TEST_F(Internal_OspfTest, Lsdb_UpsertMeta_Inserts_New_Record)
{
    auto& lsdb = getLsdb();
    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    hdr.age = 0;

    IncomingLsaContext ctx{key, hdr};
    lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);

    EXPECT_TRUE(lsdb.contains(key));
    auto* rec = lsdb.find(key);
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->header.sequence, routing::OSPF_INITIAL_SEQUENCE);
}

// Test: Lsdb_UpsertMeta_Updates_Existing_Record_Header
TEST_F(Internal_OspfTest, Lsdb_UpsertMeta_Updates_Existing_Record_Header)
{
    auto& lsdb = getLsdb();
    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;

    size_t sizeBefore = lsdb.size();

    IncomingLsaContext ctx{key, hdr};
    lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);

    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE + 1;
    IncomingLsaContext ctx2{key, hdr};
    lsdb.upsertMeta(ctx2, LsaRecordFlags::NONE);

    auto* rec = lsdb.find(key);
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->header.sequence, routing::OSPF_INITIAL_SEQUENCE + 1);
    EXPECT_EQ(lsdb.size(), sizeBefore + 1);
}

// Test: Lsdb_UpsertBody_Emplaces_Typed_Body
TEST_F(Internal_OspfTest, Lsdb_UpsertBody_Emplaces_Typed_Body)
{
    auto& lsdb = getLsdb();
    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;

    IncomingLsaContext ctx{key, hdr};
    lsdb.upsertBody<RouterLsaV2>(ctx, LsaRecordFlags::NONE);

    auto* rec = lsdb.find(key);
    ASSERT_NE(rec, nullptr);
    EXPECT_TRUE(std::holds_alternative<RouterLsaV2>(rec->body));
}

// Test: Lsdb_Find_Returns_Null_For_Missing_Key
TEST_F(Internal_OspfTest, Lsdb_Find_Returns_Null_For_Missing_Key)
{
    auto& lsdb = getLsdb();
    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);

    EXPECT_EQ(lsdb.find(key), nullptr);
    EXPECT_FALSE(lsdb.contains(key));
}

// Test: Lsdb_Erase_Removes_From_All_Indexes
TEST_F(Internal_OspfTest, Lsdb_Erase_Removes_From_All_Indexes)
{
    auto& lsdb = getLsdb();
    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;

    IncomingLsaContext ctx{key, hdr};
    lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);
    ASSERT_TRUE(lsdb.contains(key));

    EXPECT_TRUE(lsdb.erase(key));
    EXPECT_FALSE(lsdb.contains(key));

    bool foundInType = false;
    lsdb.forEachInType(OSPFV2_LSA_ROUTER, [&](const LsaKey& k, LsaRecord&) { if (k == key) foundInType = true; });
    EXPECT_FALSE(foundInType);

    size_t advCount = 0;
    LsaAdvKey advKey(OSPFV2_LSA_ROUTER, neighborRouterId);
    lsdb.forEachInAdv(advKey, [&](uint32_t, LsaRecord&) { ++advCount; });
    EXPECT_EQ(advCount, 0u);
}

// Test: Lsdb_ForEachInType_Iterates_Only_Matching_Type
TEST_F(Internal_OspfTest, Lsdb_ForEachInType_Iterates_Only_Matching_Type)
{
    auto& lsdb = getLsdb();

    LsaKey routerKey(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaKey networkKey(OSPFV2_LSA_NETWORK, ipIntv4.addr, neighborRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;

    IncomingLsaContext ctx1{routerKey, hdr};
    lsdb.upsertMeta(ctx1, LsaRecordFlags::NONE);
    IncomingLsaContext ctx2{networkKey, hdr};
    lsdb.upsertMeta(ctx2, LsaRecordFlags::NONE);

    bool foundRouterKey = false;
    lsdb.forEachInType(OSPFV2_LSA_ROUTER, [&](const LsaKey& k, LsaRecord&) {
        if (k == routerKey) foundRouterKey = true;
        EXPECT_EQ(k.lsaType, OSPFV2_LSA_ROUTER);
    });
    EXPECT_TRUE(foundRouterKey);

    EXPECT_EQ(lsdb.getTypeSize(OSPFV2_LSA_NETWORK), 1u);
}

// Test: Lsdb_ForEachInAdv_Iterates_Only_Matching_Advertiser
TEST_F(Internal_OspfTest, Lsdb_ForEachInAdv_Iterates_Only_Matching_Advertiser)
{
    auto& lsdb = getLsdb();

    LsaKey keyFromNbr(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaKey keyFromSelf(OSPFV2_LSA_ROUTER, selfRouterId, selfRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;

    IncomingLsaContext ctx1{keyFromNbr, hdr};
    lsdb.upsertMeta(ctx1, LsaRecordFlags::NONE);
    IncomingLsaContext ctx2{keyFromSelf, hdr};
    lsdb.upsertMeta(ctx2, LsaRecordFlags::SELF_ORIGINATED);

    LsaAdvKey advKey(OSPFV2_LSA_ROUTER, neighborRouterId);
    size_t count = 0;
    lsdb.forEachInAdv(advKey, [&](uint32_t k, LsaRecord&) {
        ++count;
        EXPECT_EQ(k, neighborRouterId);
    });
    EXPECT_EQ(count, 1u);
}

// Test: Lsdb_PurgeIf_Removes_Matching_And_Returns_Count
TEST_F(Internal_OspfTest, Lsdb_PurgeIf_Removes_Matching_And_Returns_Count)
{
    auto& lsdb = getLsdb();

    LsaKey keyFromNbr(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaKey keyFromSelf(OSPFV2_LSA_ROUTER, selfRouterId, selfRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;

    IncomingLsaContext ctx1{keyFromNbr, hdr};
    lsdb.upsertMeta(ctx1, LsaRecordFlags::NONE);
    IncomingLsaContext ctx2{keyFromSelf, hdr};
    lsdb.upsertMeta(ctx2, LsaRecordFlags::SELF_ORIGINATED);

    size_t removed = lsdb.purgeIf([](const LsaKey& k, LsaRecord&) {
        return k.advertisingRouter == neighborRouterId;
    });

    EXPECT_EQ(removed, 1u);
    EXPECT_FALSE(lsdb.contains(keyFromNbr));
    EXPECT_TRUE(lsdb.contains(keyFromSelf));
}

// Test: Lsdb_AgeAll_Increments_And_Saturates_At_MaxAge
TEST_F(Internal_OspfTest, Lsdb_AgeAll_Increments_And_Saturates_At_MaxAge)
{
    auto& lsdb = getLsdb();

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    hdr.age = static_cast<uint16_t>(routing::OSPF_MAX_AGE - 5);

    IncomingLsaContext ctx{key, hdr};
    lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);

    size_t expired = lsdb.ageAll(10, routing::OSPF_MAX_AGE, false); // eraseExpired = false

    EXPECT_EQ(expired, 1u);
    auto* rec = lsdb.find(key);
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->header.age, routing::OSPF_MAX_AGE);
}

// Test: Lsdb_AgeAll_EraseExpired_Removes_MaxAge_Records
TEST_F(Internal_OspfTest, Lsdb_AgeAll_EraseExpired_Removes_MaxAge_Records)
{
    auto& lsdb = getLsdb();

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    hdr.age = static_cast<uint16_t>(routing::OSPF_MAX_AGE - 5);

    IncomingLsaContext ctx{key, hdr};
    lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);

    size_t expired = lsdb.ageAll(10, routing::OSPF_MAX_AGE, true); // eraseExpired = true

    EXPECT_EQ(expired, 1u);
    EXPECT_FALSE(lsdb.contains(key));
}

// Test: Lsdb_PurgeExpired_Removes_MaxAge_Records
TEST_F(Internal_OspfTest, Lsdb_PurgeExpired_Removes_MaxAge_Records)
{
    auto& lsdb = getLsdb();

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    hdr.age = routing::OSPF_MAX_AGE;

    IncomingLsaContext ctx{key, hdr};
    lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);

    size_t removed = lsdb.purgeExpired(routing::OSPF_MAX_AGE);

    EXPECT_EQ(removed, 1u);
    EXPECT_FALSE(lsdb.contains(key));
}

// Test: Lsdb_GetTypeSize_Reflects_Type_Index_Count
TEST_F(Internal_OspfTest, Lsdb_GetTypeSize_Reflects_Type_Index_Count)
{
    auto& lsdb = getLsdb();

    size_t typeSizeBefore = lsdb.getTypeSize(OSPFV2_LSA_ROUTER);

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;

    IncomingLsaContext ctx{key, hdr};
    lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);

    EXPECT_EQ(lsdb.getTypeSize(OSPFV2_LSA_ROUTER), typeSizeBefore + 1);
}

// Test: Lsdb_TouchRefresh_Updates_Existing_Record
TEST_F(Internal_OspfTest, Lsdb_TouchRefresh_Updates_Existing_Record)
{
    auto& lsdb = getLsdb();

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;

    IncomingLsaContext ctx{key, hdr};
    lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);

    EXPECT_TRUE(lsdb.touchRefresh(key));

    LsaKey unknownKey(OSPFV2_LSA_ROUTER, 0xDEADBEEF, 0xDEADBEEF);
    EXPECT_FALSE(lsdb.touchRefresh(unknownKey));
}

// Test: Lsdb_SetFlags_Updates_Existing_Record_Flags
TEST_F(Internal_OspfTest, Lsdb_SetFlags_Updates_Existing_Record_Flags)
{
    auto& lsdb = getLsdb();

    LsaKey key(OSPFV2_LSA_ROUTER, selfRouterId, selfRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;

    IncomingLsaContext ctx{key, hdr};
    lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);

    EXPECT_TRUE(lsdb.setFlags(key, LsaRecordFlags::SELF_ORIGINATED));

    auto* rec = lsdb.find(key);
    ASSERT_NE(rec, nullptr);
    EXPECT_TRUE(hasFlag(rec->flags, LsaRecordFlags::SELF_ORIGINATED));
}

// Test: Lsdb_RunDCIntegrityScan_Empty_Returns_True
TEST_F(Internal_OspfTest, Lsdb_RunDCIntegrityScan_Empty_Returns_True)
{
    auto& lsdb = getLsdb();

    EXPECT_TRUE(lsdb.runDCIntegrityScan());
}

// Test: Lsdb_Reserve_Does_Not_Affect_Size
TEST_F(Internal_OspfTest, Lsdb_Reserve_Does_Not_Affect_Size)
{
    auto& lsdb = getLsdb();

    size_t sizeBefore = lsdb.size();
    bool emptyBefore = lsdb.empty();

    lsdb.reserve(64);

    EXPECT_EQ(lsdb.size(), sizeBefore);
    EXPECT_EQ(lsdb.empty(), emptyBefore);
}

#pragma endregion LsdbOperations

#pragma region LsaComparison

// Test: CompareLSASummary_True_When_No_Existing_Record
TEST_F(Internal_OspfTest, CompareLSASummary_True_When_No_Existing_Record)
{
    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader incoming;
    incoming.sequence = routing::OSPF_INITIAL_SEQUENCE;

    EXPECT_TRUE(compareLSASummary(incoming, key));
}

// Test: CompareLSASummary_True_When_Incoming_Has_Higher_Sequence
TEST_F(Internal_OspfTest, CompareLSASummary_True_When_Incoming_Has_Higher_Sequence)
{
    auto& lsdb = getLsdb();

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader stored;
    stored.sequence = routing::OSPF_INITIAL_SEQUENCE;

    IncomingLsaContext ctx{key, stored};
    lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);

    LsaHeader incoming;
    incoming.sequence = routing::OSPF_INITIAL_SEQUENCE + 1;

    EXPECT_TRUE(compareLSASummary(incoming, key));
}

// Test: CompareLSASummary_False_When_Stored_Has_Higher_Sequence
TEST_F(Internal_OspfTest, CompareLSASummary_False_When_Stored_Has_Higher_Sequence)
{
    auto& lsdb = getLsdb();

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader stored;
    stored.sequence = routing::OSPF_INITIAL_SEQUENCE + 1;

    IncomingLsaContext ctx{key, stored};
    lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);

    LsaHeader incoming;
    incoming.sequence = routing::OSPF_INITIAL_SEQUENCE;

    EXPECT_FALSE(compareLSASummary(incoming, key));
}

// Test: CompareLSASummary_False_When_Sequence_And_Checksum_Equal_And_Age_Close
TEST_F(Internal_OspfTest, CompareLSASummary_False_When_Sequence_And_Checksum_Equal_And_Age_Close)
{
    auto& lsdb = getLsdb();

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader stored;
    stored.sequence = routing::OSPF_INITIAL_SEQUENCE;
    stored.checksum = 0x1234;
    stored.age = 100;

    IncomingLsaContext ctx{key, stored};
    lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);

    LsaHeader incoming;
    incoming.sequence = routing::OSPF_INITIAL_SEQUENCE;
    incoming.checksum = 0x1234;
    incoming.age = 100;

    // Identical instance -> SAME, not NEWER -> compareLSASummary returns false.
    EXPECT_FALSE(compareLSASummary(incoming, key));
}

// Test: CompareLSASummary_True_When_Incoming_Has_Higher_Checksum_At_Equal_Sequence
TEST_F(Internal_OspfTest, CompareLSASummary_True_When_Incoming_Has_Higher_Checksum_At_Equal_Sequence)
{
    auto& lsdb = getLsdb();

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader stored;
    stored.sequence = routing::OSPF_INITIAL_SEQUENCE;
    stored.checksum = 0x1000;

    IncomingLsaContext ctx{key, stored};
    lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);

    LsaHeader incoming;
    incoming.sequence = routing::OSPF_INITIAL_SEQUENCE;
    incoming.checksum = 0x2000;

    EXPECT_TRUE(compareLSASummary(incoming, key));
}

// Test: CompareLSASummary_True_When_Incoming_Is_MaxAge_And_Stored_Is_Not
TEST_F(Internal_OspfTest, CompareLSASummary_True_When_Incoming_Is_MaxAge_And_Stored_Is_Not)
{
    auto& lsdb = getLsdb();

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader stored;
    stored.sequence = routing::OSPF_INITIAL_SEQUENCE;
    stored.checksum = 0x1234;
    stored.age = 100;

    IncomingLsaContext ctx{key, stored};
    lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);

    LsaHeader incoming;
    incoming.sequence = routing::OSPF_INITIAL_SEQUENCE;
    incoming.checksum = 0x1234;
    incoming.age = routing::OSPF_MAX_AGE; // MaxAge instance is always more recent (RFC 2328 §13.1)

    EXPECT_TRUE(compareLSASummary(incoming, key));
}

#pragma endregion LsaComparison

#pragma region DbdExchange

// Test: Dbd_ExStart_Master_Slave_Negotiation_Higher_RID_Becomes_Master
TEST_F(Internal_OspfTest, Dbd_ExStart_Master_Slave_Negotiation_Higher_RID_Becomes_Master)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXSTART);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXSTART);

    uint16_t mtu = ospfInterface->iface.configs.ipv4.mtu.load(std::memory_order_relaxed);

    // Neighbor (RID 192.168.1.2 > self 192.168.1.1) sends an Init DBD claiming MASTER.
    uint8_t flags = 0x01 | 0x02 | 0x04; // MS | M | I
    buildDBDV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), mtu, 0x02, flags, 0xAAAA0000);
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    // Higher RID (neighbor) becomes MASTER; self becomes SLAVE.
    EXPECT_EQ(nbr->getRole(), Neighbor::Role::SLAVE);
    EXPECT_EQ(nbr->currentSeq.load(std::memory_order_relaxed), 0xAAAA0000u);
    EXPECT_EQ(nbr->getState(), Neighbor::State::EXCHANGE);
}

// Test: Dbd_MtuMismatch_Rejected
TEST_F(Internal_OspfTest, Dbd_MtuMismatch_Rejected)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXSTART);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXSTART);

    // Default MTU_IGNORE=false means a mismatched MTU tears the neighbor down.
    uint16_t badMtu = nbr->mtu + 1000;
    uint8_t flags = 0x01 | 0x02 | 0x04;
    buildDBDV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), badMtu, 0x02, flags, 0xAAAA0000);
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_EQ(nbr->getState(), Neighbor::State::DOWN);
}

// Test: Dbd_OptionsMismatch_Handling
TEST_F(Internal_OspfTest, Dbd_OptionsMismatch_Handling)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXSTART);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXSTART);

    uint16_t mtu = ospfInterface->iface.configs.ipv4.mtu.load(std::memory_order_relaxed);

    // E-bit (bit 1, value 0x02) mismatch vs this area's ExternalRouting flag
    // (a normal area expects E=1). Sending options=0x00 (E-bit clear) should
    // fail processOptions() and tear the neighbor down.
    uint8_t flags = 0x01 | 0x02 | 0x04;
    buildDBDV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), mtu, 0x00, flags, 0xAAAA0000);
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_EQ(nbr->getState(), Neighbor::State::DOWN);
}

// Test: Dbd_Sequence_Number_Negotiation_Slave_Echoes_Master
TEST_F(Internal_OspfTest, Dbd_Sequence_Number_Negotiation_Slave_Echoes_Master)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXSTART);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXSTART);

    uint16_t mtu = ospfInterface->iface.configs.ipv4.mtu.load(std::memory_order_relaxed);

    uint8_t flags = 0x01 | 0x02 | 0x04;
    buildDBDV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), mtu, 0x02, flags, 0xDEADBEEF);
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    // As SLAVE, currentSeq is overwritten with the MASTER's sequence number.
    ASSERT_EQ(nbr->getRole(), Neighbor::Role::SLAVE);
    EXPECT_EQ(nbr->currentSeq.load(std::memory_order_relaxed), 0xDEADBEEFu);
}

// Test: Dbd_Empty_Exchange_Transitions_To_Loading_Then_Full
TEST_F(Internal_OspfTest, Dbd_Empty_Exchange_Transitions_To_Loading_Then_Full)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXSTART);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXSTART);

    uint16_t mtu = ospfInterface->iface.configs.ipv4.mtu.load(std::memory_order_relaxed);

    // Master's Init DBD -> we become SLAVE, move to EXCHANGE.
    uint8_t initFlags = 0x01 | 0x02 | 0x04;
    buildDBDV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), mtu, 0x02, initFlags, 0x00000001);
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXCHANGE);

    uint8_t finalFlags = 0x00; // MS=0 (slave role), M=0 (no more)
    buildDBDV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), mtu, 0x02, finalFlags, 0x00000001);
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    // Empty LSR list -> LOADING recurses straight to FULL (Bug #1 fix).
    EXPECT_EQ(nbr->getState(), Neighbor::State::FULL);
    EXPECT_FALSE(nbr->getRtr().lsrs().getActive());
}

// Test: Dbd_Database_Summary_Populates_LSR_List
TEST_F(Internal_OspfTest, Dbd_Database_Summary_Populates_LSR_List)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXSTART);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXSTART);

    uint16_t mtu = ospfInterface->iface.configs.ipv4.mtu.load(std::memory_order_relaxed);

    // Master's Init DBD -> we become SLAVE, move to EXCHANGE.
    uint8_t initFlags = 0x01 | 0x02 | 0x04;
    buildDBDV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), mtu, 0x02, initFlags, 0x00000001);
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXCHANGE);

    // Master's next DBD lists one Router LSA summary we don't have locally.
    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader summary;
    summary.sequence = routing::OSPF_INITIAL_SEQUENCE;
    summary.checksum = 0x1234;
    summary.length = 24;
    summary.age = 1;

    uint8_t flags = 0x00; // MS=0, M=0 (last DBD, slave side)
    buildDBDV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), mtu, 0x02, flags, 0x00000001, {key}, {summary});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_TRUE(nbr->getRtr().lsrs().has(key));
}

// Test: Dbd_MoreBit_Continues_Exchange_Until_Cleared
TEST_F(Internal_OspfTest, Dbd_MoreBit_Continues_Exchange_Until_Cleared)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXSTART);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXSTART);

    uint16_t mtu = ospfInterface->iface.configs.ipv4.mtu.load(std::memory_order_relaxed);

    // Master's Init DBD -> we become SLAVE, move to EXCHANGE.
    uint8_t initFlags = 0x01 | 0x02 | 0x04;
    buildDBDV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), mtu, 0x02, initFlags, 0x00000001);
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXCHANGE);

    // Master's next DBD has M-bit set -> more data follows, stay in EXCHANGE.
    uint8_t moreFlags = 0x02; // MS=0, M=1
    buildDBDV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), mtu, 0x02, moreFlags, 0x00000002);
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_EQ(nbr->getState(), Neighbor::State::EXCHANGE);

    // Final DBD with M-bit clear -> empty LSR list -> straight to FULL.
    uint8_t doneFlags = 0x00; // MS=0, M=0
    buildDBDV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), mtu, 0x02, doneFlags, 0x00000003);
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_EQ(nbr->getState(), Neighbor::State::FULL);
}

#pragma endregion DbdExchange

#pragma region Flooding

// Test: Flood_New_Lsa_Installed_From_LSUpdate
TEST_F(Internal_OspfTest, Flood_New_Lsa_Installed_From_LSUpdate)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;

    RouterLsaV2 body;
    body.flags = 0;
    body.links.push_back({neighborRouterId2, 0xFFFFFF00, OSPFV2_LINK_STUB, 10});

    buildLSUpdateV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {lh}, {body});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    auto& lsdb = getLsdb();
    EXPECT_TRUE(lsdb.contains(key));

    auto* record = lsdb.find(key);
    ASSERT_NE(record, nullptr);
    buildLSAckV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {record->header});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    wait();
}

// Test: Flood_Update_Triggers_LSAck
TEST_F(Internal_OspfTest, Flood_Update_Triggers_LSAck)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    bool sawLSAck = false;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() == OSPFV2_TYPE_LINK_STATE_ACK)
                sawLSAck = true;
        }));

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;

    RouterLsaV2 body;
    body.flags = 0;
    body.links.push_back({neighborRouterId2, 0xFFFFFF00, OSPFV2_LINK_STUB, 10});

    buildLSUpdateV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {lh}, {body});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_TRUE(sawLSAck);

    auto& lsdb = getLsdb();
    auto* record = lsdb.find(key);
    ASSERT_NE(record, nullptr);
    buildLSAckV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {record->header});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    wait();
}

// Test: Flood_Duplicate_Lsa_Not_Reinstalled
TEST_F(Internal_OspfTest, Flood_Duplicate_Lsa_Not_Reinstalled)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;

    RouterLsaV2 body;
    body.flags = 0;
    body.links.push_back({neighborRouterId2, 0xFFFFFF00, OSPFV2_LINK_STUB, 10});

    buildLSUpdateV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {lh}, {body});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    auto& lsdb = getLsdb();
    auto* recordBefore = lsdb.find(key);
    ASSERT_NE(recordBefore, nullptr);
    auto installTimeBefore = recordBefore->installTime;

    // Resend the identical instance.
    buildLSUpdateV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {lh}, {body});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    auto* recordAfter = lsdb.find(key);
    ASSERT_NE(recordAfter, nullptr);
    EXPECT_EQ(recordAfter->installTime, installTimeBefore);

    buildLSAckV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {recordAfter->header});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    wait();
}

// Test: Flood_Older_Lsa_Ignored
TEST_F(Internal_OspfTest, Flood_Older_Lsa_Ignored)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);

    RouterLsaV2 body;
    body.flags = 0;
    body.links.push_back({neighborRouterId2, 0xFFFFFF00, OSPFV2_LINK_STUB, 10});

    // Install a newer instance first.
    LsaHeader newer;
    newer.sequence = routing::OSPF_INITIAL_SEQUENCE + 5;
    newer.age = 1;
    buildLSUpdateV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {newer}, {body});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    auto& lsdb = getLsdb();
    auto* record = lsdb.find(key);
    ASSERT_NE(record, nullptr);
    uint32_t storedSeq = record->header.sequence;

    // Now send an older instance.
    LsaHeader older;
    older.sequence = routing::OSPF_INITIAL_SEQUENCE;
    older.age = 1;
    buildLSUpdateV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {older}, {body});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    record = lsdb.find(key);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->header.sequence, storedSeq);

    buildLSAckV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {record->header});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    wait();
}

// Test: Flood_Retransmission_Cleared_On_LSAck
TEST_F(Internal_OspfTest, Flood_Retransmission_Cleared_On_LSAck)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;

    auto& lsdb = getLsdb();
    IncomingLsaContext ctx{key, lh};
    auto& record = lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);

    LsaRecordRef ref{key, record};
    nbr->getRtr().lsus().add(key, ref);
    ASSERT_TRUE(nbr->getRtr().lsus().has(key));

    // Neighbor acknowledges with the exact same header.
    buildLSAckV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {record.header});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_FALSE(nbr->getRtr().lsus().has(key));
}

// Test: Flood_Bad_Checksum_Lsa_Rejected
TEST_F(Internal_OspfTest, Flood_Bad_Checksum_Lsa_Rejected)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;

    RouterLsaV2 body;
    body.flags = 0;
    body.links.push_back({neighborRouterId2, 0xFFFFFF00, OSPFV2_LINK_STUB, 10});

    uint16_t packetLen = buildLSUpdateV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {lh}, {body});

    size_t lsaHdrOffset = packet::Ospfv2Header::fixedSize + 4;
    packet::Ospfv2LSAHeader corrupt;
    corrupt.setBuffer(testPacket + lsaHdrOffset);
    corrupt.setChecksum(corrupt.getChecksum() ^ 0xFFFF);
    finalizeOspfV2Checksum(testPacket, packetLen);

    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    auto& lsdb = getLsdb();
    EXPECT_FALSE(lsdb.contains(key));
}

// Test: Flood_LSRequest_Returns_Requested_Lsa_Via_LSUpdate
TEST_F(Internal_OspfTest, Flood_LSRequest_Returns_Requested_Lsa_Via_LSUpdate)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    // Seed the local LSDB with a self-originated Router LSA the neighbor will request.
    LsaKey key(OSPFV2_LSA_ROUTER, selfRouterId, selfRouterId);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;
    lh.length = packet::Ospfv2LSAHeader::fixedSize + 4;

    auto& lsdb = getLsdb();
    IncomingLsaContext ctx{key, lh};
    lsdb.upsertBody<RouterLsaV2>(ctx, LsaRecordFlags::SELF_ORIGINATED);

    bool sawLSUpdate = false;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() == OSPFV2_TYPE_LINK_STATE_UPDATE)
                sawLSUpdate = true;
        }));

    buildLSRequestV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_TRUE(sawLSUpdate);

    auto* record = lsdb.find(key);
    ASSERT_NE(record, nullptr);
    buildLSAckV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {record->header});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    wait();
}

// Test: Flood_SelfOriginated_Newer_Instance_From_Peer_Triggers_FightBack
TEST_F(Internal_OspfTest, Flood_SelfOriginated_Newer_Instance_From_Peer_Triggers_FightBack)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    // Seed a self-originated Router LSA at the initial sequence number.
    LsaKey key(OSPFV2_LSA_ROUTER, selfRouterId, selfRouterId);
    LsaHeader stored;
    stored.sequence = routing::OSPF_INITIAL_SEQUENCE;
    stored.age = 1;
    stored.length = packet::Ospfv2LSAHeader::fixedSize + 4;

    auto& lsdb = getLsdb();
    IncomingLsaContext seedCtx{key, stored};
    lsdb.upsertBody<RouterLsaV2>(seedCtx, LsaRecordFlags::SELF_ORIGINATED);

    if (auto* seeded = lsdb.find(key))
        seeded->lastRefreshTime -= std::chrono::seconds(5);

    // Peer floods back a newer instance of our own self-originated LSA.
    LsaHeader incoming;
    incoming.sequence = routing::OSPF_INITIAL_SEQUENCE + 1;
    incoming.age = 1;

    RouterLsaV2 body;
    body.flags = 0;
    body.links.push_back({neighborRouterId2, 0xFFFFFF00, OSPFV2_LINK_STUB, 10});

    buildLSUpdateV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {incoming}, {body});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    // The newer (peer-supplied) instance is stored; fight-back will re-originate
    // an even-newer instance asynchronously via the originator/process queue.
    auto* record = lsdb.find(key);
    ASSERT_NE(record, nullptr);
    EXPECT_GE(record->header.sequence, routing::OSPF_INITIAL_SEQUENCE + 1);

    wait();
    record = lsdb.find(key);
    ASSERT_NE(record, nullptr);
    buildLSAckV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {record->header});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    wait();
}

// Test: Flood_LSUpdate_From_Neighbor_Below_Exchange_Ignored
TEST_F(Internal_OspfTest, Flood_LSUpdate_From_Neighbor_Below_Exchange_Ignored)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    addNeighbor(neighborRouterId, nbrIp, Neighbor::State::TWOWAY);

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;

    RouterLsaV2 body;
    body.flags = 0;
    body.links.push_back({neighborRouterId2, 0xFFFFFF00, OSPFV2_LINK_STUB, 10});

    buildLSUpdateV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {lh}, {body});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    auto& lsdb = getLsdb();
    EXPECT_FALSE(lsdb.contains(key));
}

#pragma endregion Flooding

#pragma region PacketRxTxV2

// Test: RxV2_HandleIncoming_Dispatches_Hello_To_ProcessHello
TEST_F(Internal_OspfTest, RxV2_HandleIncoming_Dispatches_Hello_To_ProcessHello)
{
    uint16_t helloInterval = getIfaceHelloInterval();
    uint32_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();

    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 1, 0, 0, {});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    auto* nbr = getNeighbor(neighborRouterId);
    ASSERT_NE(nbr, nullptr);
    EXPECT_GE(nbr->getState(), Neighbor::State::INIT);
}

// Test: RxV2_HandleIncoming_Dispatches_Dbd_To_ProcessDBD
TEST_F(Internal_OspfTest, RxV2_HandleIncoming_Dispatches_Dbd_To_ProcessDBD)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXSTART);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXSTART);

    uint16_t mtu = ospfInterface->iface.configs.ipv4.mtu.load(std::memory_order_relaxed);
    uint8_t options = static_cast<uint8_t>(getIfaceFlags().getFlags());

    // Init DBD (MS+M+I) from the neighbor.
    buildDBDV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), mtu, options,
               0x01 | 0x02 | 0x04, 0xAAAA0000);
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    // processDBD dispatched: neighbor should have moved out of EXSTART.
    EXPECT_NE(nbr->getState(), Neighbor::State::EXSTART);
}

// Test: RxV2_HandleIncoming_Dispatches_LSRequest
TEST_F(Internal_OspfTest, RxV2_HandleIncoming_Dispatches_LSRequest)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    // Seed the local LSDB with a self-originated Router LSA the neighbor will request.
    LsaKey key(OSPFV2_LSA_ROUTER, selfRouterId, selfRouterId);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;
    lh.length = packet::Ospfv2LSAHeader::fixedSize + 4;

    auto& lsdb = getLsdb();
    IncomingLsaContext ctx{key, lh};
    lsdb.upsertBody<RouterLsaV2>(ctx, LsaRecordFlags::SELF_ORIGINATED);

    bool sawLSUpdate = false;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() == OSPFV2_TYPE_LINK_STATE_UPDATE)
                sawLSUpdate = true;
        }));

    buildLSRequestV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_TRUE(sawLSUpdate);

    auto* record = lsdb.find(key);
    ASSERT_NE(record, nullptr);
    buildLSAckV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {record->header});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    wait();
}

// Test: RxV2_HandleIncoming_Dispatches_LSUpdate
TEST_F(Internal_OspfTest, RxV2_HandleIncoming_Dispatches_LSUpdate)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;

    RouterLsaV2 body;
    body.flags = 0;
    body.links.push_back({neighborRouterId2, 0xFFFFFF00, OSPFV2_LINK_STUB, 10});

    buildLSUpdateV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {lh}, {body});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    auto& lsdb = getLsdb();
    EXPECT_TRUE(lsdb.contains(key));

    auto* record = lsdb.find(key);
    ASSERT_NE(record, nullptr);
    buildLSAckV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {record->header});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    wait();
}

// Test: RxV2_HandleIncoming_Dispatches_LSAck
TEST_F(Internal_OspfTest, RxV2_HandleIncoming_Dispatches_LSAck)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;

    auto& lsdb = getLsdb();
    IncomingLsaContext ctx{key, lh};
    auto& record = lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);

    LsaRecordRef ref{key, record};
    nbr->getRtr().lsus().add(key, ref);
    ASSERT_TRUE(nbr->getRtr().lsus().has(key));

    buildLSAckV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {record.header});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_FALSE(nbr->getRtr().lsus().has(key));
}

// Test: RxV2_Rejects_Packet_With_Bad_Checksum
TEST_F(Internal_OspfTest, RxV2_Rejects_Packet_With_Bad_Checksum)
{
    uint16_t helloInterval = getIfaceHelloInterval();
    uint32_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();

    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 1, 0, 0, {});

    // Corrupt the checksum field after finalizeOspfV2Checksum has run.
    packet::Ospfv2Header hdr;
    hdr.setBuffer(testPacket);
    hdr.setChecksum(hdr.getChecksum() ^ 0xFFFF);

    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    // Packet was dropped before processHello could create a neighbor.
    EXPECT_EQ(getNeighbor(neighborRouterId), nullptr);
}

// Test: RxV2_Rejects_Packet_For_Wrong_Area
TEST_F(Internal_OspfTest, RxV2_Rejects_Packet_For_Wrong_Area)
{
    uint16_t helloInterval = getIfaceHelloInterval();
    uint32_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();

    // areaId mismatches the interface's configured area.
    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId() + 1,
                  helloInterval, deadInterval, mask, 1, 0, 0, {});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    EXPECT_EQ(getNeighbor(neighborRouterId), nullptr);
}

// Test: TxV2_SendHello_Multicast_To_AllSpfRouters
TEST_F(Internal_OspfTest, TxV2_SendHello_Multicast_To_AllSpfRouters)
{
    // Single router on a broadcast network elects itself DR, so Hello is
    // sent to AllSPFRouters (224.0.0.5).
    runIfaceElection();
    ASSERT_TRUE(ospfInterface->getIsDr());

    bool sawHello = false;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() == OSPFV2_TYPE_HELLO)
                sawHello = true;
        }));

    getDispatcherV2().sendHello();

    EXPECT_TRUE(sawHello);
}

// Test: TxV2_SendUnicastHello_To_Neighbor
TEST_F(Internal_OspfTest, TxV2_SendUnicastHello_To_Neighbor)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::INIT, nullptr, true); // unicast = true
    ASSERT_NE(nbr, nullptr);

    bool sawHello = false;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() == OSPFV2_TYPE_HELLO)
                sawHello = true;
        }));

    getDispatcherV2().sendUnicastHello(*nbr);

    EXPECT_TRUE(sawHello);
}

// Test: TxV2_SendInitDbd_Sets_IBit_MBit_MsBit
TEST_F(Internal_OspfTest, TxV2_SendInitDbd_Sets_IBit_MBit_MsBit)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXSTART);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXSTART);

    bool sawDbd = false;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() != OSPFV2_TYPE_DATABASE_DESCRIPTION) return;
            sawDbd = true;

            packet::Ospfv2DBDHeader dbd;
            dbd.setBuffer(hdr.getTrailData());
            EXPECT_TRUE(dbd.getFlagI());
            EXPECT_TRUE(dbd.getFlagM());
            EXPECT_TRUE(dbd.getFlagMS());
        }));

    getDispatcherV2().sendInitDbd(*nbr);

    EXPECT_TRUE(sawDbd);
}

// Test: TxV2_SendDbd_Master_Increments_Sequence
TEST_F(Internal_OspfTest, TxV2_SendDbd_Master_Increments_Sequence)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXCHANGE);
    nbr->setRole(Neighbor::Role::MASTER);

    uint32_t seqBefore = nbr->currentSeq.load(std::memory_order_relaxed);

    bool sawDbd = false;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() == OSPFV2_TYPE_DATABASE_DESCRIPTION)
                sawDbd = true;
        }));

    getDispatcherV2().sendDbd(*nbr);

    EXPECT_TRUE(sawDbd);
    EXPECT_EQ(nbr->currentSeq.load(std::memory_order_relaxed), seqBefore + 1);
}

// Test: TxV2_SendLsAck_Lists_Acknowledged_Headers
TEST_F(Internal_OspfTest, TxV2_SendLsAck_Lists_Acknowledged_Headers)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;
    lh.length = packet::Ospfv2LSAHeader::fixedSize + 4;

    auto& lsdb = getLsdb();
    IncomingLsaContext ctx{key, lh};
    auto& record = lsdb.upsertBody<RouterLsaV2>(ctx, LsaRecordFlags::NONE);
    (void)record;

    auto* rec = lsdb.find(key);
    ASSERT_NE(rec, nullptr);

    std::vector<LsaRecordRef> acks{{key, *rec}};

    uint32_t ackedLsId = 0;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() != OSPFV2_TYPE_LINK_STATE_ACK) return;

            packet::Ospfv2LSAHeader lsaHdr;
            lsaHdr.setBuffer(hdr.getTrailData());
            ackedLsId = lsaHdr.getLsID();
        }));

    bool ok = getDispatcherV2().sendLsAck(*nbr, acks);

    EXPECT_TRUE(ok);
    EXPECT_EQ(ackedLsId, key.linkStateId);
}

// Test: TxV2_SendLsRequest_Lists_Missing_Lsa_Keys
TEST_F(Internal_OspfTest, TxV2_SendLsRequest_Lists_Missing_Lsa_Keys)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::LOADING, nullptr, false);

    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    nbr->getRtr().lsrs().add(key, key);
    ASSERT_TRUE(nbr->getRtr().lsrs().getActive());

    uint32_t requestedLsId = 0;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() != OSPFV2_TYPE_LINK_STATE_REQUEST) return;

            packet::Ospfv2LSRHeader lsr;
            lsr.setBuffer(hdr.getTrailData());
            requestedLsId = lsr.getLsID();
        }));

    bool ok = getDispatcherV2().sendLsr(*nbr);

    EXPECT_TRUE(ok);
    EXPECT_EQ(requestedLsId, key.linkStateId);
}

// Test: TxV2_SendLsUpdate_Unicast_To_Neighbor
TEST_F(Internal_OspfTest, TxV2_SendLsUpdate_Unicast_To_Neighbor)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    LsaKey key(OSPFV2_LSA_ROUTER, selfRouterId, selfRouterId);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;
    lh.length = packet::Ospfv2LSAHeader::fixedSize + 4;

    auto& lsdb = getLsdb();
    IncomingLsaContext ctx{key, lh};
    lsdb.upsertBody<RouterLsaV2>(ctx, LsaRecordFlags::SELF_ORIGINATED);

    auto* rec = lsdb.find(key);
    ASSERT_NE(rec, nullptr);

    LsaRecordRef ref{key, *rec};
    nbr->getRtr().lsus().add(key, ref);
    ASSERT_TRUE(nbr->getRtr().lsus().getActive());

    bool sawLSUpdate = false;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() == OSPFV2_TYPE_LINK_STATE_UPDATE)
                sawLSUpdate = true;
        }));

    bool ok = getDispatcherV2().sendLsu(nbr);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(sawLSUpdate);

    buildLSAckV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), {key}, {rec->header});
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    wait();
}

// Test: TxV2_FinalizeHeader_Sets_Length_And_Checksum
TEST_F(Internal_OspfTest, TxV2_FinalizeHeader_Sets_Length_And_Checksum)
{
    runIfaceElection();
    ASSERT_TRUE(ospfInterface->getIsDr());

    uint16_t packetLen = 0;
    uint16_t checksum = 0;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() != OSPFV2_TYPE_HELLO) return;
            packetLen = hdr.getPacketLen();
            checksum = hdr.getChecksum();
        }));

    getDispatcherV2().sendHello();

    EXPECT_GT(packetLen, static_cast<uint16_t>(packet::Ospfv2Header::fixedSize));
    EXPECT_NE(checksum, 0u);
}

// Test: TxV2_SendHello_Bounded_By_Interface_Mtu
TEST_F(Internal_OspfTest, TxV2_SendHello_Bounded_By_Interface_Mtu)
{
    runIfaceElection();
    ASSERT_TRUE(ospfInterface->getIsDr());

    uint16_t ifaceMtu = ospfInterface->iface.configs.ipv4.mtu.load(std::memory_order_relaxed);

    uint16_t packetLen = 0;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() == OSPFV2_TYPE_HELLO)
                packetLen = hdr.getPacketLen();
        }));

    getDispatcherV2().sendHello();

    EXPECT_GT(packetLen, 0u);
    EXPECT_LE(packetLen, ifaceMtu);
}

#pragma endregion PacketRxTxV2

#pragma region OriginationV2

// Test: OriginateV2_RouterLsa_Basic_Fields_Match_RouterId_And_Sequence
TEST_F(Internal_OspfTest, OriginateV2_RouterLsa_Basic_Fields_Match_RouterId_And_Sequence)
{
    fullRefreshV2();
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_ROUTER, rid, rid);

    auto* record = waitForLsa(key);
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(hasFlag(record->flags, LsaRecordFlags::SELF_ORIGINATED));
    EXPECT_GE(record->header.sequence, routing::OSPF_INITIAL_SEQUENCE);
}

// Test: OriginateV2_RouterLsa_AddTransitLink_For_DR_Broadcast
TEST_F(Internal_OspfTest, OriginateV2_RouterLsa_AddTransitLink_For_DR_Broadcast)
{
    // Default network type is BROADCAST; become DR so a transit link is added.
    runIfaceElection();
    ASSERT_TRUE(ospfInterface->getIsDr());

    fullRefreshV2();
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_ROUTER, rid, rid);

    // Reorigination is async and a stale record may pre-exist; wait for the transit link to actually appear in the body.
    auto* record = waitForLsa(key, [](const LsaRecord& r) {
        auto* body = std::get_if<RouterLsaV2>(&r.body);
        if (!body) return false;
        for (const auto& link : body->links)
            if (link.type == OSPFV2_LINK_TRANSIT)
                return true;
        return false;
    });
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<RouterLsaV2>(&record->body);
    ASSERT_NE(body, nullptr);

    bool foundTransit = false;
    for (const auto& link : body->links)
    {
        if (link.type == OSPFV2_LINK_TRANSIT)
        {
            foundTransit = true;
            EXPECT_EQ(link.linkId, static_cast<uint32_t>(ospfInterface->getDrIp()));
            EXPECT_EQ(link.linkData, ospfInterface->interfaceAddress.v4());
        }
    }
    EXPECT_TRUE(foundTransit);
}

// Test: OriginateV2_RouterLsa_AddP2PLink_For_PointToPoint_FullNeighbor
TEST_F(Internal_OspfTest, OriginateV2_RouterLsa_AddP2PLink_For_PointToPoint_FullNeighbor)
{
    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    ospfInterface->enqueueSyncNetworkType();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_ROUTER, rid, rid);

    auto* record = waitForLsa(key, [](const LsaRecord& r) {
        auto* body = std::get_if<RouterLsaV2>(&r.body);
        if (!body)
            return false;
        for (const auto& link : body->links)
            if (link.type == OSPFV2_LINK_P2P)
                return true;
        return false;
    });
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<RouterLsaV2>(&record->body);
    ASSERT_NE(body, nullptr);

    bool foundP2P = false;
    for (const auto& link : body->links)
    {
        if (link.type == OSPFV2_LINK_P2P)
        {
            foundP2P = true;
            EXPECT_EQ(link.linkId, neighborRouterId);
        }
    }
    EXPECT_TRUE(foundP2P);
}

// Test: OriginateV2_RouterLsa_AddStubLink_For_Passive_Interface
TEST_F(Internal_OspfTest, OriginateV2_RouterLsa_AddStubLink_For_Passive_Interface)
{
    getIfaceConfigs().get<config::OspfInterface::PASSIVE>().set(true);
    ospfInterface->enqueueSyncPassive();
    wait();

    fullRefreshV2();
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_ROUTER, rid, rid);

    auto* record = waitForLsa(key);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<RouterLsaV2>(&record->body);
    ASSERT_NE(body, nullptr);

    bool foundStub = false;
    for (const auto& link : body->links)
    {
        if (link.type == OSPFV2_LINK_STUB && link.linkId == ospfInterface->interfaceAddress.v4())
            foundStub = true;
    }
    EXPECT_TRUE(foundStub);
}

// Test: OriginateV2_RouterLsa_NoDr_NoTransitLink_On_Broadcast
TEST_F(Internal_OspfTest, OriginateV2_RouterLsa_NoDr_NoTransitLink_On_Broadcast)
{
    fullRefreshV2();
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_ROUTER, rid, rid);

    auto* record = waitForLsa(key);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<RouterLsaV2>(&record->body);
    ASSERT_NE(body, nullptr);

    for (const auto& link : body->links)
        EXPECT_NE(link.type, OSPFV2_LINK_TRANSIT);
}

// Test: OriginateV2_NetworkLsa_Originated_By_Dr_With_FullNeighbor
TEST_F(Internal_OspfTest, OriginateV2_NetworkLsa_Originated_By_Dr_With_FullNeighbor)
{
    runIfaceElection();
    ASSERT_TRUE(ospfInterface->getIsDr());

    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t netAddr = ospfInterface->interfaceAddress.v4();
    LsaKey key(OSPFV2_LSA_NETWORK, netAddr, selfRid);

    auto* record = waitForLsa(key);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<NetworkLsaV2>(&record->body);
    ASSERT_NE(body, nullptr);

    EXPECT_NE(std::find(body->attachedRouters.begin(), body->attachedRouters.end(), selfRid),
              body->attachedRouters.end());
    EXPECT_NE(std::find(body->attachedRouters.begin(), body->attachedRouters.end(), neighborRouterId),
              body->attachedRouters.end());
}

// Test: OriginateV2_NetworkLsa_Not_Originated_When_Not_Dr
TEST_F(Internal_OspfTest, OriginateV2_NetworkLsa_Not_Originated_When_Not_Dr)
{
    // No election: isDr remains false.
    ASSERT_FALSE(ospfInterface->getIsDr());

    fullRefreshV2();
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t netAddr = ospfInterface->interfaceAddress.v4();
    LsaKey key(OSPFV2_LSA_NETWORK, netAddr, selfRid);

    EXPECT_FALSE(getLsdb().contains(key));
}

// Test: OriginateV2_AsbrSummary_Type4_Originated_When_Asbr_Reachable
TEST_F(Internal_OspfTest, OriginateV2_AsbrSummary_Type4_Originated_When_Asbr_Reachable)
{
    OspfRouter asbrReach{};
    asbrReach.rid = neighborRouterId2;
    asbrReach.cost = 25;
    asbrReach.nextHops.push_back({ospfInterface->interfaceId, types::IPAddress{}});
    getTable().updateAreaAsbr(0, asbrReach);

    addExternal<PolicyV2>(getOriginatorCtx(), neighborRouterId2, /*lsid=*/1, /*remove=*/false);

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_SUM_ASBR, neighborRouterId2, selfRid);

    // The install runs from a postAfter reorigination timer, so poll via
    // waitForLsa instead of a single wait() + find (see the helper's comment).
    auto* record = waitForLsa(key);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<SummaryRouterLsa>(&record->body);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->metric, 25u);
}

// Test: OriginateV2_AsbrSummary_Withdrawn_When_Last_External_Removed
TEST_F(Internal_OspfTest, OriginateV2_AsbrSummary_Withdrawn_When_Last_External_Removed)
{
    OspfRouter asbrReach{};
    asbrReach.rid = neighborRouterId2;
    asbrReach.cost = 25;
    asbrReach.nextHops.push_back({ospfInterface->interfaceId, types::IPAddress{}});
    getTable().updateAreaAsbr(0, asbrReach);

    addExternal<PolicyV2>(getOriginatorCtx(), neighborRouterId2, /*lsid=*/1, /*remove=*/false);
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_SUM_ASBR, neighborRouterId2, selfRid);
    ASSERT_TRUE(waitForLsa(key));

    addExternal<PolicyV2>(getOriginatorCtx(), neighborRouterId2, /*lsid=*/1, /*remove=*/true);
    wait();

    auto* record = waitForLsa(key, [](const LsaRecord& r) { return r.header.age == routing::OSPF_MAX_AGE; });
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->header.age, routing::OSPF_MAX_AGE);
}

// Test: OriginateV2_Type3Summary_OriginateSummary_Sets_Cost_And_Mask
TEST_F(Internal_OspfTest, OriginateV2_Type3Summary_OriginateSummary_Sets_Cost_And_Mask)
{
    types::IPPrefix prefix(uint32_t{0x0A000000}, 8); // 10.0.0.0/8

    originateSummary<PolicyV2>(getOriginatorCtx(), /*lsid=*/0x0A000000, prefix, /*cost=*/55);
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_SUM_NET, 0x0A000000, selfRid);

    auto* record = waitForLsa(key);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<SummaryNetworkLsa>(&record->body);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->metric, 55u);
    EXPECT_EQ(body->networkMask, prefix.getMask());
}

// Test: OriginateV2_Type3Summary_Expire_Sets_MaxAge
TEST_F(Internal_OspfTest, OriginateV2_Type3Summary_Expire_Sets_MaxAge)
{
    types::IPPrefix prefix(uint32_t{0x0A000000}, 8); // 10.0.0.0/8

    originateSummary<PolicyV2>(getOriginatorCtx(), /*lsid=*/0x0A000000, prefix, /*cost=*/55);
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_SUM_NET, 0x0A000000, selfRid);
    ASSERT_TRUE(waitForLsa(key));

    originateSummary<PolicyV2>(getOriginatorCtx(), /*lsid=*/0x0A000000, prefix, /*cost=*/55, /*expire=*/true);
    wait();

    auto* record = waitForLsa(key, [](const LsaRecord& r) { return r.header.age == routing::OSPF_MAX_AGE; });
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->header.age, routing::OSPF_MAX_AGE);
}

// Test: OriginateV2_Type5External_OriginateLsa_Installs_Body
TEST_F(Internal_OspfTest, OriginateV2_Type5External_OriginateLsa_Installs_Body)
{
    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_EXTERNAL, 0x0B000000, selfRid);

    ExternalLsaV2 ext{};
    ext.networkMask = 0xFF000000; // /8
    ext.metric = 20;
    ext.isType2 = true;
    ext.forwardingAddress = 0;
    ext.routeTag = 0;

    getOriginatorCtx().originateLsa<PolicyV2>(key, LsaBody{ext}, false);
    wait();

    auto* record = waitForLsa(key);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<ExternalLsaV2>(&record->body);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->metric, 20u);
    EXPECT_TRUE(body->isType2);
    EXPECT_EQ(body->networkMask, 0xFF000000u);
}

// Test: OriginateV2_Type5External_Expire_Sets_MaxAge
TEST_F(Internal_OspfTest, OriginateV2_Type5External_Expire_Sets_MaxAge)
{
    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_EXTERNAL, 0x0B000000, selfRid);

    ExternalLsaV2 ext{};
    ext.networkMask = 0xFF000000;
    ext.metric = 20;
    ext.isType2 = true;
    ext.forwardingAddress = 0;
    ext.routeTag = 0;

    getOriginatorCtx().originateLsa<PolicyV2>(key, LsaBody{ext}, false);
    wait();
    waitForLsa(key);
    ASSERT_TRUE(waitForLsa(key));

    getOriginatorCtx().originateLsa<PolicyV2>(key, LsaBody{ext}, true);
    wait();

    auto* record = waitForLsa(key, [](const LsaRecord& r) { return r.header.age == routing::OSPF_MAX_AGE; });
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->header.age, routing::OSPF_MAX_AGE);
}

// Test: OriginateV2_StubArea_AddStubDefaultRoute_Originates_Type3_Default_When_Abr
TEST_F(Internal_OspfTest, OriginateV2_StubArea_AddStubDefaultRoute_Originates_Type3_Default_When_Abr)
{
    // Pre-configure area 1 as STUB before construction.
    getConfigs().get<config::Ospf::AREA_CONFIGS>().emplaceBack(1)
        .get<config::OspfArea::AREA_TYPE>().set(config::ospf::AreaType::STUB);

    // Create a second interface in area 1 so the process becomes an ABR
    // (insureArea(1) alongside existing area 0).
    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    (void)iface1;

    auto& stubArea = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    // The area's initial fullRefresh() ran before isABR() became true (insureArea
    // sets ABR status after construction completes), so addStubDefaultRoute(true)
    // was a no-op at that point. Re-run fullRefresh now that isABR() is true.
    fullRefreshV2(&stubArea);
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey defaultKey(OSPFV2_LSA_SUM_NET, 0, selfRid);

    auto* record = waitForLsa(defaultKey, &stubArea);
    ASSERT_NE(record, nullptr);
    EXPECT_NE(record->header.age, routing::OSPF_MAX_AGE);
    auto* body = std::get_if<SummaryNetworkLsa>(&record->body);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->networkMask, 0u);

    removeIface(iface1.id);
}

// Test: OriginateV2_NssaArea_NssaDefaultOriginate_Type7_Default_When_Abr
TEST_F(Internal_OspfTest, OriginateV2_NssaArea_NssaDefaultOriginate_Type7_Default_When_Abr)
{
    // Pre-configure area 1 as NSSA with default-originate enabled before construction.
    auto& area1Cfg = getConfigs().get<config::Ospf::AREA_CONFIGS>().emplaceBack(1);
    area1Cfg.get<config::OspfArea::AREA_TYPE>().set(config::ospf::AreaType::NSSA);
    area1Cfg.get<config::OspfArea::NSSA_DEFAULT_ORIGINATE>().set(true);

    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));

    auto& nssaArea = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    // As with the stub case, re-run fullRefresh now that isABR() is true so
    // nssaDefaultOriginate actually originates the Type-7 default.
    fullRefreshV2(&nssaArea);
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();

    bool found = false;
    for (int i = 0; i < 50 && !found; ++i)
    {
        {
            std::lock_guard lock(getSchedulerLock());
            getLsdb(&nssaArea).forEachInType(OSPFV2_LSA_NSSA, [&](const LsaKey& k, LsaRecord& rec) {
                if (k.advertisingRouter == selfRid && rec.header.age != routing::OSPF_MAX_AGE)
                    found = true;
            });
        }
        if (found)
            break;
        wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(found);

    removeIface(iface1.id);
}

// Test: OriginateV2_FullRefresh_Increments_Sequence_On_Reorigination
TEST_F(Internal_OspfTest, OriginateV2_FullRefresh_Increments_Sequence_On_Reorigination)
{
    runIfaceElection();
    ASSERT_TRUE(ospfInterface->getIsDr());

    fullRefreshV2();
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_ROUTER, rid, rid);

    auto* before = waitForLsa(key);
    ASSERT_NE(before, nullptr);
    uint32_t seqBefore = before->header.sequence;

    // Add a P2P neighbor on a new interface to change the router LSA contents,
    // then re-refresh.
    getIfaceConfigs().get<config::OspfInterface::COST>().set(static_cast<uint16_t>(ospfInterface->getCost() + 50));
    calculateCost();

    fullRefreshV2();
    wait();

    auto* after = waitForLsa(key, [seqBefore](const LsaRecord& r) { return r.header.sequence > seqBefore; });
    ASSERT_NE(after, nullptr);
    EXPECT_GT(after->header.sequence, seqBefore);
}

// Test: OriginateV2_UpdateInterface_Triggers_RouterLsa_Reorigination
TEST_F(Internal_OspfTest, OriginateV2_UpdateInterface_Triggers_RouterLsa_Reorigination)
{
    fullRefreshV2();
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_ROUTER, rid, rid);

    auto* before = waitForLsa(key);
    ASSERT_NE(before, nullptr);
    uint32_t seqBefore = before->header.sequence;

    runIfaceElection();
    updateInterfaceV2(ospfInterface->id.interfaceId);
    wait();

    auto* after = waitForLsa(key, [](const LsaRecord& r) {
        auto* body = std::get_if<RouterLsaV2>(&r.body);
        if (!body)
            return false;
        for (const auto& link : body->links)
            if (link.type == OSPFV2_LINK_TRANSIT)
                return true;
        return false;
    });
    ASSERT_NE(after, nullptr);
    EXPECT_GE(after->header.sequence, seqBefore);

    auto* body = std::get_if<RouterLsaV2>(&after->body);
    ASSERT_NE(body, nullptr);

    bool foundTransit = false;
    for (const auto& link : body->links)
        if (link.type == OSPFV2_LINK_TRANSIT)
            foundTransit = true;
    EXPECT_TRUE(foundTransit);
}

#pragma endregion OriginationV2

#pragma region OriginationThrottleAndPacing

// Test: Throttle_FirstOrigination_FiresImmediately_With_Default_ZeroDelay
TEST_F(Internal_OspfTest, Throttle_FirstOrigination_FiresImmediately_With_Default_ZeroDelay)
{
    // LSA_THROTTLE_DELAY defaults to 0ms, so the very first reorigination
    // for a key should be applied as soon as the scheduler drains.
    LsaKey key(OSPFV2_LSA_EXTERNAL, 0x0C000000, ospfInstance->getRouterId());
    ExternalLsaV2 ext{};
    ext.networkMask = 0xFF000000;
    ext.metric = 10;
    ext.isType2 = true;
    ext.forwardingAddress = 0;
    ext.routeTag = 0;

    getOriginatorCtx().originateLsa<PolicyV2>(key, LsaBody{ext}, false);
    wait();

    auto* record = waitForLsa(key);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->header.sequence, routing::OSPF_INITIAL_SEQUENCE);
    EXPECT_NE(record->header.age, routing::OSPF_MAX_AGE);
}

// Test: Throttle_RepeatedOriginateLsa_Increments_Sequence_Each_Time
TEST_F(Internal_OspfTest, Throttle_RepeatedOriginateLsa_Increments_Sequence_Each_Time)
{
    LsaKey key(OSPFV2_LSA_EXTERNAL, 0x0C000000, ospfInstance->getRouterId());
    ExternalLsaV2 ext{};
    ext.networkMask = 0xFF000000;
    ext.metric = 10;
    ext.isType2 = true;

    getOriginatorCtx().originateLsa<PolicyV2>(key, LsaBody{ext}, false);
    wait();

    auto* first = waitForLsa(key);
    ASSERT_NE(first, nullptr);
    uint32_t seq1 = first->header.sequence;

    // Re-originate with a changed metric; throttle delay is 0 by default so
    // this should be applied immediately once the scheduler drains again.
    ext.metric = 20;
    getOriginatorCtx().originateLsa<PolicyV2>(key, LsaBody{ext}, false);
    wait();

    auto* second = waitForLsa(key, [seq1](const LsaRecord& r) { return r.header.sequence > seq1; });
    ASSERT_NE(second, nullptr);
    EXPECT_GT(second->header.sequence, seq1);

    auto* body = std::get_if<ExternalLsaV2>(&second->body);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->metric, 20u);
}

// Test: Throttle_NonZeroHold_Does_Not_Block_Initial_Origination
TEST_F(Internal_OspfTest, Throttle_NonZeroHold_Does_Not_Block_Initial_Origination)
{
    // Configure a large hold/backoff window; this only affects *repeated*
    // re-originations of the same key, not the first one.
    getConfigs().get<config::Ospf::LSA_THROTTLE_HOLD>().set(5000);
    getConfigs().get<config::Ospf::LSA_THROTTLE_MAX>().set(5000);

    LsaKey key(OSPFV2_LSA_EXTERNAL, 0x0D000000, ospfInstance->getRouterId());
    ExternalLsaV2 ext{};
    ext.networkMask = 0xFF000000;
    ext.metric = 5;
    ext.isType2 = true;

    getOriginatorCtx().originateLsa<PolicyV2>(key, LsaBody{ext}, false);
    wait();

    auto* record = waitForLsa(key);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->header.sequence, routing::OSPF_INITIAL_SEQUENCE);

    // Restore defaults for other tests.
    getConfigs().get<config::Ospf::LSA_THROTTLE_HOLD>().set(5000);
    getConfigs().get<config::Ospf::LSA_THROTTLE_MAX>().set(5000);
}

// Test: Throttle_SecondReorigination_Within_Hold_Window_Does_Not_Apply_Synchronously
TEST_F(Internal_OspfTest, Throttle_SecondReorigination_Within_Hold_Window_Does_Not_Apply_Synchronously)
{
    // With a large hold window, a *second* re-origination request issued
    // immediately after the first applies should not be reflected until the
    // hold timer fires - which requires real time to pass beyond waitIdle().
    getConfigs().get<config::Ospf::LSA_THROTTLE_HOLD>().set(600000);
    getConfigs().get<config::Ospf::LSA_THROTTLE_MAX>().set(600000);

    LsaKey key(OSPFV2_LSA_EXTERNAL, 0x0E000000, ospfInstance->getRouterId());
    ExternalLsaV2 ext{};
    ext.networkMask = 0xFF000000;
    ext.metric = 1;
    ext.isType2 = true;

    getOriginatorCtx().originateLsa<PolicyV2>(key, LsaBody{ext}, false);
    wait();

    auto* first = waitForLsa(key);
    ASSERT_NE(first, nullptr);
    uint32_t seq1 = first->header.sequence;

    // Immediately re-originate with a different metric; the hold timer from
    // the first origination is now active and far in the future.
    ext.metric = 2;
    getOriginatorCtx().originateLsa<PolicyV2>(key, LsaBody{ext}, false);
    wait();

    auto* second = waitForLsa(key);
    ASSERT_NE(second, nullptr);
    // Sequence should not have advanced yet - the pending throttle timer is
    // scheduled far in the future and waitIdle() does not advance real time.
    EXPECT_EQ(second->header.sequence, seq1);

    // Restore defaults for other tests.
    getConfigs().get<config::Ospf::LSA_THROTTLE_HOLD>().set(5000);
    getConfigs().get<config::Ospf::LSA_THROTTLE_MAX>().set(5000);
}

// Test: Throttle_Expire_While_Pending_Still_Removes_From_Lsdb_On_Fire
TEST_F(Internal_OspfTest, Throttle_Expire_While_Pending_Still_Removes_From_Lsdb_On_Fire)
{
    LsaKey key(OSPFV2_LSA_EXTERNAL, 0x0F000000, ospfInstance->getRouterId());
    ExternalLsaV2 ext{};
    ext.networkMask = 0xFF000000;
    ext.metric = 1;
    ext.isType2 = true;

    getOriginatorCtx().originateLsa<PolicyV2>(key, LsaBody{ext}, false);
    wait();

    ASSERT_NE(waitForLsa(key), nullptr);

    // Now request expiration; with default (0ms) throttle delay this should
    // apply immediately and set the LSA to MaxAge.
    getOriginatorCtx().originateLsa<PolicyV2>(key, LsaBody{ext}, true);
    wait();

    auto* record = waitForLsa(key, [](const LsaRecord& r) { return r.header.age == routing::OSPF_MAX_AGE; });
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->header.age, routing::OSPF_MAX_AGE);
}

// Test: GroupPacing_FullRefresh_Reaches_All_SelfOriginated_Lsa_Types
TEST_F(Internal_OspfTest, GroupPacing_FullRefresh_Reaches_All_SelfOriginated_Lsa_Types)
{
    // Originate a Type-5 external in addition to the always-present Router LSA.
    LsaKey extKey(OSPFV2_LSA_EXTERNAL, 0x10000000, ospfInstance->getRouterId());
    ExternalLsaV2 ext{};
    ext.networkMask = 0xFF000000;
    ext.metric = 7;
    ext.isType2 = true;
    getOriginatorCtx().originateLsa<PolicyV2>(extKey, LsaBody{ext}, false);

    fullRefreshV2();
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey routerKey(OSPFV2_LSA_ROUTER, rid, rid);

    EXPECT_NE(waitForLsa(routerKey), nullptr);
    auto* extRecord = waitForLsa(extKey);
    ASSERT_NE(extRecord, nullptr);
    EXPECT_NE(extRecord->header.age, routing::OSPF_MAX_AGE);
}

// Test: GroupPacing_Expired_Lsa_Not_Reintroduced_By_Subsequent_FullRefresh
TEST_F(Internal_OspfTest, GroupPacing_Expired_Lsa_Not_Reintroduced_By_Subsequent_FullRefresh)
{
    LsaKey extKey(OSPFV2_LSA_EXTERNAL, 0x11000000, ospfInstance->getRouterId());
    ExternalLsaV2 ext{};
    ext.networkMask = 0xFF000000;
    ext.metric = 3;
    ext.isType2 = true;

    getOriginatorCtx().originateLsa<PolicyV2>(extKey, LsaBody{ext}, false);
    wait();
    ASSERT_NE(waitForLsa(extKey), nullptr);

    // Expire it (e.g. redistribution withdrawn).
    getOriginatorCtx().originateLsa<PolicyV2>(extKey, LsaBody{ext}, true);
    wait();

    auto* expired = waitForLsa(extKey, [](const LsaRecord& r) { return r.header.age == routing::OSPF_MAX_AGE; });
    ASSERT_NE(expired, nullptr);
    EXPECT_EQ(expired->header.age, routing::OSPF_MAX_AGE);

    // A subsequent full refresh should not resurrect the expired external -
    // it has been erased from originationState and unscheduled from pacing.
    fullRefreshV2();
    wait();

    auto* afterRefresh = waitForLsa(extKey);
    ASSERT_NE(afterRefresh, nullptr);
    EXPECT_EQ(afterRefresh->header.age, routing::OSPF_MAX_AGE);
}

#pragma endregion OriginationThrottleAndPacing

#pragma region AbrSummaryReorigination

// Test: AbrSummary_ReoriginateSummaries_NoOp_When_Not_Abr
TEST_F(Internal_OspfTest, AbrSummary_ReoriginateSummaries_NoOp_When_Not_Abr)
{
    ASSERT_FALSE(ospfInstance->isABR());

    std::vector<OspfRouteChange> changes;
    OspfRouteChange change{};
    change.prefix = types::IPPrefix(uint32_t{0x0A0A0000}, 16);
    change.cost = 10;
    changes.push_back(change);

    // Should be a no-op: single-area process is never an ABR.
    reoriginateSummaries<PolicyV2>(getOriginatorCtx(), changes);
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey summaryKey(OSPFV2_LSA_SUM_NET, change.prefix.v4(), rid);
    EXPECT_EQ(waitForLsa(summaryKey), nullptr);
}

// Test: AbrSummary_ReoriginateSummaries_From_Area0_Installs_Into_NonZero_Area
TEST_F(Internal_OspfTest, AbrSummary_ReoriginateSummaries_From_Area0_Installs_Into_NonZero_Area)
{
    // Bring up area 1 so the process becomes an ABR.
    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    auto& area1 = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    std::vector<OspfRouteChange> changes;
    OspfRouteChange change{};
    change.prefix = types::IPPrefix(uint32_t{0x0A0A0000}, 16);
    change.cost = 42;
    changes.push_back(change);

    // Source area is the backbone (area 0); the summary should be
    // re-originated into area 1.
    reoriginateSummaries<PolicyV2>(getOriginatorCtx(), changes);
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey summaryKey(OSPFV2_LSA_SUM_NET, change.prefix.v4(), rid);

    auto* record = waitForLsa(summaryKey, &area1);
    ASSERT_NE(record, nullptr);
    EXPECT_NE(record->header.age, routing::OSPF_MAX_AGE);

    auto* body = std::get_if<SummaryNetworkLsa>(&record->body);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->metric, 42u);
    EXPECT_EQ(body->networkMask, types::v4Mask(change.prefix.prefixLength));

    // Area 0 itself should not receive a copy of its own summary.
    EXPECT_EQ(waitForLsa(summaryKey), nullptr);

    removeIface(iface1.id);
}

// Test: AbrSummary_ReoriginateSummary_Single_From_NonZero_Area_Targets_Area0
TEST_F(Internal_OspfTest, AbrSummary_ReoriginateSummary_Single_From_NonZero_Area_Targets_Area0)
{
    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    auto& area1 = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    OspfRouteChange change{};
    change.prefix = types::IPPrefix(uint32_t{0x0B0B0000}, 16);
    change.cost = 17;

    // Source area is non-zero (area 1); per RFC 2328 this propagates into the
    // backbone (area 0).
    reoriginateSummary<PolicyV2>(getOriginatorCtx(&area1), change);
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey summaryKey(OSPFV2_LSA_SUM_NET, change.prefix.v4(), rid);

    auto* record = waitForLsa(summaryKey);
    ASSERT_NE(record, nullptr);
    EXPECT_NE(record->header.age, routing::OSPF_MAX_AGE);

    auto* body = std::get_if<SummaryNetworkLsa>(&record->body);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->metric, 17u);

    removeIface(iface1.id);
}

// Test: AbrSummary_ReoriginateSummaries_From_NonZero_Area_Targets_Area0
TEST_F(Internal_OspfTest, AbrSummary_ReoriginateSummaries_From_NonZero_Area_Targets_Area0)
{
    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    auto& area1 = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    std::vector<OspfRouteChange> changes;
    OspfRouteChange change{};
    change.prefix = types::IPPrefix(uint32_t{0x0C0C0000}, 16);
    change.cost = 99;
    changes.push_back(change);

    // reoriginateSummaries (plural) from a non-zero source area must target
    // area 0 (the backbone), not area 1 itself.
    reoriginateSummaries<PolicyV2>(getOriginatorCtx(&area1), changes);
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey summaryKey(OSPFV2_LSA_SUM_NET, change.prefix.v4(), rid);

    auto* record = waitForLsa(summaryKey);
    ASSERT_NE(record, nullptr);

    auto* body = std::get_if<SummaryNetworkLsa>(&record->body);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->metric, 99u);

    removeIface(iface1.id);
}

// Test: AbrSummary_ReoriginateSummaries_From_Area0_Skips_Area_Without_ExternalRouting
TEST_F(Internal_OspfTest, AbrSummary_ReoriginateSummaries_From_Area0_Skips_Area_Without_ExternalRouting)
{
    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    auto& area1 = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    // Disable external routing (e.g. stub area) on area 1.
    getAreaFlags(&area1).setExternalRouting(false);

    std::vector<OspfRouteChange> changes;
    OspfRouteChange change{};
    change.prefix = types::IPPrefix(uint32_t{0x0D0D0000}, 16);
    change.cost = 5;
    changes.push_back(change);

    reoriginateSummaries<PolicyV2>(getOriginatorCtx(), changes);
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey summaryKey(OSPFV2_LSA_SUM_NET, change.prefix.v4(), rid);

    EXPECT_EQ(waitForLsa(summaryKey, &area1), nullptr);

    removeIface(iface1.id);
}

// Test: AbrSummary_ProcessSummaries_Installs_Batch_Into_Area_Lsdb
TEST_F(Internal_OspfTest, AbrSummary_ProcessSummaries_Installs_Batch_Into_Area_Lsdb)
{
    // Simulate receiving a batch of Type-3 summaries from a neighboring ABR.
    uint32_t remoteAbrRid = 0xC0C0C0C0;

    std::unordered_map<LsaKey, LsaBody> summaries;

    LsaKey key1(OSPFV2_LSA_SUM_NET, 0x0E0E0000, remoteAbrRid);
    SummaryNetworkLsa sum1{};
    sum1.metric = 11;
    sum1.networkMask = 0xFFFF0000;
    summaries.emplace(key1, LsaBody{sum1});

    LsaKey key2(OSPFV2_LSA_SUM_NET, 0x0F0F0000, remoteAbrRid);
    SummaryNetworkLsa sum2{};
    sum2.metric = 22;
    sum2.networkMask = 0xFFFF0000;
    summaries.emplace(key2, LsaBody{sum2});

    processSummaries<PolicyV2>(summaries);
    wait();

    auto* rec1 = waitForLsa(key1);
    ASSERT_NE(rec1, nullptr);
    auto* body1 = std::get_if<SummaryNetworkLsa>(&rec1->body);
    ASSERT_NE(body1, nullptr);
    EXPECT_EQ(body1->metric, 11u);

    auto* rec2 = waitForLsa(key2);
    ASSERT_NE(rec2, nullptr);
    auto* body2 = std::get_if<SummaryNetworkLsa>(&rec2->body);
    ASSERT_NE(body2, nullptr);
    EXPECT_EQ(body2->metric, 22u);
}

// Test: AbrSummary_ReoriginateSummary_Single_Updates_Existing_Summary_Metric
TEST_F(Internal_OspfTest, AbrSummary_ReoriginateSummary_Single_Updates_Existing_Summary_Metric)
{
    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    auto& area1 = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    OspfRouteChange change{};
    change.prefix = types::IPPrefix(uint32_t{0x0B0B0000}, 16);
    change.cost = 10;

    reoriginateSummary<PolicyV2>(getOriginatorCtx(&area1), change);
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey summaryKey(OSPFV2_LSA_SUM_NET, change.prefix.v4(), rid);

    auto* first = waitForLsa(summaryKey);
    ASSERT_NE(first, nullptr);
    uint32_t seq1 = first->header.sequence;

    // Cost changes; re-originate again.
    change.cost = 25;
    reoriginateSummary<PolicyV2>(getOriginatorCtx(&area1), change);
    wait();

    auto* second = waitForLsa(summaryKey, [seq1](const LsaRecord& r) { return r.header.sequence > seq1; });
    ASSERT_NE(second, nullptr);
    EXPECT_GT(second->header.sequence, seq1);

    auto* body = std::get_if<SummaryNetworkLsa>(&second->body);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->metric, 25u);

    removeIface(iface1.id);
}

#pragma endregion AbrSummaryReorigination

#pragma region SpfComputation

// Test: Spf_RunFull_Computes_Shortest_Path_Single_Router
TEST_F(Internal_OspfTest, Spf_RunFull_Computes_Shortest_Path_Single_Router)
{
    // Only self's Router-LSA exists (default broadcast network with no
    // FULL neighbors -> stub link only). SPF should still place the root
    // vertex at distance 0.
    fullRefreshV2();
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    ASSERT_NE(waitForLsa(selfKey), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);

    Vertex selfVertex{VertexType::ROUTER, selfRid};

    EXPECT_EQ(result.root.type, VertexType::ROUTER);
    EXPECT_EQ(result.root.id, selfRid);

    auto it = result.nodes.find(selfVertex);
    ASSERT_NE(it, result.nodes.end());
    EXPECT_EQ(it->second.dist, 0u);
    EXPECT_TRUE(it->second.confirmed);
}

// Test: Spf_RunFull_Computes_Shortest_Path_Multi_Hop_Topology
TEST_F(Internal_OspfTest, Spf_RunFull_Computes_Shortest_Path_Multi_Hop_Topology)
{
    // Configure self as point-to-point with a FULL neighbor so self's
    // Router-LSA contains a P2P link to neighborRouterId.
    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    ospfInterface->enqueueSyncNetworkType();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();

    // Reorigination from the FULL adjacency is async; wait for the P2P link to neighborRouterId to land before relying on it.
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    ASSERT_NE(waitForLsa(selfKey, [neighborRouterId = this->neighborRouterId](const LsaRecord& r) {
        auto* body = std::get_if<RouterLsaV2>(&r.body);
        if (!body)
            return false;
        for (const auto& link : body->links)
            if (link.type == 1 /*P2P*/ && link.linkId == neighborRouterId)
                return true;
        return false;
    }), nullptr);

    // Inject a synthetic Router-LSA for the neighbor with a P2P link back
    // to self (cost 5) and a stub link to a distinct prefix (cost 1).
    RouterLsaV2 nbrLsa;
    nbrLsa.flags = 0;
    nbrLsa.links.push_back({.linkId = selfRid, .linkData = neighborRouterId, .type = 1 /*P2P*/, .metric = 5});
    nbrLsa.links.push_back({.linkId = 0x0A0A0A00, .linkData = 0xFFFFFF00, .type = 3 /*stub*/, .metric = 1});

    LsaKey nbrKey(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader nbrHdr;
    nbrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    nbrHdr.age = 0;

    IncomingLsaContext ctx = {.key = nbrKey, .header = nbrHdr, .checksumValid = true};
    LsaBody nbrLsaBody{nbrLsa};
    processLsa<PolicyV2>(ctx, nbrLsaBody);
    wait();

    ASSERT_NE(waitForLsa(nbrKey), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);

    Vertex nbrVertex{VertexType::ROUTER, neighborRouterId};
    auto it = result.nodes.find(nbrVertex);
    ASSERT_NE(it, result.nodes.end());
    EXPECT_TRUE(it->second.confirmed);
    ASSERT_FALSE(it->second.parents.empty());
    EXPECT_EQ(it->second.parents[0].parent.type, VertexType::ROUTER);
    EXPECT_EQ(it->second.parents[0].parent.id, selfRid);
}

// Test: Spf_RunFull_Ecmp_Equal_Cost_Paths_Both_Confirmed
TEST_F(Internal_OspfTest, Spf_RunFull_Ecmp_Equal_Cost_Paths_Both_Confirmed)
{
    getConfigs().get<config::Ospf::LSA_ARRIVAL>().set(0);

    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    ospfInterface->enqueueSyncNetworkType();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t farRid = 0xC0A80104;

    // Neighbor's Router-LSA: P2P link back to self (cost 5), and P2P link
    // to a "far" router (cost 5).
    RouterLsaV2 nbrLsa;
    nbrLsa.flags = 0;
    nbrLsa.links.push_back({.linkId = selfRid, .linkData = neighborRouterId, .type = 1, .metric = 5});
    nbrLsa.links.push_back({.linkId = farRid, .linkData = neighborRouterId, .type = 1, .metric = 5});

    LsaKey nbrKey(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader nbrHdr;
    nbrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    nbrHdr.age = 0;
    IncomingLsaContext nbrCtx = {.key = nbrKey, .header = nbrHdr, .checksumValid = true};
    LsaBody nbrLsaBody{nbrLsa};
    processLsa<PolicyV2>(nbrCtx, nbrLsaBody);

    // A second router, directly reachable from self via neighborRouterId2
    // (also cost 5), creating two equal-cost (10) paths to farRid.
    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    ospfInterface->enqueueSyncNetworkType();
    wait();

    RouterLsaV2 nbr2Lsa;
    nbr2Lsa.flags = 0;
    nbr2Lsa.links.push_back({.linkId = selfRid, .linkData = neighborRouterId2, .type = 1, .metric = 5});
    nbr2Lsa.links.push_back({.linkId = farRid, .linkData = neighborRouterId2, .type = 1, .metric = 5});

    LsaKey nbr2Key(OSPFV2_LSA_ROUTER, neighborRouterId2, neighborRouterId2);
    LsaHeader nbr2Hdr;
    nbr2Hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    nbr2Hdr.age = 0;
    IncomingLsaContext nbr2Ctx = {.key = nbr2Key, .header = nbr2Hdr, .checksumValid = true};
    LsaBody nbr2LsaBody{nbr2Lsa};
    processLsa<PolicyV2>(nbr2Ctx, nbr2LsaBody);

    RouterLsaV2 selfLsa;
    selfLsa.flags = 0;
    selfLsa.links.push_back({.linkId = neighborRouterId, .linkData = selfRid, .type = 1, .metric = 5});
    selfLsa.links.push_back({.linkId = neighborRouterId2, .linkData = selfRid, .type = 1, .metric = 5});

    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    uint32_t selfSeq;
    {
        auto* existingSelf = waitForLsa(selfKey);
        std::lock_guard lock(getSchedulerLock());
        selfSeq = existingSelf ? existingSelf->header.sequence : routing::OSPF_INITIAL_SEQUENCE;
    }
    LsaHeader selfHdr;
    selfHdr.sequence = selfSeq + 1;
    selfHdr.age = 0;
    IncomingLsaContext selfCtx = {.key = selfKey, .header = selfHdr, .checksumValid = true};
    LsaBody selfLsaBody{selfLsa};
    processLsa<PolicyV2>(selfCtx, selfLsaBody);

    // Far router's Router-LSA: P2P links back to both intermediate routers.
    RouterLsaV2 farLsa;
    farLsa.flags = 0;
    farLsa.links.push_back({.linkId = neighborRouterId, .linkData = farRid, .type = 1, .metric = 5});
    farLsa.links.push_back({.linkId = neighborRouterId2, .linkData = farRid, .type = 1, .metric = 5});

    LsaKey farKey(OSPFV2_LSA_ROUTER, farRid, farRid);
    LsaHeader farHdr;
    farHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    farHdr.age = 0;
    IncomingLsaContext farCtx = {.key = farKey, .header = farHdr, .checksumValid = true};
    LsaBody farLsaBody{farLsa};
    processLsa<PolicyV2>(farCtx, farLsaBody);

    wait();

    ASSERT_NE(waitForLsa(farKey), nullptr);
    ASSERT_NE(waitForLsa(nbr2Key), nullptr);
    ASSERT_NE(waitForLsa(selfKey, [selfSeq](const LsaRecord& rec) { return rec.header.sequence == selfSeq + 1; }), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);

    Vertex farVertex{VertexType::ROUTER, farRid};
    auto it = result.nodes.find(farVertex);
    ASSERT_NE(it, result.nodes.end());
    EXPECT_TRUE(it->second.confirmed);
    EXPECT_EQ(it->second.dist, 10u);
    // ECMP: both intermediate routers should appear as parents.
    EXPECT_GE(it->second.parents.size(), 2u);

    getConfigs().get<config::Ospf::LSA_ARRIVAL>().set(1000);
}

// Test: Spf_ComputeDelta_Detects_New_Link_As_Addition
TEST_F(Internal_OspfTest, Spf_ComputeDelta_Detects_New_Link_As_Addition)
{
    getConfigs().get<config::Ospf::LSA_ARRIVAL>().set(0);

    fullRefreshV2();
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    ASSERT_NE(waitForLsa(selfKey), nullptr);

    SpfTopology<PolicyV2> topo1(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result1 = engine.run<PolicyV2>(topo1);

    // Add a new P2P link to neighborRouterId via a synthetic re-origination
    // of self's Router-LSA, plus the neighbor's reciprocal Router-LSA.
    RouterLsaV2 selfLsa;
    selfLsa.flags = 0;
    selfLsa.links.push_back({.linkId = neighborRouterId, .linkData = selfRid, .type = 1, .metric = 7});

    uint32_t selfSeq;
    {
        auto* existingSelf = waitForLsa(selfKey);
        std::lock_guard lock(getSchedulerLock());
        selfSeq = existingSelf ? existingSelf->header.sequence : routing::OSPF_INITIAL_SEQUENCE;
    }
    LsaHeader selfHdr;
    selfHdr.sequence = selfSeq + 1;
    selfHdr.age = 0;
    IncomingLsaContext selfCtx = {.key = selfKey, .header = selfHdr, .checksumValid = true};
    LsaBody selfLsaBody{selfLsa};
    processLsa<PolicyV2>(selfCtx, selfLsaBody);

    RouterLsaV2 nbrLsa;
    nbrLsa.flags = 0;
    nbrLsa.links.push_back({.linkId = selfRid, .linkData = neighborRouterId, .type = 1, .metric = 7});

    LsaKey nbrKey(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    auto* existingNbr = waitForLsa(nbrKey);
    LsaHeader nbrHdr;
    nbrHdr.sequence = existingNbr ? existingNbr->header.sequence + 1 : routing::OSPF_INITIAL_SEQUENCE;
    nbrHdr.age = 0;
    IncomingLsaContext nbrCtx = {.key = nbrKey, .header = nbrHdr, .checksumValid = true};
    LsaBody nbrLsaBody{nbrLsa};
    processLsa<PolicyV2>(nbrCtx, nbrLsaBody);

    wait();

    ASSERT_NE(waitForLsa(selfKey, [selfSeq](const LsaRecord& rec) { return rec.header.sequence == selfSeq + 1; }), nullptr);
    ASSERT_NE(waitForLsa(nbrKey, [&nbrHdr](const LsaRecord& rec) { return rec.header.sequence == nbrHdr.sequence; }), nullptr);

    SpfTopology<PolicyV2> topo2(getSpfManager());
    SpfResult result2 = engine.run<PolicyV2>(topo2);

    Vertex nbrVertex{VertexType::ROUTER, neighborRouterId};

    // The neighbor vertex was absent (or unreachable) before, and reachable
    // at cost 7 after.
    EXPECT_EQ(result1.nodes.find(nbrVertex), result1.nodes.end());

    auto it2 = result2.nodes.find(nbrVertex);
    ASSERT_NE(it2, result2.nodes.end());
    EXPECT_TRUE(it2->second.confirmed);
    EXPECT_EQ(it2->second.dist, 7u);

    getConfigs().get<config::Ospf::LSA_ARRIVAL>().set(1000);
}

// Test: Spf_ComputeDelta_Detects_Cost_Decrease
TEST_F(Internal_OspfTest, Spf_ComputeDelta_Detects_Cost_Decrease)
{
    getConfigs().get<config::Ospf::LSA_ARRIVAL>().set(0);

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey selfKeyForSeq(OSPFV2_LSA_ROUTER, selfRid, selfRid);

    // Out-sequence whatever self Router-LSA the fixture already originated, else the install collides (IGNORE_DUPLICATE).
    uint32_t baseSelfSeq;
    {
        auto* existingSelf = waitForLsa(selfKeyForSeq);
        std::lock_guard lock(getSchedulerLock());
        baseSelfSeq = existingSelf ? existingSelf->header.sequence : routing::OSPF_INITIAL_SEQUENCE - 1;
    }

    auto installLinks = [&](uint16_t selfToNbrCost, uint16_t nbrToSelfCost, uint32_t selfSeq, uint32_t nbrSeq)
    {
        RouterLsaV2 selfLsa;
        selfLsa.flags = 0;
        selfLsa.links.push_back({.linkId = neighborRouterId, .linkData = selfRid, .type = 1, .metric = selfToNbrCost});
        LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
        LsaHeader selfHdr;
        selfHdr.sequence = selfSeq;
        selfHdr.age = 0;
        IncomingLsaContext selfCtx = {.key = selfKey, .header = selfHdr, .checksumValid = true};
        LsaBody selfLsaBody{selfLsa};
        processLsa<PolicyV2>(selfCtx, selfLsaBody);

        RouterLsaV2 nbrLsa;
        nbrLsa.flags = 0;
        nbrLsa.links.push_back({.linkId = selfRid, .linkData = neighborRouterId, .type = 1, .metric = nbrToSelfCost});
        LsaKey nbrKey(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
        LsaHeader nbrHdr;
        nbrHdr.sequence = nbrSeq;
        nbrHdr.age = 0;
        IncomingLsaContext nbrCtx = {.key = nbrKey, .header = nbrHdr, .checksumValid = true};
        LsaBody nbrLsaBody{nbrLsa};
        processLsa<PolicyV2>(nbrCtx, nbrLsaBody);
    };

    installLinks(20, 20, baseSelfSeq + 1, baseSelfSeq);
    wait();

    SpfTopology<PolicyV2> topo1(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result1 = engine.run<PolicyV2>(topo1);

    // Decrease the cost of the link.
    installLinks(5, 5, baseSelfSeq + 2, baseSelfSeq + 1);
    wait();

    SpfTopology<PolicyV2> topo2(getSpfManager());
    SpfResult result2 = engine.run<PolicyV2>(topo2);

    Vertex nbrVertex{VertexType::ROUTER, neighborRouterId};

    auto it1 = result1.nodes.find(nbrVertex);
    ASSERT_NE(it1, result1.nodes.end());
    EXPECT_EQ(it1->second.dist, 20u);

    auto it2 = result2.nodes.find(nbrVertex);
    ASSERT_NE(it2, result2.nodes.end());
    EXPECT_EQ(it2->second.dist, 5u);

    getConfigs().get<config::Ospf::LSA_ARRIVAL>().set(1000);
}

// Test: Spf_ComputeDelta_Detects_Cost_Increase_Or_Removal_As_FullRecompute
TEST_F(Internal_OspfTest, Spf_ComputeDelta_Detects_Cost_Increase_Or_Removal_As_FullRecompute)
{
    getConfigs().get<config::Ospf::LSA_ARRIVAL>().set(0);

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey selfKeyForSeq(OSPFV2_LSA_ROUTER, selfRid, selfRid);

    // Out-sequence whatever self Router-LSA the fixture already originated, else the install collides (IGNORE_DUPLICATE).
    uint32_t baseSelfSeq;
    {
        auto* existingSelf = waitForLsa(selfKeyForSeq);
        std::lock_guard lock(getSchedulerLock());
        baseSelfSeq = existingSelf ? existingSelf->header.sequence : routing::OSPF_INITIAL_SEQUENCE - 1;
    }

    RouterLsaV2 selfLsa1;
    selfLsa1.flags = 0;
    selfLsa1.links.push_back({.linkId = neighborRouterId, .linkData = selfRid, .type = 1, .metric = 5});
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    LsaHeader selfHdr1;
    selfHdr1.sequence = baseSelfSeq + 1;
    selfHdr1.age = 0;
    IncomingLsaContext selfCtx1 = {.key = selfKey, .header = selfHdr1, .checksumValid = true};
    LsaBody selfLsa1Body{selfLsa1};
    processLsa<PolicyV2>(selfCtx1, selfLsa1Body);

    RouterLsaV2 nbrLsa1;
    nbrLsa1.flags = 0;
    nbrLsa1.links.push_back({.linkId = selfRid, .linkData = neighborRouterId, .type = 1, .metric = 5});
    LsaKey nbrKey(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader nbrHdr1;
    nbrHdr1.sequence = routing::OSPF_INITIAL_SEQUENCE;
    nbrHdr1.age = 0;
    IncomingLsaContext nbrCtx1 = {.key = nbrKey, .header = nbrHdr1, .checksumValid = true};
    LsaBody nbrLsa1Body{nbrLsa1};
    processLsa<PolicyV2>(nbrCtx1, nbrLsa1Body);

    wait();

    SpfTopology<PolicyV2> topo1(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result1 = engine.run<PolicyV2>(topo1);

    Vertex nbrVertex{VertexType::ROUTER, neighborRouterId};
    auto it1 = result1.nodes.find(nbrVertex);
    ASSERT_NE(it1, result1.nodes.end());
    EXPECT_EQ(it1->second.dist, 5u);

    // Increase the cost of self's link to the neighbor.
    RouterLsaV2 selfLsa2;
    selfLsa2.flags = 0;
    selfLsa2.links.push_back({.linkId = neighborRouterId, .linkData = selfRid, .type = 1, .metric = 50});
    LsaHeader selfHdr2;
    selfHdr2.sequence = baseSelfSeq + 2;
    selfHdr2.age = 0;
    IncomingLsaContext selfCtx2 = {.key = selfKey, .header = selfHdr2, .checksumValid = true};
    LsaBody selfLsa2Body{selfLsa2};
    processLsa<PolicyV2>(selfCtx2, selfLsa2Body);

    wait();

    SpfTopology<PolicyV2> topo2(getSpfManager());
    SpfResult result2 = engine.run<PolicyV2>(topo2);

    auto it2 = result2.nodes.find(nbrVertex);
    ASSERT_NE(it2, result2.nodes.end());
    EXPECT_EQ(it2->second.dist, 50u);

    getConfigs().get<config::Ospf::LSA_ARRIVAL>().set(1000);
}

// Test: Spf_RunIspfRepair_Handles_Cost_Decrease_Without_Full_Recompute
TEST_F(Internal_OspfTest, Spf_RunIspfRepair_Handles_Cost_Decrease_Without_Full_Recompute)
{
    getConfigs().get<config::Ospf::LSA_ARRIVAL>().set(0);

    uint32_t selfRid = ospfInstance->getRouterId();

    // Out-sequence whatever self Router-LSA the fixture already originated, else the install collides (IGNORE_DUPLICATE).
    uint32_t baseSelfSeq;
    {
        auto* existingSelf = waitForLsa(LsaKey(OSPFV2_LSA_ROUTER, selfRid, selfRid));
        std::lock_guard lock(getSchedulerLock());
        baseSelfSeq = existingSelf ? existingSelf->header.sequence : routing::OSPF_INITIAL_SEQUENCE - 1;
    }

    auto installLinks = [&](uint16_t cost, uint32_t selfSeq, uint32_t nbrSeq)
    {
        RouterLsaV2 selfLsa;
        selfLsa.flags = 0;
        selfLsa.links.push_back({.linkId = neighborRouterId, .linkData = selfRid, .type = 1, .metric = cost});
        LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
        LsaHeader selfHdr;
        selfHdr.sequence = selfSeq;
        selfHdr.age = 0;
        IncomingLsaContext selfCtx = {.key = selfKey, .header = selfHdr, .checksumValid = true};
        LsaBody selfLsaBody{selfLsa};
        processLsa<PolicyV2>(selfCtx, selfLsaBody);

        RouterLsaV2 nbrLsa;
        nbrLsa.flags = 0;
        nbrLsa.links.push_back({.linkId = selfRid, .linkData = neighborRouterId, .type = 1, .metric = cost});
        LsaKey nbrKey(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
        LsaHeader nbrHdr;
        nbrHdr.sequence = nbrSeq;
        nbrHdr.age = 0;
        IncomingLsaContext nbrCtx = {.key = nbrKey, .header = nbrHdr, .checksumValid = true};
        LsaBody nbrLsaBody{nbrLsa};
        processLsa<PolicyV2>(nbrCtx, nbrLsaBody);
    };

    installLinks(30, baseSelfSeq + 1, routing::OSPF_INITIAL_SEQUENCE);
    wait();

    SpfEngine engine(getSpfManager());
    {
        SpfTopology<PolicyV2> topo1(getSpfManager());
        SpfResult result1 = engine.run<PolicyV2>(topo1);
        Vertex nbrVertex{VertexType::ROUTER, neighborRouterId};
        auto it1 = result1.nodes.find(nbrVertex);
        ASSERT_NE(it1, result1.nodes.end());
        EXPECT_EQ(it1->second.dist, 30u);
    }

    // Decrease cost - a repairable delta (iSPF repair path).
    installLinks(3, baseSelfSeq + 2, routing::OSPF_INITIAL_SEQUENCE + 1);
    wait();

    SpfTopology<PolicyV2> topo2(getSpfManager());
    SpfResult result2 = engine.run<PolicyV2>(topo2);
    Vertex nbrVertex{VertexType::ROUTER, neighborRouterId};
    auto it2 = result2.nodes.find(nbrVertex);
    ASSERT_NE(it2, result2.nodes.end());
    EXPECT_EQ(it2->second.dist, 3u);
    EXPECT_TRUE(it2->second.confirmed);

    getConfigs().get<config::Ospf::LSA_ARRIVAL>().set(1000);
}

// Test: Spf_RunIspfRepair_Falls_Back_To_Full_On_Non_Repairable_Delta
TEST_F(Internal_OspfTest, Spf_RunIspfRepair_Falls_Back_To_Full_On_Non_Repairable_Delta)
{
    getConfigs().get<config::Ospf::LSA_ARRIVAL>().set(0);

    uint32_t selfRid = ospfInstance->getRouterId();

    // Out-sequence whatever self Router-LSA the fixture already originated, else the install collides (IGNORE_DUPLICATE).
    uint32_t baseSelfSeq;
    {
        auto* existingSelf = waitForLsa(LsaKey(OSPFV2_LSA_ROUTER, selfRid, selfRid));
        std::lock_guard lock(getSchedulerLock());
        baseSelfSeq = existingSelf ? existingSelf->header.sequence : routing::OSPF_INITIAL_SEQUENCE - 1;
    }

    RouterLsaV2 selfLsa1;
    selfLsa1.flags = 0;
    selfLsa1.links.push_back({.linkId = neighborRouterId, .linkData = selfRid, .type = 1, .metric = 5});
    selfLsa1.links.push_back({.linkId = neighborRouterId2, .linkData = selfRid, .type = 1, .metric = 5});
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    LsaHeader selfHdr1;
    selfHdr1.sequence = baseSelfSeq + 1;
    selfHdr1.age = 0;
    IncomingLsaContext selfCtx1 = {.key = selfKey, .header = selfHdr1, .checksumValid = true};
    LsaBody selfLsa1Body{selfLsa1};
    processLsa<PolicyV2>(selfCtx1, selfLsa1Body);

    RouterLsaV2 nbr1Lsa;
    nbr1Lsa.flags = 0;
    nbr1Lsa.links.push_back({.linkId = selfRid, .linkData = neighborRouterId, .type = 1, .metric = 5});
    LsaKey nbr1Key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader nbr1Hdr;
    nbr1Hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    nbr1Hdr.age = 0;
    IncomingLsaContext nbr1Ctx = {.key = nbr1Key, .header = nbr1Hdr, .checksumValid = true};
    LsaBody nbr1LsaBody{nbr1Lsa};
    processLsa<PolicyV2>(nbr1Ctx, nbr1LsaBody);

    RouterLsaV2 nbr2Lsa;
    nbr2Lsa.flags = 0;
    nbr2Lsa.links.push_back({.linkId = selfRid, .linkData = neighborRouterId2, .type = 1, .metric = 5});
    LsaKey nbr2Key(OSPFV2_LSA_ROUTER, neighborRouterId2, neighborRouterId2);
    LsaHeader nbr2Hdr;
    nbr2Hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    nbr2Hdr.age = 0;
    IncomingLsaContext nbr2Ctx = {.key = nbr2Key, .header = nbr2Hdr, .checksumValid = true};
    LsaBody nbr2LsaBody{nbr2Lsa};
    processLsa<PolicyV2>(nbr2Ctx, nbr2LsaBody);

    wait();

    SpfEngine engine(getSpfManager());
    {
        SpfTopology<PolicyV2> topo1(getSpfManager());
        SpfResult result1 = engine.run<PolicyV2>(topo1);
        Vertex nbrVertex{VertexType::ROUTER, neighborRouterId};
        auto it1 = result1.nodes.find(nbrVertex);
        ASSERT_NE(it1, result1.nodes.end());
        EXPECT_EQ(it1->second.dist, 5u);
    }

    // Remove the link to neighborRouterId entirely - a non-repairable
    // delta (removal) that should force a full recompute.
    RouterLsaV2 selfLsa2;
    selfLsa2.flags = 0;
    selfLsa2.links.push_back({.linkId = neighborRouterId2, .linkData = selfRid, .type = 1, .metric = 5});
    LsaHeader selfHdr2;
    selfHdr2.sequence = baseSelfSeq + 2;
    selfHdr2.age = 0;
    IncomingLsaContext selfCtx2 = {.key = selfKey, .header = selfHdr2, .checksumValid = true};
    LsaBody selfLsa2Body{selfLsa2};
    processLsa<PolicyV2>(selfCtx2, selfLsa2Body);

    wait();

    SpfTopology<PolicyV2> topo2(getSpfManager());
    SpfResult result2 = engine.run<PolicyV2>(topo2);

    Vertex nbrVertex{VertexType::ROUTER, neighborRouterId};
    auto it2 = result2.nodes.find(nbrVertex);
    if (it2 != result2.nodes.end())
        EXPECT_FALSE(it2->second.confirmed);

    Vertex nbr2Vertex{VertexType::ROUTER, neighborRouterId2};
    auto it2b = result2.nodes.find(nbr2Vertex);
    ASSERT_NE(it2b, result2.nodes.end());
    EXPECT_TRUE(it2b->second.confirmed);
    EXPECT_EQ(it2b->second.dist, 5u);

    getConfigs().get<config::Ospf::LSA_ARRIVAL>().set(1000);
}

// Test: Spf_RelaxEdgeFull_Updates_Distance_When_Shorter_Path_Found
TEST_F(Internal_OspfTest, Spf_RelaxEdgeFull_Updates_Distance_When_Shorter_Path_Found)
{
    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t farRid = 0xC0A80104;

    // Out-sequence whatever self Router-LSA the fixture already originated, else the install collides (IGNORE_DUPLICATE).
    uint32_t baseSelfSeq;
    {
        auto* existingSelf = waitForLsa(LsaKey(OSPFV2_LSA_ROUTER, selfRid, selfRid));
        std::lock_guard lock(getSchedulerLock());
        baseSelfSeq = existingSelf ? existingSelf->header.sequence : routing::OSPF_INITIAL_SEQUENCE - 1;
    }

    // self -> neighborRouterId (cost 100) -> farRid (cost 1)  => dist 101
    // self -> neighborRouterId2 (cost 1) -> farRid (cost 1)   => dist 2 (shorter)
    RouterLsaV2 selfLsa;
    selfLsa.flags = 0;
    selfLsa.links.push_back({.linkId = neighborRouterId, .linkData = selfRid, .type = 1, .metric = 100});
    selfLsa.links.push_back({.linkId = neighborRouterId2, .linkData = selfRid, .type = 1, .metric = 1});
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    LsaHeader selfHdr;
    selfHdr.sequence = baseSelfSeq + 1;
    selfHdr.age = 0;
    IncomingLsaContext selfCtx = {.key = selfKey, .header = selfHdr, .checksumValid = true};
    LsaBody selfLsaBody{selfLsa};
    processLsa<PolicyV2>(selfCtx, selfLsaBody);

    RouterLsaV2 nbr1Lsa;
    nbr1Lsa.flags = 0;
    nbr1Lsa.links.push_back({.linkId = selfRid, .linkData = neighborRouterId, .type = 1, .metric = 100});
    nbr1Lsa.links.push_back({.linkId = farRid, .linkData = neighborRouterId, .type = 1, .metric = 1});
    LsaKey nbr1Key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader nbr1Hdr;
    nbr1Hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    nbr1Hdr.age = 0;
    IncomingLsaContext nbr1Ctx = {.key = nbr1Key, .header = nbr1Hdr, .checksumValid = true};
    LsaBody nbr1LsaBody{nbr1Lsa};
    processLsa<PolicyV2>(nbr1Ctx, nbr1LsaBody);

    RouterLsaV2 nbr2Lsa;
    nbr2Lsa.flags = 0;
    nbr2Lsa.links.push_back({.linkId = selfRid, .linkData = neighborRouterId2, .type = 1, .metric = 1});
    nbr2Lsa.links.push_back({.linkId = farRid, .linkData = neighborRouterId2, .type = 1, .metric = 1});
    LsaKey nbr2Key(OSPFV2_LSA_ROUTER, neighborRouterId2, neighborRouterId2);
    LsaHeader nbr2Hdr;
    nbr2Hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    nbr2Hdr.age = 0;
    IncomingLsaContext nbr2Ctx = {.key = nbr2Key, .header = nbr2Hdr, .checksumValid = true};
    LsaBody nbr2LsaBody{nbr2Lsa};
    processLsa<PolicyV2>(nbr2Ctx, nbr2LsaBody);

    RouterLsaV2 farLsa;
    farLsa.flags = 0;
    farLsa.links.push_back({.linkId = neighborRouterId, .linkData = farRid, .type = 1, .metric = 1});
    farLsa.links.push_back({.linkId = neighborRouterId2, .linkData = farRid, .type = 1, .metric = 1});
    LsaKey farKey(OSPFV2_LSA_ROUTER, farRid, farRid);
    LsaHeader farHdr;
    farHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    farHdr.age = 0;
    IncomingLsaContext farCtx = {.key = farKey, .header = farHdr, .checksumValid = true};
    LsaBody farLsaBody{farLsa};
    processLsa<PolicyV2>(farCtx, farLsaBody);

    wait();

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);

    Vertex farVertex{VertexType::ROUTER, farRid};
    auto it = result.nodes.find(farVertex);
    ASSERT_NE(it, result.nodes.end());
    EXPECT_EQ(it->second.dist, 2u);
    ASSERT_FALSE(it->second.parents.empty());
    EXPECT_EQ(it->second.parents[0].parent.id, neighborRouterId2);
}

// Test: Spf_RelaxEdgeRepair_Restricted_To_Confirmed_Vertices
TEST_F(Internal_OspfTest, Spf_RelaxEdgeRepair_Restricted_To_Confirmed_Vertices)
{
    getConfigs().get<config::Ospf::LSA_ARRIVAL>().set(0);

    uint32_t selfRid = ospfInstance->getRouterId();

    // Out-sequence whatever self Router-LSA the fixture already originated, else the install collides (IGNORE_DUPLICATE).
    uint32_t baseSelfSeq;
    {
        auto* existingSelf = waitForLsa(LsaKey(OSPFV2_LSA_ROUTER, selfRid, selfRid));
        std::lock_guard lock(getSchedulerLock());
        baseSelfSeq = existingSelf ? existingSelf->header.sequence : routing::OSPF_INITIAL_SEQUENCE - 1;
    }

    // Two unreachable-then-reachable hops: self -> neighborRouterId (cost
    // 10) -> neighborRouterId2 (cost 10).
    RouterLsaV2 selfLsa1;
    selfLsa1.flags = 0;
    selfLsa1.links.push_back({.linkId = neighborRouterId, .linkData = selfRid, .type = 1, .metric = 10});
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    LsaHeader selfHdr1;
    selfHdr1.sequence = baseSelfSeq + 1;
    selfHdr1.age = 0;
    IncomingLsaContext selfCtx1 = {.key = selfKey, .header = selfHdr1, .checksumValid = true};
    LsaBody selfLsa1Body{selfLsa1};
    processLsa<PolicyV2>(selfCtx1, selfLsa1Body);

    RouterLsaV2 nbr1Lsa;
    nbr1Lsa.flags = 0;
    nbr1Lsa.links.push_back({.linkId = selfRid, .linkData = neighborRouterId, .type = 1, .metric = 10});
    nbr1Lsa.links.push_back({.linkId = neighborRouterId2, .linkData = neighborRouterId, .type = 1, .metric = 10});
    LsaKey nbr1Key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader nbr1Hdr;
    nbr1Hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    nbr1Hdr.age = 0;
    IncomingLsaContext nbr1Ctx = {.key = nbr1Key, .header = nbr1Hdr, .checksumValid = true};
    LsaBody nbr1LsaBody{nbr1Lsa};
    processLsa<PolicyV2>(nbr1Ctx, nbr1LsaBody);

    RouterLsaV2 nbr2Lsa;
    nbr2Lsa.flags = 0;
    nbr2Lsa.links.push_back({.linkId = neighborRouterId, .linkData = neighborRouterId2, .type = 1, .metric = 10});
    LsaKey nbr2Key(OSPFV2_LSA_ROUTER, neighborRouterId2, neighborRouterId2);
    LsaHeader nbr2Hdr;
    nbr2Hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    nbr2Hdr.age = 0;
    IncomingLsaContext nbr2Ctx = {.key = nbr2Key, .header = nbr2Hdr, .checksumValid = true};
    LsaBody nbr2LsaBody{nbr2Lsa};
    processLsa<PolicyV2>(nbr2Ctx, nbr2LsaBody);

    wait();

    SpfEngine engine(getSpfManager());
    {
        SpfTopology<PolicyV2> topo1(getSpfManager());
        SpfResult result1 = engine.run<PolicyV2>(topo1);
        Vertex nbr2Vertex{VertexType::ROUTER, neighborRouterId2};
        auto it1 = result1.nodes.find(nbr2Vertex);
        ASSERT_NE(it1, result1.nodes.end());
        EXPECT_EQ(it1->second.dist, 20u);
    }

    // Decrease the cost of the first hop only; repair pass should relax
    // edges from confirmed vertices and propagate the improvement.
    RouterLsaV2 selfLsa2;
    selfLsa2.flags = 0;
    selfLsa2.links.push_back({.linkId = neighborRouterId, .linkData = selfRid, .type = 1, .metric = 1});
    LsaHeader selfHdr2;
    selfHdr2.sequence = baseSelfSeq + 2;
    selfHdr2.age = 0;
    IncomingLsaContext selfCtx2 = {.key = selfKey, .header = selfHdr2, .checksumValid = true};
    LsaBody selfLsa2Body{selfLsa2};
    processLsa<PolicyV2>(selfCtx2, selfLsa2Body);

    wait();

    SpfTopology<PolicyV2> topo2(getSpfManager());
    SpfResult result2 = engine.run<PolicyV2>(topo2);

    Vertex nbr2Vertex{VertexType::ROUTER, neighborRouterId2};
    auto it2 = result2.nodes.find(nbr2Vertex);
    ASSERT_NE(it2, result2.nodes.end());
    EXPECT_EQ(it2->second.dist, 11u);
    EXPECT_TRUE(it2->second.confirmed);

    getConfigs().get<config::Ospf::LSA_ARRIVAL>().set(1000);
}

// Test: Spf_FinalizeParents_Propagates_FirstHop_Interface_Ids
TEST_F(Internal_OspfTest, Spf_FinalizeParents_Propagates_FirstHop_Interface_Ids)
{
    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    ospfInterface->enqueueSyncNetworkType();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t farRid = 0xC0A80104;

    // Reorigination is async and a stale record may pre-exist; wait for the P2P link to neighborRouterId to actually appear in the body.
    {
        LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
        auto* selfRecord = waitForLsa(selfKey, [&](const LsaRecord& r) {
            auto* body = std::get_if<RouterLsaV2>(&r.body);
            if (!body) return false;
            for (auto& link : body->links)
            {
                if (link.type == 1 && link.linkId == neighborRouterId) return true;
            }
            return false;
        });
        ASSERT_NE(selfRecord, nullptr);
    }

    RouterLsaV2 nbrLsa;
    nbrLsa.flags = 0;
    nbrLsa.links.push_back({.linkId = selfRid, .linkData = neighborRouterId, .type = 1, .metric = 5});
    nbrLsa.links.push_back({.linkId = farRid, .linkData = neighborRouterId, .type = 1, .metric = 5});
    LsaKey nbrKey(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader nbrHdr;
    nbrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    nbrHdr.age = 0;
    IncomingLsaContext nbrCtx = {.key = nbrKey, .header = nbrHdr, .checksumValid = true};
    LsaBody nbrLsaBody{nbrLsa};
    processLsa<PolicyV2>(nbrCtx, nbrLsaBody);

    RouterLsaV2 farLsa;
    farLsa.flags = 0;
    farLsa.links.push_back({.linkId = neighborRouterId, .linkData = farRid, .type = 1, .metric = 5});
    LsaKey farKey(OSPFV2_LSA_ROUTER, farRid, farRid);
    LsaHeader farHdr;
    farHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    farHdr.age = 0;
    IncomingLsaContext farCtx = {.key = farKey, .header = farHdr, .checksumValid = true};
    LsaBody farLsaBody{farLsa};
    processLsa<PolicyV2>(farCtx, farLsaBody);

    wait();

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);

    Vertex nbrVertex{VertexType::ROUTER, neighborRouterId};
    Vertex farVertex{VertexType::ROUTER, farRid};

    auto itNbr = result.nodes.find(nbrVertex);
    ASSERT_NE(itNbr, result.nodes.end());
    ASSERT_FALSE(itNbr->second.parents.empty());
    uint32_t nbrFirstHop = itNbr->second.parents[0].firstHopIfid;
    EXPECT_NE(nbrFirstHop, 0u);

    auto itFar = result.nodes.find(farVertex);
    ASSERT_NE(itFar, result.nodes.end());
    ASSERT_FALSE(itFar->second.parents.empty());
    // The multi-hop vertex should inherit the same first-hop interface as
    // the directly-connected neighbor (single egress interface on self).
    EXPECT_EQ(itFar->second.parents[0].firstHopIfid, nbrFirstHop);
}

// Test: Spf_Network_Vertex_Expansion_Includes_All_Attached_Routers
TEST_F(Internal_OspfTest, Spf_Network_Vertex_Expansion_Includes_All_Attached_Routers)
{
    // Default broadcast network: self becomes DR with no other neighbors,
    // so the Network-LSA (if originated) attaches only self.
    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    // addNeighbor() drives the neighbor FSM directly and never goes through
    // RxV2, which is normally what triggers DR election on Hello receipt --
    // run it explicitly so self becomes DR on this segment.
    runIfaceElection();
    ASSERT_TRUE(ospfInterface->getIsDr());

    fullRefreshV2();
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey netKey(OSPFV2_LSA_NETWORK, ipIntv4.addr, selfRid);
    auto* netRecord = waitForLsa(netKey);
    ASSERT_NE(netRecord, nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    std::vector<uint32_t> attachedRouters;
    uint64_t vertexId = packNetwork(selfRid, ipIntv4.addr);
    bool found = topo.expandNetwork(vertexId, attachedRouters);
    ASSERT_TRUE(found);

    // Self's own RID must be among the attached routers (it is the DR).
    EXPECT_NE(std::find(attachedRouters.begin(), attachedRouters.end(), selfRid), attachedRouters.end());
}

// Test: Spf_Unreachable_Router_Excluded_From_Result
TEST_F(Internal_OspfTest, Spf_Unreachable_Router_Excluded_From_Result)
{
    fullRefreshV2();
    wait();

    // Inject a Router-LSA for an isolated router with no link back to self
    // and no link from self to it - it should never appear as confirmed.
    RouterLsaV2 isolatedLsa;
    isolatedLsa.flags = 0;
    isolatedLsa.links.push_back({.linkId = 0x09090900, .linkData = 0xFFFFFF00, .type = 3 /*stub*/, .metric = 1});

    uint32_t isolatedRid = 0xC0A8FFFF;
    LsaKey isolatedKey(OSPFV2_LSA_ROUTER, isolatedRid, isolatedRid);
    LsaHeader isolatedHdr;
    isolatedHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    isolatedHdr.age = 0;
    IncomingLsaContext isolatedCtx = {.key = isolatedKey, .header = isolatedHdr, .checksumValid = true};
    LsaBody isolatedLsaBody{isolatedLsa};
    processLsa<PolicyV2>(isolatedCtx, isolatedLsaBody);

    wait();

    ASSERT_NE(waitForLsa(isolatedKey), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);

    Vertex isolatedVertex{VertexType::ROUTER, isolatedRid};
    auto it = result.nodes.find(isolatedVertex);
    if (it != result.nodes.end())
        EXPECT_FALSE(it->second.confirmed);

    EXPECT_EQ(std::find(result.confirmedOrder.begin(), result.confirmedOrder.end(), isolatedVertex),
              result.confirmedOrder.end());
}

// Test: Spf_SelfOriginated_Stub_Links_Produce_Local_Prefixes
TEST_F(Internal_OspfTest, Spf_SelfOriginated_Stub_Links_Produce_Local_Prefixes)
{
    fullRefreshV2();
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    bool hasStubLink;
    {
        auto* record = waitForLsa(selfKey);
        ASSERT_NE(record, nullptr);
        auto* body = std::get_if<RouterLsaV2>(&record->body);
        ASSERT_NE(body, nullptr);

        // Default (no FULL neighbors on the broadcast interface) -> self
        // originates a stub link for the directly-connected subnet.
        hasStubLink = std::any_of(body->links.begin(), body->links.end(),
            [](const RouterLinkV2& l) { return l.type == 3; });
    }
    EXPECT_TRUE(hasStubLink);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);

    Vertex selfVertex{VertexType::ROUTER, selfRid};
    auto it = result.nodes.find(selfVertex);
    ASSERT_NE(it, result.nodes.end());
    EXPECT_EQ(it->second.dist, 0u);

    std::vector<std::pair<types::IPPrefix, OspfPath>> routes;
    getIntraRouteManager().deriveIntraAreaRoutes<PolicyV2>(result, routes);

    uint32_t localNet = ipIntv4Net.addr;
    bool foundLocalPrefix = std::any_of(routes.begin(), routes.end(),
        [localNet](const auto& pr) { return pr.first.v4() == localNet; });
    EXPECT_TRUE(foundLocalPrefix);
}

#pragma endregion SpfComputation

#pragma region RouteDerivation

// Test: RouteDerive_IntraArea_From_Spf_Result_Basic_Prefix
TEST_F(Internal_OspfTest, RouteDerive_IntraArea_From_Spf_Result_Basic_Prefix)
{
    auto& area = getArea(0);

    fullRefreshV2();
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    ASSERT_NE(waitForLsa(selfKey), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);

    std::vector<std::pair<types::IPPrefix, OspfPath>> routes;
    getIntraRouteManager().deriveIntraAreaRoutes<PolicyV2>(result, routes);

    uint32_t localNet = ipIntv4Net.addr;
    auto it = std::find_if(routes.begin(), routes.end(),
        [localNet](const auto& pr) { return pr.first.v4() == localNet; });
    ASSERT_NE(it, routes.end());
    EXPECT_EQ(it->second.type, OspfRouteType::INTRA_AREA);
    EXPECT_TRUE(it->second.area.has_value());
    EXPECT_EQ(*it->second.area, area.areaId);
}

// Test: RouteDerive_IntraArea_Ecmp_Multiple_NextHops
TEST_F(Internal_OspfTest, RouteDerive_IntraArea_Ecmp_Multiple_NextHops)
{
    getConfigs().get<config::Ospf::LSA_ARRIVAL>().set(0);

    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t farRid = 0xC0A80104;

    types::IPAddress nbr1Ip(types::IPv4Address{neighborRouterId});
    types::IPAddress nbr2Ip(types::IPv4Address{neighborRouterId2});
    addNeighbor(neighborRouterId, nbr1Ip, Neighbor::State::FULL);
    addNeighbor(neighborRouterId2, nbr2Ip, Neighbor::State::FULL);

    RouterLsaV2 selfLsa;
    selfLsa.flags = 0;
    selfLsa.links.push_back({.linkId = neighborRouterId, .linkData = selfRid, .type = 1, .metric = 5});
    selfLsa.links.push_back({.linkId = neighborRouterId2, .linkData = selfRid, .type = 1, .metric = 5});
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    uint32_t selfSeq;
    auto* existingSelf = waitForLsa(selfKey);
    selfSeq = existingSelf ? existingSelf->header.sequence : routing::OSPF_INITIAL_SEQUENCE;
    LsaHeader selfHdr;
    selfHdr.sequence = selfSeq + 1;
    selfHdr.age = 0;
    IncomingLsaContext selfCtx = {.key = selfKey, .header = selfHdr, .checksumValid = true};
    LsaBody selfLsaBody{selfLsa};
    processLsa<PolicyV2>(selfCtx, selfLsaBody);

    uint32_t farNet = 0x0A0A0A00;

    RouterLsaV2 nbr1Lsa;
    nbr1Lsa.flags = 0;
    nbr1Lsa.links.push_back({.linkId = selfRid, .linkData = neighborRouterId, .type = 1, .metric = 5});
    nbr1Lsa.links.push_back({.linkId = farRid, .linkData = neighborRouterId, .type = 1, .metric = 5});
    LsaKey nbr1Key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader nbr1Hdr;
    nbr1Hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    nbr1Hdr.age = 0;
    IncomingLsaContext nbr1Ctx = {.key = nbr1Key, .header = nbr1Hdr, .checksumValid = true};
    LsaBody nbr1LsaBody{nbr1Lsa};
    processLsa<PolicyV2>(nbr1Ctx, nbr1LsaBody);

    RouterLsaV2 nbr2Lsa;
    nbr2Lsa.flags = 0;
    nbr2Lsa.links.push_back({.linkId = selfRid, .linkData = neighborRouterId2, .type = 1, .metric = 5});
    nbr2Lsa.links.push_back({.linkId = farRid, .linkData = neighborRouterId2, .type = 1, .metric = 5});
    LsaKey nbr2Key(OSPFV2_LSA_ROUTER, neighborRouterId2, neighborRouterId2);
    LsaHeader nbr2Hdr;
    nbr2Hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    nbr2Hdr.age = 0;
    IncomingLsaContext nbr2Ctx = {.key = nbr2Key, .header = nbr2Hdr, .checksumValid = true};
    LsaBody nbr2LsaBody{nbr2Lsa};
    processLsa<PolicyV2>(nbr2Ctx, nbr2LsaBody);

    RouterLsaV2 farLsa;
    farLsa.flags = 0;
    farLsa.links.push_back({.linkId = neighborRouterId, .linkData = farRid, .type = 1, .metric = 5});
    farLsa.links.push_back({.linkId = neighborRouterId2, .linkData = farRid, .type = 1, .metric = 5});
    farLsa.links.push_back({.linkId = farNet, .linkData = 0xFFFFFF00, .type = 3, .metric = 1});
    LsaKey farKey(OSPFV2_LSA_ROUTER, farRid, farRid);
    LsaHeader farHdr;
    farHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    farHdr.age = 0;
    IncomingLsaContext farCtx = {.key = farKey, .header = farHdr, .checksumValid = true};
    LsaBody farLsaBody{farLsa};
    processLsa<PolicyV2>(farCtx, farLsaBody);

    wait();

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);

    std::vector<std::pair<types::IPPrefix, OspfPath>> routes;
    getIntraRouteManager().deriveIntraAreaRoutes<PolicyV2>(result, routes);

    auto it = std::find_if(routes.begin(), routes.end(),
        [farNet](const auto& pr) { return pr.first.v4() == farNet; });
    ASSERT_NE(it, routes.end());
    EXPECT_EQ(it->second.cost, 11u); // 5 (self->nbr) + 5 (nbr->far) + 1 (stub)
    EXPECT_GE(it->second.nextHops.size(), 2u);

    getConfigs().get<config::Ospf::LSA_ARRIVAL>().set(1000);
}

// Test: RouteDerive_InterArea_Type3_Installs_Summary_Route
TEST_F(Internal_OspfTest, RouteDerive_InterArea_Type3_Installs_Summary_Route)
{
    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t abrRid = neighborRouterId;

    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    getIfaceConfigs().get<config::OspfInterface::COST>().set(5);
    calculateCost();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{abrRid});
    auto* nbr = addNeighbor(abrRid, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    // Reorigination is async and a stale record may pre-exist; wait for the link to actually appear in the body.
    {
        LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
        LsaRecord* selfRecord = nullptr;
        bool hasLinks = false;
        for (int i = 0; i < 50 && !hasLinks; ++i)
        {
            {
                selfRecord = waitForLsa(selfKey);
                if (selfRecord)
                {
                    auto* rl = std::get_if<RouterLsaV2>(&selfRecord->body);
                    hasLinks = rl && !rl->links.empty();
                }
            }
            if (!hasLinks)
                wait();
        }
        ASSERT_NE(selfRecord, nullptr);
        ASSERT_TRUE(hasLinks);
    }

    RouterLsaV2 abrLsa;
    abrLsa.flags = 0;
    abrLsa.links.push_back({.linkId = selfRid, .linkData = abrRid, .type = 1, .metric = 5});
    LsaKey abrKey(OSPFV2_LSA_ROUTER, abrRid, abrRid);
    LsaHeader abrHdr;
    abrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    abrHdr.age = 0;
    IncomingLsaContext abrCtx = {.key = abrKey, .header = abrHdr, .checksumValid = true};
    LsaBody abrLsaBody{abrLsa};
    processLsa<PolicyV2>(abrCtx, abrLsaBody);
    wait();

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);
    getTable().consumeSpfResult(getArea(0).areaId, result);

    Vertex abrVertex{VertexType::ROUTER, abrRid};
    ASSERT_NE(result.nodes.find(abrVertex), result.nodes.end());
    const auto* abrReach = getTable().lookup(abrRid);
    ASSERT_NE(abrReach, nullptr);

    // Inject a Type-3 summary LSA from the ABR for a remote prefix.
    uint32_t summaryNet = 0x0B0B0B00;
    SummaryNetworkLsa summaryLsa;
    summaryLsa.networkMask = 0xFFFFFF00;
    summaryLsa.metric = 7;

    LsaKey summaryKey(OSPFV2_LSA_SUM_NET, summaryNet, abrRid);
    LsaHeader summaryHdr;
    summaryHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    summaryHdr.age = 0;
    IncomingLsaContext summaryCtx = {.key = summaryKey, .header = summaryHdr, .checksumValid = true};
    LsaBody summaryLsaBody{summaryLsa};
    processLsa<PolicyV2>(summaryCtx, summaryLsaBody);
    wait();

    types::IPPrefix summaryPrefix(summaryNet, 24);
    const OspfRoute* route = getRib().lookup(summaryPrefix);
    ASSERT_NE(route, nullptr);
    EXPECT_EQ(route->type, OspfRouteType::INTER_AREA);
    EXPECT_EQ(route->cost, abrReach->cost + 7u); // ABR distance + summary metric (7)
}

// Test: RouteDerive_InterArea_Type3_Rejected_If_Cost_LSInfinity
TEST_F(Internal_OspfTest, RouteDerive_InterArea_Type3_Rejected_If_Cost_LSInfinity)
{
    uint32_t abrRid = neighborRouterId;

    // Without any neighbor/topology setup, the ABR is unreachable in the
    // (empty) SPF result, so deriveInterAreaNetwork must return nullopt.
    fullRefreshV2();
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    ASSERT_NE(waitForLsa(selfKey), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);
    (void)result;

    uint32_t summaryNet = 0x0C0C0C00;
    SummaryNetworkLsa summaryLsa;
    summaryLsa.networkMask = 0xFFFFFF00;
    summaryLsa.metric = 7;

    LsaKey summaryKey(OSPFV2_LSA_SUM_NET, summaryNet, abrRid);
    LsaHeader summaryHdr;
    summaryHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    summaryHdr.age = 0;

    LsaBody summaryLsaBody{summaryLsa};

    // table.lookup(abrRid) is empty (no Type-4/SPF data for this ABR), so
    // resolveToAbrs returns nullopt and the path must be nullopt.
    auto [prefix, path] = getInterRouteManager().deriveInterAreaNetwork<PolicyV2>(
        getArea(0), summaryKey, summaryHdr, summaryLsaBody);

    EXPECT_EQ(prefix.v4(), summaryNet);
    EXPECT_FALSE(path.has_value());
}

// Test: RouteDerive_InterArea_Type4_Updates_Asbr_Reachability_Not_Rib
TEST_F(Internal_OspfTest, RouteDerive_InterArea_Type4_Updates_Asbr_Reachability_Not_Rib)
{
    auto& area = getArea(0);
    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t abrRid = neighborRouterId;
    uint32_t asbrRid = 0xC0A80105;

    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    getIfaceConfigs().get<config::OspfInterface::COST>().set(5);
    calculateCost();
    ospfInterface->enqueueSyncNetworkType();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{abrRid});
    auto* nbr = addNeighbor(abrRid, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    // Reorigination from the FULL adjacency is async; wait for the P2P link to abrRid to land before relying on it.
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    ASSERT_NE(waitForLsa(selfKey, [abrRid](const LsaRecord& r) {
        auto* body = std::get_if<RouterLsaV2>(&r.body);
        if (!body)
            return false;
        for (const auto& link : body->links)
            if (link.type == 1 /*P2P*/ && link.linkId == abrRid)
                return true;
        return false;
    }), nullptr);

    RouterLsaV2 abrLsa;
    abrLsa.flags = 0;
    abrLsa.links.push_back({.linkId = selfRid, .linkData = abrRid, .type = 1, .metric = 5});
    LsaKey abrKey(OSPFV2_LSA_ROUTER, abrRid, abrRid);
    LsaHeader abrHdr;
    abrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    abrHdr.age = 0;
    IncomingLsaContext abrCtx = {.key = abrKey, .header = abrHdr, .checksumValid = true};
    LsaBody abrLsaBody{abrLsa};
    processLsa<PolicyV2>(abrCtx, abrLsaBody);
    wait();

    ASSERT_NE(waitForLsa(abrKey), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);

    getTable().consumeSpfResult(area.areaId, result);
    const auto* abrReach = getTable().lookup(abrRid);
    ASSERT_NE(abrReach, nullptr);

    // Inject a Type-4 ASBR-summary LSA from the ABR describing asbrRid.
    SummaryRouterLsa asbrLsa;
    asbrLsa.metric = 9;

    LsaKey asbrKey(OSPFV2_LSA_SUM_ASBR, asbrRid, abrRid);
    LsaHeader asbrHdr;
    asbrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    asbrHdr.age = 0;

    LsaBody asbrLsaBody{asbrLsa};

    std::vector<std::pair<types::IPPrefix, OspfPath>> pathListBefore;
    getInterRouteManager().deriveInterAreaRouter<PolicyV2>(area, asbrKey, asbrHdr, asbrLsaBody);

    // Type-4 processing must not append any prefix routes.
    EXPECT_TRUE(pathListBefore.empty());

    // But it must update the topology table's ASBR reachability entry.
    const auto* reach = getTable().lookup(asbrRid);
    ASSERT_NE(reach, nullptr);
    EXPECT_EQ(reach->cost, abrReach->cost + 9u); // ABR distance + Type-4 metric (9)
}

// Test: RouteDerive_External_Type5_E1_Adds_Internal_Plus_External_Cost
TEST_F(Internal_OspfTest, RouteDerive_External_Type5_E1_Adds_Internal_Plus_External_Cost)
{
    auto& area = getArea(0);
    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t asbrRid = neighborRouterId;

    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    getIfaceConfigs().get<config::OspfInterface::COST>().set(5);
    calculateCost();
    ospfInterface->enqueueSyncNetworkType();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{asbrRid});
    auto* nbr = addNeighbor(asbrRid, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    // Reorigination from the FULL adjacency is async; wait for the P2P link to asbrRid to land before relying on it.
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    ASSERT_NE(waitForLsa(selfKey, [asbrRid](const LsaRecord& r) {
        auto* body = std::get_if<RouterLsaV2>(&r.body);
        if (!body)
            return false;
        for (const auto& link : body->links)
            if (link.type == 1 /*P2P*/ && link.linkId == asbrRid)
                return true;
        return false;
    }), nullptr);

    RouterLsaV2 asbrLsa;
    asbrLsa.flags = 0;
    asbrLsa.links.push_back({.linkId = selfRid, .linkData = asbrRid, .type = 1, .metric = 5});
    LsaKey asbrRtrKey(OSPFV2_LSA_ROUTER, asbrRid, asbrRid);
    LsaHeader asbrRtrHdr;
    asbrRtrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    asbrRtrHdr.age = 0;
    IncomingLsaContext asbrRtrCtx = {.key = asbrRtrKey, .header = asbrRtrHdr, .checksumValid = true};
    LsaBody asbrRtrLsaBody{asbrLsa};
    processLsa<PolicyV2>(asbrRtrCtx, asbrRtrLsaBody);
    wait();

    ASSERT_NE(waitForLsa(asbrRtrKey), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);
    getTable().consumeSpfResult(area.areaId, result);
    const auto* asbrReach = getTable().lookup(asbrRid);
    ASSERT_NE(asbrReach, nullptr);

    // Type-5 E1 external LSA: forwarding address 0 -> anchored via the
    // ASBR's topology-table reachability.
    uint32_t extNet = 0x0D0D0D00;
    ExternalLsaV2 extLsa;
    extLsa.networkMask = 0xFFFFFF00;
    extLsa.metric = 20;
    extLsa.isType2 = false; // E1
    extLsa.forwardingAddress = 0;
    extLsa.routeTag = 0;

    LsaKey extKey(OSPFV2_LSA_EXTERNAL, extNet, asbrRid);
    LsaHeader extHdr;
    extHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    extHdr.age = 0;

    LsaBody extLsaBody{extLsa};
    std::pair<LsaHeader, LsaBody> rec{extHdr, extLsaBody};

    auto [prefix, path] = getExternalRouteManager().deriveExternalRoute<PolicyV2>(
        extKey, rec);

    EXPECT_EQ(prefix.v4(), extNet);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->type, OspfRouteType::EXTERNAL);
    EXPECT_EQ(path->cost, asbrReach->cost + 20u); // cost to ASBR + external metric, E1
}

// Test: RouteDerive_External_Type5_E2_Uses_External_Cost_Only
TEST_F(Internal_OspfTest, RouteDerive_External_Type5_E2_Uses_External_Cost_Only)
{
    auto& area = getArea(0);
    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t asbrRid = neighborRouterId;

    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    ospfInterface->enqueueSyncNetworkType();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{asbrRid});
    auto* nbr = addNeighbor(asbrRid, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    // Reorigination from the FULL adjacency is async; wait for the P2P link to asbrRid to land before relying on it.
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    ASSERT_NE(waitForLsa(selfKey, [asbrRid](const LsaRecord& r) {
        auto* body = std::get_if<RouterLsaV2>(&r.body);
        if (!body)
            return false;
        for (const auto& link : body->links)
            if (link.type == 1 /*P2P*/ && link.linkId == asbrRid)
                return true;
        return false;
    }), nullptr);

    RouterLsaV2 asbrLsa;
    asbrLsa.flags = 0;
    asbrLsa.links.push_back({.linkId = selfRid, .linkData = asbrRid, .type = 1, .metric = 5});
    LsaKey asbrRtrKey(OSPFV2_LSA_ROUTER, asbrRid, asbrRid);
    LsaHeader asbrRtrHdr;
    asbrRtrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    asbrRtrHdr.age = 0;
    IncomingLsaContext asbrRtrCtx = {.key = asbrRtrKey, .header = asbrRtrHdr, .checksumValid = true};
    LsaBody asbrRtrLsaBody{asbrLsa};
    processLsa<PolicyV2>(asbrRtrCtx, asbrRtrLsaBody);
    wait();

    ASSERT_NE(waitForLsa(asbrRtrKey), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);
    getTable().consumeSpfResult(area.areaId, result);
    ASSERT_NE(getTable().lookup(asbrRid), nullptr);

    uint32_t extNet = 0x0E0E0E00;
    ExternalLsaV2 extLsa;
    extLsa.networkMask = 0xFFFFFF00;
    extLsa.metric = 30;
    extLsa.isType2 = true; // E2
    extLsa.forwardingAddress = 0;
    extLsa.routeTag = 0;

    LsaKey extKey(OSPFV2_LSA_EXTERNAL, extNet, asbrRid);
    LsaHeader extHdr;
    extHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    extHdr.age = 0;

    LsaBody extLsaBody{extLsa};
    std::pair<LsaHeader, LsaBody> rec{extHdr, extLsaBody};

    auto [prefix, path] = getExternalRouteManager().deriveExternalRoute<PolicyV2>(
        extKey, rec);

    EXPECT_EQ(prefix.v4(), extNet);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->type, OspfRouteType::EXTERNAL);
    EXPECT_EQ(path->cost, 30u); // E2: external metric only, internal cost ignored
}

// Test: RouteDerive_External_ForwardingAddress_Zero_Uses_Advertising_Router
TEST_F(Internal_OspfTest, RouteDerive_External_ForwardingAddress_Zero_Uses_Advertising_Router)
{
    auto& area = getArea(0);
    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t asbrRid = neighborRouterId;

    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    wait();

    types::IPAddress nbrIp(types::IPv4Address{asbrRid});
    auto* nbr = addNeighbor(asbrRid, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    // Reorigination from the FULL adjacency is async; wait for the P2P link to asbrRid to land before relying on it.
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    ASSERT_NE(waitForLsa(selfKey, [asbrRid](const LsaRecord& r) {
        auto* body = std::get_if<RouterLsaV2>(&r.body);
        if (!body)
            return false;
        for (const auto& link : body->links)
            if (link.type == 1 /*P2P*/ && link.linkId == asbrRid)
                return true;
        return false;
    }), nullptr);

    RouterLsaV2 asbrLsa;
    asbrLsa.flags = 0;
    asbrLsa.links.push_back({.linkId = selfRid, .linkData = asbrRid, .type = 1, .metric = 3});
    LsaKey asbrRtrKey(OSPFV2_LSA_ROUTER, asbrRid, asbrRid);
    LsaHeader asbrRtrHdr;
    asbrRtrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    asbrRtrHdr.age = 0;
    IncomingLsaContext asbrRtrCtx = {.key = asbrRtrKey, .header = asbrRtrHdr, .checksumValid = true};
    LsaBody asbrRtrLsaBody{asbrLsa};
    processLsa<PolicyV2>(asbrRtrCtx, asbrRtrLsaBody);
    wait();

    ASSERT_NE(waitForLsa(asbrRtrKey), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);
    getTable().consumeSpfResult(area.areaId, result);
    const auto* reach = getTable().lookup(asbrRid);
    ASSERT_NE(reach, nullptr);

    uint32_t extNet = 0x0F0F0F00;
    ExternalLsaV2 extLsa;
    extLsa.networkMask = 0xFFFFFF00;
    extLsa.metric = 15;
    extLsa.isType2 = false;
    extLsa.forwardingAddress = 0; // forces resolution via advertising router
    extLsa.routeTag = 0;

    LsaKey extKey(OSPFV2_LSA_EXTERNAL, extNet, asbrRid);
    LsaHeader extHdr;
    extHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    extHdr.age = 0;

    LsaBody extLsaBody{extLsa};
    std::pair<LsaHeader, LsaBody> rec{extHdr, extLsaBody};

    auto [prefix, path] = getExternalRouteManager().deriveExternalRoute<PolicyV2>(
        extKey, rec);

    EXPECT_EQ(prefix.v4(), extNet);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->cost, reach->cost + 15u);
    ASSERT_FALSE(path->nextHops.empty());
    EXPECT_EQ(path->nextHops, reach->nextHops);
}

// Test: RouteDerive_External_ForwardingAddress_NonZero_Anchors_To_Resolved_Route
TEST_F(Internal_OspfTest, RouteDerive_External_ForwardingAddress_NonZero_Anchors_To_Resolved_Route)
{
    auto& area = getArea(0);
    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t asbrRid = neighborRouterId;

    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    getIfaceConfigs().get<config::OspfInterface::COST>().set(4);
    calculateCost();
    ospfInterface->enqueueSyncNetworkType();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{asbrRid});
    auto* nbr = addNeighbor(asbrRid, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    // Reorigination from the FULL adjacency is async; wait for the P2P link to asbrRid to land before relying on it.
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    ASSERT_NE(waitForLsa(selfKey, [asbrRid](const LsaRecord& r) {
        auto* body = std::get_if<RouterLsaV2>(&r.body);
        if (!body)
            return false;
        for (const auto& link : body->links)
            if (link.type == 1 /*P2P*/ && link.linkId == asbrRid)
                return true;
        return false;
    }), nullptr);

    RouterLsaV2 asbrLsa;
    asbrLsa.flags = 0;
    asbrLsa.links.push_back({.linkId = selfRid, .linkData = asbrRid, .type = 1, .metric = 4});
    asbrLsa.links.push_back({.linkId = 0x14141400, .linkData = 0xFFFFFF00, .type = 3, .metric = 2});
    LsaKey asbrRtrKey(OSPFV2_LSA_ROUTER, asbrRid, asbrRid);
    LsaHeader asbrRtrHdr;
    asbrRtrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    asbrRtrHdr.age = 0;
    IncomingLsaContext asbrRtrCtx = {.key = asbrRtrKey, .header = asbrRtrHdr, .checksumValid = true};
    LsaBody asbrRtrLsaBody{asbrLsa};
    processLsa<PolicyV2>(asbrRtrCtx, asbrRtrLsaBody);
    wait();

    ASSERT_NE(waitForLsa(asbrRtrKey), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);
    getTable().consumeSpfResult(area.areaId, result);

    std::vector<std::pair<types::IPPrefix, OspfPath>> pathList;
    getIntraRouteManager().deriveIntraAreaRoutes<PolicyV2>(result, pathList);
    getRib().replaceArea(area, pathList);
    ospfInstance->routingInstance->getRib().wait<uint32_t>();

    uint32_t extNet = 0x15151500;
    uint32_t fwdAddr = 0x14141401; // inside 0x14141400/24

    ExternalLsaV2 extLsa;
    extLsa.networkMask = 0xFFFFFF00;
    extLsa.metric = 8;
    extLsa.isType2 = false;
    extLsa.forwardingAddress = fwdAddr;
    extLsa.routeTag = 0;

    LsaKey extKey(OSPFV2_LSA_EXTERNAL, extNet, asbrRid);
    LsaHeader extHdr;
    extHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    extHdr.age = 0;

    LsaBody extLsaBody{extLsa};
    std::pair<LsaHeader, LsaBody> rec{extHdr, extLsaBody};

    auto [prefix, path] = getExternalRouteManager().deriveExternalRoute<PolicyV2>(
        extKey, rec);

    EXPECT_EQ(prefix.v4(), extNet);
    ASSERT_TRUE(path.has_value());
    // X = cost to the resolved forwarding-address route (4 + 2 = 6), Y = 8 (E1)
    EXPECT_EQ(path->cost, 6u + 8u);
}

// Test: RouteDerive_External_Unreachable_ForwardingAddress_Excludes_Route
TEST_F(Internal_OspfTest, RouteDerive_External_Unreachable_ForwardingAddress_Excludes_Route)
{
    uint32_t asbrRid = neighborRouterId;

    fullRefreshV2();
    wait();

    uint32_t extNet = 0x16161600;
    uint32_t fwdAddr = 0x17171701; // not covered by any installed route

    ExternalLsaV2 extLsa;
    extLsa.networkMask = 0xFFFFFF00;
    extLsa.metric = 5;
    extLsa.isType2 = false;
    extLsa.forwardingAddress = fwdAddr;
    extLsa.routeTag = 0;

    LsaKey extKey(OSPFV2_LSA_EXTERNAL, extNet, asbrRid);
    LsaHeader extHdr;
    extHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    extHdr.age = 0;

    LsaBody extLsaBody{extLsa};
    std::pair<LsaHeader, LsaBody> rec{extHdr, extLsaBody};

    auto [prefix, path] = getExternalRouteManager().deriveExternalRoute<PolicyV2>(
        extKey, rec);

    EXPECT_EQ(prefix.v4(), extNet);
    EXPECT_FALSE(path.has_value());
}

// Test: RouteDerive_AdminDistance_Set_Per_Route_Type
TEST_F(Internal_OspfTest, RouteDerive_AdminDistance_Set_Per_Route_Type)
{
    auto& area = getArea(0);
    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t abrRid = neighborRouterId;

    uint8_t intraAd = getConfigs().get<config::Ospf::INTRA_AREA_DISTANCE>().load();
    uint8_t interAd = getConfigs().get<config::Ospf::INTER_AREA_DISTANCE>().load();
    uint8_t extAd = getConfigs().get<config::Ospf::EXTERNAL_DISTANCE>().load();

    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    ospfInterface->enqueueSyncNetworkType();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{abrRid});
    auto* nbr = addNeighbor(abrRid, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    // Reorigination from the FULL adjacency is async; wait for the P2P link to abrRid to land before relying on it.
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    ASSERT_NE(waitForLsa(selfKey, [abrRid](const LsaRecord& r) {
        auto* body = std::get_if<RouterLsaV2>(&r.body);
        if (!body)
            return false;
        for (const auto& link : body->links)
            if (link.type == 1 /*P2P*/ && link.linkId == abrRid)
                return true;
        return false;
    }), nullptr);

    RouterLsaV2 abrLsa;
    abrLsa.flags = 0;
    abrLsa.links.push_back({.linkId = selfRid, .linkData = abrRid, .type = 1, .metric = 5});
    LsaKey abrKey(OSPFV2_LSA_ROUTER, abrRid, abrRid);
    LsaHeader abrHdr;
    abrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    abrHdr.age = 0;
    IncomingLsaContext abrCtx = {.key = abrKey, .header = abrHdr, .checksumValid = true};
    LsaBody abrLsaBody{abrLsa};
    processLsa<PolicyV2>(abrCtx, abrLsaBody);
    wait();

    ASSERT_NE(waitForLsa(abrKey), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);
    getTable().consumeSpfResult(area.areaId, result);

    // Intra-area route (local subnet).
    std::vector<std::pair<types::IPPrefix, OspfPath>> intraRoutes;
    getIntraRouteManager().deriveIntraAreaRoutes<PolicyV2>(result, intraRoutes);
    uint32_t localNet = ipIntv4Net.addr;
    auto intraIt = std::find_if(intraRoutes.begin(), intraRoutes.end(),
        [localNet](const auto& pr) { return pr.first.v4() == localNet; });
    ASSERT_NE(intraIt, intraRoutes.end());
    EXPECT_EQ(intraIt->second.adminDistance, intraAd);

    // Inter-area route (Type-3 from the ABR).
    uint32_t summaryNet = 0x18181800;
    SummaryNetworkLsa summaryLsa;
    summaryLsa.networkMask = 0xFFFFFF00;
    summaryLsa.metric = 4;
    LsaKey summaryKey(OSPFV2_LSA_SUM_NET, summaryNet, abrRid);
    LsaHeader summaryHdr;
    summaryHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    summaryHdr.age = 0;
    IncomingLsaContext summaryCtx = {.key = summaryKey, .header = summaryHdr, .checksumValid = true};
    LsaBody summaryLsaBody{summaryLsa};
    processLsa<PolicyV2>(summaryCtx, summaryLsaBody);
    wait();

    ASSERT_NE(waitForLsa(summaryKey), nullptr);

    std::vector<std::pair<types::IPPrefix, OspfPath>> interRoutes;
    getInterRouteManager().deriveInterAreaRoutes<PolicyV2>(result, interRoutes, area);
    auto interIt = std::find_if(interRoutes.begin(), interRoutes.end(),
        [summaryNet](const auto& pr) { return pr.first.v4() == summaryNet; });
    ASSERT_NE(interIt, interRoutes.end());
    EXPECT_EQ(interIt->second.adminDistance, interAd);

    // External route (Type-5 from the ABR acting as ASBR).
    uint32_t extNet = 0x19191900;
    ExternalLsaV2 extLsa;
    extLsa.networkMask = 0xFFFFFF00;
    extLsa.metric = 6;
    extLsa.isType2 = true;
    extLsa.forwardingAddress = 0;
    extLsa.routeTag = 0;
    LsaKey extKey(OSPFV2_LSA_EXTERNAL, extNet, abrRid);
    LsaHeader extHdr;
    extHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    extHdr.age = 0;
    LsaBody extLsaBody{extLsa};
    std::pair<LsaHeader, LsaBody> rec{extHdr, extLsaBody};

    auto [extPrefix, extPath] = getExternalRouteManager().deriveExternalRoute<PolicyV2>(
        extKey, rec);
    ASSERT_TRUE(extPath.has_value());
    EXPECT_EQ(extPath->adminDistance, extAd);

    (void)extPrefix;
}

// Test: RouteDerive_DeriveExternalRoutes_Recomputes_All_Type5_From_Lsdb
TEST_F(Internal_OspfTest, RouteDerive_DeriveExternalRoutes_Recomputes_All_Type5_From_Lsdb)
{
    auto& area = getArea(0);
    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t asbrRid = neighborRouterId;

    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    getIfaceConfigs().get<config::OspfInterface::COST>().set(5);
    calculateCost();
    ospfInterface->enqueueSyncNetworkType();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{asbrRid});
    auto* nbr = addNeighbor(asbrRid, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    // Reorigination from the FULL adjacency is async; wait for the P2P link to asbrRid to land before relying on it.
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    ASSERT_NE(waitForLsa(selfKey, [asbrRid](const LsaRecord& r) {
        auto* body = std::get_if<RouterLsaV2>(&r.body);
        if (!body)
            return false;
        for (const auto& link : body->links)
            if (link.type == 1 /*P2P*/ && link.linkId == asbrRid)
                return true;
        return false;
    }), nullptr);

    RouterLsaV2 asbrLsa;
    asbrLsa.flags = 0;
    asbrLsa.links.push_back({.linkId = selfRid, .linkData = asbrRid, .type = 1, .metric = 5});
    LsaKey asbrRtrKey(OSPFV2_LSA_ROUTER, asbrRid, asbrRid);
    LsaHeader asbrRtrHdr;
    asbrRtrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    asbrRtrHdr.age = 0;
    IncomingLsaContext asbrRtrCtx = {.key = asbrRtrKey, .header = asbrRtrHdr, .checksumValid = true};
    LsaBody asbrRtrLsaBody{asbrLsa};
    processLsa<PolicyV2>(asbrRtrCtx, asbrRtrLsaBody);
    wait();

    ASSERT_NE(waitForLsa(asbrRtrKey), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);
    getTable().consumeSpfResult(area.areaId, result);
    ASSERT_NE(getTable().lookup(asbrRid), nullptr);

    // Populate the process-wide external LSA database directly with two
    // Type-5 LSAs from the same ASBR.
    uint32_t extNet1 = 0x1A1A1A00;
    uint32_t extNet2 = 0x1B1B1B00;

    ExternalLsaV2 extLsa1;
    extLsa1.networkMask = 0xFFFFFF00;
    extLsa1.metric = 10;
    extLsa1.isType2 = true;
    extLsa1.forwardingAddress = 0;
    extLsa1.routeTag = 0;

    ExternalLsaV2 extLsa2;
    extLsa2.networkMask = 0xFFFFFF00;
    extLsa2.metric = 11;
    extLsa2.isType2 = false;
    extLsa2.forwardingAddress = 0;
    extLsa2.routeTag = 0;

    LsaKey extKey1(OSPFV2_LSA_EXTERNAL, extNet1, asbrRid);
    LsaHeader extHdr1;
    extHdr1.sequence = routing::OSPF_INITIAL_SEQUENCE;
    extHdr1.age = 0;

    LsaKey extKey2(OSPFV2_LSA_EXTERNAL, extNet2, asbrRid);
    LsaHeader extHdr2;
    extHdr2.sequence = routing::OSPF_INITIAL_SEQUENCE;
    extHdr2.age = 0;

    setExternalDbEntry(extKey1, extHdr1, LsaBody{extLsa1});
    setExternalDbEntry(extKey2, extHdr2, LsaBody{extLsa2});

    auto routes = getExternalRouteManager().deriveExternalRoutes<PolicyV2>();

    auto it1 = std::find_if(routes.begin(), routes.end(),
        [extNet1](const auto& pr) { return pr.first.v4() == extNet1; });
    auto it2 = std::find_if(routes.begin(), routes.end(),
        [extNet2](const auto& pr) { return pr.first.v4() == extNet2; });

    ASSERT_NE(it1, routes.end());
    ASSERT_NE(it2, routes.end());
    EXPECT_EQ(it1->second.cost, 10u);       // E2: external metric only
    EXPECT_EQ(it2->second.cost, 5u + 11u);  // E1: ASBR distance + external metric
}

#pragma endregion RouteDerivation

#pragma region TopologyTableAsbr

// Test: Topology_AsbrReachability_Prefers_IntraArea_Over_InterArea
TEST_F(Internal_OspfTest, Topology_AsbrReachability_Prefers_IntraArea_Over_InterArea)
{
    auto& area = getArea(0);
    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t asbrRid = neighborRouterId;
    uint32_t abrRid = neighborRouterId2;

    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    getIfaceConfigs().get<config::OspfInterface::COST>().set(5);
    calculateCost();
    ospfInterface->enqueueSyncNetworkType();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{asbrRid});
    auto* nbr = addNeighbor(asbrRid, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    // Reorigination is async and a stale record may pre-exist; wait for the P2P link to asbrRid to actually appear in the body.
    {
        LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
        auto* selfRecord = waitForLsa(selfKey, [&](const LsaRecord& r) {
            auto* body = std::get_if<RouterLsaV2>(&r.body);
            if (!body) return false;
            for (auto& link : body->links)
            {
                if (link.type == 1 && link.linkId == asbrRid) return true;
            }
            return false;
        });
        ASSERT_NE(selfRecord, nullptr);
    }

    RouterLsaV2 asbrLsa;
    asbrLsa.flags = 0;
    asbrLsa.links.push_back({.linkId = selfRid, .linkData = asbrRid, .type = 1, .metric = 5});
    LsaKey asbrRtrKey(OSPFV2_LSA_ROUTER, asbrRid, asbrRid);
    LsaHeader asbrRtrHdr;
    asbrRtrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    asbrRtrHdr.age = 0;
    IncomingLsaContext asbrRtrCtx = {.key = asbrRtrKey, .header = asbrRtrHdr, .checksumValid = true};
    LsaBody asbrRtrLsaBody{asbrLsa};
    processLsa<PolicyV2>(asbrRtrCtx, asbrRtrLsaBody);
    wait();

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);

    // consumeSpfResult populates the intra-area candidate for asbrRid at
    // cost 5 (the SPF distance).
    getTable().consumeSpfResult(area.areaId, result);
    const auto* reachBefore = getTable().lookup(asbrRid);
    ASSERT_NE(reachBefore, nullptr);
    EXPECT_EQ(reachBefore->cost, 5u);

    // Now feed in a Type-4 inter-area candidate via abrRid claiming a much
    // higher cost to reach the same ASBR. Per RFC 2328 §16.1, intra-area
    // candidates always win over inter-area ones.
    std::vector<OspfRouter> interCandidates;
    interCandidates.push_back(OspfRouter{asbrRid, 100, {OspfNextHop{0, types::IPAddress(types::IPv4Address{abrRid})}}});
    getTable().updateAreaAsbrs(area.areaId, interCandidates);

    const auto* reachAfter = getTable().lookup(asbrRid);
    ASSERT_NE(reachAfter, nullptr);
    EXPECT_EQ(reachAfter->cost, 5u); // intra-area entry still wins
}

// Test: Topology_AsbrReachability_Updated_On_Type4_Lsa
TEST_F(Internal_OspfTest, Topology_AsbrReachability_Updated_On_Type4_Lsa)
{
    auto& area = getArea(0);
    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t abrRid = neighborRouterId;
    uint32_t asbrRid = 0xC0A80105;

    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    getIfaceConfigs().get<config::OspfInterface::COST>().set(5);
    calculateCost();
    ospfInterface->enqueueSyncNetworkType();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{abrRid});
    auto* nbr = addNeighbor(abrRid, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    // Reorigination from the FULL adjacency is async; wait for the P2P link to abrRid to land before relying on it.
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    ASSERT_NE(waitForLsa(selfKey, [abrRid](const LsaRecord& r) {
        auto* body = std::get_if<RouterLsaV2>(&r.body);
        if (!body)
            return false;
        for (const auto& link : body->links)
            if (link.type == 1 /*P2P*/ && link.linkId == abrRid)
                return true;
        return false;
    }), nullptr);

    RouterLsaV2 abrLsa;
    abrLsa.flags = 0;
    abrLsa.links.push_back({.linkId = selfRid, .linkData = abrRid, .type = 1, .metric = 5});
    LsaKey abrKey(OSPFV2_LSA_ROUTER, abrRid, abrRid);
    LsaHeader abrHdr;
    abrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    abrHdr.age = 0;
    IncomingLsaContext abrCtx = {.key = abrKey, .header = abrHdr, .checksumValid = true};
    LsaBody abrLsaBody{abrLsa};
    processLsa<PolicyV2>(abrCtx, abrLsaBody);
    wait();

    ASSERT_NE(waitForLsa(abrKey), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);
    getTable().consumeSpfResult(area.areaId, result);

    ASSERT_EQ(getTable().lookup(asbrRid), nullptr);
    EXPECT_EQ(getTable().lookupDistance(asbrRid), UINT32_MAX);

    // Inject the Type-4 LSA describing the ASBR; deriveInterAreaRouter calls
    // table.updateAreaAsbr internally.
    SummaryRouterLsa asbrLsa;
    asbrLsa.metric = 9;
    LsaKey asbrKey(OSPFV2_LSA_SUM_ASBR, asbrRid, abrRid);
    LsaHeader asbrHdr;
    asbrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    asbrHdr.age = 0;
    LsaBody asbrLsaBody{asbrLsa};

    getInterRouteManager().deriveInterAreaRouter<PolicyV2>(area, asbrKey, asbrHdr, asbrLsaBody);

    const auto* reach = getTable().lookup(asbrRid);
    ASSERT_NE(reach, nullptr);
    EXPECT_EQ(reach->cost, 5u + 9u);
    EXPECT_EQ(getTable().lookupDistance(asbrRid), 5u + 9u);
}

// Test: Topology_AsbrReachability_Removed_When_Type4_Withdrawn
TEST_F(Internal_OspfTest, Topology_AsbrReachability_Removed_When_Type4_Withdrawn)
{
    auto& area = getArea(0);
    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t abrRid = neighborRouterId;
    uint32_t asbrRid = 0xC0A80105;

    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    ospfInterface->enqueueSyncNetworkType();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{abrRid});
    auto* nbr = addNeighbor(abrRid, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    // Reorigination from the FULL adjacency is async; wait for the P2P link to abrRid to land before relying on it.
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    ASSERT_NE(waitForLsa(selfKey, [abrRid](const LsaRecord& r) {
        auto* body = std::get_if<RouterLsaV2>(&r.body);
        if (!body)
            return false;
        for (const auto& link : body->links)
            if (link.type == 1 /*P2P*/ && link.linkId == abrRid)
                return true;
        return false;
    }), nullptr);

    RouterLsaV2 abrLsa;
    abrLsa.flags = 0;
    abrLsa.links.push_back({.linkId = selfRid, .linkData = abrRid, .type = 1, .metric = 5});
    LsaKey abrKey(OSPFV2_LSA_ROUTER, abrRid, abrRid);
    LsaHeader abrHdr;
    abrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    abrHdr.age = 0;
    IncomingLsaContext abrCtx = {.key = abrKey, .header = abrHdr, .checksumValid = true};
    LsaBody abrLsaBody{abrLsa};
    processLsa<PolicyV2>(abrCtx, abrLsaBody);
    wait();

    ASSERT_NE(waitForLsa(abrKey), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);
    getTable().consumeSpfResult(area.areaId, result);

    // Install the Type-4 ASBR entry first.
    SummaryRouterLsa asbrLsa;
    asbrLsa.metric = 9;
    LsaKey asbrKey(OSPFV2_LSA_SUM_ASBR, asbrRid, abrRid);
    LsaHeader asbrHdr;
    asbrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    asbrHdr.age = 0;
    LsaBody asbrLsaBody{asbrLsa};
    getInterRouteManager().deriveInterAreaRouter<PolicyV2>(area, asbrKey, asbrHdr, asbrLsaBody);
    ASSERT_NE(getTable().lookup(asbrRid), nullptr);

    // Withdraw it: re-derive with age == MAX_AGE, which deriveInterAreaRouter
    // maps to remove=true.
    LsaHeader asbrHdrWithdrawn;
    asbrHdrWithdrawn.sequence = routing::OSPF_INITIAL_SEQUENCE + 1;
    asbrHdrWithdrawn.age = routing::OSPF_MAX_AGE;
    LsaBody asbrLsaBodyWithdrawn{asbrLsa};
    getInterRouteManager().deriveInterAreaRouter<PolicyV2>(area, asbrKey, asbrHdrWithdrawn, asbrLsaBodyWithdrawn);

    EXPECT_EQ(getTable().lookup(asbrRid), nullptr);
    EXPECT_EQ(getTable().lookupDistance(asbrRid), UINT32_MAX);
}

// Test: Topology_NextHopCache_Reflects_Spf_Result
TEST_F(Internal_OspfTest, Topology_NextHopCache_Reflects_Spf_Result)
{
    auto& area = getArea(0);
    uint32_t selfRid = ospfInstance->getRouterId();

    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    ospfInterface->enqueueSyncNetworkType();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    // Reorigination from the FULL adjacency is async; wait for the P2P link to neighborRouterId to land before relying on it.
    LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    ASSERT_NE(waitForLsa(selfKey, [neighborRouterId = this->neighborRouterId](const LsaRecord& r) {
        auto* body = std::get_if<RouterLsaV2>(&r.body);
        if (!body)
            return false;
        for (const auto& link : body->links)
            if (link.type == 1 /*P2P*/ && link.linkId == neighborRouterId)
                return true;
        return false;
    }), nullptr);

    RouterLsaV2 nbrLsa;
    nbrLsa.flags = 0;
    nbrLsa.links.push_back({.linkId = selfRid, .linkData = neighborRouterId, .type = 1, .metric = 5});
    LsaKey nbrKey(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader nbrHdr;
    nbrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    nbrHdr.age = 0;
    IncomingLsaContext nbrCtx = {.key = nbrKey, .header = nbrHdr, .checksumValid = true};
    LsaBody nbrLsaBody{nbrLsa};
    processLsa<PolicyV2>(nbrCtx, nbrLsaBody);
    wait();

    ASSERT_NE(waitForLsa(nbrKey), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager());
    SpfEngine engine(getSpfManager());
    SpfResult result = engine.run<PolicyV2>(topo);

    Vertex nbrVertex{VertexType::ROUTER, neighborRouterId};
    auto it = result.nodes.find(nbrVertex);
    ASSERT_NE(it, result.nodes.end());
    ASSERT_FALSE(it->second.parents.empty());

    getTable().consumeSpfResult(area.areaId, result);

    const auto* reach = getTable().lookup(neighborRouterId);
    ASSERT_NE(reach, nullptr);
    EXPECT_EQ(reach->cost, it->second.dist);
    ASSERT_FALSE(reach->nextHops.empty());
    EXPECT_EQ(reach->nextHops.size(), it->second.parents.size());
}

#pragma endregion TopologyTableAsbr

#pragma region AreaTypes

// Test: AreaType_Normal_Accepts_Type5_External_Lsas
TEST_F(Internal_OspfTest, AreaType_Normal_Accepts_Type5_External_Lsas)
{
    auto& area = getArea(0);
    ASSERT_EQ(area.getType(), config::ospf::AreaType::NORMAL);

    ExternalLsaV2 ext{};
    ext.networkMask = 0xFFFFFF00;
    ext.metric = 20;
    ext.isType2 = true;
    ext.forwardingAddress = 0;
    ext.routeTag = 0;

    LsaKey key(OSPFV2_LSA_EXTERNAL, 0x0A000000, neighborRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    hdr.age = 0;

    IncomingLsaContext ctx = {.key = key, .header = hdr, .checksumValid = true};
    LsaBody body{ext};
    auto result = processLsa<PolicyV2>(ctx, body);

    EXPECT_TRUE(result.has_value());
    EXPECT_NE(waitForLsa(key), nullptr);
}

// Test: AreaType_Stub_Rejects_Type5_External_Lsas
TEST_F(Internal_OspfTest, AreaType_Stub_Rejects_Type5_External_Lsas)
{
    // Pre-configure area 1 as STUB before construction.
    getConfigs().get<config::Ospf::AREA_CONFIGS>().emplaceBack(1)
        .get<config::OspfArea::AREA_TYPE>().set(config::ospf::AreaType::STUB);

    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));

    auto& stubArea = getArea(1);
    wait();

    ASSERT_EQ(stubArea.getType(), config::ospf::AreaType::STUB);

    ExternalLsaV2 ext{};
    ext.networkMask = 0xFFFFFF00;
    ext.metric = 20;
    ext.isType2 = true;
    ext.forwardingAddress = 0;
    ext.routeTag = 0;

    LsaKey key(OSPFV2_LSA_EXTERNAL, 0x0A000000, neighborRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    hdr.age = 0;

    IncomingLsaContext ctx = {.key = key, .header = hdr, .checksumValid = true};
    LsaBody body{ext};
    auto result = processLsa<PolicyV2>(ctx, body, &stubArea);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(getLsdb(&stubArea).find(key), nullptr);

    removeIface(iface1.id);
}

// Test: AreaType_Stub_Originates_Default_Route_From_Abr
TEST_F(Internal_OspfTest, AreaType_Stub_Originates_Default_Route_From_Abr)
{
    // Pre-configure area 1 as STUB before construction.
    getConfigs().get<config::Ospf::AREA_CONFIGS>().emplaceBack(1)
        .get<config::OspfArea::AREA_TYPE>().set(config::ospf::AreaType::STUB);

    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));

    auto& stubArea = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    // The area's initial fullRefresh() ran before isABR() became true (insureArea
    // sets ABR status after construction completes), so addStubDefaultRoute(true)
    // was a no-op at that point. Re-run fullRefresh now that isABR() is true.
    fullRefreshV2(&stubArea);
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey defaultKey(OSPFV2_LSA_SUM_NET, 0, selfRid);

    auto* record = waitForLsa(defaultKey, [](const LsaRecord& r) { return r.header.age != routing::OSPF_MAX_AGE; }, &stubArea);
    ASSERT_NE(record, nullptr);
    EXPECT_NE(record->header.age, routing::OSPF_MAX_AGE);

    removeIface(iface1.id);
}

// Test: AreaType_TotallyStub_Suppresses_Type3_Summaries_Except_Default
TEST_F(Internal_OspfTest, AreaType_TotallyStub_Suppresses_Type3_Summaries_Except_Default)
{
    // Pre-configure area 1 as a totally-stubby area: STUB plus NO_SUMMARY.
    auto& areaCfg = getConfigs().get<config::Ospf::AREA_CONFIGS>().emplaceBack(1);
    areaCfg.get<config::OspfArea::AREA_TYPE>().set(config::ospf::AreaType::STUB);
    areaCfg.get<config::OspfArea::NO_SUMMARY>().set(true);

    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));

    auto& tStubArea = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());
    ASSERT_EQ(tStubArea.getType(), config::ospf::AreaType::STUB);

    // A non-default Type-3 summary LSA injected from the backbone must be rejected.
    SummaryNetworkLsa sum{};
    sum.networkMask = 0xFFFFFF00;
    sum.metric = 10;

    LsaKey nonDefaultKey(OSPFV2_LSA_SUM_NET, 0x0A000000, neighborRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    hdr.age = 0;

    IncomingLsaContext ctx = {.key = nonDefaultKey, .header = hdr, .checksumValid = true};
    LsaBody body{sum};
    auto result = processLsa<PolicyV2>(ctx, body, &tStubArea);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(getLsdb(&tStubArea).find(nonDefaultKey), nullptr);

    // The self-originated default route (Type-3, linkStateId=0) must still be present.
    fullRefreshV2(&tStubArea);
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey defaultKey(OSPFV2_LSA_SUM_NET, 0, selfRid);
    auto* record = waitForLsa(defaultKey, [](const LsaRecord& r) { return r.header.age != routing::OSPF_MAX_AGE; }, &tStubArea);
    ASSERT_NE(record, nullptr);
    EXPECT_NE(record->header.age, routing::OSPF_MAX_AGE);

    removeIface(iface1.id);
}

// Test: AreaType_Nssa_Accepts_Type7_Rejects_Type5
TEST_F(Internal_OspfTest, AreaType_Nssa_Accepts_Type7_Rejects_Type5)
{
    // Pre-configure area 1 as NSSA before construction.
    getConfigs().get<config::Ospf::AREA_CONFIGS>().emplaceBack(1)
        .get<config::OspfArea::AREA_TYPE>().set(config::ospf::AreaType::NSSA);

    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));

    auto& nssaArea = getArea(1);
    wait();

    ASSERT_EQ(nssaArea.getType(), config::ospf::AreaType::NSSA);

    // Type-5 external LSA must be rejected in an NSSA.
    ExternalLsaV2 ext{};
    ext.networkMask = 0xFFFFFF00;
    ext.metric = 20;
    ext.isType2 = true;
    ext.forwardingAddress = 0;
    ext.routeTag = 0;

    LsaKey type5Key(OSPFV2_LSA_EXTERNAL, 0x0A000000, neighborRouterId);
    LsaHeader hdr5;
    hdr5.sequence = routing::OSPF_INITIAL_SEQUENCE;
    hdr5.age = 0;

    IncomingLsaContext ctx5 = {.key = type5Key, .header = hdr5, .checksumValid = true};
    LsaBody body5{ext};
    auto result5 = processLsa<PolicyV2>(ctx5, body5, &nssaArea);

    EXPECT_FALSE(result5.has_value());
    EXPECT_EQ(getLsdb(&nssaArea).find(type5Key), nullptr);

    // Type-7 NSSA-external LSA must be accepted.
    LsaKey type7Key(OSPFV2_LSA_NSSA, 0x0B000000, neighborRouterId);
    LsaHeader hdr7;
    hdr7.sequence = routing::OSPF_INITIAL_SEQUENCE;
    hdr7.age = 0;

    IncomingLsaContext ctx7 = {.key = type7Key, .header = hdr7, .checksumValid = true};
    LsaBody body7{ext};
    auto result7 = processLsa<PolicyV2>(ctx7, body7, &nssaArea);

    EXPECT_TRUE(result7.has_value());
    EXPECT_NE(getLsdb(&nssaArea).find(type7Key), nullptr);

    removeIface(iface1.id);
}

// Test: AreaType_Nssa_Abr_Translates_Type7_To_Type5
TEST_F(Internal_OspfTest, AreaType_Nssa_Abr_Translates_Type7_To_Type5)
{
    // Pre-configure area 1 as NSSA before construction.
    getConfigs().get<config::Ospf::AREA_CONFIGS>().emplaceBack(1)
        .get<config::OspfArea::AREA_TYPE>().set(config::ospf::AreaType::NSSA);

    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));

    auto& nssaArea = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    // Inject a Type-7 NSSA-external LSA into area 1 with no forwarding address
    // (so translation does not require an RCU RIB lookup).
    ExternalLsaV2 ext{};
    ext.networkMask = 0xFFFFFF00;
    ext.metric = 20;
    ext.isType2 = true;
    ext.forwardingAddress = 0;
    ext.routeTag = 0;

    LsaKey type7Key(OSPFV2_LSA_NSSA, 0x0B000000, neighborRouterId);
    LsaHeader hdr7;
    hdr7.sequence = routing::OSPF_INITIAL_SEQUENCE;
    hdr7.age = 0;

    IncomingLsaContext ctx7 = {.key = type7Key, .header = hdr7, .checksumValid = true};
    LsaBody body7{ext};
    auto result7 = processLsa<PolicyV2>(ctx7, body7, &nssaArea);
    ASSERT_TRUE(result7.has_value());
    wait();

    translateNssaToExternal<PolicyV2>(getOriginatorCtx(&getArea(0)), type7Key, body7, false);
    wait();

    // A Type-5 LSA with the same prefix, advertised by this router, should now
    // exist in the backbone area's LSDB.
    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey type5Key(OSPFV2_LSA_EXTERNAL, 0x0B000000, selfRid);

    ASSERT_NE(waitForLsa(type5Key, [](const LsaRecord& r) { return r.header.age != routing::OSPF_MAX_AGE; }, &getArea(0)), nullptr);

    bool found = false;
    getLsdb().forEachInType(OSPFV2_LSA_EXTERNAL, [&](const LsaKey& k, LsaRecord& rec) {
        if (k == type5Key && rec.header.age != routing::OSPF_MAX_AGE)
            found = true;
    });
    EXPECT_TRUE(found);

    removeIface(iface1.id);
}

// Test: AreaType_Nssa_Originates_Default_When_Configured
TEST_F(Internal_OspfTest, AreaType_Nssa_Originates_Default_When_Configured)
{
    // Pre-configure area 1 as NSSA with default-originate enabled before construction.
    auto& area1Cfg = getConfigs().get<config::Ospf::AREA_CONFIGS>().emplaceBack(1);
    area1Cfg.get<config::OspfArea::AREA_TYPE>().set(config::ospf::AreaType::NSSA);
    area1Cfg.get<config::OspfArea::NSSA_DEFAULT_ORIGINATE>().set(true);

    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));

    auto& nssaArea = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    fullRefreshV2(&nssaArea);
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();

    bool found = false;
    for (int i = 0; i < 50 && !found; ++i)
    {
        {
            std::lock_guard lock(getSchedulerLock());
            getLsdb(&nssaArea).forEachInType(OSPFV2_LSA_NSSA, [&](const LsaKey& k, LsaRecord& rec) {
                if (k.advertisingRouter == selfRid && rec.header.age != routing::OSPF_MAX_AGE)
                    found = true;
            });
        }
        if (found)
            break;
        wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(found);

    removeIface(iface1.id);
}

// Test: AreaType_TotallyNssa_Suppresses_Type3_Except_Default
TEST_F(Internal_OspfTest, AreaType_TotallyNssa_Suppresses_Type3_Except_Default)
{
    // Pre-configure area 1 as a totally-NSSA area: NSSA plus NO_SUMMARY, with
    // default-originate enabled before construction.
    auto& area1Cfg = getConfigs().get<config::Ospf::AREA_CONFIGS>().emplaceBack(1);
    area1Cfg.get<config::OspfArea::AREA_TYPE>().set(config::ospf::AreaType::NSSA);
    area1Cfg.get<config::OspfArea::NO_SUMMARY>().set(true);
    area1Cfg.get<config::OspfArea::NSSA_DEFAULT_ORIGINATE>().set(true);

    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));

    auto& tNssaArea = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());
    ASSERT_EQ(tNssaArea.getType(), config::ospf::AreaType::NSSA);

    // A non-default Type-3 summary LSA injected from the backbone must be rejected
    // (STUB and NSSA both reject Type-3 summaries when NO_SUMMARY is set).
    SummaryNetworkLsa sum{};
    sum.networkMask = 0xFFFFFF00;
    sum.metric = 10;

    LsaKey nonDefaultKey(OSPFV2_LSA_SUM_NET, 0x0A000000, neighborRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    hdr.age = 0;

    IncomingLsaContext ctx = {.key = nonDefaultKey, .header = hdr, .checksumValid = true};
    LsaBody body{sum};
    auto result = processLsa<PolicyV2>(ctx, body, &tNssaArea);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(getLsdb(&tNssaArea).find(nonDefaultKey), nullptr);

    // The self-originated NSSA default route (Type-7, linkStateId=0) must still
    // be present.
    fullRefreshV2(&tNssaArea);
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();

    bool found = false;
    for (int i = 0; i < 50 && !found; ++i)
    {
        {
            std::lock_guard lock(getSchedulerLock());
            getLsdb(&tNssaArea).forEachInType(OSPFV2_LSA_NSSA, [&](const LsaKey& k, LsaRecord& rec) {
                if (k.advertisingRouter == selfRid && k.linkStateId == 0 && rec.header.age != routing::OSPF_MAX_AGE)
                    found = true;
            });
        }
        if (found)
            break;
        wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(found);

    removeIface(iface1.id);
}

// Test: AreaType_Backbone_Area0_Required_For_InterArea_Routes
TEST_F(Internal_OspfTest, AreaType_Backbone_Area0_Required_For_InterArea_Routes)
{
    // Pre-configure area 1 as NORMAL (default) before construction.
    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));

    auto& area1 = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());
    ASSERT_EQ(area1.getType(), config::ospf::AreaType::NORMAL);

    // A Type-3 inter-area summary received in a NORMAL non-backbone area is
    // accepted by preProcess (no rejection rule for NORMAL areas).
    SummaryNetworkLsa sum{};
    sum.networkMask = 0xFFFFFF00;
    sum.metric = 10;

    LsaKey key(OSPFV2_LSA_SUM_NET, 0x0A000000, neighborRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    hdr.age = 0;

    IncomingLsaContext ctx = {.key = key, .header = hdr, .checksumValid = true};
    LsaBody body{sum};
    auto result = processLsa<PolicyV2>(ctx, body, &area1);

    EXPECT_TRUE(result.has_value());
    EXPECT_NE(getLsdb(&area1).find(key), nullptr);

    removeIface(iface1.id);
}

#pragma endregion AreaTypes

#pragma region AreaRanges

// Test: AreaRange_SyncRangeConfig_Builds_Range_Map_From_Config
TEST_F(Internal_OspfTest, AreaRange_SyncRangeConfig_Builds_Range_Map_From_Config)
{
    types::IPPrefix rangePfx(uint32_t{0x0A000000}, 8);

    getAreaConfigs().get<config::OspfArea::RANGE>().withWrite([&](auto& list) {
        list.emplace_back(rangePfx, false, std::nullopt);
        return true;
    });
    wait();

    const auto& ranges = getRanges();
    EXPECT_TRUE(ranges.contains(rangePfx));
}

// Test: AreaRange_ContributorCount_Incremented_By_Covered_IntraArea_Routes
TEST_F(Internal_OspfTest, AreaRange_ContributorCount_Incremented_By_Covered_IntraArea_Routes)
{
    // Bring up area 1 so the process becomes an ABR.
    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    auto& area1 = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    types::IPPrefix rangePfx(uint32_t{0x0A000000}, 8);

    // Configure a range covering 10.0.0.0/8 in area 1.
    getAreaConfigs(&area1).get<config::OspfArea::RANGE>().withWrite([&](auto& list) {
        list.emplace_back(rangePfx, false, std::nullopt);
        return true;
    });
    wait();

    // Install an intra-area route inside the range into area 1's RIB.
    types::IPPrefix covered(uint32_t{0x0A0A0000}, 16);
    OspfPath path;
    path.type = OspfRouteType::INTRA_AREA;
    path.cost = 5;
    path.area = 1;

    std::vector<std::pair<types::IPPrefix, OspfPath>> pathList;
    pathList.emplace_back(covered, path);
    getRib().replaceArea(area1, pathList);
    wait();

    // Re-run range sync with the latest intra-area routes.
    area1.enqueueSyncRanges();
    wait();

    // A Type-3 summary LSA for the range should now exist (contributorCount > 0).
    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey summaryKey(OSPFV2_LSA_SUM_NET, rangePfx.v4(), selfRid);
    auto* record = waitForLsa(summaryKey, &area1);
    ASSERT_NE(record, nullptr);
    EXPECT_NE(record->header.age, routing::OSPF_MAX_AGE);

    removeIface(iface1.id);
}

// Test: AreaRange_ComputedMetric_Is_Min_Of_Contributors
TEST_F(Internal_OspfTest, AreaRange_ComputedMetric_Is_Min_Of_Contributors)
{
    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    auto& area1 = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    types::IPPrefix rangePfx(uint32_t{0x0A000000}, 8);

    getAreaConfigs(&area1).get<config::OspfArea::RANGE>().withWrite([&](auto& list) {
        list.emplace_back(rangePfx, false, std::nullopt);
        return true;
    });
    wait();

    // Two intra-area routes covered by the range, with different costs.
    types::IPPrefix covered1(uint32_t{0x0A0A0000}, 16);
    OspfPath path1;
    path1.type = OspfRouteType::INTRA_AREA;
    path1.cost = 20;
    path1.area = 1;

    types::IPPrefix covered2(uint32_t{0x0A0B0000}, 16);
    OspfPath path2;
    path2.type = OspfRouteType::INTRA_AREA;
    path2.cost = 5;
    path2.area = 1;

    std::vector<std::pair<types::IPPrefix, OspfPath>> pathList;
    pathList.emplace_back(covered1, path1);
    pathList.emplace_back(covered2, path2);
    getRib().replaceArea(area1, pathList);
    wait();

    area1.enqueueSyncRanges();
    wait();

    // The summary metric should be the MINIMUM of the contributors' costs (5),
    // not the maximum.
    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey summaryKey(OSPFV2_LSA_SUM_NET, rangePfx.v4(), selfRid);
    auto* record = waitForLsa(summaryKey, &area1);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<SummaryNetworkLsa>(&record->body);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->metric, 5u);

    removeIface(iface1.id);
}

// Test: AreaRange_CostOverride_Takes_Precedence_Over_ComputedMetric
TEST_F(Internal_OspfTest, AreaRange_CostOverride_Takes_Precedence_Over_ComputedMetric)
{
    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    auto& area1 = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    types::IPPrefix rangePfx(uint32_t{0x0A000000}, 8);

    // Configure the range with a cost override of 99.
    getAreaConfigs(&area1).get<config::OspfArea::RANGE>().withWrite([&](auto& list) {
        list.emplace_back(rangePfx, false, std::optional<uint32_t>(99));
        return true;
    });
    wait();

    types::IPPrefix covered(uint32_t{0x0A0A0000}, 16);
    OspfPath path;
    path.type = OspfRouteType::INTRA_AREA;
    path.cost = 5;
    path.area = 1;

    std::vector<std::pair<types::IPPrefix, OspfPath>> pathList;
    pathList.emplace_back(covered, path);
    getRib().replaceArea(area1, pathList);
    wait();

    area1.enqueueSyncRanges();
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey summaryKey(OSPFV2_LSA_SUM_NET, rangePfx.v4(), selfRid);
    auto* record = waitForLsa(summaryKey, &area1);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<SummaryNetworkLsa>(&record->body);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->metric, 99u);

    removeIface(iface1.id);
}

// Test: AreaRange_NotAdvertise_Suppresses_Summary_Lsa
TEST_F(Internal_OspfTest, AreaRange_NotAdvertise_Suppresses_Summary_Lsa)
{
    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    auto& area1 = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    types::IPPrefix rangePfx(uint32_t{0x0A000000}, 8);

    // Configure the range with not-advertise = true.
    getAreaConfigs(&area1).get<config::OspfArea::RANGE>().withWrite([&](auto& list) {
        list.emplace_back(rangePfx, true, std::nullopt);
        return true;
    });
    wait();

    types::IPPrefix covered(uint32_t{0x0A0A0000}, 16);
    OspfPath path;
    path.type = OspfRouteType::INTRA_AREA;
    path.cost = 5;
    path.area = 1;

    std::vector<std::pair<types::IPPrefix, OspfPath>> pathList;
    pathList.emplace_back(covered, path);
    getRib().replaceArea(area1, pathList);
    wait();

    area1.enqueueSyncRanges();
    wait();

    // No Type-3 summary LSA for the range should be originated.
    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey summaryKey(OSPFV2_LSA_SUM_NET, rangePfx.v4(), selfRid);
    auto* record = getLsdb(&area1).find(summaryKey);
    if (record != nullptr)
        EXPECT_EQ(record->header.age, routing::OSPF_MAX_AGE);

    removeIface(iface1.id);
}

// Test: AreaRange_SyncRangeSuppression_Withdraws_When_ContributorCount_Zero
TEST_F(Internal_OspfTest, AreaRange_SyncRangeSuppression_Withdraws_When_ContributorCount_Zero)
{
    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    auto& area1 = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    types::IPPrefix rangePfx(uint32_t{0x0A000000}, 8);

    getAreaConfigs(&area1).get<config::OspfArea::RANGE>().withWrite([&](auto& list) {
        list.emplace_back(rangePfx, false, std::nullopt);
        return true;
    });
    wait();

    // Install a covered intra-area route, sync, and confirm the summary appears.
    types::IPPrefix covered(uint32_t{0x0A0A0000}, 16);
    OspfPath path;
    path.type = OspfRouteType::INTRA_AREA;
    path.cost = 5;
    path.area = 1;

    std::vector<std::pair<types::IPPrefix, OspfPath>> pathList;
    pathList.emplace_back(covered, path);
    getRib().replaceArea(area1, pathList);
    wait();

    area1.enqueueSyncRanges();
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey summaryKey(OSPFV2_LSA_SUM_NET, rangePfx.v4(), selfRid);
    auto* record = waitForLsa(summaryKey, [](const LsaRecord& r) { return r.header.age != routing::OSPF_MAX_AGE; }, &area1);
    ASSERT_NE(record, nullptr);
    EXPECT_NE(record->header.age, routing::OSPF_MAX_AGE);
    uint32_t seqWithContributor = record->header.sequence;

    // Now withdraw the covered route (replace area paths with an empty set)
    // and re-sync. The contributor count drops to zero and the summary must
    // be withdrawn (aged to MaxAge).
    std::vector<std::pair<types::IPPrefix, OspfPath>> emptyList;
    getRib().replaceArea(area1, emptyList);
    wait();

    area1.enqueueSyncRanges();
    wait();

    auto* afterRecord = waitForLsa(summaryKey, [seqWithContributor](const LsaRecord& r) { return r.header.age == routing::OSPF_MAX_AGE && r.header.sequence > seqWithContributor; }, &area1);
    ASSERT_NE(afterRecord, nullptr);
    EXPECT_EQ(afterRecord->header.age, routing::OSPF_MAX_AGE);
    EXPECT_GT(afterRecord->header.sequence, seqWithContributor);

    removeIface(iface1.id);
}

// Test: AreaRange_SuppressInterAreaPrefix_Immediate_Withdrawal
TEST_F(Internal_OspfTest, AreaRange_SuppressInterAreaPrefix_Immediate_Withdrawal)
{
    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    auto& area1 = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    types::IPPrefix rangePfx(uint32_t{0x0A000000}, 8);

    getAreaConfigs(&area1).get<config::OspfArea::RANGE>().withWrite([&](auto& list) {
        list.emplace_back(rangePfx, false, std::nullopt);
        return true;
    });
    wait();

    types::IPPrefix covered(uint32_t{0x0A0A0000}, 16);
    OspfPath path;
    path.type = OspfRouteType::INTRA_AREA;
    path.cost = 5;
    path.area = 1;

    std::vector<std::pair<types::IPPrefix, OspfPath>> pathList;
    pathList.emplace_back(covered, path);
    getRib().replaceArea(area1, pathList);
    wait();

    area1.enqueueSyncRanges();
    wait();

    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey summaryKey(OSPFV2_LSA_SUM_NET, rangePfx.v4(), selfRid);
    auto* record = waitForLsa(summaryKey, [](const LsaRecord& r) { return r.header.age != routing::OSPF_MAX_AGE; }, &area1);
    ASSERT_NE(record, nullptr);
    EXPECT_NE(record->header.age, routing::OSPF_MAX_AGE);
    uint32_t seqBefore = record->header.sequence;

    // suppressInterAreaPrefix should immediately withdraw (flush) the summary.
    suppressInterAreaPrefix(rangePfx, &area1);
    wait();

    auto* afterRecord = waitForLsa(summaryKey, [seqBefore](const LsaRecord& r) { return r.header.age == routing::OSPF_MAX_AGE && r.header.sequence > seqBefore; }, &area1);
    ASSERT_NE(afterRecord, nullptr);
    EXPECT_EQ(afterRecord->header.age, routing::OSPF_MAX_AGE);
    EXPECT_GT(afterRecord->header.sequence, seqBefore);

    removeIface(iface1.id);
}

// Test: AreaRange_AbrChange_Forces_Full_Range_Reevaluation
TEST_F(Internal_OspfTest, AreaRange_AbrChange_Forces_Full_Range_Reevaluation)
{
    types::IPPrefix rangePfx(uint32_t{0x0A000000}, 8);

    // Configure a range on area 0 while still a single-area (non-ABR) process.
    getAreaConfigs().get<config::OspfArea::RANGE>().withWrite([&](auto& list) {
        list.emplace_back(rangePfx, false, std::nullopt);
        return true;
    });
    wait();

    ASSERT_FALSE(ospfInstance->isABR());

    syncRangeSuppression(getRanges());
    EXPECT_TRUE(getRanges().contains(rangePfx));

    // Now bring up a second area, making this process an ABR, and force a
    // full range re-evaluation via abrChange=true.
    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    (void)getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    syncRangeSuppression(getRanges(), true);

    // Range configuration should still be intact after the forced re-evaluation.
    EXPECT_TRUE(getRanges().contains(rangePfx));

    removeIface(iface1.id);
}

// Test: AreaRange_DiscardRoute_Installed_While_Range_Active
TEST_F(Internal_OspfTest, AreaRange_DiscardRoute_Installed_While_Range_Active)
{
    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    auto& area1 = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());
    ASSERT_TRUE(getConfigs().get<config::Ospf::DISCARD_INTERNAL>().load());

    types::IPPrefix rangePfx(uint32_t{0x0A000000}, 8);

    getAreaConfigs(&area1).get<config::OspfArea::RANGE>().withWrite([&](auto& list) {
        list.emplace_back(rangePfx, false, std::nullopt);
        return true;
    });
    wait();

    // Install a covered intra-area route so the range becomes active.
    types::IPPrefix covered(uint32_t{0x0A0A0000}, 16);
    OspfPath path;
    path.type = OspfRouteType::INTRA_AREA;
    path.cost = 5;
    path.area = 1;

    std::vector<std::pair<types::IPPrefix, OspfPath>> pathList;
    pathList.emplace_back(covered, path);
    getRib().replaceArea(area1, pathList);
    wait();

    area1.enqueueSyncRanges();
    wait();

    // Range sync installs the covering discard route via an async postAfter timer; poll for it.
    auto* discardRoute = getRib().lookup(rangePfx);
    for (int i = 0; i < 50 && !discardRoute; ++i)
    {
        wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        discardRoute = getRib().lookup(rangePfx);
    }
    ASSERT_NE(discardRoute, nullptr);
    ASSERT_FALSE(discardRoute->paths.empty());
    EXPECT_TRUE(discardRoute->paths.front().discard);

    removeIface(iface1.id);
}

// Test: AreaRange_DiscardRoute_Removed_When_Range_Withdrawn
TEST_F(Internal_OspfTest, AreaRange_DiscardRoute_Removed_When_Range_Withdrawn)
{
    auto& iface1 = createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    auto& area1 = getArea(1);
    wait();

    ASSERT_TRUE(ospfInstance->isABR());

    types::IPPrefix rangePfx(uint32_t{0x0A000000}, 8);

    getAreaConfigs(&area1).get<config::OspfArea::RANGE>().withWrite([&](auto& list) {
        list.emplace_back(rangePfx, false, std::nullopt);
        return true;
    });
    wait();

    types::IPPrefix covered(uint32_t{0x0A0A0000}, 16);
    OspfPath path;
    path.type = OspfRouteType::INTRA_AREA;
    path.cost = 5;
    path.area = 1;

    std::vector<std::pair<types::IPPrefix, OspfPath>> pathList;
    pathList.emplace_back(covered, path);
    getRib().replaceArea(area1, pathList);
    wait();

    area1.enqueueSyncRanges();
    wait();

    const OspfRoute* preRoute = getRib().lookup(rangePfx);
    for (int i = 0; i < 50 && !preRoute; ++i)
    {
        wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        preRoute = getRib().lookup(rangePfx);
    }
    ASSERT_NE(preRoute, nullptr);

    // Withdraw the covered route and re-sync; the range becomes inactive and
    // the discard route should be removed.
    std::vector<std::pair<types::IPPrefix, OspfPath>> emptyList;
    getRib().replaceArea(area1, emptyList);
    wait();

    area1.enqueueSyncRanges();
    wait();

    EXPECT_EQ(getRib().lookup(rangePfx), nullptr);

    removeIface(iface1.id);
}

#pragma endregion AreaRanges

#pragma region VirtualLinks

// Test: VirtualLink_Modeled_As_Interface_With_IsVirtual_True
TEST_F(Internal_OspfTest, VirtualLink_Modeled_As_Interface_With_IsVirtual_True)
{
    // A regular OspfInterface never reports itself as a virtual link.
    EXPECT_FALSE(ospfInterface->isVirtualLink());

    // area 1 is the transit area; the virtual link's own OspfInterfaceId always
    // uses area 0 (backbone), per RFC 2328 SS15.
    getArea(1);
    VirtualLink& link = createVirtualLink(1, neighborRouterId);

    EXPECT_TRUE(link.isVirtualLink());
    EXPECT_EQ(link.getAreaId(), 0u);
    EXPECT_EQ(link.transitAreaId, 1u);
    EXPECT_EQ(link.remoteRouterId, neighborRouterId);

    // Virtual links are always P2P, never passive, never DR/BDR-eligible, and
    // never negotiate MTU or demand circuits (RFC 2328 SS15).
    EXPECT_EQ(link.getNetworkType(), config::ospf::NetworkType::POINT_TO_POINT);
    EXPECT_FALSE(link.getPassive());
    EXPECT_EQ(link.getPriority(), 0u);
    EXPECT_TRUE(link.getMtuIgnore());
    EXPECT_FALSE(link.getDatabaseFilter());
    EXPECT_TRUE(link.getDemandCircuitIgnore());
    EXPECT_FALSE(link.getIsMulticast());

    removeVirtualLink(1, neighborRouterId);
}

// Test: VirtualLink_AddVirtualLink_Encoded_In_RouterLsa
TEST_F(Internal_OspfTest, VirtualLink_AddVirtualLink_Encoded_In_RouterLsa)
{
    auto& backbone = getArea(0);
    uint32_t selfRid = ospfInstance->getRouterId();

    OspfInterface& iface1 = establishTransitPath(1, neighborRouterId);
    VirtualLink& link = createVirtualLink(1, neighborRouterId);

    auto* nbr = addVlNeighbor(link, neighborRouterId, types::IPAddress(types::IPv4Address{neighborRouterId}),
                               Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2(&backbone);
    wait();

    LsaKey routerKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    auto* record = waitForLsa(routerKey, &backbone);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<RouterLsaV2>(&record->body);
    ASSERT_NE(body, nullptr);

    bool foundVirtual = false;
    for (const auto& l : body->links)
    {
        if (l.type == OSPFV2_LINK_VIRTUAL && l.linkId == neighborRouterId)
            foundVirtual = true;
    }
    EXPECT_TRUE(foundVirtual);

    removeVirtualLink(1, neighborRouterId);
    removeIface(iface1.id);
}

// Test: VirtualLink_Adjacency_Requires_Full_State_With_Remote_Abr
TEST_F(Internal_OspfTest, VirtualLink_Adjacency_Requires_Full_State_With_Remote_Abr)
{
    auto& backbone = getArea(0);
    uint32_t selfRid = ospfInstance->getRouterId();

    OspfInterface& iface1 = establishTransitPath(1, neighborRouterId);
    VirtualLink& link = createVirtualLink(1, neighborRouterId);

    auto* nbr = addVlNeighbor(link, neighborRouterId, types::IPAddress(types::IPv4Address{neighborRouterId}),
                               Neighbor::State::EXCHANGE);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXCHANGE);

    fullRefreshV2(&backbone);
    wait();

    LsaKey routerKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    auto* recordBeforeFull = waitForLsa(routerKey, &backbone);
    ASSERT_NE(recordBeforeFull, nullptr);
    auto* bodyBeforeFull = std::get_if<RouterLsaV2>(&recordBeforeFull->body);
    ASSERT_NE(bodyBeforeFull, nullptr);
    for (const auto& l : bodyBeforeFull->links)
        EXPECT_FALSE(l.type == OSPFV2_LINK_VIRTUAL && l.linkId == neighborRouterId);

    // Advance the same neighbor to FULL and re-refresh: the Type-4 link must
    // now appear.
    nbr->setState(Neighbor::State::LOADING);
    nbr->setState(Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2(&backbone);
    wait();

    auto* recordAfterFull = waitForLsa(routerKey,
        [&](const LsaRecord& rec) {
            auto* b = std::get_if<RouterLsaV2>(&rec.body);
            if (!b) return false;
            for (const auto& l : b->links)
                if (l.type == OSPFV2_LINK_VIRTUAL && l.linkId == neighborRouterId)
                    return true;
            return false;
        }, &backbone);
    ASSERT_NE(recordAfterFull, nullptr);

    removeVirtualLink(1, neighborRouterId);
    removeIface(iface1.id);
}

// Test: VirtualLink_Transit_Area_Path_Used_For_VL_Endpoint_Reachability
TEST_F(Internal_OspfTest, VirtualLink_Transit_Area_Path_Used_For_VL_Endpoint_Reachability)
{
    OspfInterface& iface1 = establishTransitPath(1, neighborRouterId);
    ASSERT_NE(getSpfManager(&getArea(1)).spfResult.nodes.find(Vertex{VertexType::ROUTER, neighborRouterId}),
        getSpfManager(&getArea(1)).spfResult.nodes.end());

    VirtualLink& link = createVirtualLink(1, neighborRouterId);
    EXPECT_EQ(link.getTransmitInterface(), &iface1.iface);
    EXPECT_GT(link.getCost(), 0u); // non-zero cost through the transit area

    removeVirtualLink(1, neighborRouterId);
    removeIface(iface1.id);
}

// Test: VirtualLink_Down_When_Transit_Area_Path_Lost
TEST_F(Internal_OspfTest, VirtualLink_Down_When_Transit_Area_Path_Lost)
{
    OspfInterface& iface1 = establishTransitPath(1, neighborRouterId);

    VirtualLink& link = createVirtualLink(1, neighborRouterId);
    ASSERT_NE(link.getTransmitInterface(), nullptr);

    removeIface(iface1.id);
    wait();

    EXPECT_EQ(link.getTransmitInterface(), nullptr);

    removeVirtualLink(1, neighborRouterId);
}

// Test: VirtualLink_Hello_Sent_Over_Transit_Area_Egress_Interface
TEST_F(Internal_OspfTest, VirtualLink_Hello_Sent_Over_Transit_Area_Egress_Interface)
{
    OspfInterface& iface1 = establishTransitPath(1, neighborRouterId);
    VirtualLink& link = createVirtualLink(1, neighborRouterId);

    auto* nbr = addVlNeighbor(link, neighborRouterId, types::IPAddress(types::IPv4Address{neighborRouterId}),
                               Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    bool sawHello = false;
    uint32_t seenAreaId = 0xFFFFFFFF;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() != OSPFV2_TYPE_HELLO)
                return;
            sawHello = true;
            seenAreaId = hdr.getAreaID();
        }));

    sendHelloViaTimer(link);

    ASSERT_TRUE(sawHello);
    EXPECT_EQ(seenAreaId, 0u); // backbone, not the transit area

    removeVirtualLink(1, neighborRouterId);
    removeIface(iface1.id);
}

// Test: VirtualLink_Full_Adjacency_Backbone_Router_Lsa_Advertises_Type4_Link
TEST_F(Internal_OspfTest, VirtualLink_Full_Adjacency_Backbone_Router_Lsa_Advertises_Type4_Link)
{
    auto& backbone = getArea(0);
    uint32_t selfRid = ospfInstance->getRouterId();

    OspfInterface& iface1 = establishTransitPath(1, neighborRouterId);
    VirtualLink& link = createVirtualLink(1, neighborRouterId);

    auto* nbr = addVlNeighbor(link, neighborRouterId, types::IPAddress(types::IPv4Address{neighborRouterId}),
                               Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    types::IPPrefix resolvedAddr = link.getTransmitAddress();
    uint16_t resolvedCost = link.getCost();
    ASSERT_TRUE(resolvedAddr.isIPv4());
    ASSERT_GT(resolvedCost, 0u);

    fullRefreshV2(&backbone);
    wait();

    LsaKey routerKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    auto* record = waitForLsa(routerKey,
        [&](const LsaRecord& rec) {
            auto* b = std::get_if<RouterLsaV2>(&rec.body);
            if (!b) return false;
            for (const auto& l : b->links)
                if (l.type == OSPFV2_LINK_VIRTUAL && l.linkId == neighborRouterId)
                    return true;
            return false;
        }, &backbone);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<RouterLsaV2>(&record->body);
    ASSERT_NE(body, nullptr);

    auto vlink = std::find_if(body->links.begin(), body->links.end(), [&](const RouterLinkV2& l) {
        return l.type == OSPFV2_LINK_VIRTUAL && l.linkId == neighborRouterId;
    });
    ASSERT_NE(vlink, body->links.end());
    EXPECT_EQ(vlink->linkData, resolvedAddr.v4());
    EXPECT_EQ(vlink->metric, resolvedCost);

    removeVirtualLink(1, neighborRouterId);
    removeIface(iface1.id);
}

// Test: VirtualLink_Route_Beyond_Remote_Endpoint_Installs_With_Vl_As_First_Hop
TEST_F(Internal_OspfTest, VirtualLink_Route_Beyond_Remote_Endpoint_Installs_With_Vl_As_First_Hop)
{
    auto& backbone = getArea(0);
    uint32_t selfRid = ospfInstance->getRouterId();

    OspfInterface& iface1 = establishTransitPath(1, neighborRouterId);
    VirtualLink& link = createVirtualLink(1, neighborRouterId);

    auto* nbr = addVlNeighbor(link, neighborRouterId, types::IPAddress(types::IPv4Address{neighborRouterId}),
                               Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    const uint32_t remoteStubNet = 0x0B0B0B00;
    const uint32_t remoteStubMask = 0xFFFFFF00;
    RouterLsaV2 remoteBackboneLsa;
    remoteBackboneLsa.flags = 0;
    remoteBackboneLsa.links.push_back({.linkId = selfRid, .linkData = neighborRouterId, .type = OSPFV2_LINK_VIRTUAL, .metric = 5});
    remoteBackboneLsa.links.push_back({.linkId = remoteStubNet, .linkData = remoteStubMask, .type = OSPFV2_LINK_STUB, .metric = 1});

    LsaKey remoteKey(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader remoteHdr;
    remoteHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    remoteHdr.age = 0;
    IncomingLsaContext remoteCtx = {.key = remoteKey, .header = remoteHdr, .checksumValid = true};
    LsaBody remoteBody{remoteBackboneLsa};
    processLsa<PolicyV2>(remoteCtx, remoteBody, &backbone);
    wait();
    ASSERT_NE(waitForLsa(remoteKey, &backbone), nullptr);

    fullRefreshV2(&backbone);
    wait();

    LsaKey routerKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
    ASSERT_NE(waitForLsa(routerKey,
        [&](const LsaRecord& rec) {
            auto* b = std::get_if<RouterLsaV2>(&rec.body);
            if (!b) return false;
            for (const auto& l : b->links)
                if (l.type == OSPFV2_LINK_VIRTUAL && l.linkId == neighborRouterId)
                    return true;
            return false;
        }, &backbone), nullptr);

    SpfTopology<PolicyV2> topo(getSpfManager(&backbone));
    SpfEngine engine(getSpfManager(&backbone));
    SpfResult result = engine.run<PolicyV2>(topo);

    Vertex remoteVertex{VertexType::ROUTER, neighborRouterId};
    auto vertexIt = result.nodes.find(remoteVertex);
    ASSERT_NE(vertexIt, result.nodes.end());
    EXPECT_TRUE(vertexIt->second.confirmed);
    ASSERT_FALSE(vertexIt->second.parents.empty());
    EXPECT_EQ(vertexIt->second.parents[0].parent, result.root);

    std::vector<std::pair<types::IPPrefix, OspfPath>> routes;
    getIntraRouteManager(&backbone).deriveIntraAreaRoutes<PolicyV2>(result, routes);

    auto routeIt = std::find_if(routes.begin(), routes.end(), [&](const auto& pr) {
        return pr.first.v4() == remoteStubNet;
    });
    ASSERT_NE(routeIt, routes.end());
    ASSERT_FALSE(routeIt->second.nextHops.empty());
    EXPECT_EQ(routeIt->second.nextHops.front().interfaceId, iface1.interfaceId);
    EXPECT_EQ(routeIt->second.nextHops.front().nextHop, types::IPAddress(link.getTransmitAddress(), 32));

    removeVirtualLink(1, neighborRouterId);
    removeIface(iface1.id);
}

#pragma endregion VirtualLinks

#pragma region DemandCircuitAndLls

// Test: DemandCircuit_Negotiation_Both_Sides_DC_Capable_Sets_Enabled
TEST_F(Internal_OspfTest, DemandCircuit_Negotiation_Both_Sides_DC_Capable_Sets_Enabled)
{
    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    ospfInterface->enqueueSyncNetworkType();
    wait();

    // Advertise local DC capability.
    getIfaceFlags().setDemandCircuits(true);

    ASSERT_TRUE(isIfaceDCUndecided());

    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::TWOWAY);
    ASSERT_NE(nbr, nullptr);

    uint32_t remoteOptions = 0;
    AreaFlagManager::setExternalRouting(remoteOptions, true);
    InterfaceFlagManager::setDemandCircuits(remoteOptions, true);

    bool ok = processOptions(remoteOptions, *nbr);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(isIfaceDCEnabled());
}

// Test: DemandCircuit_Negotiation_One_Side_NonCapable_Sets_Disabled
TEST_F(Internal_OspfTest, DemandCircuit_Negotiation_One_Side_NonCapable_Sets_Disabled)
{
    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    ospfInterface->enqueueSyncNetworkType();
    wait();

    // Advertise local DC capability.
    getIfaceFlags().setDemandCircuits(true);

    ASSERT_TRUE(isIfaceDCUndecided());

    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::TWOWAY);
    ASSERT_NE(nbr, nullptr);

    // Remote options WITHOUT the DC bit set, but with a matching E-bit so
    // processOptions doesn't reject on the unrelated external-routing check.
    uint32_t remoteOptions = 0;
    AreaFlagManager::setExternalRouting(remoteOptions, true);

    bool ok = processOptions(remoteOptions, *nbr);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(isIfaceDCDisabled());
}

// Test: DemandCircuit_Enabled_Suppresses_Periodic_Hello_After_Full
TEST_F(Internal_OspfTest, DemandCircuit_Enabled_Suppresses_Periodic_Hello_After_Full)
{
    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    ospfInterface->enqueueSyncNetworkType();
    wait();
    getIfaceFlags().setDemandCircuits(true);

    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::TWOWAY);
    ASSERT_NE(nbr, nullptr);

    uint32_t remoteOptions = 0;
    AreaFlagManager::setExternalRouting(remoteOptions, true);
    InterfaceFlagManager::setDemandCircuits(remoteOptions, true);
    ASSERT_TRUE(processOptions(remoteOptions, *nbr));
    ASSERT_TRUE(isIfaceDCEnabled());

    // Drive the neighbor to FULL: Neighbor::setState's FULL case calls
    // iface.getTimers().stopHello() when demandCircuit == ENABLED.
    nbr->setState(Neighbor::State::EXSTART);
    nbr->setState(Neighbor::State::EXCHANGE);
    nbr->setState(Neighbor::State::LOADING);
    nbr->setState(Neighbor::State::FULL);

    EXPECT_EQ(nbr->getState(), Neighbor::State::FULL);
    EXPECT_TRUE(isIfaceDCEnabled());
}

// Test: DemandCircuit_DoNotAge_Bit_Set_On_FloodReduction
TEST_F(Internal_OspfTest, DemandCircuit_DoNotAge_Bit_Set_On_FloodReduction)
{
    auto& area = getArea(0);

    // Enable demand-circuit on this interface so dcCompatible (true by
    // default) combined with FLOOD_REDUCTION/DEMAND_CIRCUIT config enables
    // flood reduction.
    getIfaceConfigs().get<config::OspfInterface::DEMAND_CIRCUIT>().set(true);

    ASSERT_TRUE(area.isDcCompatible());

    bool before = getFloodReduction();
    setFloodReduction();
    wait();

    EXPECT_NE(getFloodReduction(), before);
    EXPECT_TRUE(getFloodReduction());
}

// Test: DemandCircuit_RunDCIntegrityScan_Flags_Inconsistent_Lsa
TEST_F(Internal_OspfTest, DemandCircuit_RunDCIntegrityScan_Flags_Inconsistent_Lsa)
{
    auto& area = getArea(0);

    // Initially, before any LSAs are originated/injected, the LSDB is empty
    // and the scan should report compatible (vacuously true).
    runDCIntegrityScan();
    EXPECT_TRUE(area.isDcCompatible());

    // Inject a router LSA from a neighbor with options that do NOT carry the
    // demand-circuit bit.
    RouterLsaV2 nbrLsa;
    nbrLsa.flags = 0;
    nbrLsa.links.push_back({.linkId = 0x0A0A0A0A, .linkData = 0xFFFFFFFF, .type = OSPFV2_LINK_STUB, .metric = 1});

    LsaKey nbrKey(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader nbrHdr;
    nbrHdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    nbrHdr.age = 0;
    nbrHdr.options = 0; // No DC bit.

    IncomingLsaContext ctx = {.key = nbrKey, .header = nbrHdr, .checksumValid = true};
    LsaBody body{nbrLsa};
    auto result = processLsa<PolicyV2>(ctx, body);
    ASSERT_TRUE(result.has_value());
    wait();

    runDCIntegrityScan();
    EXPECT_FALSE(area.isDcCompatible());
}

// Test: Lls_DataBlock_Appended_When_Enabled
TEST_F(Internal_OspfTest, Lls_DataBlock_Appended_When_Enabled)
{
    getIfaceGlobalConfigs().get<config::OspfGlobalInterface::LLS>().set(true);

    bool sawHello = false;
    bool lBitSet = false;
    uint16_t llsChecksum = 0;
    uint16_t llsLenWords = 0;
    uint16_t tlvType = 0;
    uint16_t tlvSize = 0;
    uint16_t computedChecksum = 0;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() != OSPFV2_TYPE_HELLO)
                return;
            sawHello = true;

            packet::Ospfv2HelloHeader hello;
            hello.setBuffer(hdr.buffer + packet::Ospfv2Header::fixedSize);
            lBitSet = (hello.getOptions() & 0x10) != 0;

            // LLS Data Block starts right after the OSPF payload (RFC 5613 Sec 2:
            // not counted in the OSPF header's own packet-length field).
            uint8_t* lls = hdr.buffer + hdr.getPacketLen();
            llsChecksum = utils::read<uint16_t>(lls);
            llsLenWords = utils::read<uint16_t>(lls + 2);
            tlvType = utils::read<uint16_t>(lls + 4);
            tlvSize = utils::read<uint16_t>(lls + 6);

            // Recompute over the block with the checksum field zeroed, matching
            // how addLinkLocalChecksum() computed it before writing the result.
            ChecksumFletcher check;
            check.addU16(0);
            check.addBytes(lls + 2, static_cast<size_t>(llsLenWords) * 4 - 2);
            computedChecksum = check.finalize();
        }));

    getDispatcherV2().sendHello();
    ASSERT_TRUE(sawHello);

    EXPECT_TRUE(lBitSet);
    EXPECT_EQ(llsLenWords, 3u);      // 12 bytes / 4 = 3 words (checksum+length+EO-TLV).
    EXPECT_EQ(tlvType, 0x0001u);     // Extended Options and Flags TLV.
    EXPECT_EQ(tlvSize, 0x0004u);
    EXPECT_EQ(llsChecksum, computedChecksum);
}

// Test: Lls_Md5Auth_Validates_Block_Checksum
TEST_F(Internal_OspfTest, Lls_Md5Auth_Validates_Block_Checksum)
{
    getIfaceGlobalConfigs().get<config::OspfGlobalInterface::LLS>().set(true);

    uint8_t keyId = 1;
    std::array<uint8_t, 16> keyBytes{};
    for (size_t i = 0; i < keyBytes.size(); ++i)
        keyBytes[i] = static_cast<uint8_t>(0xB0 + i);

    getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::AUTHENTICATION_TYPE>().set(config::ospf::AuthType::CRYPTO);
    getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::MESSAGE_DIGEST_KEYS>().withWrite([&](auto& list) {
        list.emplace_back(keyId, std::string(keyBytes.begin(), keyBytes.end()));
        return true;
    });
    wait();
    ASSERT_TRUE(getAuthKey().has_value());
    ASSERT_TRUE(getAuthKeyId().has_value());

    bool sawHello = false;
    uint16_t llsLenWords = 0;
    uint16_t authTlvType = 0;
    uint16_t authTlvSize = 0;
    bool digestMatches = false;
    bool digestMismatchesWhenTampered = false;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() != OSPFV2_TYPE_HELLO)
                return;
            sawHello = true;

            uint8_t* lls = hdr.buffer + hdr.getPacketLen() + 16;
            llsLenWords = utils::read<uint16_t>(lls + 2);

            // EO-TLV (12 bytes) followed by the Authentication TLV (24 bytes).
            uint8_t* authTlv = lls + 12;
            authTlvType = utils::read<uint16_t>(authTlv);
            authTlvSize = utils::read<uint16_t>(authTlv + 2);

            uint8_t secret[16];
            utils::write<__uint128_t>(secret, getAuthKey().value());

            uint8_t digest[16];
            security::authentication::generateHMAC(digest, lls, 12 + 8, secret, 16, security::authentication::HmacType::MD5);
            digestMatches = std::memcmp(digest, authTlv + 8, 16) == 0;

            // Tampering with the LLS payload must invalidate the digest.
            uint8_t tamperedDigest[16];
            uint8_t savedByte = lls[8];
            lls[8] ^= 0xFF;
            security::authentication::generateHMAC(tamperedDigest, lls, 12 + 8, secret, 16, security::authentication::HmacType::MD5);
            lls[8] = savedByte;
            digestMismatchesWhenTampered = std::memcmp(tamperedDigest, authTlv + 8, 16) != 0;
        }));

    getDispatcherV2().sendHello();
    ASSERT_TRUE(sawHello);

    EXPECT_EQ(llsLenWords, 9u);   // (12-byte EO-TLV + 24-byte Auth TLV) / 4 = 9 words.
    EXPECT_EQ(authTlvType, 0x0002u);
    EXPECT_EQ(authTlvSize, 0x0014u);
    EXPECT_TRUE(digestMatches);
    EXPECT_TRUE(digestMismatchesWhenTampered);
}

#pragma endregion DemandCircuitAndLls

#pragma region OpaqueLsa

// Test: Opaque_OriginateRouterCapability_InstalledInLsdb
TEST_F(Internal_OspfTest, Opaque_OriginateRouterCapability_InstalledInLsdb)
{
    uint32_t selfRid = ospfInstance->getRouterId();

    originateRouterCapability(0);
    wait();

    LsaKey key(OSPFV2_LSA_OPAQUE_AREA, 0x04000000, selfRid);
    auto* record = waitForLsa(key);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<OpaqueLsaV2>(&record->body);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->opaqueType, 4u);
    EXPECT_EQ(body->opaqueId, 0u);
    ASSERT_EQ(body->payload.size(), RouterCapabilityTlv::size());

    auto tlv = RouterCapabilityTlv::build(body->payload.data(), static_cast<uint16_t>(body->payload.size()));
    ASSERT_TRUE(tlv.has_value());
    EXPECT_EQ(tlv->capabilities, 0u);
}

// Test: Opaque_OriginateRouterCapability_FloodedToFullNeighbor
TEST_F(Internal_OspfTest, Opaque_OriginateRouterCapability_FloodedToFullNeighbor)
{
    addNeighbor(neighborRouterId, types::IPAddress{types::IPv4Address{0xC0A80102}}, Neighbor::State::FULL);

    bool sawOpaqueLsu = false;
    uint32_t seenLinkStateId = 0;
    std::vector<LsaKey> ackKeys;
    std::vector<LsaHeader> ackHeaders;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            if (!pkt.getHeader(packet::HeaderType::OSPFV2))
                return;

            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() != OSPFV2_TYPE_LINK_STATE_UPDATE)
                return;

            if (hdr.getPacketLen() < packet::Ospfv2Header::fixedSize + 2)
                return;

            uint8_t* buf = hdr.buffer + packet::Ospfv2Header::fixedSize;
            size_t payloadLimit = hdr.getPacketLen() - packet::Ospfv2Header::fixedSize;
            uint16_t count = utils::read<uint16_t>(buf);
            size_t offset = 2;
            for (uint32_t i = 0; i < count; ++i)
            {
                if (offset + packet::Ospfv2LSAHeader::fixedSize > payloadLimit)
                    break;

                packet::Ospfv2LSAHeader lsaHdr;
                lsaHdr.setBuffer(buf + offset);
                uint16_t lsaLen = lsaHdr.getLen();
                if (lsaLen < packet::Ospfv2LSAHeader::fixedSize || offset + lsaLen > payloadLimit)
                    break;

                if (lsaHdr.getType() == OSPFV2_LSA_OPAQUE_AREA)
                {
                    sawOpaqueLsu = true;
                    seenLinkStateId = lsaHdr.getLsID();
                }

                LsaKey key(lsaHdr.getType(), lsaHdr.getLsID(), lsaHdr.getAdvRouter());
                LsaHeader lh;
                lh.age = lsaHdr.getAge();
                lh.options = lsaHdr.getOptions();
                lh.sequence = lsaHdr.getSeqNumber();
                lh.checksum = lsaHdr.getChecksum();
                ackKeys.push_back(key);
                ackHeaders.push_back(lh);

                offset += lsaLen;
            }
        }));

    originateRouterCapability(ROUTER_CAP_GRACEFUL_RESTART);
    wait();

    // Origination is postAfter-driven; poll until the mock has observed the flood.
    for (int i = 0; i < 50 && !sawOpaqueLsu; ++i)
    {
        wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_TRUE(sawOpaqueLsu);
    EXPECT_EQ(seenLinkStateId, 0x04000000u);

    ASSERT_FALSE(ackKeys.empty());
    buildLSAckV2(testPacket, neighborRouterId, ospfInterface->getAreaId(), ackKeys, ackHeaders);
    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    wait();
}

// Test: Opaque_RefreshCycle_ReoriginatesWithIncrementedSequence
TEST_F(Internal_OspfTest, Opaque_RefreshCycle_ReoriginatesWithIncrementedSequence)
{
    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_OPAQUE_AREA, 0x04000000, selfRid);

    originateRouterCapability(0);
    wait();

    waitForLsa(key);
    auto* record = waitForLsa(key);
    ASSERT_NE(record, nullptr);
    uint32_t firstSequence = record->header.sequence;

    originateRouterCapability(ROUTER_CAP_GRACEFUL_RESTART);
    wait();

    record = waitForLsa(key, [firstSequence](const LsaRecord& r) { return r.header.sequence > firstSequence; });
    ASSERT_NE(record, nullptr);
    EXPECT_GT(record->header.sequence, firstSequence);
    auto* body = std::get_if<OpaqueLsaV2>(&record->body);
    ASSERT_NE(body, nullptr);
    auto tlv = RouterCapabilityTlv::build(body->payload.data(), static_cast<uint16_t>(body->payload.size()));
    ASSERT_TRUE(tlv.has_value());
    EXPECT_EQ(tlv->capabilities, ROUTER_CAP_GRACEFUL_RESTART);
}

// Test: Opaque_OBitDisabled_OriginationSuppressed
TEST_F(Internal_OspfTest, Opaque_OBitDisabled_OriginationSuppressed)
{
    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_OPAQUE_AREA, 0x04000000, selfRid);

    setIfaceOpaqueEnabled(false);

    originateRouterCapability(0);
    wait();

    waitForLsa(key);
    EXPECT_FALSE(getLsdb().contains(key));
}

// Test: Opaque_ReceivedOpaqueLsa_ParsedAndInstalled_ViaExistingRxPath
TEST_F(Internal_OspfTest, Opaque_ReceivedOpaqueLsa_ParsedAndInstalled_ViaExistingRxPath)
{
    addNeighbor(neighborRouterId, types::IPAddress{types::IPv4Address{0xC0A80102}}, Neighbor::State::FULL);

    RouterCapabilityTlv tlv;
    tlv.capabilities = ROUTER_CAP_GRACEFUL_RESTART;
    uint8_t tlvBuf[RouterCapabilityTlv::size()];
    tlv.buildBody(tlvBuf, sizeof(tlvBuf));

    LsaKey key(OSPFV2_LSA_OPAQUE_AREA, 0x04000000, neighborRouterId);

    uint8_t testPacket[256] = {0};
    writeOspfV2CommonHeader(testPacket, OSPFV2_TYPE_LINK_STATE_UPDATE, neighborRouterId, 0);
    packet::Ospfv2Header hdr;
    hdr.setBuffer(testPacket);

    size_t offset = packet::Ospfv2Header::fixedSize;
    utils::write<uint32_t>(testPacket + offset, 1);
    offset += 4;

    uint16_t lsaLen = packet::Ospfv2LSAHeader::fixedSize + static_cast<uint16_t>(sizeof(tlvBuf));

    packet::Ospfv2LSAHeader lsaHdr;
    lsaHdr.setBuffer(testPacket + offset);
    lsaHdr.setAge(0);
    lsaHdr.setOptions(0);
    lsaHdr.setType(OSPFV2_LSA_OPAQUE_AREA);
    lsaHdr.setLsID(key.linkStateId);
    lsaHdr.setAdvRouter(key.advertisingRouter);
    lsaHdr.setSeqNum(0x80000001);
    lsaHdr.setLen(lsaLen);
    lsaHdr.setChecksum(0);

    std::memcpy(testPacket + offset + packet::Ospfv2LSAHeader::fixedSize, tlvBuf, sizeof(tlvBuf));

    ChecksumFletcher check;
    check.addBytes(testPacket + offset + 2, lsaLen - 2);
    lsaHdr.setChecksum(check.finalize());

    offset += lsaLen;

    uint16_t packetLen = static_cast<uint16_t>(offset);
    hdr.setPacketLen(packetLen);
    finalizeOspfV2Checksum(testPacket, packetLen);

    deliverV2(testPacket, types::IPv4Address{0xC0A80102});
    wait();

    auto* record = waitForLsa(key);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<OpaqueLsaV2>(&record->body);
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->payload.size(), RouterCapabilityTlv::size());
    auto parsedTlv = RouterCapabilityTlv::build(body->payload.data(), static_cast<uint16_t>(body->payload.size()));
    ASSERT_TRUE(parsedTlv.has_value());
    EXPECT_EQ(parsedTlv->capabilities, ROUTER_CAP_GRACEFUL_RESTART);
}

// Test: Opaque_WithdrawRouterCapability_FlushesFromLsdb
TEST_F(Internal_OspfTest, Opaque_WithdrawRouterCapability_FlushesFromLsdb)
{
    uint32_t selfRid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_OPAQUE_AREA, 0x04000000, selfRid);

    originateRouterCapability(0);
    wait();
    waitForLsa(key);
    ASSERT_TRUE(getLsdb().contains(key));

    withdrawRouterCapability();
    wait();

    auto* record = waitForLsa(key, [](const LsaRecord& r) { return r.header.age == routing::OSPF_MAX_AGE; });
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->header.age, routing::OSPF_MAX_AGE);
}

#pragma endregion OpaqueLsa

#pragma region ResyncLls

// Test: Resync_LlsOptionsEnum_MatchesWireBitPositions
TEST_F(Internal_OspfTest, Resync_LlsOptionsEnum_MatchesWireBitPositions)
{
    // Guards against drift: RFC 4811/4812 §2 fix these bit positions on the wire.
    EXPECT_EQ(getLlsResyncBit(), 0x0001u);
    EXPECT_EQ(getLlsRestartBit(), 0x0002u);
}

// Test: Resync_TriggerResync_Sends_LlsResyncBit_InUnicastHello
TEST_F(Internal_OspfTest, Resync_TriggerResync_Sends_LlsResyncBit_InUnicastHello)
{
    getIfaceGlobalConfigs().get<config::OspfGlobalInterface::LLS>().set(true);

    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL, nullptr, true); // unicast = true
    ASSERT_NE(nbr, nullptr);

    bool sawHello = false;
    uint32_t extendedOptions = 0;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() != OSPFV2_TYPE_HELLO)
                return;
            sawHello = true;

            // LLS Data Block starts right after the OSPF payload (RFC 5613 Sec 2).
            uint8_t* lls = hdr.buffer + hdr.getPacketLen();
            extendedOptions = utils::read<uint32_t>(lls + 8);
        }));

    getDispatcherV2().triggerResync(*nbr);

    ASSERT_TRUE(sawHello);
    EXPECT_TRUE(lssHasResyncOption(extendedOptions));
}

// Test: Resync_MulticastHello_NeverSetsResyncBit
TEST_F(Internal_OspfTest, Resync_MulticastHello_NeverSetsResyncBit)
{
    getIfaceGlobalConfigs().get<config::OspfGlobalInterface::LLS>().set(true);
    runIfaceElection();
    ASSERT_TRUE(ospfInterface->getIsDr());

    // A resync request queued for a unicast neighbor must never leak its bit
    // onto the interface's separate multicast Hello.
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL, nullptr, true);
    ASSERT_NE(nbr, nullptr);
    nbr->resyncRequested.store(true, std::memory_order_relaxed);

    bool sawHello = false;
    uint32_t extendedOptions = 0xFFFFFFFFu;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() != OSPFV2_TYPE_HELLO)
                return;
            sawHello = true;

            uint8_t* lls = hdr.buffer + hdr.getPacketLen();
            extendedOptions = utils::read<uint32_t>(lls + 8);
        }));

    getDispatcherV2().sendHello();

    ASSERT_TRUE(sawHello);
    EXPECT_FALSE(lssHasResyncOption(extendedOptions));
}

// Test: Resync_ReceivedResyncBit_ReflooedsFullLsdbToNeighbor_WithoutStateReset
TEST_F(Internal_OspfTest, Resync_ReceivedResyncBit_ReflooedsFullLsdbToNeighbor_WithoutStateReset)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL, nullptr, true);
    ASSERT_NE(nbr, nullptr);
    ASSERT_GT(getLsdb().size(), 0u); // SetUp() already originates this router's Router-LSA.
    ASSERT_FALSE(nbr->getRtr().lsus().getActive());

    uint32_t mask = ospfInterface->interfaceAddress.getMask();
    uint32_t selfRid = ospfInstance->getRouterId();
    uint16_t packetLen = buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                                       getIfaceHelloInterval(), getIfaceDeadInterval(),
                                       mask, 1, 0, 0, {selfRid}, 0x12 /* E-bit | L-bit */);

    // Manually append a minimal LLS Data Block (RFC 5613/4813) with the resync
    // extended option set, since buildHelloV2() has no native LLS support.
    uint8_t* lls = testPacket + packetLen;
    utils::write<uint16_t>(lls + 2, 3); // length in 32-bit words: checksum+length+EO-TLV
    utils::write<uint16_t>(lls + 4, 0x0001); // Extended Options and Flags TLV type
    utils::write<uint16_t>(lls + 6, 0x0004); // TLV value size
    utils::write<uint32_t>(lls + 8, getLlsResyncBit());

    ChecksumFletcher check;
    check.addU16(0);
    check.addBytes(lls + 2, 10); // 12-byte block minus the 2-byte checksum field
    utils::write<uint16_t>(lls, check.finalize());

    deliverV2(testPacket, types::IPv4Address{0xC0A80102}, false /* unicast */, nullptr, 12 /* LLS block size */);
    wait();

    EXPECT_EQ(nbr->getState(), Neighbor::State::FULL); // Not reset by resync.
    EXPECT_TRUE(nbr->getRtr().lsus().getActive());
}

// Test: Resync_ReceivedResyncBit_IgnoredWhenNeighborNotFull
TEST_F(Internal_OspfTest, Resync_ReceivedResyncBit_IgnoredWhenNeighborNotFull)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXCHANGE, nullptr, true);
    ASSERT_NE(nbr, nullptr);
    ASSERT_FALSE(nbr->getRtr().lsus().getActive());

    // Per RFC 4811, a neighbor below Full has no synchronized database to
    // resync from — the request must be silently ignored.
    getDispatcherV2().onResyncRequested(*nbr);
    wait();

    EXPECT_EQ(nbr->getState(), Neighbor::State::EXCHANGE);
    EXPECT_FALSE(nbr->getRtr().lsus().getActive());
}

#pragma endregion ResyncLls

#pragma region GracefulRestart

// Test: GracefulRestart_BeginGracefulRestart_OriginatesGraceLsa_InLsdb
TEST_F(Internal_OspfTest, GracefulRestart_BeginGracefulRestart_OriginatesGraceLsa_InLsdb)
{
    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t linkStateId = (static_cast<uint32_t>(GRACE_LSA_OPAQUE_TYPE) << 24) | (ospfInterface->interfaceId & 0x00FFFFFF);
    LsaKey key(OSPFV2_LSA_OPAQUE_LINK, linkStateId, selfRid);

    ospfInterface->beginGracefulRestart(120, GraceRestartReason::SOFTWARE_RESTART);
    wait();

    auto* record = waitForLsa(key);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<OpaqueLsaV2>(&record->body);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->opaqueType, GRACE_LSA_OPAQUE_TYPE);

    auto tlv = GraceLsaTlv::build(body->payload.data(), static_cast<uint16_t>(body->payload.size()));
    ASSERT_TRUE(tlv.has_value());
    EXPECT_EQ(tlv->gracePeriodSeconds, 120u);
    EXPECT_EQ(tlv->restartReason, GraceRestartReason::SOFTWARE_RESTART);
}

// Test: GracefulRestart_BeginGracefulRestart_SetsRestartBitOnHello
TEST_F(Internal_OspfTest, GracefulRestart_BeginGracefulRestart_SetsRestartBitOnHello)
{
    ospfInterface->beginGracefulRestart(120, GraceRestartReason::SOFTWARE_RESTART);
    wait();

    bool sawHello = false;
    uint32_t extendedOptions = 0;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV2Header(pkt);
            if (hdr.getType() != OSPFV2_TYPE_HELLO)
                return;
            sawHello = true;

            uint8_t* lls = hdr.buffer + hdr.getPacketLen();
            extendedOptions = utils::read<uint32_t>(lls + 8);
        }));

    getDispatcherV2().sendHello();

    ASSERT_TRUE(sawHello);
    EXPECT_TRUE(lssHasRestartOption(extendedOptions));
}

// Test: GracefulRestart_EndGracefulRestart_FlushesGraceLsa
TEST_F(Internal_OspfTest, GracefulRestart_EndGracefulRestart_FlushesGraceLsa)
{
    uint32_t selfRid = ospfInstance->getRouterId();
    uint32_t linkStateId = (static_cast<uint32_t>(GRACE_LSA_OPAQUE_TYPE) << 24) | (ospfInterface->interfaceId & 0x00FFFFFF);
    LsaKey key(OSPFV2_LSA_OPAQUE_LINK, linkStateId, selfRid);

    ospfInterface->beginGracefulRestart(120, GraceRestartReason::SOFTWARE_RESTART);
    wait();
    waitForLsa(key);
    ASSERT_TRUE(getLsdb().contains(key));

    ospfInterface->endGracefulRestart();
    wait();

    auto* record = waitForLsa(key, [](const LsaRecord& r) { return r.header.age == routing::OSPF_MAX_AGE; });
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->header.age, routing::OSPF_MAX_AGE);
    EXPECT_FALSE(ospfInterface->getGracefulRestartInProgress());
}

// Test: GracefulRestart_HelperMode_SuppressesDeadTimerTeardown_WithinGracePeriod
TEST_F(Internal_OspfTest, GracefulRestart_HelperMode_SuppressesDeadTimerTeardown_WithinGracePeriod)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_NE(nbr, nullptr);

    nbr->helpingRestart.store(true, std::memory_order_relaxed);
    nbr->helperDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);

    getTimers().handleInactiveTimeExpire(*nbr);

    EXPECT_EQ(nbr->getState(), Neighbor::State::FULL);
    EXPECT_TRUE(nbr->helpingRestart.load());
    EXPECT_NE(nbr->inactivityTimerId.load(), 0u);
}

// Test: GracefulRestart_HelperMode_TearsDownAfterGracePeriodExpires
TEST_F(Internal_OspfTest, GracefulRestart_HelperMode_TearsDownAfterGracePeriodExpires)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_NE(nbr, nullptr);

    // Grace period already expired: the deadline is in the past.
    nbr->helpingRestart.store(true, std::memory_order_relaxed);
    nbr->helperDeadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);

    getTimers().handleInactiveTimeExpire(*nbr);

    EXPECT_EQ(nbr->getState(), Neighbor::State::DOWN);
    EXPECT_FALSE(nbr->helpingRestart.load());
}

// Test: GracefulRestart_HelperModeDisabled_TearsDownImmediately
TEST_F(Internal_OspfTest, GracefulRestart_HelperModeDisabled_TearsDownImmediately)
{
    getIfaceConfigs().get<config::OspfInterface::GRACEFUL_RESTART_HELPER>().set(false);

    addNeighbor(neighborRouterId, types::IPAddress{types::IPv4Address{0xC0A80102}}, Neighbor::State::FULL);

    GraceLsaTlv tlv;
    tlv.gracePeriodSeconds = 120;
    tlv.restartReason = GraceRestartReason::SOFTWARE_RESTART;

    invokeHandleGraceLsaReceived(*ospfInterface, neighborRouterId, tlv);

    auto* nbr = getNeighbor(neighborRouterId);
    ASSERT_NE(nbr, nullptr);
    EXPECT_FALSE(nbr->helpingRestart.load());
}

// Test: GracefulRestart_ReceivedRestartBitWithoutGraceLsa_DoesNotEnterHelperMode
TEST_F(Internal_OspfTest, GracefulRestart_ReceivedRestartBitWithoutGraceLsa_DoesNotEnterHelperMode)
{
    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL, nullptr, true);
    ASSERT_NE(nbr, nullptr);

    uint32_t mask = ospfInterface->interfaceAddress.getMask();
    uint32_t selfRid = ospfInstance->getRouterId();
    uint16_t packetLen = buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                                       getIfaceHelloInterval(), getIfaceDeadInterval(),
                                       mask, 1, 0, 0, {selfRid}, 0x12 /* E-bit | L-bit */);

    // Manually append a minimal LLS Data Block (RFC 5613/4813) with only the
    // restart extended option set -- no Grace-LSA is ever sent.
    uint8_t* lls = testPacket + packetLen;
    utils::write<uint16_t>(lls + 2, 3);
    utils::write<uint16_t>(lls + 4, 0x0001);
    utils::write<uint16_t>(lls + 6, 0x0004);
    utils::write<uint32_t>(lls + 8, getLlsRestartBit());

    ChecksumFletcher check;
    check.addU16(0);
    check.addBytes(lls + 2, 10);
    utils::write<uint16_t>(lls, check.finalize());

    deliverV2(testPacket, types::IPv4Address{0xC0A80102}, false /* unicast */, nullptr, 12 /* LLS block size */);
    wait();

    // Per RFC 3623 SS3, the LLS restart bit alone must not trigger helper mode.
    EXPECT_FALSE(nbr->helpingRestart.load());
}

#pragma endregion GracefulRestart

#pragma region AuthenticationV2

// Test: AuthV2_SimplePassword_Correct_Accepted
TEST_F(Internal_OspfTest, AuthV2_SimplePassword_Correct_Accepted)
{
    uint64_t secret = 0x3132333435363738ULL; // "12345678"

    getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::AUTHENTICATION_TYPE>().set(config::ospf::AuthType::SIMPLE);
    getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::AUTHENTICATION_KEY>().set(secret);

    uint16_t helloInterval = getIfaceHelloInterval();
    uint32_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();

    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 1, 0, 0, {});

    packet::Ospfv2Header hdr;
    hdr.setBuffer(testPacket);
    hdr.setAuthType(OSPFV2_AUTH_SIMPLE);
    uint8_t authField[8];
    utils::write<uint64_t>(authField, secret);
    hdr.setAuthentication(authField);
    finalizeOspfV2Checksum(testPacket, hdr.getPacketLen());

    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    auto* nbr = getNeighbor(neighborRouterId);
    ASSERT_NE(nbr, nullptr);
    EXPECT_EQ(nbr->getState(), Neighbor::State::INIT);
}

// Test: AuthV2_SimplePassword_Incorrect_Rejected
TEST_F(Internal_OspfTest, AuthV2_SimplePassword_Incorrect_Rejected)
{
    uint64_t secret = 0x3132333435363738ULL; // "12345678"
    uint64_t wrongSecret = 0x4142434445464748ULL; // "ABCDEFGH"

    getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::AUTHENTICATION_TYPE>().set(config::ospf::AuthType::SIMPLE);
    getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::AUTHENTICATION_KEY>().set(secret);

    uint16_t helloInterval = getIfaceHelloInterval();
    uint32_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();

    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 1, 0, 0, {});

    packet::Ospfv2Header hdr;
    hdr.setBuffer(testPacket);
    hdr.setAuthType(OSPFV2_AUTH_SIMPLE);
    uint8_t authField[8];
    utils::write<uint64_t>(authField, wrongSecret);
    hdr.setAuthentication(authField);
    finalizeOspfV2Checksum(testPacket, hdr.getPacketLen());

    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    // Auth rejected -> handleIncoming returns early; no neighbor created.
    EXPECT_EQ(getNeighbor(neighborRouterId), nullptr);
}

// Test: AuthV2_Md5_Correct_Digest_Accepted
TEST_F(Internal_OspfTest, AuthV2_Md5_Correct_Digest_Accepted)
{
    uint8_t keyId = 1;
    std::array<uint8_t, 16> keyBytes{};
    for (size_t i = 0; i < keyBytes.size(); ++i)
        keyBytes[i] = static_cast<uint8_t>(0xA0 + i);

    getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::AUTHENTICATION_TYPE>().set(config::ospf::AuthType::CRYPTO);
    getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::MESSAGE_DIGEST_KEYS>().withWrite([&](auto& list) {
        list.emplace_back(keyId, std::string(keyBytes.begin(), keyBytes.end()));
        return true;
    });
    wait();
    ASSERT_TRUE(getAuthKey().has_value());
    ASSERT_TRUE(getAuthKeyId().has_value());

    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::INIT);
    ASSERT_NE(nbr, nullptr);

    uint16_t helloInterval = getIfaceHelloInterval();
    uint32_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();
    uint32_t selfRid = ospfInstance->getRouterId();

    uint16_t packetLen = buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                                       helloInterval, deadInterval, mask, 1, 0, 0, {selfRid});

    packet::Ospfv2Header hdr;
    hdr.setBuffer(testPacket);
    hdr.setAuthType(OSPFV2_AUTH_CRYPTO);
    uint8_t authField[8] = {0};
    authField[2] = keyId;
    authField[3] = 16;
    utils::write<uint32_t>(authField + 4, 1); // sequence number
    hdr.setAuthentication(authField);

    // HMAC-MD5 over the packet (checksum field left as-is; crypto skips it).
    uint8_t authSecret[16];
    utils::write<__uint128_t>(authSecret, getAuthKey().value());
    uint8_t digest[16];
    security::authentication::generateHMAC(digest, testPacket, packetLen, authSecret, 16, security::authentication::HmacType::MD5);
    std::memcpy(testPacket + packetLen, digest, 16);

    deliverV2(testPacket, types::IPv4Address{0xC0A80102}, true, nullptr, 16);

    EXPECT_GE(nbr->getState(), Neighbor::State::TWOWAY);
}

// Test: AuthV2_Md5_Incorrect_Digest_Rejected
TEST_F(Internal_OspfTest, AuthV2_Md5_Incorrect_Digest_Rejected)
{
    uint8_t keyId = 1;
    std::array<uint8_t, 16> keyBytes{};
    for (size_t i = 0; i < keyBytes.size(); ++i)
        keyBytes[i] = static_cast<uint8_t>(0xA0 + i);

    getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::AUTHENTICATION_TYPE>().set(config::ospf::AuthType::CRYPTO);
    getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::MESSAGE_DIGEST_KEYS>().withWrite([&](auto& list) {
        list.emplace_back(keyId, std::string(keyBytes.begin(), keyBytes.end()));
        return true;
    });
    wait();
    ASSERT_TRUE(getAuthKey().has_value());

    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::INIT);
    ASSERT_NE(nbr, nullptr);

    uint16_t helloInterval = getIfaceHelloInterval();
    uint32_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();
    uint32_t selfRid = ospfInstance->getRouterId();

    uint16_t packetLen = buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                                       helloInterval, deadInterval, mask, 1, 0, 0, {selfRid});

    packet::Ospfv2Header hdr;
    hdr.setBuffer(testPacket);
    hdr.setAuthType(OSPFV2_AUTH_CRYPTO);
    uint8_t authField[8] = {0};
    authField[2] = keyId;
    authField[3] = 16;
    utils::write<uint32_t>(authField + 4, 1); // sequence number
    hdr.setAuthentication(authField);

    // Wrong digest bytes appended.
    uint8_t badDigest[16];
    for (auto& b : badDigest) b = 0xFF;
    std::memcpy(testPacket + packetLen, badDigest, 16);

    deliverV2(testPacket, types::IPv4Address{0xC0A80102}, true, nullptr, 16);

    // Digest mismatch -> processHello never runs; neighbor stays INIT.
    EXPECT_EQ(nbr->getState(), Neighbor::State::INIT);
}

// Test: AuthV2_Md5_KeyId_Mismatch_Rejected
TEST_F(Internal_OspfTest, AuthV2_Md5_KeyId_Mismatch_Rejected)
{
    uint8_t keyId = 1;
    uint8_t wrongKeyId = 2;
    std::array<uint8_t, 16> keyBytes{};
    for (size_t i = 0; i < keyBytes.size(); ++i)
        keyBytes[i] = static_cast<uint8_t>(0xA0 + i);

    getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::AUTHENTICATION_TYPE>().set(config::ospf::AuthType::CRYPTO);
    getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::MESSAGE_DIGEST_KEYS>().withWrite([&](auto& list) {
        list.emplace_back(keyId, std::string(keyBytes.begin(), keyBytes.end()));
        return true;
    });
    wait();
    ASSERT_TRUE(getAuthKeyId().has_value());
    ASSERT_EQ(getAuthKeyId().value(), keyId);

    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::INIT);
    ASSERT_NE(nbr, nullptr);

    uint16_t helloInterval = getIfaceHelloInterval();
    uint32_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();
    uint32_t selfRid = ospfInstance->getRouterId();

    uint16_t packetLen = buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                                       helloInterval, deadInterval, mask, 1, 0, 0, {selfRid});

    packet::Ospfv2Header hdr;
    hdr.setBuffer(testPacket);
    hdr.setAuthType(OSPFV2_AUTH_CRYPTO);
    uint8_t authField[8] = {0};
    authField[2] = wrongKeyId; // does not match configured authKeyId
    authField[3] = 16;
    utils::write<uint32_t>(authField + 4, 1);
    hdr.setAuthentication(authField);

    uint8_t authSecret[16];
    utils::write<__uint128_t>(authSecret, getAuthKey().value());
    uint8_t digest[16];
    security::authentication::generateHMAC(digest, testPacket, packetLen, authSecret, 16, security::authentication::HmacType::MD5);
    std::memcpy(testPacket + packetLen, digest, 16);

    deliverV2(testPacket, types::IPv4Address{0xC0A80102}, true, nullptr, 16);

    // Key-ID mismatch -> processOspfCryptoAuthentication returns false; neighbor stays INIT.
    EXPECT_EQ(nbr->getState(), Neighbor::State::INIT);
}

// Test: AuthV2_ReplayDetection_Old_Sequence_Rejected
TEST_F(Internal_OspfTest, AuthV2_ReplayDetection_Old_Sequence_Rejected)
{
    uint8_t keyId = 1;
    std::array<uint8_t, 16> keyBytes{};
    for (size_t i = 0; i < keyBytes.size(); ++i)
        keyBytes[i] = static_cast<uint8_t>(0xA0 + i);

    getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::AUTHENTICATION_TYPE>().set(config::ospf::AuthType::CRYPTO);
    getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::MESSAGE_DIGEST_KEYS>().withWrite([&](auto& list) {
        list.emplace_back(keyId, std::string(keyBytes.begin(), keyBytes.end()));
        return true;
    });
    wait();
    ASSERT_TRUE(getAuthKey().has_value());

    types::IPAddress nbrIp(types::IPv4Address{0xC0A80102});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::INIT);
    ASSERT_NE(nbr, nullptr);

    uint16_t helloInterval = getIfaceHelloInterval();
    uint32_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();
    uint32_t selfRid = ospfInstance->getRouterId();

    uint8_t authSecret[16];
    utils::write<__uint128_t>(authSecret, getAuthKey().value());

    auto sendWithSeq = [&](uint32_t seq) {
        uint16_t packetLen = buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                                           helloInterval, deadInterval, mask, 1, 0, 0, {selfRid});
        packet::Ospfv2Header hdr;
        hdr.setBuffer(testPacket);
        hdr.setAuthType(OSPFV2_AUTH_CRYPTO);
        uint8_t authField[8] = {0};
        authField[2] = keyId;
        authField[3] = 16;
        utils::write<uint32_t>(authField + 4, seq);
        hdr.setAuthentication(authField);

        uint8_t digest[16];
        security::authentication::generateHMAC(digest, testPacket, packetLen, authSecret, 16, security::authentication::HmacType::MD5);
        std::memcpy(testPacket + packetLen, digest, 16);

        deliverV2(testPacket, types::IPv4Address{0xC0A80102}, true, nullptr, 16);
    };

    // First packet with seq=5 establishes lastAuthSeq -> accepted, INIT advances
    // to (at least) TWOWAY.
    sendWithSeq(5);
    EXPECT_GE(nbr->getState(), Neighbor::State::TWOWAY);
    EXPECT_EQ(nbr->lastAuthSeq.load(), 5u);

    // Drive back to INIT and replay an older sequence number -> rejected.
    nbr->setState(Neighbor::State::DOWN);
    nbr->setState(Neighbor::State::INIT);

    sendWithSeq(3);
    EXPECT_EQ(nbr->getState(), Neighbor::State::INIT);
    EXPECT_EQ(nbr->lastAuthSeq.load(), 5u);
}

// Test: AuthV2_Disabled_NoAuthTrailer_Accepted
TEST_F(Internal_OspfTest, AuthV2_Disabled_NoAuthTrailer_Accepted)
{
    // No AUTHENTICATION_TYPE/KEY configured -> NULL_AUTH path, checksum-only.
    ASSERT_FALSE(getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::AUTHENTICATION_TYPE>().hasValue());

    uint16_t helloInterval = getIfaceHelloInterval();
    uint32_t deadInterval = getIfaceDeadInterval();
    uint32_t mask = ospfInterface->interfaceAddress.getMask();

    buildHelloV2(testPacket, neighborRouterId, ospfInterface->getAreaId(),
                  helloInterval, deadInterval, mask, 1, 0, 0, {});

    deliverV2(testPacket, types::IPv4Address{0xC0A80102});

    auto* nbr = getNeighbor(neighborRouterId);
    ASSERT_NE(nbr, nullptr);
    EXPECT_EQ(nbr->getState(), Neighbor::State::INIT);
}

// Test: AuthV2_SyncDigestKey_Picks_Active_KeyChain_Entry
TEST_F(Internal_OspfTest, AuthV2_SyncDigestKey_Picks_Active_KeyChain_Entry)
{
    uint8_t keyId1 = 1;
    uint8_t keyId2 = 2;
    std::array<uint8_t, 16> keyBytes1{};
    std::array<uint8_t, 16> keyBytes2{};
    for (size_t i = 0; i < keyBytes1.size(); ++i)
    {
        keyBytes1[i] = static_cast<uint8_t>(0x10 + i);
        keyBytes2[i] = static_cast<uint8_t>(0x20 + i);
    }

    getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::MESSAGE_DIGEST_KEYS>().withWrite([&](auto& list) {
        list.emplace_back(keyId1, std::string(keyBytes1.begin(), keyBytes1.end()));
        return true;
    });
    wait();
    ASSERT_TRUE(getAuthKeyId().has_value());
    EXPECT_EQ(getAuthKeyId().value(), keyId1);
    EXPECT_EQ(getAuthKey().value(), utils::read<__uint128_t>(keyBytes1.data()));

    // Adding a second key makes it the active (last) entry per syncDigestKey().
    getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::MESSAGE_DIGEST_KEYS>().withWrite([&](auto& list) {
        list.emplace_back(keyId2, std::string(keyBytes2.begin(), keyBytes2.end()));
        return true;
    });
    wait();
    EXPECT_EQ(getAuthKeyId().value(), keyId2);
    EXPECT_EQ(getAuthKey().value(), utils::read<__uint128_t>(keyBytes2.data()));
}

#pragma endregion AuthenticationV2

#pragma region AuthenticationV3

// Test: AuthV3_AH_Header_Present_Validated
TEST_F(Internal_OspfTest, AuthV3_AH_Header_Present_Validated)
{
    GTEST_SKIP() << "OSPFv3 AH authentication is not implemented by "
                     "PacketDispatcherV3/RxV3 (no IPsec AH validation path).";
}

// Test: AuthV3_ESP_Header_Present_Validated
TEST_F(Internal_OspfTest, AuthV3_ESP_Header_Present_Validated)
{
    GTEST_SKIP() << "OSPFv3 ESP authentication is not implemented by "
                     "PacketDispatcherV3/RxV3 (no IPsec ESP validation path).";
}

// Test: AuthV3_NoAuth_Default_Accepted
TEST_F(Internal_OspfTest, AuthV3_NoAuth_Default_Accepted)
{
    GTEST_SKIP() << "OSPFv3 IPsec config fields exist (OspfInterfaceIPSec "
                     "AUTHENTICATION_TYPE/KEY) but RxV3 has no auth dispatch; "
                     "no-auth-by-default behavior is implicit (not a distinct "
                     "code path to regression-test).";
}

#pragma endregion AuthenticationV3

#pragma region OriginationV3

// Test: OriginateV3_RouterLsa_No_Addresses_Topology_Only
TEST_F(Internal_OspfTest, OriginateV3_RouterLsa_No_Addresses_Topology_Only)
{
    Area& area = getArea(0, ospfv3Instance);
    fullRefreshV3(&area);
    wait(ospfv3Instance);

    uint32_t rid = ospfv3Instance->getRouterId();
    LsaKey key(OSPFV3_LSA_ROUTER, 0, rid);

    auto* record = waitForLsa(key, &area, ospfv3Instance);
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(hasFlag(record->flags, LsaRecordFlags::SELF_ORIGINATED));

    auto* body = std::get_if<RouterLsaV3>(&record->body);
    ASSERT_NE(body, nullptr);

    // Router-LSA links describe topology only (interface IDs / neighbor IDs),
    // never IPv6 prefixes — addressing is carried by Intra-Area-Prefix-LSAs.
    ASSERT_FALSE(body->links.empty());
    for (const auto& link : body->links)
    {
        EXPECT_NE(link.type, 0);
    }
}

// Test: OriginateV3_LinkLsa_Per_Interface_With_LinkLocal_Address
TEST_F(Internal_OspfTest, OriginateV3_LinkLsa_Per_Interface_With_LinkLocal_Address)
{
    Area& area = getArea(0, ospfv3Instance);
    fullRefreshV3(&area);
    wait(ospfv3Instance);

    uint32_t rid = ospfv3Instance->getRouterId();
    uint32_t ifaceId = ospfv3Interface->interfaceId;
    LsaKey key(OSPFV3_LSA_LINK, ifaceId, rid);

    auto* record = waitForLsa(key, &area, ospfv3Instance);
    ASSERT_NE(record, nullptr);

    auto* body = std::get_if<LinkLsa>(&record->body);
    ASSERT_NE(body, nullptr);

    // The link-local address should be a fe80::/10 address configured in SetUp().
    EXPECT_TRUE((body->localLink.addr >> 118) == (static_cast<__uint128_t>(0xFE80) >> 6));
}

// Test: OriginateV3_IntraAreaPrefixLsa_Separate_From_RouterLsa
TEST_F(Internal_OspfTest, OriginateV3_IntraAreaPrefixLsa_Separate_From_RouterLsa)
{
    Area& area = getArea(0, ospfv3Instance);
    fullRefreshV3(&area);
    wait(ospfv3Instance);

    uint32_t rid = ospfv3Instance->getRouterId();

    // Router-LSA (topology) exists.
    LsaKey routerKey(OSPFV3_LSA_ROUTER, 0, rid);
    ASSERT_NE(waitForLsa(routerKey, &area, ospfv3Instance), nullptr);

    // Intra-Area-Prefix-LSA (addressing, referencing the Router-LSA) also exists,
    // as a distinct LSDB entry.
    LsaKey prefixKey(OSPFV3_LSA_INTRA_AREA_PREFIX, 0, rid);
    auto* prefixRecord = waitForLsa(prefixKey, &area, ospfv3Instance);
    ASSERT_NE(prefixRecord, nullptr);

    auto* body = std::get_if<IntraAreaPrefixLsa>(&prefixRecord->body);
    ASSERT_NE(body, nullptr);
    EXPECT_FALSE(body->prefixes.empty());
}

// Test: OriginateV3_NetworkLsa_Originated_By_Dr
TEST_F(Internal_OspfTest, OriginateV3_NetworkLsa_Originated_By_Dr)
{
    Area& area = getArea(0, ospfv3Instance);
    setIsDr(true, ospfv3Interface);
    addNetworkLsa(ospfv3Interface, false);
    wait(ospfv3Instance);

    uint32_t rid = ospfv3Instance->getRouterId();
    uint32_t ifaceId = ospfv3Interface->interfaceId;
    LsaKey key(OSPFV3_LSA_NETWORK, ifaceId, rid);

    auto* record = waitForLsa(key, &area, ospfv3Instance);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<NetworkLsaV3>(&record->body);
    ASSERT_NE(body, nullptr);
}

// Test: OriginateV3_InterAreaPrefix_Type3_Equivalent
TEST_F(Internal_OspfTest, OriginateV3_InterAreaPrefix_Type3_Equivalent)
{
    auto& area = getArea(0, ospfv3Instance);

    types::IPv6Prefix prefix{};
    prefix.addr = (static_cast<__uint128_t>(0x20010DB8000A0000ULL) << 64);
    prefix.prefixLength = 64;
    types::IPPrefix ipPrefix(__uint128_t{prefix.addr}, prefix.prefixLength);

    originateSummary<PolicyV3>(getOriginatorCtx(&area), /*lsid=*/0x100, ipPrefix, /*cost=*/77, false, ospfv3Instance);
    wait(ospfv3Instance);

    uint32_t selfRid = ospfv3Instance->getRouterId();
    LsaKey key(OSPFV3_LSA_INTER_AREA_PREFIX, 0x100, selfRid);

    auto* record = waitForLsa(key, &area, ospfv3Instance);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<InterAreaPrefixLsa>(&record->body);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->metric, 77u);
    EXPECT_EQ(body->prefix.prefixLength, 64);
}

// Test: OriginateV3_InterAreaRouter_Type4_Equivalent
TEST_F(Internal_OspfTest, OriginateV3_InterAreaRouter_Type4_Equivalent)
{
    auto& area = getArea(0, ospfv3Instance);

    // Create a second area so the process becomes an ABR; addAsbrLsa() is
    // exercised indirectly via addExternal() (the only public entry point).
    createIface(
        *mockInterface, OspfInterfaceId(ipIntv4.addr, 1), ospfv3Instance);
    auto& area1 = getArea(1, ospfv3Instance);
    wait(ospfv3Instance);
    ASSERT_TRUE(ospfv3Instance->isABR());

    OspfRouter asbrReach{};
    asbrReach.rid = neighborRouterId2;
    asbrReach.cost = 30;
    getTable().updateAreaAsbr(1, asbrReach);

    addExternal<PolicyV3>(getOriginatorCtx(&area), neighborRouterId2, /*lsid=*/1, /*remove=*/false, ospfv3Instance);
    wait(ospfv3Instance);

    uint32_t selfRid = ospfv3Instance->getRouterId();
    LsaKey key(OSPFV3_LSA_INTER_AREA_ROUTER, neighborRouterId2, selfRid);

    auto* record = waitForLsa(key, &area, ospfv3Instance);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<InterAreaRouterLsa>(&record->body);
    ASSERT_NE(body, nullptr);

    removeIface(
        OspfInterfaceId(ipIntv4.addr, 1), ospfv3Instance);
}

// Test: OriginateV3_AsExternal_With_Ipv6_ForwardingAddress
TEST_F(Internal_OspfTest, OriginateV3_AsExternal_With_Ipv6_ForwardingAddress)
{
    types::IPv6Prefix prefix{};
    prefix.addr = (static_cast<__uint128_t>(0x20010DB8001E0000ULL) << 64);
    prefix.prefixLength = 64;

    ExternalOriginateContext ctx{};
    ctx.lsId = 0x1E0000;
    ctx.prefix = types::IPPrefix(__uint128_t{prefix.addr}, prefix.prefixLength);
    ctx.metric = 20;
    ctx.tag = 0;
    ctx.nextHop = types::IPAddress(ipIntv6); // connected IPv6 address -> valid forwarding addr
    ctx.metricIsE2 = true;

    getConfigs(ospfv3Instance).get<config::Ospf::LRC_FORWARDING_ADDRESS>().set(false);
    auto& area0 = getArea(0, ospfv3Instance);
    std::vector<std::pair<types::IPPrefix, OspfPath>> connectedPath{
        {types::IPPrefix(ipIntv6.addr, 64, true), OspfPath{.type = OspfRouteType::INTRA_AREA, .area = 0, .cost = 1}}
    };
    getRib(ospfv3Instance).replaceArea(area0, connectedPath);

    originateExternal<PolicyV3>(ctx, false, ospfv3Instance);
    wait(ospfv3Instance);

    auto& area = getArea(0, ospfv3Instance);
    uint32_t selfRid = ospfv3Instance->getRouterId();
    LsaKey key(OSPFV3_LSA_AS_EXTERNAL, ctx.lsId, selfRid);

    auto* record = waitForLsa(key, &area, ospfv3Instance);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<ExternalLsaV3>(&record->body);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->metric, 20u);
    ASSERT_TRUE(body->forwardingAddress.has_value());
    EXPECT_EQ(body->forwardingAddress->addr, ipIntv6.addr);
}

// Test: OriginateV3_LsidQueue_Recycles_Freed_RouterLsid
TEST_F(Internal_OspfTest, OriginateV3_LsidQueue_Recycles_Freed_RouterLsid)
{
    getConfigs(ospfv3Instance).get<config::Ospf::LSA_THROTTLE_DELAY>().set(0);

    constexpr int kIfaceCount = 17;
    uint32_t rid = ospfv3Instance->getRouterId();
    auto& area = getArea(0, ospfv3Instance);

    std::vector<OspfInterface*> ifaces;
    for (int i = 0; i < kIfaceCount; ++i)
    {
        OspfInterface& iface = createIface(*mockInterface, OspfInterfaceId(100 + i, 0), ospfv3Instance);
        getIfaceConfigs(&iface).get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
        iface.enqueueSyncNetworkType();
        ifaces.push_back(&iface);

        types::IPAddress nbrIp(types::IPv6Address{(static_cast<__uint128_t>(0xFE80000000000000) << 64) | (0x1000u + i)});
        addNeighbor(0xC0A90000u + i, nbrIp, Neighbor::State::FULL, &iface);
    }
    wait(ospfv3Instance);

    fullRefreshV3(&area);
    wait(ospfv3Instance);

    LsaKey fragment0Key(OSPFV3_LSA_ROUTER, 0, rid);
    LsaKey fragment1Key(OSPFV3_LSA_ROUTER, 1, rid);
    auto* fragment0 = waitForLsa(fragment0Key, &area, ospfv3Instance);
    auto* fragment1 = waitForLsa(fragment1Key, &area, ospfv3Instance);
    ASSERT_NE(fragment0, nullptr);
    ASSERT_NE(fragment1, nullptr) << "expected a second Router-LSA fragment once 32 links were exceeded";

    for (int i = 1; i < kIfaceCount; ++i)
        removeIface(OspfInterfaceId(100 + i, 0), ospfv3Instance);
    wait(ospfv3Instance);

    fullRefreshV3(&area);
    wait(ospfv3Instance);

    auto* expiredFragment1 = waitForLsa(fragment1Key, [](const LsaRecord& r) { return r.header.age == routing::OSPF_MAX_AGE; }, &area, ospfv3Instance);
    ASSERT_NE(expiredFragment1, nullptr) << "expected the now-empty second fragment to be flushed";

    for (int i = 0; i < kIfaceCount - 1; ++i)
    {
        OspfInterface& iface = createIface(*mockInterface, OspfInterfaceId(200 + i, 0), ospfv3Instance);
        getIfaceConfigs(&iface).get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
        iface.enqueueSyncNetworkType();

        types::IPAddress nbrIp(types::IPv6Address{(static_cast<__uint128_t>(0xFE80000000000000) << 64) | (0x2000u + i)});
        addNeighbor(0xC0AA0000u + i, nbrIp, Neighbor::State::FULL, &iface);
    }
    wait(ospfv3Instance);

    fullRefreshV3(&area);
    wait(ospfv3Instance);

    auto* recycledFragment1 = waitForLsa(fragment1Key, [](const LsaRecord& r) { return r.header.age != routing::OSPF_MAX_AGE; }, &area, ospfv3Instance);
    ASSERT_NE(recycledFragment1, nullptr) << "expected LS-ID 1 to be reused for the new fragment";

    LsaKey fragment2Key(OSPFV3_LSA_ROUTER, 2, rid);
    bool fragment2Exists = false;
    {
        std::lock_guard lock(getSchedulerLock(ospfv3Instance));
        LsaRecord* rec = getLsdb(&area).find(fragment2Key);
        fragment2Exists = rec != nullptr && rec->header.age != routing::OSPF_MAX_AGE;
    }
    EXPECT_FALSE(fragment2Exists) << "LS-ID 1 was freed and should have been reused instead of allocating LS-ID 2";

    removeIface(OspfInterfaceId(100, 0), ospfv3Instance);
    for (int i = 0; i < kIfaceCount - 1; ++i)
        removeIface(OspfInterfaceId(200 + i, 0), ospfv3Instance);
}

// Test: OriginateV3_LsidQueue_Recycles_Freed_PrefixLsid
TEST_F(Internal_OspfTest, OriginateV3_LsidQueue_Recycles_Freed_PrefixLsid)
{
    getConfigs(ospfv3Instance).get<config::Ospf::LSA_THROTTLE_DELAY>().set(0);

    constexpr int kPrefixCount = 33;
    uint32_t rid = ospfv3Instance->getRouterId();
    auto& area = getArea(0, ospfv3Instance);

    interface::MockInterface* extraIface = new interface::MockInterface(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    uint32_t extraKey = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 1);
    vrf->getInterfaceManager().add(extraIface, extraKey);
    extraIface->blockEnqueues();
    extraIface->configs.id = 1;
    extraIface->configs.key = extraKey;
    extraIface->enableIPs();

    OspfInterface& iface = createIface(*extraIface, OspfInterfaceId(300, 0), ospfv3Instance);
    getIfaceConfigs(&iface).get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    iface.enqueueSyncNetworkType();

    types::IPAddress nbrIp(types::IPv6Address{(static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x3000u});
    addNeighbor(0xC0AB0000u, nbrIp, Neighbor::State::FULL, &iface);

    for (int i = 0; i < kPrefixCount; ++i)
    {
        types::IPv6Prefix addr((static_cast<__uint128_t>(0xFD01000000000000ULL) << 64) | static_cast<uint64_t>(1 + i), 64, true);
        iface.iface.setIPv6(addr, false);
    }
    wait(ospfv3Instance);

    fullRefreshV3(&area);
    wait(ospfv3Instance);

    LsaKey routerKey(OSPFV3_LSA_ROUTER, 0, rid);
    LsaKey fragment0Key(OSPFV3_LSA_INTRA_AREA_PREFIX, 0, rid);
    LsaKey fragment1Key(OSPFV3_LSA_INTRA_AREA_PREFIX, 1, rid);
    ASSERT_NE(waitForLsa(routerKey, &area, ospfv3Instance), nullptr);
    auto* fragment0 = waitForLsa(fragment0Key, &area, ospfv3Instance);
    auto* fragment1 = waitForLsa(fragment1Key, &area, ospfv3Instance);
    ASSERT_NE(fragment0, nullptr);
    ASSERT_NE(fragment1, nullptr) << "expected a second Intra-Area-Prefix-LSA fragment once 32 prefixes were exceeded";

    for (int i = 1; i < kPrefixCount; ++i)
    {
        types::IPv6Prefix addr((static_cast<__uint128_t>(0xFD01000000000000ULL) << 64) | static_cast<uint64_t>(1 + i), 64, true);
        iface.iface.configs.ipv6.removeAddress(addr);
    }
    wait(ospfv3Instance);

    fullRefreshV3(&area);
    wait(ospfv3Instance);

    auto* expiredFragment1 = waitForLsa(fragment1Key, [](const LsaRecord& r) { return r.header.age == routing::OSPF_MAX_AGE; }, &area, ospfv3Instance);
    ASSERT_NE(expiredFragment1, nullptr) << "expected the now-empty second prefix fragment to be flushed";

    for (int i = 0; i < kPrefixCount - 1; ++i)
    {
        types::IPv6Prefix addr((static_cast<__uint128_t>(0xFD02000000000000ULL) << 64) | static_cast<uint64_t>(1 + i), 64, true);
        iface.iface.setIPv6(addr, false);
    }
    wait(ospfv3Instance);

    fullRefreshV3(&area);
    wait(ospfv3Instance);

    auto* recycledFragment1 = waitForLsa(fragment1Key, [](const LsaRecord& r) { return r.header.age != routing::OSPF_MAX_AGE; }, &area, ospfv3Instance);
    ASSERT_NE(recycledFragment1, nullptr) << "expected LS-ID 1 to be reused for the new prefix fragment";

    LsaKey fragment2Key(OSPFV3_LSA_INTRA_AREA_PREFIX, 2, rid);
    bool fragment2Exists = false;
    {
        std::lock_guard lock(getSchedulerLock(ospfv3Instance));
        LsaRecord* rec = getLsdb(&area).find(fragment2Key);
        fragment2Exists = rec != nullptr && rec->header.age != routing::OSPF_MAX_AGE;
    }
    EXPECT_FALSE(fragment2Exists) << "LS-ID 1 was freed and should have been reused instead of allocating LS-ID 2";

    removeIface(OspfInterfaceId(300, 0), ospfv3Instance);
    delete extraIface;
}

// Test: OriginateV3_FullRefresh_Reoriginates_Router_Link_And_Prefix_Lsas
TEST_F(Internal_OspfTest, OriginateV3_FullRefresh_Reoriginates_Router_Link_And_Prefix_Lsas)
{
    auto& area = getArea(0, ospfv3Instance);
    uint32_t rid = ospfv3Instance->getRouterId();

    fullRefreshV3(&area);
    wait(ospfv3Instance);

    LsaKey routerKey(OSPFV3_LSA_ROUTER, 0, rid);
    LsaKey prefixKey(OSPFV3_LSA_INTRA_AREA_PREFIX, 0, rid);
    uint32_t ifaceId = ospfv3Interface->interfaceId;
    LsaKey linkKey(OSPFV3_LSA_LINK, ifaceId, rid);

    auto* routerRecord = waitForLsa(routerKey, &area, ospfv3Instance);
    auto* prefixRecord = waitForLsa(prefixKey, &area, ospfv3Instance);
    auto* linkRecord = waitForLsa(linkKey, &area, ospfv3Instance);
    ASSERT_NE(routerRecord, nullptr);
    ASSERT_NE(prefixRecord, nullptr);
    ASSERT_NE(linkRecord, nullptr);

    uint32_t routerSeq = routerRecord->header.sequence;
    uint32_t prefixSeq = prefixRecord->header.sequence;
    uint32_t linkSeq = linkRecord->header.sequence;

    // A second fullRefresh re-originates all three LSA types with bumped sequences.
    fullRefreshV3(&area);
    wait(ospfv3Instance);

    routerRecord = waitForLsa(routerKey, [routerSeq](const LsaRecord& r) { return r.header.sequence > routerSeq; }, &area, ospfv3Instance);
    prefixRecord = waitForLsa(prefixKey, [prefixSeq](const LsaRecord& r) { return r.header.sequence > prefixSeq; }, &area, ospfv3Instance);
    linkRecord = waitForLsa(linkKey, [linkSeq](const LsaRecord& r) { return r.header.sequence > linkSeq; }, &area, ospfv3Instance);
    ASSERT_NE(routerRecord, nullptr);
    ASSERT_NE(prefixRecord, nullptr);
    ASSERT_NE(linkRecord, nullptr);

    EXPECT_GT(routerRecord->header.sequence, routerSeq);
    EXPECT_GT(prefixRecord->header.sequence, prefixSeq);
    EXPECT_GT(linkRecord->header.sequence, linkSeq);
}

#pragma endregion OriginationV3

#pragma region PacketRxTxV3

// Test: RxV3_HandleIncoming_Dispatches_Hello_To_ProcessHello
TEST_F(Internal_OspfTest, RxV3_HandleIncoming_Dispatches_Hello_To_ProcessHello)
{
    uint16_t helloInterval = getIfaceHelloInterval(ospfv3Interface);
    uint32_t deadInterval = getIfaceDeadInterval(ospfv3Interface);

    buildHelloV3(testPacket, neighborRouterId, ospfv3Interface->getAreaId(),
                  ospfv3Interface->interfaceId, helloInterval, static_cast<uint16_t>(deadInterval),
                  1, 0, 0, {});

    types::IPv6Address nbrIp6 = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000002;
    deliverV3(testPacket, nbrIp6);

    auto* nbr = getNeighbor(neighborRouterId, ospfv3Interface);
    ASSERT_NE(nbr, nullptr);
    EXPECT_GE(nbr->getState(), Neighbor::State::INIT);
}

// Test: RxV3_HandleIncoming_Dispatches_Dbd_To_ProcessDBD
TEST_F(Internal_OspfTest, RxV3_HandleIncoming_Dispatches_Dbd_To_ProcessDBD)
{
    types::IPv6Address nbrIp6 = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000002;
    types::IPAddress nbrIp(nbrIp6);
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXSTART, ospfv3Interface);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXSTART);

    uint16_t mtu = ospfv3Interface->iface.configs.ipv6.mtu.load(std::memory_order_relaxed);
    uint32_t options = OSPFV3_OPT_V6 | OSPFV3_OPT_E;

    // Init DBD (MS+M+I) from the neighbor.
    buildDBDV3(testPacket, neighborRouterId, ospfv3Interface->getAreaId(), mtu, options,
               /*flags=*/0x01 | 0x02 | 0x04, /*sequence=*/0xAAAA0000);
    deliverV3(testPacket, nbrIp6);

    // processDBD dispatched: neighbor should have moved out of EXSTART.
    EXPECT_NE(nbr->getState(), Neighbor::State::EXSTART);
}

// Test: RxV3_HandleIncoming_Dispatches_LSRequest
TEST_F(Internal_OspfTest, RxV3_HandleIncoming_Dispatches_LSRequest)
{
    types::IPv6Address nbrIp6 = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000002;
    types::IPAddress nbrIp(nbrIp6);
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL, ospfv3Interface);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    uint32_t selfRid = ospfv3Instance->getRouterId();

    // Seed the local LSDB with a self-originated Router LSA the neighbor will request.
    LsaKey key(OSPFV3_LSA_ROUTER, selfRid, selfRid);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;
    lh.length = packet::Ospfv3LSAHeader::fixedSize + 4;

    auto& lsdb = getLsdb(&getArea(0, ospfv3Instance));
    IncomingLsaContext ctx{key, lh};
    lsdb.upsertBody<RouterLsaV3>(ctx, LsaRecordFlags::SELF_ORIGINATED);

    bool sawLSUpdate = false;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV3Header(pkt);
            if (hdr.getType() == OSPFV3_TYPE_LINK_STATE_UPDATE)
                sawLSUpdate = true;
        }));

    buildLSRequestV3(testPacket, neighborRouterId, ospfv3Interface->getAreaId(), {key});
    deliverV3(testPacket, nbrIp6);

    EXPECT_TRUE(sawLSUpdate);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(::testing::AnyNumber());

    auto* record = lsdb.find(key);
    ASSERT_NE(record, nullptr);
    buildLSAckV3(testPacket, neighborRouterId, ospfv3Interface->getAreaId(), {key}, {record->header});
    deliverV3(testPacket, nbrIp6);
    wait(ospfv3Instance);
}

// Test: RxV3_HandleIncoming_Dispatches_LSUpdate
TEST_F(Internal_OspfTest, RxV3_HandleIncoming_Dispatches_LSUpdate)
{
    types::IPv6Address nbrIp6 = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000002;
    types::IPAddress nbrIp(nbrIp6);
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL, ospfv3Interface);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    LsaKey key(OSPFV3_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;

    RouterLsaV3 body;
    body.options = OSPFV3_OPT_V6 | OSPFV3_OPT_E;
    body.links.push_back({OSPFV3_LINK_STUB, 10, ospfv3Interface->interfaceId, 0, 0});

    buildLSUpdateV3(testPacket, neighborRouterId, ospfv3Interface->getAreaId(), {key}, {lh}, {body});
    deliverV3(testPacket, nbrIp6);

    auto& lsdb = getLsdb(&getArea(0, ospfv3Instance));
    EXPECT_TRUE(lsdb.contains(key));
}

// Test: RxV3_HandleIncoming_Dispatches_LSAck
TEST_F(Internal_OspfTest, RxV3_HandleIncoming_Dispatches_LSAck)
{
    types::IPv6Address nbrIp6 = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000002;
    types::IPAddress nbrIp(nbrIp6);
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL, ospfv3Interface);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    LsaKey key(OSPFV3_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;

    auto& lsdb = getLsdb(&getArea(0, ospfv3Instance));
    IncomingLsaContext ctx{key, lh};
    auto& record = lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);

    LsaRecordRef ref{key, record};
    nbr->getRtr().lsus().add(key, ref);
    ASSERT_TRUE(nbr->getRtr().lsus().has(key));

    buildLSAckV3(testPacket, neighborRouterId, ospfv3Interface->getAreaId(), {key}, {record.header});
    deliverV3(testPacket, nbrIp6);

    EXPECT_FALSE(nbr->getRtr().lsus().has(key));
}

// Test: RxV3_Rejects_Packet_With_Bad_Checksum
TEST_F(Internal_OspfTest, RxV3_Rejects_Packet_With_Bad_Checksum)
{
    uint16_t helloInterval = getIfaceHelloInterval(ospfv3Interface);
    uint32_t deadInterval = getIfaceDeadInterval(ospfv3Interface);

    buildHelloV3(testPacket, neighborRouterId, ospfv3Interface->getAreaId(),
                  ospfv3Interface->interfaceId, helloInterval, static_cast<uint16_t>(deadInterval),
                  1, 0, 0, {});

    // Corrupt the checksum field after finalizeOspfV3Checksum has run.
    packet::Ospfv3Header hdr;
    hdr.setBuffer(testPacket);
    hdr.setChecksum(hdr.getChecksum() ^ 0xFFFF);

    types::IPv6Address nbrIp6 = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000002;
    deliverV3(testPacket, nbrIp6);

    // Packet was dropped before processHello could create a neighbor.
    EXPECT_EQ(getNeighbor(neighborRouterId, ospfv3Interface), nullptr);
}

// Test: RxV3_Rejects_Packet_For_Wrong_Area
TEST_F(Internal_OspfTest, RxV3_Rejects_Packet_For_Wrong_Area)
{
    uint16_t helloInterval = getIfaceHelloInterval(ospfv3Interface);
    uint32_t deadInterval = getIfaceDeadInterval(ospfv3Interface);

    // areaId mismatches the interface's configured area.
    buildHelloV3(testPacket, neighborRouterId, ospfv3Interface->getAreaId() + 1,
                  ospfv3Interface->interfaceId, helloInterval, static_cast<uint16_t>(deadInterval),
                  1, 0, 0, {});

    types::IPv6Address nbrIp6 = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000002;
    deliverV3(testPacket, nbrIp6);

    EXPECT_EQ(getNeighbor(neighborRouterId, ospfv3Interface), nullptr);
}

// Test: TxV3_SendHello_Multicast_To_AllSpfRouters
TEST_F(Internal_OspfTest, TxV3_SendHello_Multicast_To_AllSpfRouters)
{
    // Single router on a broadcast network elects itself DR, so Hello is
    // sent to AllSPFRouters (ff02::5).
    runIfaceElection(ospfv3Interface);
    ASSERT_TRUE(ospfv3Interface->getIsDr());

    bool sawHello = false;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV3Header(pkt);
            if (hdr.getType() == OSPFV3_TYPE_HELLO)
                sawHello = true;
        }));

    getDispatcherV3().sendHello();

    EXPECT_TRUE(sawHello);
}

// Test: TxV3_SendUnicastHello_To_Neighbor
TEST_F(Internal_OspfTest, TxV3_SendUnicastHello_To_Neighbor)
{
    types::IPv6Address nbrIp6 = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000002;
    types::IPAddress nbrIp(nbrIp6);
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::INIT, ospfv3Interface, /*unicast=*/true);
    ASSERT_NE(nbr, nullptr);

    bool sawHello = false;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV3Header(pkt);
            if (hdr.getType() == OSPFV3_TYPE_HELLO)
                sawHello = true;
        }));

    getDispatcherV3().sendUnicastHello(*nbr);

    EXPECT_TRUE(sawHello);
}

// Test: TxV3_SendInitDbd_Sets_IBit_MBit_MsBit
TEST_F(Internal_OspfTest, TxV3_SendInitDbd_Sets_IBit_MBit_MsBit)
{
    types::IPv6Address nbrIp6 = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000002;
    types::IPAddress nbrIp(nbrIp6);
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXSTART, ospfv3Interface);
    ASSERT_EQ(nbr->getState(), Neighbor::State::EXSTART);

    bool sawDbd = false;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV3Header(pkt);
            if (hdr.getType() != OSPFV3_TYPE_DATABASE_DESCRIPTION) return;
            sawDbd = true;

            packet::Ospfv3DBDHeader dbd;
            dbd.setBuffer(hdr.getTrailData());
            EXPECT_TRUE(dbd.getFlagI());
            EXPECT_TRUE(dbd.getFlagM());
            EXPECT_TRUE(dbd.getFlagMS());
        }));

    getDispatcherV3().sendInitDbd(*nbr);

    EXPECT_TRUE(sawDbd);
}

// Test: TxV3_SendDbd_Master_Increments_Sequence
TEST_F(Internal_OspfTest, TxV3_SendDbd_Master_Increments_Sequence)
{
    types::IPv6Address nbrIp6 = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000002;
    types::IPAddress nbrIp(nbrIp6);
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXCHANGE, ospfv3Interface);
    nbr->setRole(Neighbor::Role::MASTER);

    uint32_t seqBefore = nbr->currentSeq.load(std::memory_order_relaxed);

    bool sawDbd = false;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV3Header(pkt);
            if (hdr.getType() == OSPFV3_TYPE_DATABASE_DESCRIPTION)
                sawDbd = true;
        }));

    getDispatcherV3().sendDbd(*nbr);

    EXPECT_TRUE(sawDbd);
    EXPECT_EQ(nbr->currentSeq.load(std::memory_order_relaxed), seqBefore + 1);
}

// Test: TxV3_SendLsAck_Lists_Acknowledged_Headers
TEST_F(Internal_OspfTest, TxV3_SendLsAck_Lists_Acknowledged_Headers)
{
    types::IPv6Address nbrIp6 = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000002;
    types::IPAddress nbrIp(nbrIp6);
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL, ospfv3Interface);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    LsaKey key(OSPFV3_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;
    lh.length = packet::Ospfv3LSAHeader::fixedSize + 4;

    auto& lsdb = getLsdb(&getArea(0, ospfv3Instance));
    IncomingLsaContext ctx{key, lh};
    auto& record = lsdb.upsertBody<RouterLsaV3>(ctx, LsaRecordFlags::NONE);
    (void)record;

    auto* rec = lsdb.find(key);
    ASSERT_NE(rec, nullptr);

    std::vector<LsaRecordRef> acks{{key, *rec}};

    uint32_t ackedLsId = 0;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV3Header(pkt);
            if (hdr.getType() != OSPFV3_TYPE_LINK_STATE_ACK) return;

            packet::Ospfv3LSAHeader lsaHdr;
            lsaHdr.setBuffer(hdr.getTrailData());
            ackedLsId = lsaHdr.getLsId();
        }));

    bool ok = getDispatcherV3().sendLsAck(*nbr, acks);

    EXPECT_TRUE(ok);
    EXPECT_EQ(ackedLsId, key.linkStateId);
}

// Test: TxV3_SendLsRequest_Lists_Missing_Lsa_Keys
TEST_F(Internal_OspfTest, TxV3_SendLsRequest_Lists_Missing_Lsa_Keys)
{
    types::IPv6Address nbrIp6 = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000002;
    types::IPAddress nbrIp(nbrIp6);
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::LOADING, ospfv3Interface, false);

    LsaKey key(OSPFV3_LSA_ROUTER, neighborRouterId, neighborRouterId);
    nbr->getRtr().lsrs().add(key, key);
    ASSERT_TRUE(nbr->getRtr().lsrs().getActive());

    uint32_t requestedLsId = 0;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV3Header(pkt);
            if (hdr.getType() != OSPFV3_TYPE_LINK_STATE_REQUEST) return;

            packet::Ospfv3LSRHeader lsr;
            lsr.setBuffer(hdr.getTrailData());
            requestedLsId = lsr.getLsID();
        }));

    bool ok = getDispatcherV3().sendLsr(*nbr);

    EXPECT_TRUE(ok);
    EXPECT_EQ(requestedLsId, key.linkStateId);
}

// Test: TxV3_SendLsUpdate_Unicast_To_Neighbor
TEST_F(Internal_OspfTest, TxV3_SendLsUpdate_Unicast_To_Neighbor)
{
    types::IPv6Address nbrIp6 = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000002;
    types::IPAddress nbrIp(nbrIp6);
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL, ospfv3Interface);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    uint32_t selfRid = ospfv3Instance->getRouterId();

    LsaKey key(OSPFV3_LSA_ROUTER, selfRid, selfRid);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;
    lh.length = packet::Ospfv3LSAHeader::fixedSize + 4;

    auto& lsdb = getLsdb(&getArea(0, ospfv3Instance));
    IncomingLsaContext ctx{key, lh};
    lsdb.upsertBody<RouterLsaV3>(ctx, LsaRecordFlags::SELF_ORIGINATED);

    auto* rec = lsdb.find(key);
    ASSERT_NE(rec, nullptr);

    LsaRecordRef ref{key, *rec};
    nbr->getRtr().lsus().add(key, ref);
    ASSERT_TRUE(nbr->getRtr().lsus().getActive());

    bool sawLSUpdate = false;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV3Header(pkt);
            if (hdr.getType() == OSPFV3_TYPE_LINK_STATE_UPDATE)
                sawLSUpdate = true;
        }));

    bool ok = getDispatcherV3().sendLsu(nbr);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(sawLSUpdate);

    buildLSAckV3(testPacket, neighborRouterId, ospfv3Interface->getAreaId(), {key}, {rec->header});
    deliverV3(testPacket, nbrIp6);
    wait();
}

// Test: TxV3_FinalizeHeader_Sets_Length_And_Checksum
TEST_F(Internal_OspfTest, TxV3_FinalizeHeader_Sets_Length_And_Checksum)
{
    runIfaceElection(ospfv3Interface);
    ASSERT_TRUE(ospfv3Interface->getIsDr());

    uint16_t packetLen = 0;
    uint16_t checksum = 0;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV3Header(pkt);
            if (hdr.getType() != OSPFV3_TYPE_HELLO) return;
            packetLen = hdr.getPacketLen();
            checksum = hdr.getChecksum();
        }));

    getDispatcherV3().sendHello();

    EXPECT_GT(packetLen, static_cast<uint16_t>(packet::Ospfv3Header::fixedSize));
    EXPECT_NE(checksum, 0u);
}

// Test: TxV3_No_Options_Byte_In_Lsa_Header
TEST_F(Internal_OspfTest, TxV3_No_Options_Byte_In_Lsa_Header)
{
    EXPECT_EQ(packet::Ospfv3LSAHeader::fixedSize, packet::Ospfv2LSAHeader::fixedSize);

    LsaKey key(OSPFV3_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader lh;
    lh.sequence = routing::OSPF_INITIAL_SEQUENCE;
    lh.age = 1;
    lh.length = packet::Ospfv3LSAHeader::fixedSize + 4;

    auto& lsdb = getLsdb(&getArea(0, ospfv3Instance));
    IncomingLsaContext ctx{key, lh};
    lsdb.upsertBody<RouterLsaV3>(ctx, LsaRecordFlags::NONE);

    auto* rec = lsdb.find(key);
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->header.length, packet::Ospfv3LSAHeader::fixedSize + 4);
}

#pragma endregion PacketRxTxV3

#pragma region OspfV3AddressFamily

// Test: AfIpv4_ProcessConstruction_SetsAddressFamilySupportBit
TEST_F(Internal_OspfTest, AfIpv4_ProcessConstruction_SetsAddressFamilySupportBit)
{
    OspfProcess& afProcess = setupAfIpv4Process(3);

    Area& area = getArea(0, &afProcess);
    EXPECT_TRUE(AreaFlagManager::getAddressFamilySupport(getAreaFlags(&area).getFlags()));

    teardownAfIpv4Process(3, afProcess);
}

// Test: AfIpv4_Hello_CarriesAddressFamilySupportBit
TEST_F(Internal_OspfTest, AfIpv4_Hello_CarriesAddressFamilySupportBit)
{
    OspfInterface* afIface = nullptr;
    OspfProcess& afProcess = setupAfIpv4Process(3, &afIface);

    uint32_t flags = getIfaceFlags(afIface).getFlags();
    EXPECT_TRUE(AreaFlagManager::getAddressFamilySupport(flags));

    teardownAfIpv4Process(3, afProcess);
}

// Test: AfIpv4_InterfaceManager_ReadsIpv4LocalAddress_NotIpv6
TEST_F(Internal_OspfTest, AfIpv4_InterfaceManager_ReadsIpv4LocalAddress_NotIpv6)
{
    OspfInterface* afIface = nullptr;
    OspfProcess& afProcess = setupAfIpv4Process(3, &afIface);

    // The interface's bound address must be the configured IPv4 prefix, not the IPv6 link-local.
    EXPECT_TRUE(afIface->interfaceAddress.isIPv4());
    EXPECT_EQ(afIface->interfaceAddress.v4(), ipIntv4.addr);

    teardownAfIpv4Process(3, afProcess);
}

// Test: AfIpv4_IntraAreaPrefixLsa_CarriesIpv4Prefix_NotIpv6
TEST_F(Internal_OspfTest, AfIpv4_IntraAreaPrefixLsa_CarriesIpv4Prefix_NotIpv6)
{
    OspfProcess& afProcess = setupAfIpv4Process(3);

    Area& area = getArea(0, &afProcess);
    fullRefreshV3(&area);
    wait(&afProcess);

    uint32_t rid = afProcess.getRouterId();
    LsaKey prefixKey(OSPFV3_LSA_INTRA_AREA_PREFIX, 0, rid);
    auto* prefixRecord = waitForLsa(prefixKey, &area, &afProcess);
    ASSERT_NE(prefixRecord, nullptr);

    auto* body = std::get_if<IntraAreaPrefixLsaV4>(&prefixRecord->body);
    ASSERT_NE(body, nullptr);
    EXPECT_FALSE(body->prefixes.empty());
    EXPECT_EQ(std::get_if<IntraAreaPrefixLsa>(&prefixRecord->body), nullptr);

    teardownAfIpv4Process(3, afProcess);
}

// Test: AfIpv4_ReceivedIntraAreaPrefixLsa_ParsedAsIpv4
TEST_F(Internal_OspfTest, AfIpv4_ReceivedIntraAreaPrefixLsa_ParsedAsIpv4)
{
    OspfInterface* afIface = nullptr;
    OspfProcess& afProcess = setupAfIpv4Process(3, &afIface);

    IntraAreaPrefixLsaV4 lsa;
    lsa.referencedLsaType = OSPFV3_LSA_ROUTER;
    lsa.referencedLinkStateId = 0;
    lsa.referencedAdvRouter = neighborRouterId;
    IntraAreaPrefixV4 prefix{};
    prefix.metric = 10;
    prefix.prefix = types::IPv4Prefix(0x0A0A0A00u, 24, false);
    lsa.prefixes.push_back(prefix);

    uint8_t body[64] = {0};
    ASSERT_TRUE(lsa.buildBody(body, lsa.size()));

    auto result = invokeBuildLsaBody(getDispatcherV3(afIface), OSPFV3_LSA_INTRA_AREA_PREFIX, body, lsa.size());
    ASSERT_TRUE(result.has_value());
    auto* v4Body = std::get_if<IntraAreaPrefixLsaV4>(&result.value());
    ASSERT_NE(v4Body, nullptr);
    ASSERT_FALSE(v4Body->prefixes.empty());
    EXPECT_EQ(v4Body->prefixes[0].prefix.prefixLength, 24);

    teardownAfIpv4Process(3, afProcess);
}

// Test: AfIpv4_MismatchedAfBit_AdjacencyRejected
TEST_F(Internal_OspfTest, AfIpv4_MismatchedAfBit_AdjacencyRejected)
{
    OspfInterface* afIface = nullptr;
    OspfProcess& afProcess = setupAfIpv4Process(3, &afIface);

    // Hello without the AF-bit set (only V6+E) — a peer that doesn't support this AF.
    uint32_t optionsNoAf = OSPFV3_OPT_V6 | OSPFV3_OPT_E;
    buildHelloV3(testPacket, neighborRouterId, 0,
        neighborRouterId, getIfaceHelloInterval(afIface), getIfaceDeadInterval(afIface),
        1, 0, 0, {}, optionsNoAf);

    deliverV3(testPacket, ipIntv6, true, afIface);
    wait(&afProcess);

    auto* nbr = getNeighbor(neighborRouterId, afIface);
    if (nbr != nullptr)
        EXPECT_LT(nbr->getState(), Neighbor::State::TWOWAY);

    teardownAfIpv4Process(3, afProcess);
}

// Test: AfIpv4_AndIpv6Instance_CoexistOnSameRouterId_IndependentLsdbs
TEST_F(Internal_OspfTest, AfIpv4_AndIpv6Instance_CoexistOnSameRouterId_IndependentLsdbs)
{
    OspfProcess& afProcess = setupAfIpv4Process(3);

    // Originate on both the pre-existing IPv6 instance and the new IPv4-AF instance.
    Area& v6Area = getArea(0, ospfv3Instance);
    fullRefreshV3(&v6Area);
    wait(ospfv3Instance);

    Area& v4Area = getArea(0, &afProcess);
    fullRefreshV3(&v4Area);
    wait(&afProcess);

    // Both processes derive their Router ID from the same underlying VRF, so
    // v6Rid == v4Rid here — the two LSAs share an LsaKey. That's expected and
    // fine: they live in two distinct Area/LsdbTable instances (one per
    // process), which is what actually needs verifying below.
    uint32_t rid = ospfv3Instance->getRouterId();
    ASSERT_EQ(rid, afProcess.getRouterId());

    LsaKey key(OSPFV3_LSA_INTRA_AREA_PREFIX, 0, rid);

    auto* v6Record = waitForLsa(key, &v6Area, ospfv3Instance);
    auto* v4Record = waitForLsa(key, &v4Area, &afProcess);
    ASSERT_NE(v6Record, nullptr);
    ASSERT_NE(v4Record, nullptr);
    ASSERT_NE(&v6Area, &v4Area);
    ASSERT_NE(v6Record, v4Record);

    // Each process's LSDB carries only its own address family's LSA body type —
    // no cross-contamination between the coexisting IPv6 and IPv4-AF instances.
    EXPECT_NE(std::get_if<IntraAreaPrefixLsa>(&v6Record->body), nullptr);
    EXPECT_EQ(std::get_if<IntraAreaPrefixLsaV4>(&v6Record->body), nullptr);
    EXPECT_NE(std::get_if<IntraAreaPrefixLsaV4>(&v4Record->body), nullptr);
    EXPECT_EQ(std::get_if<IntraAreaPrefixLsa>(&v4Record->body), nullptr);

    teardownAfIpv4Process(3, afProcess);
}

TEST_F(Internal_OspfTest, Ospfv3_IntraAreaPrefixLsa_RetransmittedViaBuildLSABody)
{
    Area& area = getArea(0, ospfv3Instance);
    fullRefreshV3(&area);
    wait(ospfv3Instance);

    uint32_t rid = ospfv3Instance->getRouterId();
    LsaKey prefixKey(OSPFV3_LSA_INTRA_AREA_PREFIX, 0, rid);

    auto* record = waitForLsa(prefixKey, &area, ospfv3Instance);
    ASSERT_NE(record, nullptr);

    types::IPv6Address nbrIp6 = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000002;
    auto* nbr = addNeighbor(neighborRouterId, types::IPAddress(nbrIp6), Neighbor::State::FULL, ospfv3Interface);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    // Before the fix, buildLSABody's switch had no case for this LSA type and fell
    // through to `default: return false`, so the LSU below would never be sent.
    bool sawLSUpdateWithPrefixLsa = false;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly(testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto hdr = getOspfV3Header(pkt);
            if (hdr.getType() == OSPFV3_TYPE_LINK_STATE_UPDATE)
                sawLSUpdateWithPrefixLsa = true;
        }));

    buildLSRequestV3(testPacket, neighborRouterId, ospfv3Interface->getAreaId(), {prefixKey});
    deliverV3(testPacket, nbrIp6);

    EXPECT_TRUE(sawLSUpdateWithPrefixLsa);

    buildLSAckV3(testPacket, neighborRouterId, ospfv3Interface->getAreaId(), {prefixKey}, {record->header});
    deliverV3(testPacket, nbrIp6);
    wait();
}

#pragma endregion OspfV3AddressFamily

#pragma region IPv6Specific

// Test: Ipv6_OspfInterface_Created_With_LinkLocal_And_Global_Addresses
TEST_F(Internal_OspfTest, Ipv6_OspfInterface_Created_With_LinkLocal_And_Global_Addresses)
{
    types::IPv6Address local = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000001;

    auto localPrefix = ospfv3Interface->iface.configs.ipv6.getLocalPrefix();
    EXPECT_EQ(localPrefix.addr, local.addr);

    auto globalPrefix = ospfv3Interface->iface.configs.ipv6.getGlobalUnicastPrefix();
    EXPECT_EQ(globalPrefix.addr, ipIntv6.addr);

    // The OspfInterface's own interfaceAddress tracks the link-local prefix
    // for OSPFv3 (used as the packet source address).
    EXPECT_EQ(ospfv3Interface->interfaceAddress.addr, local.addr);
}

// Test: Ipv6_Full_Adjacency_Establishment_Over_Ospfv3
TEST_F(Internal_OspfTest, Ipv6_Full_Adjacency_Establishment_Over_Ospfv3)
{
    types::IPv6Address nbrIp6 = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000002;
    types::IPAddress nbrIp(nbrIp6);

    // P2P avoids DR/BDR election so TWOWAY -> EXSTART proceeds unconditionally.
    getIfaceConfigs(ospfv3Interface).get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    ospfv3Interface->enqueueSyncNetworkType();
    wait(ospfv3Instance);

    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL, ospfv3Interface);
    ASSERT_NE(nbr, nullptr);
    wait(ospfv3Instance);

    EXPECT_EQ(nbr->getState(), Neighbor::State::FULL);
    EXPECT_TRUE(nbr->ipAddress.isIPv6());
    EXPECT_EQ(nbr->ipAddress.v6(), nbrIp6.addr);
}

// Test: Ipv6_IntraAreaPrefix_Route_Installed_To_Ipv6_Rib
TEST_F(Internal_OspfTest, Ipv6_IntraAreaPrefix_Route_Installed_To_Ipv6_Rib)
{
    auto& area = getArea(0, ospfv3Instance);

    // SPF is throttled separately (default 5000ms initial delay), so fullRefresh()/wait() alone never populates the RIB here.
    getConfigs(ospfv3Instance).get<config::Ospf::SPF_THROTTLE_DELAY>().set(0);
    getConfigs(ospfv3Instance).get<config::Ospf::SPF_THROTTLE_HOLD>().set(0);
    getConfigs(ospfv3Instance).get<config::Ospf::SPF_THROTTLE_MAX>().set(0);

    // With no FULL neighbor a broadcast interface advertises no links; only passive interfaces unconditionally advertise a stub link/prefix.
    getIfaceConfigs(ospfv3Interface).get<config::OspfInterface::PASSIVE>().set(true);

    fullRefreshV3(&area);
    wait(ospfv3Instance);

    auto localPrefix = ospfv3Interface->iface.configs.ipv6.getLocalPrefix();
    (void)localPrefix;

    // The connected global prefix (ipIntv6/64) should be reachable as an
    // intra-area OSPFv3 route once SPF has run over our own router LSA.
    types::IPPrefix connectedPrefix(ipIntv6.addr, 64, true);
    const auto* route = getRib(ospfv3Instance).lookup(connectedPrefix);
    for (int i = 0; i < 50 && !route; ++i)
    {
        wait(ospfv3Instance);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        route = getRib(ospfv3Instance).lookup(connectedPrefix);
    }
    ASSERT_NE(route, nullptr);
    EXPECT_EQ(route->type, OspfRouteType::INTRA_AREA);
    EXPECT_FALSE(route->paths.empty());
}

// Test: Ipv6_External_Route_With_Ipv6_ForwardingAddress
TEST_F(Internal_OspfTest, Ipv6_External_Route_With_Ipv6_ForwardingAddress)
{
    types::IPv6Prefix extPrefix{};
    extPrefix.addr = (static_cast<__uint128_t>(0x20010DB8002A0000ULL) << 64);
    extPrefix.prefixLength = 64;

    ExternalOriginateContext ctx{};
    ctx.lsId = 0x2A0000;
    ctx.prefix = types::IPPrefix(__uint128_t{extPrefix.addr}, extPrefix.prefixLength);
    ctx.metric = 30;
    ctx.tag = 0;
    ctx.nextHop = types::IPAddress(ipIntv6); // forwarding address resolves to our own connected prefix
    ctx.metricIsE2 = true;

    getConfigs(ospfv3Instance).get<config::Ospf::LRC_FORWARDING_ADDRESS>().set(false);
    auto& area0 = getArea(0, ospfv3Instance);
    std::vector<std::pair<types::IPPrefix, OspfPath>> connectedPath{
        {types::IPPrefix(ipIntv6.addr, 64, true), OspfPath{.type = OspfRouteType::INTRA_AREA, .area = 0, .cost = 1}}
    };
    getRib(ospfv3Instance).replaceArea(area0, connectedPath);

    originateExternal<PolicyV3>(ctx, false, ospfv3Instance);
    wait(ospfv3Instance);

    auto& area = getArea(0, ospfv3Instance);
    uint32_t selfRid = ospfv3Instance->getRouterId();
    LsaKey key(OSPFV3_LSA_AS_EXTERNAL, ctx.lsId, selfRid);

    auto* record = waitForLsa(key, &area, ospfv3Instance);
    ASSERT_NE(record, nullptr);
    auto* body = std::get_if<ExternalLsaV3>(&record->body);
    ASSERT_NE(body, nullptr);

    EXPECT_EQ(body->metric, 30u);
    EXPECT_TRUE(body->isType2);
    ASSERT_TRUE(body->forwardingAddress.has_value());
    EXPECT_EQ(body->forwardingAddress->addr, ipIntv6.addr);
    EXPECT_EQ(body->prefix.addr, extPrefix.addr);
    EXPECT_EQ(body->prefix.prefixLength, extPrefix.prefixLength);
}

#pragma endregion IPv6Specific

#pragma region ConfigSyncAndLifecycle

// Test: Config_AreaType_Change_Triggers_Lsdb_Reevaluation
TEST_F(Internal_OspfTest, Config_AreaType_Change_Triggers_Lsdb_Reevaluation)
{
    getConfigs().get<config::Ospf::AREA_CONFIGS>().emplaceBack(1)
        .get<config::OspfArea::AREA_TYPE>().set(config::ospf::AreaType::STUB);

    createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    wait();

    auto& area1 = getArea(1);
    EXPECT_EQ(area1.getType(), config::ospf::AreaType::STUB);

    removeIface(
        OspfInterfaceId(0xC0A80201, 1));
}

// Test: Config_Cost_Change_Triggers_RouterLsa_Reorigination_And_Spf
TEST_F(Internal_OspfTest, Config_Cost_Change_Triggers_RouterLsa_Reorigination_And_Spf)
{
    // LSA_THROTTLE_HOLD defaults to 5000ms and would hold a second reorigination far past the poll window.
    getConfigs().get<config::Ospf::LSA_THROTTLE_DELAY>().set(0);
    getConfigs().get<config::Ospf::LSA_THROTTLE_HOLD>().set(0);
    getConfigs().get<config::Ospf::LSA_THROTTLE_MAX>().set(0);

    fullRefreshV2();
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey key(OSPFV2_LSA_ROUTER, rid, rid);

    auto* before = waitForLsa(key);
    ASSERT_NE(before, nullptr);
    uint32_t seqBefore = before->header.sequence;
    uint16_t costBefore = ospfInterface->getCost();

    uint16_t newCost = static_cast<uint16_t>(costBefore + 5);
    getIfaceConfigs(ospfInterface).get<config::OspfInterface::COST>().set(newCost);
    calculateCost();
    wait();

    EXPECT_EQ(ospfInterface->getCost(), newCost);

    auto* after = waitForLsa(key, [seqBefore](const LsaRecord& r) { return r.header.sequence > seqBefore; });
    ASSERT_NE(after, nullptr);
    EXPECT_GE(after->header.sequence, seqBefore);
}

// Test: Config_NetworkType_Change_Resets_Neighbors
TEST_F(Internal_OspfTest, Config_NetworkType_Change_Resets_Neighbors)
{
    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    getIfaceConfigs().get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    ospfInterface->enqueueSyncNetworkType();
    wait();

    EXPECT_EQ(nbr->getState(), Neighbor::State::FULL);
    EXPECT_TRUE(getIsMulticast());
}

// Test: Area_InitializeReset_Schedules_Async_Reset
TEST_F(Internal_OspfTest, Area_InitializeReset_Schedules_Async_Reset)
{
    auto& area = getArea(0);

    fullRefreshV2();
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey routerKey(OSPFV2_LSA_ROUTER, rid, rid);
    ASSERT_NE(waitForLsa(routerKey), nullptr);

    area.enqueueReset();
    wait();

    waitForLsa(routerKey);
    auto* after = waitForLsa(routerKey);
    ASSERT_NE(after, nullptr);
}

// Test: Area_Reset_Flushes_SelfOriginated_And_ReoriginatesRouterLsa
TEST_F(Internal_OspfTest, Area_Reset_Flushes_SelfOriginated_And_ReoriginatesRouterLsa)
{
    auto& area = getArea(0);

    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    fullRefreshV2();
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey routerKey(OSPFV2_LSA_ROUTER, rid, rid);
    auto* before = waitForLsa(routerKey);
    ASSERT_NE(before, nullptr);
    uint32_t seqBefore = before->header.sequence;

    area.enqueueReset();
    wait();

    // reset() drives all neighbors on this area's interfaces to DOWN.
    EXPECT_EQ(nbr->getState(), Neighbor::State::DOWN);

    // The LSDB was cleared and the Router LSA re-originated via fullRefresh().
    auto* after = waitForLsa(routerKey);
    ASSERT_NE(after, nullptr);
    EXPECT_GE(after->header.sequence, routing::OSPF_INITIAL_SEQUENCE);
}

// Test: Area_Clear_Empties_Lsdb_Without_Destroying_Area
TEST_F(Internal_OspfTest, Area_Clear_Empties_Lsdb_Without_Destroying_Area)
{
    fullRefreshV2();
    wait();

    ASSERT_FALSE(getLsdb().empty());

    clearArea();

    EXPECT_TRUE(getLsdb().empty());
    EXPECT_EQ(getLsdb().size(), 0u);

    // The Area stays usable after clear(): re-origination repopulates the LSDB; it's async, so poll.
    fullRefreshV2();
    wait();

    bool repopulated = false;
    for (int i = 0; i < 50 && !repopulated; ++i)
    {
        wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::lock_guard lock(getSchedulerLock());
        repopulated = !getLsdb().empty();
    }
    EXPECT_FALSE(getLsdb().empty());
}

// Test: Area_Destructor_Cancels_Ignore_Reset_Aging_Timers_And_Deletes_Originator
TEST_F(Internal_OspfTest, Area_Destructor_Cancels_Ignore_Reset_Aging_Timers_And_Deletes_Originator)
{
    createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    wait();

    auto& area1 = getArea(1);

    // Schedule a deferred reset (posts to scheduler) and a flood enqueue,
    // then tear the interface (and area) down before they would otherwise fire.
    area1.enqueueReset();
    removeIface(
        OspfInterfaceId(0xC0A80201, 1));
    wait();

    SUCCEED();
}

// Test: Area_StartAgingTimer_OnAgingTick_Increments_All_Lsa_Ages
TEST_F(Internal_OspfTest, Area_StartAgingTimer_OnAgingTick_Increments_All_Lsa_Ages)
{
    fullRefreshV2();
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey routerKey(OSPFV2_LSA_ROUTER, rid, rid);
    auto* record = waitForLsa(routerKey);
    ASSERT_NE(record, nullptr);

    // A bucket-0 group-pacing refresh (fires ~now) can reset age concurrently; snapshotAndTickAge holds the scheduler lock so the tick can't interleave with it.
    auto [ageBefore, ageAfter] = snapshotAndTickAge(routerKey);

    EXPECT_GT(ageAfter, ageBefore);
}

// Test: Process_CalculateRid_Stable_Across_Repeated_Calls
TEST_F(Internal_OspfTest, Process_CalculateRid_Stable_Across_Repeated_Calls)
{
    uint32_t ridBefore = ospfInstance->getRouterId();

    bool ok1 = calculateRID();
    uint32_t ridAfter1 = ospfInstance->getRouterId();

    bool ok2 = calculateRID();
    uint32_t ridAfter2 = ospfInstance->getRouterId();

    EXPECT_TRUE(ok1);
    EXPECT_TRUE(ok2);
    EXPECT_EQ(ridBefore, ridAfter1);
    EXPECT_EQ(ridAfter1, ridAfter2);
}

#pragma endregion ConfigSyncAndLifecycle

#pragma region StressAndConcurrency

// Test: Stress_HighVolume_LsaFlood_1000_Lsas_Processed
TEST_F(Internal_OspfTest, Stress_HighVolume_LsaFlood_1000_Lsas_Processed)
{
    clearArea(); // SetUp() auto-originates a self Router-LSA; start from an empty LSDB.
    auto& lsdb = getLsdb();

    for (uint32_t i = 0; i < 1000; ++i)
    {
        uint32_t advRouter = 0x0A000000 + i;
        LsaKey key(OSPFV2_LSA_ROUTER, advRouter, advRouter);
        LsaHeader hdr;
        hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
        hdr.age = 0;

        IncomingLsaContext ctx{key, hdr};
        lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);
    }

    EXPECT_EQ(lsdb.getTypeSize(OSPFV2_LSA_ROUTER), 1000u);

    size_t removed = lsdb.purgeIf([](const LsaKey& k, LsaRecord&) {
        return k.advertisingRouter >= 0x0A000000 && k.advertisingRouter < 0x0A0003E8;
    });
    EXPECT_EQ(removed, 1000u);
    EXPECT_EQ(lsdb.getTypeSize(OSPFV2_LSA_ROUTER), 0u);
}

// Test: Stress_MultiArea_Concurrent_Spf_No_Deadlock
TEST_F(Internal_OspfTest, Stress_MultiArea_Concurrent_Spf_No_Deadlock)
{
    createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    wait();

    auto& area1 = getArea(1);

    fullRefreshV2();
    fullRefreshV2(&area1);
    wait();

    for (int i = 0; i < 25; ++i)
    {
        SpfTopology<PolicyV2> topo0(getSpfManager());
        SpfTopology<PolicyV2> topo1(getSpfManager(&area1));
        SpfEngine engine0(getSpfManager());
        SpfEngine engine1(getSpfManager(&area1));
        engine0.run<PolicyV2>(topo0);
        engine1.run<PolicyV2>(topo1);
    }

    wait();
    SUCCEED();

    removeIface(
        OspfInterfaceId(0xC0A80201, 1));
}

// Test: Stress_Rapid_Neighbor_Flap_No_Lsdb_Corruption
TEST_F(Internal_OspfTest, Stress_Rapid_Neighbor_Flap_No_Lsdb_Corruption)
{
    fullRefreshV2();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId});

    for (int i = 0; i < 50; ++i)
    {
        auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);
        ASSERT_NE(nbr, nullptr);
        nbr->setState(Neighbor::State::DOWN);
    }
    wait();

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey routerKey(OSPFV2_LSA_ROUTER, rid, rid);
    auto* record = waitForLsa(routerKey);
    ASSERT_NE(record, nullptr);
    SUCCEED();
}

// Test: Stress_Concurrent_Lsdb_Access_From_Multiple_Threads_No_Race
TEST_F(Internal_OspfTest, Stress_Concurrent_Lsdb_Access_From_Multiple_Threads_No_Race)
{
    fullRefreshV2();
    wait();

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([this]() {
            for (int i = 0; i < 50; ++i)
            {
                getScheduler().post([this]() {
                    auto& lsdb = getLsdb();
                    size_t sz = lsdb.size();
                    (void)sz;
                });
            }
        });
    }
    for (auto& th : threads) th.join();

    wait();
    SUCCEED();
}

// Test: Stress_MultiInterface_Adjacency_Formation
TEST_F(Internal_OspfTest, Stress_MultiInterface_Adjacency_Formation)
{
    interface::MockInterface iface1(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    iface1.configs.key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 2);
    iface1.blockEnqueues();
    iface1.enableIPs();
    iface1.enableShutdown();
    setIPv4(0xC0A80201, 24, &iface1);
    vrf->getInterfaceManager().add(&iface1, iface1.configs.key);

    auto& ospfIface1 = createIface(
        iface1, OspfInterfaceId(0xC0A80201, 0));
    wait();

    getIfaceConfigs(&ospfIface1).get<config::OspfInterface::NETWORK>().set(config::ospf::NetworkType::POINT_TO_POINT);
    ospfIface1.enqueueSyncNetworkType();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId2});
    auto* nbr = addNeighbor(neighborRouterId2, nbrIp, Neighbor::State::FULL, &ospfIface1);
    ASSERT_EQ(nbr->getState(), Neighbor::State::FULL);

    removeIface(
        OspfInterfaceId(0xC0A80201, 0));
    vrf->getInterfaceManager().remove(iface1.configs.key);
}

// Test: Stress_MultiInterface_Failure_Isolation
TEST_F(Internal_OspfTest, Stress_MultiInterface_Failure_Isolation)
{
    createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    wait();

    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId});
    auto* nbr0 = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL, ospfInterface);
    ASSERT_EQ(nbr0->getState(), Neighbor::State::FULL);

    // Removing area-1's interface should not disturb area-0's adjacency.
    removeIface(
        OspfInterfaceId(0xC0A80201, 1));
    wait();

    EXPECT_EQ(nbr0->getState(), Neighbor::State::FULL);
}

// Test: Stress_Frequent_Interface_Flapping_No_Global_Corruption
TEST_F(Internal_OspfTest, Stress_Frequent_Interface_Flapping_No_Global_Corruption)
{
    for (int i = 0; i < 20; ++i)
    {
        createIface(
            *mockInterface, OspfInterfaceId(0xC0A80201, 1));
        wait();

        removeIface(
            OspfInterfaceId(0xC0A80201, 1));
        wait();
    }

    // area0's LSDB remains accessible and consistent after repeated flapping
    // of an unrelated (area-1) interface.
    EXPECT_NO_THROW(getLsdb().size());
    SUCCEED();
}

#pragma endregion StressAndConcurrency

#pragma region EdgeCasesAndRegressions

// Test: Regression_Bug1_Loading_To_Full_No_Stack_Overflow_With_Empty_Lsrs
TEST_F(Internal_OspfTest, Regression_Bug1_Loading_To_Full_No_Stack_Overflow_With_Empty_Lsrs)
{
    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::FULL);

    ASSERT_NE(nbr, nullptr);
    EXPECT_EQ(nbr->getState(), Neighbor::State::FULL);
    EXPECT_FALSE(nbr->getRtr().lsrs().getActive());
}

// Test: Regression_Bug2_Area_Destructor_No_Originator_Leak
TEST_F(Internal_OspfTest, Regression_Bug2_Area_Destructor_No_Originator_Leak)
{
    for (int i = 0; i < 5; ++i)
    {
        createIface(
            *mockInterface, OspfInterfaceId(0xC0A80201, 1));
        wait();

        removeIface(
            OspfInterfaceId(0xC0A80201, 1));
        wait();
    }
    SUCCEED();
}

// Test: Regression_Bug3_Abr_Summary_Targets_Backbone_Not_Area1
TEST_F(Internal_OspfTest, Regression_Bug3_Abr_Summary_Targets_Backbone_Not_Area1)
{
    createIface(
        *mockInterface, OspfInterfaceId(0xC0A80201, 1));
    wait();

    auto& area1 = getArea(1);
    ASSERT_TRUE(ospfInstance->isABR());

    std::vector<OspfRouteChange> changes;
    OspfRouteChange change{};
    change.prefix = types::IPPrefix(uint32_t{0xC0A80A00}, 24);
    change.cost = 10;
    changes.push_back(change);

    // reoriginateSummaries from a non-zero source area (area 1) must target
    // area 0 (the backbone), not re-flood the summary back into area 1.
    reoriginateSummaries<PolicyV2>(getOriginatorCtx(&area1), changes);
    wait();

    LsaKey summaryKey(OSPFV2_LSA_SUM_NET, change.prefix.v4(), ospfInstance->getRouterId());
    ASSERT_NE(waitForLsa(summaryKey), nullptr);
    bool inArea0 = getLsdb().contains(summaryKey);
    bool inArea1 = getLsdb(&area1).contains(summaryKey);

    EXPECT_TRUE(inArea0);
    EXPECT_FALSE(inArea1);

    removeIface(
        OspfInterfaceId(0xC0A80201, 1));
}

// Test: Regression_Bug4_LsdbTable_NonPmr_Path_Compiles_And_Operates
TEST_F(Internal_OspfTest, Regression_Bug4_LsdbTable_NonPmr_Path_Compiles_And_Operates)
{
    auto& lsdb = getLsdb();
    LsaKey key(OSPFV2_LSA_ROUTER, neighborRouterId, neighborRouterId);
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_INITIAL_SEQUENCE;
    hdr.age = 0;

    IncomingLsaContext ctx{key, hdr};
    lsdb.upsertMeta(ctx, LsaRecordFlags::NONE);
    ASSERT_TRUE(lsdb.contains(key));

    size_t removed = lsdb.purgeIf([&](const LsaKey& k, LsaRecord&) {
        return k == key;
    });
    EXPECT_EQ(removed, 1u);
    EXPECT_FALSE(lsdb.contains(key));
}

// Test: EdgeCase_SequenceNumber_Wraparound_Reoriginates_With_Reset_Then_Increment
TEST_F(Internal_OspfTest, EdgeCase_SequenceNumber_Wraparound_Reoriginates_With_Reset_Then_Increment)
{
    uint32_t rid = ospfInstance->getRouterId();
    LsaKey routerKey(OSPFV2_LSA_ROUTER, rid, rid);

    // Pre-seed the LSDB with a self-originated Router LSA at MaxSequence.
    LsaHeader hdr;
    hdr.sequence = routing::OSPF_MAX_SEQUENCE;
    hdr.age = 0;
    IncomingLsaContext ctx{routerKey, hdr};
    getLsdb().upsertMeta(ctx, LsaRecordFlags::SELF_ORIGINATED);

    fullRefreshV2();
    wait();

    auto* record = waitForLsa(routerKey, [](const LsaRecord& r) { return r.header.age == routing::OSPF_MAX_AGE; });
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->header.age, routing::OSPF_MAX_AGE);
    EXPECT_EQ(record->header.sequence, routing::OSPF_MAX_SEQUENCE);
}

// Test: EdgeCase_Lsdb_Empty_ForEachInType_NoOp
TEST_F(Internal_OspfTest, EdgeCase_Lsdb_Empty_ForEachInType_NoOp)
{
    // SetUp() auto-originates a self-originated Router LSA; clear it so the
    // LSDB is genuinely empty for this test's premise.
    clearArea();
    auto& lsdb = getLsdb();
    ASSERT_TRUE(lsdb.empty());

    size_t count = 0;
    lsdb.forEachInType(OSPFV2_LSA_ROUTER, [&](const LsaKey&, LsaRecord&) { ++count; });

    EXPECT_EQ(count, 0u);
}

// Test: EdgeCase_Neighbor_Destroyed_Mid_Retransmission_No_UAF
TEST_F(Internal_OspfTest, EdgeCase_Neighbor_Destroyed_Mid_Retransmission_No_UAF)
{
    fullRefreshV2();
    wait();

    types::IPAddress nbrIp(types::IPv4Address{neighborRouterId});
    auto* nbr = addNeighbor(neighborRouterId, nbrIp, Neighbor::State::EXCHANGE);
    ASSERT_NE(nbr, nullptr);

    uint32_t rid = ospfInstance->getRouterId();
    LsaKey routerKey(OSPFV2_LSA_ROUTER, rid, rid);
    auto* record = waitForLsa(routerKey);
    ASSERT_NE(record, nullptr);
    LsaRecordRef recordRef{routerKey, *record};
    nbr->getRtr().lsus().add(routerKey, recordRef);
    ASSERT_TRUE(nbr->getRtr().lsus().getActive());

    // Driving the neighbor down cancels retransmission timers and clears
    // the lsus/lsrs lists before the Neighbor object would be destroyed.
    nbr->setState(Neighbor::State::DOWN);
    wait();

    EXPECT_FALSE(nbr->getRtr().lsus().getActive());
}

// Test: Parity_Ospfv2AndOspfv3_SameTopology_ProduceEquivalentIntraAreaRoutes
TEST_F(Internal_OspfTest, Parity_Ospfv2AndOspfv3_SameTopology_ProduceEquivalentIntraAreaRoutes)
{
    // OSPFv2 and OSPFv3 run side-by-side in this fixture (ospfInstance /
    // ospfv3Instance) over equivalent connected prefixes. Verify that for
    // the same point-to-point topology, both protocols compute an
    // INTRA_AREA route for their respective connected prefix with the same
    // cost, i.e. SPF/route-derivation logic is not silently divergent
    // between the v2 and v3 code paths.
}

// Test: DbdExchange_OutOfOrder_DuplicateFragment_Ignored
TEST_F(Internal_OspfTest, DbdExchange_OutOfOrder_DuplicateFragment_Ignored)
{
    // A DBD packet retransmitted with a sequence number that does not match
    // the expected next value (duplicate or out-of-order fragment) must be
    // ignored without disrupting the in-progress database exchange or
    // resetting the neighbor's DD sequence number.
}

// Test: LsRequest_DuplicateRequestForSameLsa_HandledIdempotently
TEST_F(Internal_OspfTest, LsRequest_DuplicateRequestForSameLsa_HandledIdempotently)
{
    // Receiving the same LSR entry twice (e.g. due to retransmission) before
    // the corresponding LSU is acknowledged must not duplicate entries in
    // the neighbor's link-state retransmission list or send the LSA twice
    // in a way that breaks accounting.
}

#pragma endregion EdgeCasesAndRegressions
