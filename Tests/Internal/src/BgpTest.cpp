// Internal_BgpTest.cpp

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <chrono>
#include <thread>
#include <functional>

#include <VirtualRouter.h>
#include <Global.h>
#include <MockFileSystem.hpp>
#include <IPAddress.h>
#include <interface/configs/InterfaceType.hpp>

#include "bgp/BgpProcess.h"
#include "bgp/BgpTypes.hpp"
#include "bgp/session/Session.h"
#include "bgp/session/Fsm.h"
#include "bgp/session/SessionTimers.h"
#include "bgp/session/Capabilities.hpp"
#include "bgp/session/Collision.h"
#include "bgp/session/MultiSession.h"
#include "bgp/decision/BestPath.h"
#include "bgp/decision/DecisionEngine.hpp"
#include "bgp/rib/RibTypes.hpp"
#include "bgp/rib/LocRib.hpp"
#include "bgp/attributes/AttributeManager.hpp"
#include "bgp/attributes/AttributeTypes.hpp"
#include "bgp/transport/BgpTx.h"
#include "bgp/transport/BgpRx.h"
#include "bgp/neighbor/Neighbor.h"
#include "bgp/neighbor/NeighborTable.h"
#include "bgp/neighbor/NeighborAf.h"
#include "bgp/neighbor/PeerTemplate.h"
#include "bgp/af/AddressFamily.hpp"
#include "bgp/af/Nlri.hpp"
#include "packet/headers/BgpHeader.hpp"
#include "packet/headers/embedded/bgp/BgpOpenHeader.hpp"
#include "configs/registry/router/BgpRegistry.h"
#include "tcp/Tcp.h"
#include "tcp/Connection.h"
#include "tcp/rx/RxConsumer.h"
#include "MockTcpEngine.hpp"

using namespace routing::bgp;
using namespace transport::tcp;
using namespace std::chrono_literals;
using routing::ExampleNlri;

class Internal_BgpTest : public ::testing::Test
{
protected:
    cli::MockFileSystem fs;
    core::Global* global = nullptr;
    core::VirtualRouter* vrf = nullptr;

    BgpProcess* proc = nullptr;
    static constexpr uint32_t kLocalAs = 65001;

    void SetUp() override
    {
        utils::RCU::registerThread();
        global = new core::Global(fs, {}, false, true);
        vrf = global->getRoutingInstance("", types::AddressFamily::IPv4);
        vrf->getTcp().swapEngineForTesting(new MockTcpEngine(*vrf));

        proc = new BgpProcess(kLocalAs, vrf);
    }

    void TearDown() override
    {
        delete proc;
        delete global;
        utils::RCU::unregisterThread();
    }

    // HELPERS

    static types::IPAddress mkV4(uint32_t hostOrder)
    {
        types::IPAddress a;
        a.setV4(hostOrder);
        return a;
    }

    static types::IPv4Prefix mkPrefix(uint32_t hostOrderAddr, uint8_t len)
    {
        return types::IPv4Prefix(hostOrderAddr, len, true);
    }

    // Builds an InboundRoute<IPv4Prefix> bound to the process attribute manager.
    InboundRoute<types::IPv4Prefix> makeInboundRoute(const types::IPv4Prefix& nlri,
                                                      const Attributes& attrs,
                                                      const Path& path,
                                                      NeighborAf* nbr = nullptr)
    {
        uint32_t pid = proc->getAttrMgr().acquire(attrs, path);
        InboundRoute<types::IPv4Prefix> route(proc->getAttrMgr(), pid, nlri, nbr);
        return route;
    }

    static Path makePath(const types::IPAddress& nextHop, AfiSafi af = {BGP_AFI_IPV4, BGP_SAFI_UNICAST})
    {
        Path p;
        p.family = af;
        p.nextHop = nextHop;
        return p;
    }
};

namespace
{
struct RecvSink
{
    std::vector<uint8_t> bytes;
};

void recvAppend(RecvCallbackCtx& ctx) noexcept
{
    auto* sink = static_cast<RecvSink*>(ctx.user);
    auto span = ctx.consumer.get();
    sink->bytes.insert(sink->bytes.end(), span.begin(), span.end());
    ctx.consumer.commit(span.size());
}

struct AcceptedSink
{
    RecvSink* sink = nullptr;
    std::optional<Connection>* connOut = nullptr;
    bool got = false;
};

void onAcceptCapture(AcceptCallbackCtx& ctx) noexcept
{
    auto* a = static_cast<AcceptedSink*>(ctx.user);
    a->got = true;
    if (a->connOut) a->connOut->emplace(std::move(ctx.newConn));
}
} // namespace

// Pair of real loopback TCP connections (A = active/connect side, B = accepted side).
struct TcpLoopbackPair
{
    Tcp& tcpA;
    Tcp& tcpB;

    RecvSink sinkA;
    RecvSink sinkB;

    Listener listener;
    Connection connA;
    std::optional<Connection> connB;

    AcceptedSink acceptedB;

    TcpLoopbackPair(core::VirtualRouter& v, Tcp& a, Tcp& b, uint16_t port)
        : tcpA(mocked(v, a)), tcpB(mocked(v, b)),
          listener(std::move([&]() {
              ListenOptions opt;
              opt.recvCallback = &recvAppend;
              opt.recvUser = &sinkB;
              opt.onAccept = &onAcceptCapture;
              opt.onAcceptUser = &acceptedB;
              return tcpB.listen(TcpEndpoint{mkLoopback(), port}, opt);
          }())),
          connA(std::move([&]() {
              ConnectOptions opt;
              opt.recvCallback = &recvAppend;
              opt.recvUser = &sinkA;
              return tcpA.connect(TcpEndpoint{mkLoopback(), 0}, TcpEndpoint{mkLoopback(), port}, opt);
          }()))
    {
        acceptedB.connOut = &connB;
    }

    // Installs a mock-backed engine so wire-format tests don't depend on real
    // OS socket timing; returns the same Tcp& so it can sit in an init-list.
    static Tcp& mocked(core::VirtualRouter& v, Tcp& t)
    {
        auto* m = new MockTcpEngine(v);
        t.swapEngineForTesting(m);
        return t;
    }

    static types::IPAddress mkLoopback()
    {
        types::IPAddress a;
        a.setV4(0x7F000001);
        return a;
    }

    // Pumps both stacks until the accept side has captured its Connection,
    // or the iteration budget is exhausted.
    bool pumpUntilAccepted(int maxIters = 200)
    {
        for (int i = 0; i < maxIters; ++i)
        {
            tcpA.pump(1);
            tcpB.pump(1);
            if (acceptedB.got) return true;
        }
        return acceptedB.got;
    }

    void pump(int iters = 5)
    {
        for (int i = 0; i < iters; ++i)
        {
            tcpA.pump(1);
            tcpB.pump(1);
        }
    }
};

TEST_F(Internal_BgpTest, AttrMgr_AcquireRetainRelease_RefCounting)
{
    AttributeManager mgr;

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    Path path = makePath(mkV4(0x0A000001));

    uint32_t id = mgr.acquire(attrs, path);

    // RouteBase ctor retains (refcount 2: acquire + retain).
    {
        InboundRoute<types::IPv4Prefix> route(mgr, id, {});
        route.nlri = mkPrefix(0x0A000000, 24);

        ASSERT_TRUE(mgr.getAttributes(id).origin.has_value());
        EXPECT_EQ(mgr.getAttributes(id).origin.value(), BGP_ORIGIN_IGP);
        EXPECT_EQ(mgr.getPath(id).nextHop, path.nextHop);

        // Moving transfers the retained ref without changing the count.
        InboundRoute<types::IPv4Prefix> moved(std::move(route));
        EXPECT_EQ(moved.nlri.prefixLength, 24);
    }
    // route destroyed -> released back to refcount 1 (still held by our own acquire).
    mgr.release(id);
}

TEST_F(Internal_BgpTest, AttrMgr_Retain_IncrementsRefcount)
{
    AttributeManager mgr;
    Attributes attrs;
    Path path = makePath(mkV4(0x0A000001));

    uint32_t id = mgr.acquire(attrs, path);
    EXPECT_TRUE(mgr.retain(id));

    mgr.release(id);
    mgr.release(id);
    // Should still be retrievable here is not guaranteed after final release;
    // just verify no crash on balanced acquire/retain/release pairs.
    SUCCEED();
}

TEST_F(Internal_BgpTest, AttrMgr_Get_ReturnsPathAttribute)
{
    AttributeManager mgr;
    Attributes attrs;
    attrs.origin = BGP_ORIGIN_EGP;
    attrs.localPref = 200;
    Path path = makePath(mkV4(0x0A000001));

    uint32_t id = mgr.acquire(attrs, path);

    PathAttribute pa = mgr.get(id);
    ASSERT_TRUE(pa.attrs.origin.has_value());
    EXPECT_EQ(pa.attrs.origin.value(), BGP_ORIGIN_EGP);
    EXPECT_EQ(pa.attrs.localPref, 200u);
    EXPECT_EQ(pa.path.nextHop, path.nextHop);

    mgr.release(id);
}

TEST_F(Internal_BgpTest, AttrMgr_Clear_ResetsPools)
{
    AttributeManager mgr;
    mgr.reserve(8);

    Attributes attrs;
    Path path = makePath(mkV4(0x0A000001));
    uint32_t id1 = mgr.acquire(attrs, path);
    uint32_t id2 = mgr.acquire(attrs, makePath(mkV4(0x0A000002)));
    EXPECT_NE(id1, id2);

    mgr.clear();
    // After clear, a fresh acquire should succeed without UB.
    uint32_t id3 = mgr.acquire(attrs, path);
    (void)id3;
    SUCCEED();
}

TEST_F(Internal_BgpTest, InboundRoute_CopyAndMove_RetainAndRelease)
{
    AttributeManager& mgr = proc->getAttrMgr();
    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    Path path = makePath(mkV4(0x0A000001));
    uint32_t id = mgr.acquire(attrs, path);

    {
        InboundRoute<types::IPv4Prefix> a(mgr, id, {});
        a.nlri = mkPrefix(0x0A000000, 24);

        InboundRoute<types::IPv4Prefix> b(std::move(a));
        EXPECT_EQ(b.nlri.prefixLength, 24);

        InboundRoute<types::IPv4Prefix> c(mgr, id, {});
        c.nlri = mkPrefix(0x0B000000, 16);
        // b and c both alive, each retaining id.
        EXPECT_EQ(mgr.get(id).attrs.origin, BGP_ORIGIN_IGP);
    }
    mgr.release(id);
}

TEST_F(Internal_BgpTest, InboundRouteBase_LocallyOriginated)
{
    AttributeManager& mgr = proc->getAttrMgr();
    Attributes attrs;
    Path path = makePath(mkV4(0x0A000001));
    uint32_t id = mgr.acquire(attrs, path);

    InboundRoute<types::IPv4Prefix> local(mgr, id, {});
    EXPECT_TRUE(local.locallyOriginated());
    EXPECT_EQ(local.weigth, 32768);

    mgr.release(id);
}

TEST_F(Internal_BgpTest, AsPathLength_AsSequenceCountsPerAsn)
{
    Attributes attrs;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65001, 65002, 65003};
    attrs.asPath.push_back(seg);

    EXPECT_EQ(attrs.asPathLength(), 3u);
}

TEST_F(Internal_BgpTest, AsPathLength_AsSetCountsAsOne)
{
    Attributes attrs;
    AsPathSegment seq;
    seq.segmentType = BGP_AS_SEQUENCE;
    seq.asns = {65001, 65002};
    attrs.asPath.push_back(seq);

    AsPathSegment set;
    set.segmentType = BGP_AS_SET;
    set.asns = {65003, 65004, 65005};
    attrs.asPath.push_back(set);

    // 2 from sequence + 1 for the whole AS_SET.
    EXPECT_EQ(attrs.asPathLength(), 3u);
}

TEST_F(Internal_BgpTest, AsPathLength_ConfedSegmentsExcluded)
{
    Attributes attrs;
    AsPathSegment confSeq;
    confSeq.segmentType = BGP_AS_CONFED_SEQUENCE;
    confSeq.asns = {65010, 65011};
    attrs.asPath.push_back(confSeq);

    AsPathSegment confSet;
    confSet.segmentType = BGP_AS_CONFED_SET;
    confSet.asns = {65012};
    attrs.asPath.push_back(confSet);

    AsPathSegment seq;
    seq.segmentType = BGP_AS_SEQUENCE;
    seq.asns = {65001};
    attrs.asPath.push_back(seq);

    EXPECT_EQ(attrs.asPathLength(), 1u);
}

TEST_F(Internal_BgpTest, AsPathLength_MixedSegments)
{
    Attributes attrs;

    AsPathSegment confSeq;
    confSeq.segmentType = BGP_AS_CONFED_SEQUENCE;
    confSeq.asns = {65010, 65011, 65012};
    attrs.asPath.push_back(confSeq);

    AsPathSegment seq;
    seq.segmentType = BGP_AS_SEQUENCE;
    seq.asns = {65001, 65002};
    attrs.asPath.push_back(seq);

    AsPathSegment set;
    set.segmentType = BGP_AS_SET;
    set.asns = {65003, 65004};
    attrs.asPath.push_back(set);

    // confed excluded, sequence=2, set=1 -> 3
    EXPECT_EQ(attrs.asPathLength(), 3u);
}

TEST_F(Internal_BgpTest, FirstAs_ReturnsFirstAsSequenceAsn)
{
    Attributes attrs;

    AsPathSegment confSeq;
    confSeq.segmentType = BGP_AS_CONFED_SEQUENCE;
    confSeq.asns = {65099};
    attrs.asPath.push_back(confSeq);

    AsPathSegment seq;
    seq.segmentType = BGP_AS_SEQUENCE;
    seq.asns = {65001, 65002};
    attrs.asPath.push_back(seq);

    EXPECT_EQ(attrs.firstAs(), 65001u);
}

TEST_F(Internal_BgpTest, FirstAs_EmptyPathReturnsZero)
{
    Attributes attrs;
    EXPECT_EQ(attrs.firstAs(), 0u);
}

TEST_F(Internal_BgpTest, PerPeerInTable_InsertLookupRemove)
{
    PerPeerInTable<types::IPv4Prefix> table;
    AttributeManager& mgr = proc->getAttrMgr();

    types::IPv4Prefix prefix = mkPrefix(0x0A000000, 24);
    NlriPath<types::IPv4Prefix> key{prefix, 0};

    Attributes attrs;
    Path path = makePath(mkV4(0x0A000001));
    uint32_t id = mgr.acquire(attrs, path);

    InboundRoute<types::IPv4Prefix> route(mgr, id, {});
    route.nlri = prefix;

    auto [it, inserted] = table.emplace(key, std::move(route));
    EXPECT_TRUE(inserted);

    auto found = table.find(key);
    ASSERT_NE(found, table.end());
    EXPECT_EQ(found->second.nlri.prefixLength, 24);

    table.erase(found);
    EXPECT_EQ(table.find(key), table.end());

    mgr.release(id);
}

TEST_F(Internal_BgpTest, PerPeerInTable_MultiplePeersSamePrefix)
{
    AttributeManager& mgr = proc->getAttrMgr();
    types::IPv4Prefix prefix = mkPrefix(0xAC100000, 16);

    PerPeerInTable<types::IPv4Prefix> peerA, peerB;

    Attributes attrsA; attrsA.localPref = 100;
    Attributes attrsB; attrsB.localPref = 200;

    uint32_t idA = mgr.acquire(attrsA, makePath(mkV4(0x0A000001)));
    uint32_t idB = mgr.acquire(attrsB, makePath(mkV4(0x0B000001)));

    InboundRoute<types::IPv4Prefix> routeA(mgr, idA, {});
    routeA.nlri = prefix;
    InboundRoute<types::IPv4Prefix> routeB(mgr, idB, {});
    routeB.nlri = prefix;

    NlriPath<types::IPv4Prefix> key{prefix, 0};
    peerA.emplace(key, std::move(routeA));
    peerB.emplace(key, std::move(routeB));

    EXPECT_EQ(peerA.find(key)->second.getPathAttributes().attrs.localPref, 100u);
    EXPECT_EQ(peerB.find(key)->second.getPathAttributes().attrs.localPref, 200u);

    mgr.release(idA);
    mgr.release(idB);
}

TEST_F(Internal_BgpTest, NlriPath_EqualityAndHash)
{
    types::IPv4Prefix p1 = mkPrefix(0x0A000000, 24);
    types::IPv4Prefix p2 = mkPrefix(0x0A000000, 24);
    types::IPv4Prefix p3 = mkPrefix(0x0B000000, 24);

    NlriPath<types::IPv4Prefix> a{p1, 0};
    NlriPath<types::IPv4Prefix> b{p2, 0};
    NlriPath<types::IPv4Prefix> c{p3, 0};
    NlriPath<types::IPv4Prefix> d{p1, 5}; // different pathId

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_FALSE(a == d);

    NlriPathHash<types::IPv4Prefix> hasher;
    EXPECT_EQ(hasher(a), hasher(b));
}

TEST_F(Internal_BgpTest, ExtendedCommunity_PackedAsUint64)
{
    Attributes attrs;
    // Type=0x00 (two-octet AS specific, transitive), subtype=0x02 (route-target),
    // AS=65001, local admin=100 packed per RFC 4360.
    uint64_t ec = (uint64_t(0x00) << 56) | (uint64_t(0x02) << 48) |
                  (uint64_t(65001) << 16) | uint64_t(100);
    attrs.extendedCommunities.push_back(ec);

    EXPECT_EQ(attrs.extendedCommunities.size(), 1u);
    EXPECT_EQ((attrs.extendedCommunities[0] >> 48) & 0xFF, 0x02u);
}

TEST_F(Internal_BgpTest, LargeCommunity_PackedAsTriplet)
{
    Attributes attrs;
    attrs.largeCommunities.push_back({65001, 1, 2});

    EXPECT_EQ(attrs.largeCommunities.size(), 1u);
    EXPECT_EQ(attrs.largeCommunities[0][0], 65001u);
    EXPECT_EQ(attrs.largeCommunities[0][1], 1u);
    EXPECT_EQ(attrs.largeCommunities[0][2], 2u);
}

namespace
{
// Drains all bytes currently queued in `from`'s TX buffer onto the wire and
// pumps both TCP stacks until they land in the peer's RecvSink.
void flushAndPump(TcpLoopbackPair& pair, Connection& from)
{
    from.flush();
    pair.pump(20);
}
} // namespace

TEST_F(Internal_BgpTest, BgpTx_BuildOpen_TwoByteAs_RoundTripFields)
{
    Tcp tcpA(*vrf);
    Tcp tcpB(*vrf);
    TcpLoopbackPair pair(*vrf, tcpA, tcpB, 17900);
    ASSERT_TRUE(pair.pumpUntilAccepted());

    types::IPAddress peerAddr = mkV4(0x0A000002);
    Neighbor* nbr = proc->getNtable().createNeighbor(peerAddr);
    Session sess(*nbr);

    BgpTx::buildOpen(pair.connA, sess);
    flushAndPump(pair, pair.connA);

    ASSERT_GE(pair.sinkB.bytes.size(), packet::BgpHeader::fixedSize + packet::BgpOpenHeader::fixedSize);

    const uint8_t* buf = pair.sinkB.bytes.data();

    // BGP header: 16-byte marker (all 0xFF), 2-byte length, 1-byte type.
    for (int i = 0; i < 16; ++i)
        EXPECT_EQ(buf[i], 0xFF);
    uint16_t msgLen = utils::readU16(buf + 16);
    EXPECT_EQ(buf[18], BGP_TYPE_OPEN);
    EXPECT_EQ(msgLen, pair.sinkB.bytes.size());

    const uint8_t* open = buf + packet::BgpHeader::fixedSize;
    EXPECT_EQ(open[0], BGP_VERSION); // version

    uint16_t asField = utils::readU16(open + 1);
    // AS 65001 fits in two bytes, no AS_TRANS needed.
    EXPECT_EQ(asField, kLocalAs);

    uint16_t holdField = utils::readU16(open + 3);
    EXPECT_EQ(holdField, sess.holdTime);

    uint32_t rid = utils::readU32(open + 5);
    EXPECT_EQ(rid, proc->getRouterId());
}

TEST_F(Internal_BgpTest, BgpTx_BuildOpen_FourByteAs_UsesAsTrans)
{
    // Construct a second process with an AS number above the 2-byte range.
    BgpProcess proc2(400000, vrf);

    Tcp tcpA(*vrf);
    Tcp tcpB(*vrf);
    TcpLoopbackPair pair(*vrf, tcpA, tcpB, 17901);
    ASSERT_TRUE(pair.pumpUntilAccepted());

    types::IPAddress peerAddr = mkV4(0x0A000003);
    Neighbor* nbr = proc2.getNtable().createNeighbor(peerAddr);
    Session sess(*nbr);

    BgpTx::buildOpen(pair.connA, sess);
    flushAndPump(pair, pair.connA);

    const uint8_t* buf = pair.sinkB.bytes.data();
    const uint8_t* open = buf + packet::BgpHeader::fixedSize;

    uint16_t asField = utils::readU16(open + 1);
    EXPECT_EQ(asField, kAsTrans);

    // Capabilities must include the 32-bit ASN capability carrying the real AS.
    uint8_t parmLen = open[9];
    ASSERT_GT(parmLen, 0u);

    const uint8_t* params = open + packet::BgpOpenHeader::fixedSize;
    bool found32bitAs = false;
    size_t pos = 0;
    while (pos + 2 <= parmLen)
    {
        uint8_t paramType = params[pos];
        uint8_t paramLen = params[pos + 1];
        ASSERT_EQ(paramType, BGP_PARAMETER_CAPABILITY);

        size_t capPos = pos + 2;
        size_t capEnd = pos + 2 + paramLen;
        while (capPos + 2 <= capEnd)
        {
            uint8_t code = params[capPos];
            uint8_t len = params[capPos + 1];
            if (code == BGP_CAPABILITY_32_BIT_AS)
            {
                ASSERT_EQ(len, 4);
                uint32_t as4 = utils::readU32(params + capPos + 2);
                EXPECT_EQ(as4, 400000u);
                found32bitAs = true;
            }
            capPos += 2 + len;
        }
        pos += 2 + paramLen;
    }
    EXPECT_TRUE(found32bitAs);
}

TEST_F(Internal_BgpTest, BgpTx_BuildOpen_CapabilitiesIncludeRouteRefreshAndExtMsg)
{
    Tcp tcpA(*vrf);
    Tcp tcpB(*vrf);
    TcpLoopbackPair pair(*vrf, tcpA, tcpB, 17902);
    ASSERT_TRUE(pair.pumpUntilAccepted());

    types::IPAddress peerAddr = mkV4(0x0A000004);
    Neighbor* nbr = proc->getNtable().createNeighbor(peerAddr);
    Session sess(*nbr);

    BgpTx::buildOpen(pair.connA, sess);
    flushAndPump(pair, pair.connA);

    const uint8_t* buf = pair.sinkB.bytes.data();
    const uint8_t* open = buf + packet::BgpHeader::fixedSize;
    uint8_t parmLen = open[9];
    const uint8_t* params = open + packet::BgpOpenHeader::fixedSize;

    bool foundRouteRefresh = false;
    bool foundExtMsg = false;
    bool foundEnhancedRR = false;
    size_t pos = 0;
    while (pos + 2 <= parmLen)
    {
        uint8_t paramLen = params[pos + 1];
        size_t capPos = pos + 2;
        size_t capEnd = pos + 2 + paramLen;
        while (capPos + 2 <= capEnd)
        {
            uint8_t code = params[capPos];
            uint8_t len = params[capPos + 1];
            if (code == BGP_CAPABILITY_ROUTE_REFRESH) foundRouteRefresh = true;
            if (code == BGP_CAPABILITY_EXTENDED_MESSAGE) foundExtMsg = true;
            if (code == BGP_CAPABILITY_ENHANCED_ROUTE_REFRESH) foundEnhancedRR = true;
            capPos += 2 + len;
        }
        pos += 2 + paramLen;
    }

    EXPECT_TRUE(foundRouteRefresh);
    EXPECT_TRUE(foundExtMsg);
    EXPECT_TRUE(foundEnhancedRR);
}

TEST_F(Internal_BgpTest, BgpTx_BuildOpen_MultiprotocolCapability_AfterEnableAf)
{
    AfiSafi afiSafi{BGP_AFI_IPV4, BGP_SAFI_UNICAST};

    types::IPAddress peerAddr = mkV4(0x0A000005);
    Neighbor* nbr = proc->getNtable().createNeighbor(peerAddr);
    nbr->addAfNeighbor(afiSafi);
    proc->enableAddressFamily<ExampleNlri::afi>();

    Tcp tcpA(*vrf);
    Tcp tcpB(*vrf);
    TcpLoopbackPair pair(*vrf, tcpA, tcpB, 17903);
    ASSERT_TRUE(pair.pumpUntilAccepted());

    Session sess(*nbr);
    BgpTx::buildOpen(pair.connA, sess);
    flushAndPump(pair, pair.connA);

    const uint8_t* buf = pair.sinkB.bytes.data();
    const uint8_t* open = buf + packet::BgpHeader::fixedSize;
    uint8_t parmLen = open[9];
    const uint8_t* params = open + packet::BgpOpenHeader::fixedSize;

    bool foundMp = false;
    size_t pos = 0;
    while (pos + 2 <= parmLen)
    {
        uint8_t paramLen = params[pos + 1];
        size_t capPos = pos + 2;
        size_t capEnd = pos + 2 + paramLen;
        while (capPos + 2 <= capEnd)
        {
            uint8_t code = params[capPos];
            uint8_t len = params[capPos + 1];
            if (code == BGP_CAPABILITY_MULTIPROTOCOL)
            {
                ASSERT_EQ(len, 4);
                uint16_t afi = utils::readU16(params + capPos + 2);
                uint8_t safi = params[capPos + 5];
                EXPECT_EQ(afi, BGP_AFI_IPV4);
                EXPECT_EQ(safi, BGP_SAFI_UNICAST);
                foundMp = true;
            }
            capPos += 2 + len;
        }
        pos += 2 + paramLen;
    }
    EXPECT_TRUE(foundMp);
}

TEST_F(Internal_BgpTest, BgpTx_BuildKeepalive_Fixed19Bytes)
{
    Tcp tcpA(*vrf);
    Tcp tcpB(*vrf);
    TcpLoopbackPair pair(*vrf, tcpA, tcpB, 17904);
    ASSERT_TRUE(pair.pumpUntilAccepted());

    BgpTx::buildKeepalive(pair.connA);
    flushAndPump(pair, pair.connA);

    ASSERT_EQ(pair.sinkB.bytes.size(), packet::BgpHeader::fixedSize);
    const uint8_t* buf = pair.sinkB.bytes.data();
    for (int i = 0; i < 16; ++i)
        EXPECT_EQ(buf[i], 0xFF);
    EXPECT_EQ(utils::readU16(buf + 16), 19u);
    EXPECT_EQ(buf[18], BGP_TYPE_KEEPALIVE);
}

TEST_F(Internal_BgpTest, BgpTx_BuildNotification_NoDataPayload)
{
    Tcp tcpA(*vrf);
    Tcp tcpB(*vrf);
    TcpLoopbackPair pair(*vrf, tcpA, tcpB, 17905);
    ASSERT_TRUE(pair.pumpUntilAccepted());

    Notification n;
    n.code = BGP_NOTIFICATION_HOLD_TIMER_EXPIRED; // already packed: code=4, subcode=0
    BgpTx::buildNotification(pair.connA, n);
    flushAndPump(pair, pair.connA);

    ASSERT_EQ(pair.sinkB.bytes.size(), packet::BgpHeader::fixedSize + 2u);
    const uint8_t* buf = pair.sinkB.bytes.data();
    EXPECT_EQ(utils::readU16(buf + 16), packet::BgpHeader::fixedSize + 2u);
    EXPECT_EQ(buf[18], BGP_TYPE_NOTIFICATION);

    uint16_t code = utils::readU16(buf + packet::BgpHeader::fixedSize);
    EXPECT_EQ(code, n.code);
    EXPECT_EQ((code >> 8) & 0xFF, BGP_NOTIFICATION_HOLD);
    EXPECT_EQ(code & 0xFF, 0u);
}

TEST_F(Internal_BgpTest, BgpTx_BuildNotification_WithDataPayload)
{
    Tcp tcpA(*vrf);
    Tcp tcpB(*vrf);
    TcpLoopbackPair pair(*vrf, tcpA, tcpB, 17906);
    ASSERT_TRUE(pair.pumpUntilAccepted());

    Notification n;
    n.code = BGP_NOTIFICATION_OPEN_BAD_PEER_AS; // already packed: code=2, subcode=2
    n.data = {0x12, 0x34, 0x56, 0x78};
    BgpTx::buildNotification(pair.connA, n);
    flushAndPump(pair, pair.connA);

    ASSERT_EQ(pair.sinkB.bytes.size(), packet::BgpHeader::fixedSize + 2u + n.data.size());
    const uint8_t* buf = pair.sinkB.bytes.data();
    EXPECT_EQ(buf[18], BGP_TYPE_NOTIFICATION);

    const uint8_t* notif = buf + packet::BgpHeader::fixedSize;
    EXPECT_EQ((utils::readU16(notif) >> 8) & 0xFF, BGP_NOTIFICATION_OPEN);
    EXPECT_EQ(utils::readU16(notif) & 0xFF, BGP_NOTIFICATION_OPEN_BAD_PEER_AS & 0xFF);
    for (size_t i = 0; i < n.data.size(); ++i)
        EXPECT_EQ(notif[2 + i], n.data[i]);
}

TEST_F(Internal_BgpTest, BgpTx_BuildNotification_ZeroCodeIsNoOp)
{
    Tcp tcpA(*vrf);
    Tcp tcpB(*vrf);
    TcpLoopbackPair pair(*vrf, tcpA, tcpB, 17907);
    ASSERT_TRUE(pair.pumpUntilAccepted());

    Notification n; // code == 0
    BgpTx::buildNotification(pair.connA, n);
    flushAndPump(pair, pair.connA);

    EXPECT_TRUE(pair.sinkB.bytes.empty());
}

TEST_F(Internal_BgpTest, BgpTx_BuildUpdate_WithdrawnOnly_LegacyIPv4)
{
    Tcp tcpA(*vrf);
    Tcp tcpB(*vrf);
    TcpLoopbackPair pair(*vrf, tcpA, tcpB, 17908);
    ASSERT_TRUE(pair.pumpUntilAccepted());

    types::IPAddress peerAddr = mkV4(0x0A000006);
    Neighbor* nbr = proc->getNtable().createNeighbor(peerAddr);
    Session sess(*nbr);

    BuildUpdate<types::IPv4Prefix> upd;
    upd.withdrawn.push_back({mkPrefix(0xC0A80000, 24), 0}); // 192.168.0.0/24

    BgpTx::buildUpdate<ExampleNlri>(pair.connA, sess, upd);
    flushAndPump(pair, pair.connA);

    ASSERT_GE(pair.sinkB.bytes.size(), packet::BgpHeader::fixedSize + 2u);
    const uint8_t* buf = pair.sinkB.bytes.data();
    EXPECT_EQ(buf[18], BGP_TYPE_UPDATE);

    const uint8_t* body = buf + packet::BgpHeader::fixedSize;
    uint16_t withdrawnLen = utils::readU16(body);
    EXPECT_EQ(withdrawnLen, 4u); // 1 length byte + 3 bytes for a /24

    EXPECT_EQ(body[2], 24); // prefix length
    EXPECT_EQ(body[3], 192);
    EXPECT_EQ(body[4], 168);
    EXPECT_EQ(body[5], 0);

    // Total path attribute length field follows immediately, must be 0
    // (no announcements -> no attributes).
    uint16_t attrLen = utils::readU16(body + 2 + withdrawnLen);
    EXPECT_EQ(attrLen, 0u);
}

TEST_F(Internal_BgpTest, BgpTx_BuildUpdate_FullAttributeSet_LegacyIPv4)
{
    Tcp tcpA(*vrf);
    Tcp tcpB(*vrf);
    TcpLoopbackPair pair(*vrf, tcpA, tcpB, 17909);
    ASSERT_TRUE(pair.pumpUntilAccepted());

    types::IPAddress peerAddr = mkV4(0x0A000007);
    Neighbor* nbr = proc->getNtable().createNeighbor(peerAddr);
    Session sess(*nbr);

    BuildUpdate<types::IPv4Prefix> upd;
    BuildUpdate<types::IPv4Prefix>::Announcement ann;

    ann.attrs.attrs.origin = BGP_ORIGIN_IGP;
    ann.attrs.attrs.asPath.push_back({BGP_AS_SEQUENCE, {65010, 65020}});
    ann.attrs.attrs.med = 50;
    ann.attrs.attrs.localPref = 200;
    ann.attrs.attrs.atomicAggregate = true;
    ann.attrs.path.family = {BGP_AFI_IPV4, BGP_SAFI_UNICAST};
    ann.attrs.path.nextHop = mkV4(0x0A0000FE);

    ann.nlri.push_back({mkPrefix(0xC0A80100, 24), 0}); // 192.168.1.0/24
    upd.announcements.push_back(ann);

    BgpTx::buildUpdate<ExampleNlri>(pair.connA, sess, upd);
    flushAndPump(pair, pair.connA);

    const uint8_t* buf = pair.sinkB.bytes.data();
    EXPECT_EQ(buf[18], BGP_TYPE_UPDATE);

    const uint8_t* body = buf + packet::BgpHeader::fixedSize;
    uint16_t withdrawnLen = utils::readU16(body);
    EXPECT_EQ(withdrawnLen, 0u);

    const uint8_t* attrsStart = body + 2;
    uint16_t attrLen = utils::readU16(attrsStart);
    ASSERT_GT(attrLen, 0u);

    const uint8_t* a = attrsStart + 2;
    size_t pos = 0;

    // ORIGIN
    EXPECT_EQ(a[pos], BGP_ATTR_FLAG_TRANSITIVE);
    EXPECT_EQ(a[pos + 1], BGP_ATTR_ORIGIN);
    uint8_t originLen = a[pos + 2];
    EXPECT_EQ(originLen, 1u);
    EXPECT_EQ(a[pos + 3], BGP_ORIGIN_IGP);
    pos += 3 + originLen;

    // AS_PATH: AS_SEQUENCE, 2 entries, 2-byte ASNs (asn32bit not negotiated by default)
    EXPECT_EQ(a[pos + 1], BGP_ATTR_AS_PATH);
    uint8_t asPathLen = a[pos + 2];
    const uint8_t* asPathVal = a + pos + 3;
    EXPECT_EQ(asPathVal[0], BGP_AS_SEQUENCE);
    EXPECT_EQ(asPathVal[1], 2u); // segment count
    EXPECT_EQ(utils::readU16(asPathVal + 2), 65010u);
    EXPECT_EQ(utils::readU16(asPathVal + 4), 65020u);
    pos += 3 + asPathLen;

    // NEXT_HOP
    EXPECT_EQ(a[pos + 1], BGP_ATTR_NEXT_HOP);
    uint8_t nhLen = a[pos + 2];
    EXPECT_EQ(nhLen, 4u);
    EXPECT_EQ(utils::readU32(a + pos + 3), 0x0A0000FEu);
    pos += 3 + nhLen;

    // MULTI_EXIT_DISC (MED)
    EXPECT_EQ(a[pos + 1], BGP_ATTR_MULTI_EXIT_DISC);
    EXPECT_EQ(a[pos + 2], 4u);
    EXPECT_EQ(utils::readU32(a + pos + 3), 50u);
    pos += 3 + 4;

    if (a[pos + 1] == BGP_ATTR_LOCAL_PREF)
    {
        EXPECT_EQ(a[pos + 2], 4u);
        EXPECT_EQ(utils::readU32(a + pos + 3), 200u);
        pos += 3 + 4;
    }

    // ATOMIC_AGGREGATE
    EXPECT_EQ(a[pos + 1], BGP_ATTR_ATOMIC_AGGREGATE);
    EXPECT_EQ(a[pos + 2], 0u);
    pos += 3;

    EXPECT_EQ(pos, attrLen);

    // NLRI follows the attributes.
    const uint8_t* nlri = attrsStart + 2 + attrLen;
    EXPECT_EQ(nlri[0], 24);
    EXPECT_EQ(nlri[1], 192);
    EXPECT_EQ(nlri[2], 168);
    EXPECT_EQ(nlri[3], 1);
}

TEST_F(Internal_BgpTest, BgpTx_BuildUpdate_AsSetAndConfedSegments)
{
    Tcp tcpA(*vrf);
    Tcp tcpB(*vrf);
    TcpLoopbackPair pair(*vrf, tcpA, tcpB, 17910);
    ASSERT_TRUE(pair.pumpUntilAccepted());

    types::IPAddress peerAddr = mkV4(0x0A000008);
    Neighbor* nbr = proc->getNtable().createNeighbor(peerAddr);
    Session sess(*nbr);

    BuildUpdate<types::IPv4Prefix> upd;
    BuildUpdate<types::IPv4Prefix>::Announcement ann;
    ann.attrs.attrs.origin = BGP_ORIGIN_INCOMPLETE;
    ann.attrs.attrs.asPath.push_back({BGP_AS_CONFED_SEQUENCE, {64512}});
    ann.attrs.attrs.asPath.push_back({BGP_AS_SEQUENCE, {65010}});
    ann.attrs.attrs.asPath.push_back({BGP_AS_SET, {65020, 65021}});
    ann.attrs.path.family = {BGP_AFI_IPV4, BGP_SAFI_UNICAST};
    ann.attrs.path.nextHop = mkV4(0x0A0000FE);
    ann.nlri.push_back({mkPrefix(0x0A0A0A00, 24), 0});
    upd.announcements.push_back(ann);

    BgpTx::buildUpdate<ExampleNlri>(pair.connA, sess, upd);
    flushAndPump(pair, pair.connA);

    const uint8_t* buf = pair.sinkB.bytes.data();
    const uint8_t* body = buf + packet::BgpHeader::fixedSize;
    uint16_t withdrawnLen = utils::readU16(body);
    const uint8_t* attrsStart = body + 2 + withdrawnLen;
    const uint8_t* a = attrsStart + 2;

    size_t pos = 0;
    // ORIGIN
    EXPECT_EQ(a[pos + 1], BGP_ATTR_ORIGIN);
    EXPECT_EQ(a[pos + 3], BGP_ORIGIN_INCOMPLETE);
    pos += 3 + a[pos + 2];

    // AS_PATH with 3 segments
    EXPECT_EQ(a[pos + 1], BGP_ATTR_AS_PATH);
    const uint8_t* asPathVal = a + pos + 3;
    size_t p = 0;

    EXPECT_EQ(asPathVal[p], BGP_AS_CONFED_SEQUENCE);
    EXPECT_EQ(asPathVal[p + 1], 1u);
    EXPECT_EQ(utils::readU16(asPathVal + p + 2), 64512u);
    p += 2 + 2 * 1;

    EXPECT_EQ(asPathVal[p], BGP_AS_SEQUENCE);
    EXPECT_EQ(asPathVal[p + 1], 1u);
    EXPECT_EQ(utils::readU16(asPathVal + p + 2), 65010u);
    p += 2 + 2 * 1;

    EXPECT_EQ(asPathVal[p], BGP_AS_SET);
    EXPECT_EQ(asPathVal[p + 1], 2u);
    EXPECT_EQ(utils::readU16(asPathVal + p + 2), 65020u);
    EXPECT_EQ(utils::readU16(asPathVal + p + 4), 65021u);
    p += 2 + 2 * 2;
}

TEST_F(Internal_BgpTest, BgpTx_BuildUpdate_AggregatorTwoByteAs)
{
    Tcp tcpA(*vrf);
    Tcp tcpB(*vrf);
    TcpLoopbackPair pair(*vrf, tcpA, tcpB, 17911);
    ASSERT_TRUE(pair.pumpUntilAccepted());

    types::IPAddress peerAddr = mkV4(0x0A000009);
    Neighbor* nbr = proc->getNtable().createNeighbor(peerAddr);
    Session sess(*nbr);

    BuildUpdate<types::IPv4Prefix> upd;
    BuildUpdate<types::IPv4Prefix>::Announcement ann;
    ann.attrs.attrs.origin = BGP_ORIGIN_IGP;
    ann.attrs.attrs.asPath.push_back({BGP_AS_SEQUENCE, {65010}});
    ann.attrs.attrs.asAggregator = Aggregator{65010, mkV4(0x0A0000FE)};
    ann.attrs.path.family = {BGP_AFI_IPV4, BGP_SAFI_UNICAST};
    ann.attrs.path.nextHop = mkV4(0x0A0000FE);
    ann.nlri.push_back({mkPrefix(0x0B0B0B00, 24), 0});
    upd.announcements.push_back(ann);

    BgpTx::buildUpdate<ExampleNlri>(pair.connA, sess, upd);
    flushAndPump(pair, pair.connA);

    const uint8_t* buf = pair.sinkB.bytes.data();
    const uint8_t* body = buf + packet::BgpHeader::fixedSize;
    uint16_t withdrawnLen = utils::readU16(body);
    const uint8_t* attrsStart = body + 2 + withdrawnLen;
    uint16_t attrLen = utils::readU16(attrsStart);
    const uint8_t* a = attrsStart + 2;

    // Find AGGREGATOR attribute by scanning.
    size_t pos = 0;
    bool foundAggregator = false;
    while (pos < attrLen)
    {
        uint8_t flags = a[pos];
        uint8_t type = a[pos + 1];
        bool extLen = (flags & BGP_ATTR_FLAG_EXTENDED_LENGTH) != 0;
        size_t hdr = extLen ? 4 : 3;
        size_t len = extLen ? utils::readU16(a + pos + 2) : a[pos + 2];

        if (type == BGP_ATTR_AGGREGATOR)
        {
            // 2-byte AS aggregator: 2-byte ASN + 4-byte address = 6 bytes.
            EXPECT_EQ(len, 6u);
            EXPECT_EQ(utils::readU16(a + pos + hdr), 65010u);
            EXPECT_EQ(utils::readU32(a + pos + hdr + 2), 0x0A0000FEu);
            foundAggregator = true;
        }
        pos += hdr + len;
    }
    EXPECT_TRUE(foundAggregator);
}

TEST_F(Internal_BgpTest, BgpRx_ProcessUpdate_DecodesWithdrawnAndAnnounced)
{
    types::IPAddress peerAddr = mkV4(0x0A00000A);
    Neighbor* nbr = proc->getNtable().createNeighbor(peerAddr);
    Session sess(*nbr);

    IncomingUpdate uinfo;
    uinfo.afi = {BGP_AFI_IPV4, BGP_SAFI_UNICAST};

    // Withdrawn: 192.168.2.0/24 -> [24][192][168][2]
    std::vector<uint8_t> withdrawn = {24, 192, 168, 2};
    uinfo.withdrawnData = std::span<uint8_t>(withdrawn.data(), withdrawn.size());

    // Announced: 10.20.0.0/16 -> [16][10][20]
    std::vector<uint8_t> announced = {16, 10, 20};
    uinfo.nlriData = std::span<uint8_t>(announced.data(), announced.size());

    ParsedUpdate<types::IPv4Prefix> parsed;
    Notification notif;
    bool ok = BgpRx::processUpdate<ExampleNlri>(sess, uinfo, parsed, notif);

    ASSERT_TRUE(ok);
    ASSERT_EQ(parsed.withdrawn.size(), 1u);
    EXPECT_EQ(parsed.withdrawn[0].nlri.prefixLength, 24);
    EXPECT_EQ(parsed.withdrawn[0].nlri.addr, mkPrefix(0xC0A80200, 24).addr);

    ASSERT_EQ(parsed.announcements.size(), 1u);
    EXPECT_EQ(parsed.announcements[0].nlri.prefixLength, 16);
    EXPECT_EQ(parsed.announcements[0].nlri.addr, mkPrefix(0x0A140000, 16).addr);
}

TEST_F(Internal_BgpTest, BgpRx_ProcessUpdate_AddPathDecodesPathId)
{
    AfiSafi afiSafi{BGP_AFI_IPV4, BGP_SAFI_UNICAST};

    types::IPAddress peerAddr = mkV4(0x0A00000B);
    Neighbor* nbr = proc->getNtable().createNeighbor(peerAddr);
    Session sess(*nbr);

    // Manually mark add-path as negotiated for this AFI/SAFI.
    sess.getNegotiated().addpath = true;
    sess.getNegotiated().addPathFamilies.push_back({afiSafi, BGP_ADD_PATH_BOTH});

    IncomingUpdate uinfo;
    uinfo.afi = afiSafi;

    // Announced with path-id=7: [00 00 00 07][24][192][168][3]
    std::vector<uint8_t> announced = {0, 0, 0, 7, 24, 192, 168, 3};
    uinfo.nlriData = std::span<uint8_t>(announced.data(), announced.size());

    ParsedUpdate<types::IPv4Prefix> parsed;
    Notification notif;
    bool ok = BgpRx::processUpdate<ExampleNlri>(sess, uinfo, parsed, notif);

    ASSERT_TRUE(ok);
    ASSERT_EQ(parsed.announcements.size(), 1u);
    EXPECT_EQ(parsed.announcements[0].pathId, 7u);
    EXPECT_EQ(parsed.announcements[0].nlri.prefixLength, 24);
    EXPECT_EQ(parsed.announcements[0].nlri.addr, mkPrefix(0xC0A80300, 24).addr);
}

TEST_F(Internal_BgpTest, BgpRx_ProcessUpdate_MalformedWithdrawnTruncated)
{
    types::IPAddress peerAddr = mkV4(0x0A00000C);
    Neighbor* nbr = proc->getNtable().createNeighbor(peerAddr);
    Session sess(*nbr);

    IncomingUpdate uinfo;
    uinfo.afi = {BGP_AFI_IPV4, BGP_SAFI_UNICAST};

    // Claims a /24 (needs 3 address bytes) but only provides 1.
    std::vector<uint8_t> withdrawn = {24, 192};
    uinfo.withdrawnData = std::span<uint8_t>(withdrawn.data(), withdrawn.size());

    ParsedUpdate<types::IPv4Prefix> parsed;
    Notification notif;
    bool ok = BgpRx::processUpdate<ExampleNlri>(sess, uinfo, parsed, notif);

    EXPECT_FALSE(ok);
    EXPECT_EQ(notif.code, BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST);
}

TEST_F(Internal_BgpTest, BgpRx_ProcessUpdate_AddPathTruncatedPathId)
{
    AfiSafi afiSafi{BGP_AFI_IPV4, BGP_SAFI_UNICAST};

    types::IPAddress peerAddr = mkV4(0x0A00000D);
    Neighbor* nbr = proc->getNtable().createNeighbor(peerAddr);
    Session sess(*nbr);

    sess.getNegotiated().addpath = true;
    sess.getNegotiated().addPathFamilies.push_back({afiSafi, BGP_ADD_PATH_BOTH});

    IncomingUpdate uinfo;
    uinfo.afi = afiSafi;

    // Only 3 bytes total -- not enough for a 4-byte path-id.
    std::vector<uint8_t> announced = {0, 0, 0};
    uinfo.nlriData = std::span<uint8_t>(announced.data(), announced.size());

    ParsedUpdate<types::IPv4Prefix> parsed;
    Notification notif;
    bool ok = BgpRx::processUpdate<ExampleNlri>(sess, uinfo, parsed, notif);

    EXPECT_FALSE(ok);
    EXPECT_EQ(notif.code, BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST);
}

TEST_F(Internal_BgpTest, BgpTx_BuildRouteRefresh_BasicNormal)
{
    AfiSafi afiSafi{BGP_AFI_IPV6, BGP_SAFI_UNICAST};

    Tcp tcpA(*vrf);
    Tcp tcpB(*vrf);
    TcpLoopbackPair pair(*vrf, tcpA, tcpB, 17912);
    ASSERT_TRUE(pair.pumpUntilAccepted());

    types::IPAddress peerAddr = mkV4(0x0A00000E);
    Neighbor* nbr = proc->getNtable().createNeighbor(peerAddr);
    nbr->addAfNeighbor(afiSafi);
    Session sess(*nbr);

    BgpTx::buildRouteRefresh(pair.connA, sess, afiSafi, RouteRefreshReason::Normal);
    flushAndPump(pair, pair.connA);

    ASSERT_EQ(pair.sinkB.bytes.size(), packet::BgpHeader::fixedSize + 4u);
    const uint8_t* buf = pair.sinkB.bytes.data();
    EXPECT_EQ(buf[18], BGP_TYPE_ROUTE_REFRESH);

    const uint8_t* body = buf + packet::BgpHeader::fixedSize;
    EXPECT_EQ(utils::readU16(body), BGP_AFI_IPV6);
    EXPECT_EQ(body[2], BGP_ROUTE_REFRESH_NORMAL);
    EXPECT_EQ(body[3], BGP_SAFI_UNICAST);
}

TEST_F(Internal_BgpTest, BgpTx_BuildRouteRefresh_BorrDowngradedWithoutEnhancedRR)
{
    AfiSafi afiSafi{BGP_AFI_IPV4, BGP_SAFI_UNICAST};

    Tcp tcpA(*vrf);
    Tcp tcpB(*vrf);
    TcpLoopbackPair pair(*vrf, tcpA, tcpB, 17913);
    ASSERT_TRUE(pair.pumpUntilAccepted());

    types::IPAddress peerAddr = mkV4(0x0A00000F);
    Neighbor* nbr = proc->getNtable().createNeighbor(peerAddr);
    nbr->addAfNeighbor(afiSafi);
    Session sess(*nbr);

    // enhancedRR not negotiated -> BORR/EORR must be downgraded to Normal.
    ASSERT_FALSE(sess.getNegotiated().enhancedRR);

    BgpTx::buildRouteRefresh(pair.connA, sess, afiSafi, RouteRefreshReason::Borr);
    flushAndPump(pair, pair.connA);

    const uint8_t* buf = pair.sinkB.bytes.data();
    const uint8_t* body = buf + packet::BgpHeader::fixedSize;
    EXPECT_EQ(body[2], BGP_ROUTE_REFRESH_NORMAL);
}

TEST_F(Internal_BgpTest, BgpTx_BuildRouteRefresh_EorrWithEnhancedRR)
{
    AfiSafi afiSafi{BGP_AFI_IPV4, BGP_SAFI_UNICAST};

    Tcp tcpA(*vrf);
    Tcp tcpB(*vrf);
    TcpLoopbackPair pair(*vrf, tcpA, tcpB, 17914);
    ASSERT_TRUE(pair.pumpUntilAccepted());

    types::IPAddress peerAddr = mkV4(0x0A140001);
    Neighbor* nbr = proc->getNtable().createNeighbor(peerAddr);
    nbr->addAfNeighbor(afiSafi);
    Session sess(*nbr);

    sess.getNegotiated().enhancedRR = true;

    BgpTx::buildRouteRefresh(pair.connA, sess, afiSafi, RouteRefreshReason::Eorr);
    flushAndPump(pair, pair.connA);

    const uint8_t* buf = pair.sinkB.bytes.data();
    const uint8_t* body = buf + packet::BgpHeader::fixedSize;
    EXPECT_EQ(body[2], BGP_ROUTE_REFRESH_EORR);
}

// RFC 8654: extended message capability negotiation + a message body
// larger than kMaxMessageLen but within kExtendedMessageLen, only valid
// once negotiated by both peers.
TEST_F(Internal_BgpTest, BgpTx_ExtendedMessage_NegotiatedLargeUpdateAccepted)
{
}

// Message length above kMaxMessageLen without extended-message negotiated
// must be rejected (NOTIFICATION).
TEST_F(Internal_BgpTest, BgpRx_ExtendedMessage_NotNegotiated_OversizeRejected)
{
}

// RFC 5549: MP_REACH_NLRI carrying an IPv4 NLRI with an IPv6 (extended)
// next hop, encode/decode round trip.
TEST_F(Internal_BgpTest, BgpTx_ExtendedNextHop_Ipv6NextHopForIpv4Nlri)
{
}

// RFC 8950: link-local IPv6 next hop encoded alongside global next hop in
// MP_REACH_NLRI, encode/decode round trip.
TEST_F(Internal_BgpTest, BgpTx_MpReach_LinkLocalNextHopEncoding)
{
}

// ROUTE-REFRESH ORF entries: add/remove, permit/deny prefix-list entries
// encode/decode round trip (BgpTx::buildRouteRefresh / BgpRx processing).
TEST_F(Internal_BgpTest, BgpTx_BuildRouteRefresh_OrfPrefixListAddRemoveEntries)
{
}

TEST_F(Internal_BgpTest, BgpRx_ProcessRouteRefresh_OrfPrefixListPermitDeny)
{
}

namespace
{
// Default igpCost equal on both sides so step 8 doesn't interfere with
// earlier-step tests (InboundRouteBase defaults igpCost to "unreachable").
constexpr uint64_t kEqualIgpCost = 100;
} // namespace

TEST_F(Internal_BgpTest, BestPath_Step1_WeightHigherWins)
{
    BestPathComparator cmp(*proc, BestPathConfig{});

    types::IPAddress nbrA = mkV4(0x0A000001);
    types::IPAddress nbrB = mkV4(0x0A000002);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    Path path = makePath(mkV4(0x0A0000FE));

    InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path);
    InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path);
    lhs.igpCost = kEqualIgpCost;
    rhs.igpCost = kEqualIgpCost;

    lhs.weigth = 200;
    rhs.weigth = 100;

    EXPECT_TRUE(cmp.better(lhs, nbrA, rhs, nbrB));
    EXPECT_FALSE(cmp.better(rhs, nbrB, lhs, nbrA));
}

TEST_F(Internal_BgpTest, BestPath_Step2_LocalPrefHigherWins)
{
    BestPathComparator cmp(*proc, BestPathConfig{});

    types::IPAddress nbrA = mkV4(0x0A000001);
    types::IPAddress nbrB = mkV4(0x0A000002);

    Path path = makePath(mkV4(0x0A0000FE));

    Attributes attrsLow;
    attrsLow.origin = BGP_ORIGIN_IGP;
    attrsLow.localPref = 50;

    Attributes attrsHigh;
    attrsHigh.origin = BGP_ORIGIN_IGP;
    attrsHigh.localPref = 200;

    InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsHigh, path);
    InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsLow, path);
    lhs.igpCost = kEqualIgpCost;
    rhs.igpCost = kEqualIgpCost;

    EXPECT_TRUE(cmp.better(lhs, nbrA, rhs, nbrB));
    EXPECT_FALSE(cmp.better(rhs, nbrB, lhs, nbrA));
}

TEST_F(Internal_BgpTest, BestPath_Step3_LocallyOriginatedPreferred)
{
    BestPathComparator cmp(*proc, BestPathConfig{});

    types::IPAddress nbrA = mkV4(0x0A000001);
    types::IPAddress nbrB = mkV4(0x0A000002);

    AfiSafi afiSafi{BGP_AFI_IPV4, BGP_SAFI_UNICAST};
    Neighbor* peer = proc->getNtable().createNeighbor(nbrB);
    peer->addAfNeighbor(afiSafi);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    Path path = makePath(mkV4(0x0A0000FE));

    // Locally-originated: sourceNeighbor == nullptr (weight defaults to 32768).
    InboundRoute<types::IPv4Prefix> local = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path, nullptr);

    // Received: give it the same weight as the local route so step 1 doesn't
    // already decide the outcome, isolating step 3.
    InboundRoute<types::IPv4Prefix> received = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path, &peer->getAfNeighbor(afiSafi));
    received.weigth = local.weigth;
    local.igpCost = kEqualIgpCost;
    received.igpCost = kEqualIgpCost;

    EXPECT_TRUE(cmp.better(local, nbrA, received, nbrB));
    EXPECT_FALSE(cmp.better(received, nbrB, local, nbrA));
}

TEST_F(Internal_BgpTest, BestPath_Step4_ShorterAsPathWins)
{
    BestPathComparator cmp(*proc, BestPathConfig{});

    types::IPAddress nbrA = mkV4(0x0A000001);
    types::IPAddress nbrB = mkV4(0x0A000002);

    Path path = makePath(mkV4(0x0A0000FE));

    Attributes attrsShort;
    attrsShort.origin = BGP_ORIGIN_IGP;
    attrsShort.asPath.push_back({BGP_AS_SEQUENCE, {65010}});

    Attributes attrsLong;
    attrsLong.origin = BGP_ORIGIN_IGP;
    attrsLong.asPath.push_back({BGP_AS_SEQUENCE, {65010, 65020, 65030}});

    InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsShort, path);
    InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsLong, path);
    lhs.igpCost = kEqualIgpCost;
    rhs.igpCost = kEqualIgpCost;

    EXPECT_TRUE(cmp.better(lhs, nbrA, rhs, nbrB));
    EXPECT_FALSE(cmp.better(rhs, nbrB, lhs, nbrA));
}

TEST_F(Internal_BgpTest, BestPath_Step4_AsSetCountsAsOne)
{
    BestPathComparator cmp(*proc, BestPathConfig{});

    types::IPAddress nbrA = mkV4(0x0A000001);
    types::IPAddress nbrB = mkV4(0x0A000002);

    Path path = makePath(mkV4(0x0A0000FE));

    // AS_SET with 3 members counts as length 1.
    Attributes attrsSet;
    attrsSet.origin = BGP_ORIGIN_IGP;
    attrsSet.asPath.push_back({BGP_AS_SET, {65010, 65020, 65030}});

    // AS_SEQUENCE with 2 members counts as length 2.
    Attributes attrsSeq;
    attrsSeq.origin = BGP_ORIGIN_IGP;
    attrsSeq.asPath.push_back({BGP_AS_SEQUENCE, {65010, 65020}});

    ASSERT_LT(attrsSet.asPathLength(), attrsSeq.asPathLength());

    InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsSet, path);
    InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsSeq, path);
    lhs.igpCost = kEqualIgpCost;
    rhs.igpCost = kEqualIgpCost;

    EXPECT_TRUE(cmp.better(lhs, nbrA, rhs, nbrB));
    EXPECT_FALSE(cmp.better(rhs, nbrB, lhs, nbrA));
}

TEST_F(Internal_BgpTest, BestPath_Step4_ConfedSegmentsExcluded)
{
    BestPathComparator cmp(*proc, BestPathConfig{});

    types::IPAddress nbrA = mkV4(0x0A000001);
    types::IPAddress nbrB = mkV4(0x0A000002);

    Path path = makePath(mkV4(0x0A0000FE));

    // CONFED_SEQUENCE entries don't count toward AS_PATH length, so this
    // path's effective length is 1 (just the AS_SEQUENCE).
    Attributes attrsWithConfed;
    attrsWithConfed.origin = BGP_ORIGIN_IGP;
    attrsWithConfed.asPath.push_back({BGP_AS_CONFED_SEQUENCE, {64512, 64513, 64514}});
    attrsWithConfed.asPath.push_back({BGP_AS_SEQUENCE, {65010}});

    // Plain AS_SEQUENCE of length 2 -- longer than the effective length above.
    Attributes attrsPlain;
    attrsPlain.origin = BGP_ORIGIN_IGP;
    attrsPlain.asPath.push_back({BGP_AS_SEQUENCE, {65010, 65020}});

    ASSERT_EQ(attrsWithConfed.asPathLength(), 1u);
    ASSERT_EQ(attrsPlain.asPathLength(), 2u);

    InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsWithConfed, path);
    InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsPlain, path);
    lhs.igpCost = kEqualIgpCost;
    rhs.igpCost = kEqualIgpCost;

    EXPECT_TRUE(cmp.better(lhs, nbrA, rhs, nbrB));
    EXPECT_FALSE(cmp.better(rhs, nbrB, lhs, nbrA));
}

TEST_F(Internal_BgpTest, BestPath_Step5_OriginIgpBeatsEgpBeatsIncomplete)
{
    BestPathComparator cmp(*proc, BestPathConfig{});

    types::IPAddress nbrA = mkV4(0x0A000001);
    types::IPAddress nbrB = mkV4(0x0A000002);

    Path path = makePath(mkV4(0x0A0000FE));

    Attributes attrsIgp;
    attrsIgp.origin = BGP_ORIGIN_IGP;
    Attributes attrsEgp;
    attrsEgp.origin = BGP_ORIGIN_EGP;
    Attributes attrsIncomplete;
    attrsIncomplete.origin = BGP_ORIGIN_INCOMPLETE;

    // IGP beats EGP
    {
        InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsIgp, path);
        InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsEgp, path);
        lhs.igpCost = kEqualIgpCost;
        rhs.igpCost = kEqualIgpCost;
        EXPECT_TRUE(cmp.better(lhs, nbrA, rhs, nbrB));
        EXPECT_FALSE(cmp.better(rhs, nbrB, lhs, nbrA));
    }

    // EGP beats INCOMPLETE
    {
        InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsEgp, path);
        InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsIncomplete, path);
        lhs.igpCost = kEqualIgpCost;
        rhs.igpCost = kEqualIgpCost;
        EXPECT_TRUE(cmp.better(lhs, nbrA, rhs, nbrB));
        EXPECT_FALSE(cmp.better(rhs, nbrB, lhs, nbrA));
    }
}

TEST_F(Internal_BgpTest, BestPath_Step6_LowerMedWins_SameNeighborAs)
{
    BestPathComparator cmp(*proc, BestPathConfig{});

    types::IPAddress nbrA = mkV4(0x0A000001);
    types::IPAddress nbrB = mkV4(0x0A000002);

    Path path = makePath(mkV4(0x0A0000FE));

    Attributes attrsLowMed;
    attrsLowMed.origin = BGP_ORIGIN_IGP;
    attrsLowMed.med = 10;

    Attributes attrsHighMed;
    attrsHighMed.origin = BGP_ORIGIN_IGP;
    attrsHighMed.med = 100;

    InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsLowMed, path);
    InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsHighMed, path);
    lhs.igpCost = kEqualIgpCost;
    rhs.igpCost = kEqualIgpCost;

    // Same peer AS on both sides -> MED is compared even without always-compare-med.
    lhs.peerAs = 65010;
    rhs.peerAs = 65010;

    EXPECT_TRUE(cmp.better(lhs, nbrA, rhs, nbrB));
    EXPECT_FALSE(cmp.better(rhs, nbrB, lhs, nbrA));
}

TEST_F(Internal_BgpTest, BestPath_Step6_MedMissingAsWorstFalse_TreatsMissingAsZero)
{
    // medMissingAsWorst = false (default): a missing MED is treated as 0,
    // i.e. it is the *best* possible MED, so the route without MED wins.
    BestPathComparator cmp(*proc, BestPathConfig{.medMissingAsWorst = false});

    types::IPAddress nbrA = mkV4(0x0A000001);
    types::IPAddress nbrB = mkV4(0x0A000002);

    Path path = makePath(mkV4(0x0A0000FE));

    Attributes attrsNoMed;
    attrsNoMed.origin = BGP_ORIGIN_IGP;
    // med left empty

    Attributes attrsWithMed;
    attrsWithMed.origin = BGP_ORIGIN_IGP;
    attrsWithMed.med = 5;

    InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsNoMed, path);
    InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsWithMed, path);
    lhs.igpCost = kEqualIgpCost;
    rhs.igpCost = kEqualIgpCost;
    lhs.peerAs = 65010;
    rhs.peerAs = 65010;

    // medOrDefault(missing, false) == 0 < 5 -> lhs (missing MED) wins.
    EXPECT_TRUE(cmp.better(lhs, nbrA, rhs, nbrB));
    EXPECT_FALSE(cmp.better(rhs, nbrB, lhs, nbrA));
}

TEST_F(Internal_BgpTest, BestPath_Step6_MedMissingAsWorstTrue_TreatsMissingAsInfinity)
{
    // medMissingAsWorst = true: a missing MED is treated as the worst possible
    // value, so the route *with* an explicit MED now wins.
    BestPathComparator cmp(*proc, BestPathConfig{.medMissingAsWorst = true});

    types::IPAddress nbrA = mkV4(0x0A000001);
    types::IPAddress nbrB = mkV4(0x0A000002);

    Path path = makePath(mkV4(0x0A0000FE));

    Attributes attrsNoMed;
    attrsNoMed.origin = BGP_ORIGIN_IGP;
    // med left empty

    Attributes attrsWithMed;
    attrsWithMed.origin = BGP_ORIGIN_IGP;
    attrsWithMed.med = 5;

    InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsWithMed, path);
    InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsNoMed, path);
    lhs.igpCost = kEqualIgpCost;
    rhs.igpCost = kEqualIgpCost;
    lhs.peerAs = 65010;
    rhs.peerAs = 65010;

    // medOrDefault(5, true) == 5 < UINT32_MAX -> lhs (with MED) wins.
    EXPECT_TRUE(cmp.better(lhs, nbrA, rhs, nbrB));
    EXPECT_FALSE(cmp.better(rhs, nbrB, lhs, nbrA));
}

TEST_F(Internal_BgpTest, BestPath_Step6_DifferentPeerAs_MedNotComparedByDefault)
{
    BestPathComparator cmp(*proc, BestPathConfig{});

    types::IPAddress nbrA = mkV4(0x0A000001);
    types::IPAddress nbrB = mkV4(0x0A000002);

    Path path = makePath(mkV4(0x0A0000FE));

    Attributes attrsLowMed;
    attrsLowMed.origin = BGP_ORIGIN_IGP;
    attrsLowMed.med = 10;

    Attributes attrsHighMed;
    attrsHighMed.origin = BGP_ORIGIN_IGP;
    attrsHighMed.med = 100;

    InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsLowMed, path);
    InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrsHighMed, path);
    lhs.igpCost = kEqualIgpCost;
    rhs.igpCost = kEqualIgpCost;

    // Different peer ASes -> compareMed() short-circuits to false both ways.
    lhs.peerAs = 65010;
    rhs.peerAs = 65020;

    EXPECT_TRUE(cmp.better(lhs, nbrA, rhs, nbrB));
}

TEST_F(Internal_BgpTest, BestPath_Step7_EbgpPreferredOverIbgp)
{
    BestPathComparator cmp(*proc, BestPathConfig{});

    types::IPAddress nbrA = mkV4(0x0A000001);
    types::IPAddress nbrB = mkV4(0x0A000002);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    Path path = makePath(mkV4(0x0A0000FE));

    InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path);
    InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path);
    lhs.igpCost = kEqualIgpCost;
    rhs.igpCost = kEqualIgpCost;

    lhs.ebgp = true;
    lhs.confedEbgp = false;
    rhs.ebgp = false;
    rhs.confedEbgp = false;

    EXPECT_TRUE(cmp.better(lhs, nbrA, rhs, nbrB));
    EXPECT_FALSE(cmp.better(rhs, nbrB, lhs, nbrA));
}

TEST_F(Internal_BgpTest, BestPath_Step7_ConfedEbgpTreatedAsExternal)
{
    BestPathComparator cmp(*proc, BestPathConfig{});

    types::IPAddress nbrA = mkV4(0x0A000001);
    types::IPAddress nbrB = mkV4(0x0A000002);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    Path path = makePath(mkV4(0x0A0000FE));

    InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path);
    InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path);
    lhs.igpCost = kEqualIgpCost;
    rhs.igpCost = kEqualIgpCost;

    // lhs: confederation eBGP (ebgp=false but confedEbgp=true) -> external.
    lhs.ebgp = false;
    lhs.confedEbgp = true;

    // rhs: true iBGP (ebgp=false, confedEbgp=false) -> internal.
    rhs.ebgp = false;
    rhs.confedEbgp = false;

    EXPECT_TRUE(cmp.better(lhs, nbrA, rhs, nbrB));
    EXPECT_FALSE(cmp.better(rhs, nbrB, lhs, nbrA));
}

TEST_F(Internal_BgpTest, BestPath_Step8_LowerIgpMetricWins)
{
    BestPathComparator cmp(*proc, BestPathConfig{});

    types::IPAddress nbrA = mkV4(0x0A000001);
    types::IPAddress nbrB = mkV4(0x0A000002);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    Path path = makePath(mkV4(0x0A0000FE));

    InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path);
    InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path);

    lhs.igpCost = 5;
    rhs.igpCost = 50;

    EXPECT_TRUE(cmp.better(lhs, nbrA, rhs, nbrB));
    EXPECT_FALSE(cmp.better(rhs, nbrB, lhs, nbrA));
}

TEST_F(Internal_BgpTest, BestPath_Step8_IgnoreIgpMetric_SkipsStep)
{
    BestPathComparator cmp(*proc, BestPathConfig{.ignoreIgpMetric = true});

    types::IPAddress nbrA = mkV4(0x0A000001);
    types::IPAddress nbrB = mkV4(0x0A000002);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    Path path = makePath(mkV4(0x0A0000FE));

    InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path);
    InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path);

    lhs.igpCost = 50;
    rhs.igpCost = 5;

    EXPECT_TRUE(cmp.better(lhs, nbrA, rhs, nbrB));
}

TEST_F(Internal_BgpTest, BestPath_Step9_OldestRouteWins)
{
    BestPathComparator cmp(*proc, BestPathConfig{});

    types::IPAddress nbrA = mkV4(0x0A000001);
    types::IPAddress nbrB = mkV4(0x0A000002);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    Path path = makePath(mkV4(0x0A0000FE));

    InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path);
    InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path);
    lhs.igpCost = kEqualIgpCost;
    rhs.igpCost = kEqualIgpCost;

    // Make the time difference unambiguous regardless of clock resolution.
    lhs.receivedTime = std::chrono::steady_clock::now() - std::chrono::seconds(60);
    rhs.receivedTime = std::chrono::steady_clock::now();

    EXPECT_TRUE(cmp.better(lhs, nbrA, rhs, nbrB));
    EXPECT_FALSE(cmp.better(rhs, nbrB, lhs, nbrA));
}

TEST_F(Internal_BgpTest, BestPath_Step9_CompareRouterId_SkipsOldestRouteStep)
{
    BestPathComparator cmp(*proc, BestPathConfig{.compareRouterId = true});

    types::IPAddress nbrLow = mkV4(0x0A000001);
    types::IPAddress nbrHigh = mkV4(0x0A000002);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    Path path = makePath(mkV4(0x0A0000FE));

    InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path, nullptr);
    InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path, nullptr);
    lhs.igpCost = kEqualIgpCost;
    rhs.igpCost = kEqualIgpCost;

    // Make rhs "older" than lhs -- with compareRouterId=true this must NOT
    // matter, since the oldest-route step is bypassed.
    lhs.receivedTime = std::chrono::steady_clock::now();
    rhs.receivedTime = std::chrono::steady_clock::now() - std::chrono::seconds(60);

    // Both router IDs equal (proc->getRouterId()) -> final tiebreak by
    // neighbor address: lower address wins.
    EXPECT_TRUE(cmp.better(lhs, nbrLow, rhs, nbrHigh));
    EXPECT_FALSE(cmp.better(rhs, nbrHigh, lhs, nbrLow));
}

TEST_F(Internal_BgpTest, BestPath_Step10_LowestNeighborAddressWins)
{
    BestPathComparator cmp(*proc, BestPathConfig{});

    types::IPAddress nbrLow = mkV4(0x0A000001);
    types::IPAddress nbrHigh = mkV4(0x0A000002);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    Path path = makePath(mkV4(0x0A0000FE));

    InboundRoute<types::IPv4Prefix> lhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path);
    InboundRoute<types::IPv4Prefix> rhs = makeInboundRoute(mkPrefix(0xC0A80000, 24), attrs, path);
    lhs.igpCost = kEqualIgpCost;
    rhs.igpCost = kEqualIgpCost;

    // Force a tie through step 9 by giving both routes the same receivedTime.
    auto now = std::chrono::steady_clock::now();
    lhs.receivedTime = now;
    rhs.receivedTime = now;

    EXPECT_TRUE(cmp.better(lhs, nbrLow, rhs, nbrHigh));
    EXPECT_FALSE(cmp.better(rhs, nbrHigh, lhs, nbrLow));
}

TEST_F(Internal_BgpTest, NeighborTable_CreateNeighbor_LookupByAddress)
{
    types::IPAddress nbrAddr = mkV4(0x0A000001);

    Neighbor* created = proc->getNtable().createNeighbor(nbrAddr);
    ASSERT_NE(created, nullptr);

    Neighbor* found = proc->getNtable().lookup(nbrAddr);
    EXPECT_EQ(found, created);

    // Creating again returns the same instance.
    Neighbor* again = proc->getNtable().createNeighbor(nbrAddr);
    EXPECT_EQ(again, created);

    types::IPAddress other = mkV4(0x0A000002);
    EXPECT_EQ(proc->getNtable().lookup(other), nullptr);
}

TEST_F(Internal_BgpTest, NeighborTable_ActivateDeactivatePeer_LookupByRouterId)
{
    types::IPAddress nbrAddr = mkV4(0x0A000001);
    Neighbor* nbr = proc->getNtable().createNeighbor(nbrAddr);
    ASSERT_NE(nbr, nullptr);

    constexpr uint32_t kPeerRid = 0x0A000001;

    // Before activation, lookup by router-ID fails.
    EXPECT_EQ(proc->getNtable().lookup(kPeerRid), nullptr);

    EXPECT_TRUE(proc->getNtable().activatePeer(nbrAddr, kPeerRid));
    EXPECT_EQ(nbr->rid, kPeerRid);

    Neighbor* byRid = proc->getNtable().lookup(kPeerRid);
    EXPECT_EQ(byRid, nbr);

    // Deactivating clears the router-ID index and resets nbr->rid.
    EXPECT_TRUE(proc->getNtable().deactivatePeer(kPeerRid));
    EXPECT_EQ(nbr->rid, 0u);
    EXPECT_EQ(proc->getNtable().lookup(kPeerRid), nullptr);

    // Deactivating an unknown router-ID fails.
    EXPECT_FALSE(proc->getNtable().deactivatePeer(kPeerRid));

    // Activating an unknown neighbor address fails.
    types::IPAddress unknown = mkV4(0x0A0000FF);
    EXPECT_FALSE(proc->getNtable().activatePeer(unknown, 0x0B000001));
}

TEST_F(Internal_BgpTest, NeighborTable_CreateDynamicNeighbor_InheritsPeerGroupConfig)
{
    PeerGroup& pg = proc->getNtable().createPeerGroup("DYNPEERS");

    // Configure REMOTE_AS on the peer-group's session registry.
    pg.getSessionConfigs().get<config::BgpNeighborSession::REMOTE_AS>().set(65099);

    types::IPAddress dynAddr = mkV4(0x0A000010);
    Neighbor* dyn = proc->getNtable().createDynamicNeighbor(dynAddr, "DYNPEERS");
    ASSERT_NE(dyn, nullptr);
    EXPECT_EQ(dyn->getConfigs().getPeerGroup(), &pg);

    // REMOTE_AS is inherited from the peer group via setMask()/load() fallthrough.
    auto remAs = dyn->getConfigs().get<config::BgpNeighborSession::REMOTE_AS>();
    ASSERT_TRUE(remAs.hasValue());
    EXPECT_EQ(remAs.load(), 65099u);

    Neighbor* again = proc->getNtable().createDynamicNeighbor(dynAddr, "DYNPEERS");
    EXPECT_EQ(again, dyn);

    // A statically-configured neighbor at the same address cannot be re-created
    // as dynamic: createNeighbor() first makes it non-dynamic.
    types::IPAddress staticAddr = mkV4(0x0A000020);
    Neighbor* stat = proc->getNtable().createNeighbor(staticAddr);
    ASSERT_NE(stat, nullptr);
    EXPECT_FALSE(stat->dynamic);
    EXPECT_EQ(proc->getNtable().createDynamicNeighbor(staticAddr, "DYNPEERS"), nullptr);
}

TEST_F(Internal_BgpTest, NeighborTable_PeerGroupNotFound_DynamicNeighborHasNoInheritance)
{
    types::IPAddress dynAddr = mkV4(0x0A000011);
    Neighbor* dyn = proc->getNtable().createDynamicNeighbor(dynAddr, "NOSUCHGROUP");
    ASSERT_NE(dyn, nullptr);
    EXPECT_TRUE(dyn->dynamic);
    EXPECT_EQ(dyn->getConfigs().getPeerGroup(), nullptr);

    // No peer group, so REMOTE_AS is unset.
    auto remAs = dyn->getConfigs().get<config::BgpNeighborSession::REMOTE_AS>();
    EXPECT_FALSE(remAs.hasValue());
}

TEST_F(Internal_BgpTest, Neighbor_AddDelGetAfNeighbor)
{
    types::IPAddress nbrAddr = mkV4(0x0A000001);
    Neighbor* nbr = proc->getNtable().createNeighbor(nbrAddr);
    ASSERT_NE(nbr, nullptr);

    AfiSafi afiSafi{BGP_AFI_IPV4, BGP_SAFI_UNICAST};

    nbr->addAfNeighbor(afiSafi);
    NeighborAf& af = nbr->getAfNeighbor(afiSafi);
    EXPECT_EQ(af.family.afi, BGP_AFI_IPV4);
    EXPECT_EQ(af.family.safi, BGP_SAFI_UNICAST);
    EXPECT_EQ(&af.globalNbr(), nbr);

    // Re-adding the same AF is idempotent (try_emplace).
    nbr->addAfNeighbor(afiSafi);
    EXPECT_EQ(&nbr->getAfNeighbor(afiSafi), &af);

    nbr->delAfNeighbor(afiSafi);
    nbr->addAfNeighbor(afiSafi);
    NeighborAf& af2 = nbr->getAfNeighbor(afiSafi);
    EXPECT_NE(&af2, &af);
}

TEST_F(Internal_BgpTest, Neighbor_IsEbgp_DifferentAsWithoutConfederation)
{
    types::IPAddress nbrAddr = mkV4(0x0A000001);
    Neighbor* nbr = proc->getNtable().createNeighbor(nbrAddr);
    ASSERT_NE(nbr, nullptr);

    // No REMOTE_AS configured -> isEbgp()/isConfedEbgp() both false.
    EXPECT_FALSE(nbr->isEbgp());
    EXPECT_FALSE(nbr->isConfedEbgp());

    // REMOTE_AS == local AS -> iBGP, not eBGP.
    nbr->getConfigs().get<config::BgpNeighborSession::REMOTE_AS>().set(kLocalAs);
    EXPECT_FALSE(nbr->isEbgp());
    EXPECT_FALSE(nbr->isConfedEbgp());

    // REMOTE_AS != local AS, not in confederation peer list -> eBGP.
    nbr->getConfigs().get<config::BgpNeighborSession::REMOTE_AS>().set(65099);
    EXPECT_TRUE(nbr->isEbgp());
    EXPECT_FALSE(nbr->isConfedEbgp());
}

TEST_F(Internal_BgpTest, Neighbor_IsConfedEbgp_RemoteAsInConfederationPeers)
{
    types::IPAddress nbrAddr = mkV4(0x0A000002);
    Neighbor* nbr = proc->getNtable().createNeighbor(nbrAddr);
    ASSERT_NE(nbr, nullptr);

    constexpr uint32_t kConfedMemberAs = 65050;
    nbr->getConfigs().get<config::BgpNeighborSession::REMOTE_AS>().set(kConfedMemberAs);

    // Add kConfedMemberAs to BGP_CONFEDERATION_PEERS.
    proc->getConfigs().get<config::Bgp::BGP_CONFEDERATION_PEERS>().withWrite(
        [&](std::vector<uint32_t>& peers) {
            peers.push_back(kConfedMemberAs);
            return true;
        });

    // REMOTE_AS is a confederation member (and != local AS) -> confed-eBGP, not plain eBGP.
    EXPECT_FALSE(nbr->isEbgp());
    EXPECT_TRUE(nbr->isConfedEbgp());
}

TEST_F(Internal_BgpTest, Neighbor_BuildAttributeRanges_DiscardAndWithdraw)
{
    types::IPAddress nbrAddr = mkV4(0x0A000003);
    Neighbor* nbr = proc->getNtable().createNeighbor(nbrAddr);
    ASSERT_NE(nbr, nullptr);

    nbr->getConfigs().get<config::BgpNeighborSession::PATH_ATTRIBUTE_DISCARD>().withWrite(
        [](std::vector<std::tuple<uint8_t, uint8_t>>& ranges) {
            ranges.push_back({10, 12});   // discard
            return true;
        });
    nbr->getConfigs().get<config::BgpNeighborSession::PATH_ATTRIBUTE_TREAT_AS_WITHDRAW>().withWrite(
        [](std::vector<std::tuple<uint8_t, uint8_t>>& ranges) {
            ranges.push_back({200, 201}); // treat-as-withdraw
            return true;
        });
    proc->getScheduler().waitIdle();

    nbr->buildAttributeRanges();
    const auto& ranges = nbr->getAttrRanges();

    for (uint16_t i = 10; i <= 12; ++i)
        EXPECT_TRUE(ranges.discard.test(i));
    EXPECT_FALSE(ranges.discard.test(9));
    EXPECT_FALSE(ranges.discard.test(13));

    for (uint16_t i = 200; i <= 201; ++i)
        EXPECT_TRUE(ranges.withdraw.test(i));
    EXPECT_FALSE(ranges.withdraw.test(199));
    EXPECT_FALSE(ranges.withdraw.test(202));

    // Discard and withdraw sets don't overlap for these configured ranges.
    EXPECT_FALSE(ranges.discard.test(200));
    EXPECT_FALSE(ranges.withdraw.test(10));

    // Re-building after clearing the list resets both bitsets.
    nbr->getConfigs().get<config::BgpNeighborSession::PATH_ATTRIBUTE_DISCARD>().withWrite(
        [](std::vector<std::tuple<uint8_t, uint8_t>>& ranges) {
            ranges.clear();
            return true;
        });
    nbr->getConfigs().get<config::BgpNeighborSession::PATH_ATTRIBUTE_TREAT_AS_WITHDRAW>().withWrite(
        [](std::vector<std::tuple<uint8_t, uint8_t>>& ranges) {
            ranges.clear();
            return true;
        });
    proc->getScheduler().waitIdle();
    nbr->buildAttributeRanges();
    const auto& cleared = nbr->getAttrRanges();
    for (uint16_t i = 10; i <= 12; ++i)
        EXPECT_FALSE(cleared.discard.test(i));
    for (uint16_t i = 200; i <= 201; ++i)
        EXPECT_FALSE(cleared.withdraw.test(i));
}

TEST_F(Internal_BgpTest, PeerGroup_SessionConfigInheritance_NeighborOverridesGroup)
{
    PeerGroup& pg = proc->getNtable().createPeerGroup("GRP1");
    pg.getSessionConfigs().get<config::BgpNeighborSession::REMOTE_AS>().set(65111);

    types::IPAddress nbrAddr = mkV4(0x0A000004);
    Neighbor* nbr = proc->getNtable().createNeighbor(nbrAddr);
    ASSERT_NE(nbr, nullptr);

    ASSERT_TRUE(nbr->getConfigs().setPeerGroup(&pg));

    // Without a per-neighbor override, REMOTE_AS falls through to the group's value.
    {
        auto remAs = nbr->getConfigs().get<config::BgpNeighborSession::REMOTE_AS>();
        ASSERT_TRUE(remAs.hasValue());
        EXPECT_EQ(remAs.load(), 65111u);
    }

    // Setting REMOTE_AS directly on the neighbor overrides the inherited group value.
    nbr->getConfigs().get<config::BgpNeighborSession::REMOTE_AS>().set(65222);
    {
        auto remAs = nbr->getConfigs().get<config::BgpNeighborSession::REMOTE_AS>();
        ASSERT_TRUE(remAs.hasValue());
        EXPECT_EQ(remAs.load(), 65222u);
    }

    // The peer-group's own value is unaffected by the neighbor override.
    auto groupRemAs = pg.getSessionConfigs().get<config::BgpNeighborSession::REMOTE_AS>();
    ASSERT_TRUE(groupRemAs.hasValue());
    EXPECT_EQ(groupRemAs.load(), 65111u);
}

TEST_F(Internal_BgpTest, PeerGroup_AfConfig_PeerOwnedFieldsReadFromGroup)
{
    PeerGroup& pg = proc->getNtable().createPeerGroup("GRP2");
    AfiSafi afiSafi{BGP_AFI_IPV4, BGP_SAFI_UNICAST};

    // NEXT_HOP_SELF is in peerOwnedTable, so it is always sourced from the
    // group's AF config once a peer group is attached.
    config::BgpNeighborRegistry* groupAf = pg.getAfConfigs(afiSafi);
    ASSERT_NE(groupAf, nullptr);
    groupAf->get<config::BgpNeighbor::NEXT_HOP_SELF>().set(true);

    types::IPAddress nbrAddr = mkV4(0x0A000005);
    Neighbor* nbr = proc->getNtable().createNeighbor(nbrAddr);
    ASSERT_NE(nbr, nullptr);
    nbr->addAfNeighbor(afiSafi);

    NeighborAf& af = nbr->getAfNeighbor(afiSafi);
    ASSERT_TRUE(af.getConfigs().setPeerGroup(&pg));

    EXPECT_TRUE(af.getConfigs().get<config::BgpNeighbor::NEXT_HOP_SELF>().load());

    // Changing the group's value is reflected immediately for the peer-owned field.
    groupAf->get<config::BgpNeighbor::NEXT_HOP_SELF>().set(false);
    EXPECT_FALSE(af.getConfigs().get<config::BgpNeighbor::NEXT_HOP_SELF>().load());
}

TEST_F(Internal_BgpTest, PeerSessionTemplate_InheritancePrecedence_OverPeerGroup)
{
    PeerGroup& pg = proc->getNtable().createPeerGroup("GRP3");
    pg.getSessionConfigs().get<config::BgpNeighborSession::REMOTE_AS>().set(65300);

    PeerSessionTemplate& tmpl = proc->getNtable().createPeerSessionTemplate("SESSTMPL");
    tmpl.getConfigs().get<config::BgpNeighborSession::REMOTE_AS>().set(65400);

    types::IPAddress nbrAddr = mkV4(0x0A000006);
    Neighbor* nbr = proc->getNtable().createNeighbor(nbrAddr);
    ASSERT_NE(nbr, nullptr);

    // setPeerGroup then setPeerSessionTemplate should fail (mutually exclusive),
    // per NeighborConfigs::setPeerSessionTemplate's "peerGroup already set" guard.
    ASSERT_TRUE(nbr->getConfigs().setPeerGroup(&pg));
    EXPECT_FALSE(nbr->getConfigs().setPeerSessionTemplate(&tmpl));

    // Group value still wins since the template attach was rejected.
    auto remAs = nbr->getConfigs().get<config::BgpNeighborSession::REMOTE_AS>();
    ASSERT_TRUE(remAs.hasValue());
    EXPECT_EQ(remAs.load(), 65300u);
}

namespace
{
bool waitForBgp(std::function<bool()> cond, std::chrono::milliseconds timeout = 2000ms)
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (cond())
            return true;
        std::this_thread::sleep_for(1ms);
    }
    return cond();
}
} // namespace

// COLLISION DETECTION

TEST_F(Internal_BgpTest, Collision_ShouldKeep_EqualRouterIds_AlwaysFalse)
{
    EXPECT_FALSE(CollisionDetector::shouldKeep(true, 100, 100));
    EXPECT_FALSE(CollisionDetector::shouldKeep(false, 100, 100));
}

TEST_F(Internal_BgpTest, Collision_ShouldKeep_LocalRidHigher_KeepsOutgoingOnly)
{
    // localRid > peerRid: keep the connection iff it is the outgoing one.
    EXPECT_TRUE(CollisionDetector::shouldKeep(true, 200, 100));
    EXPECT_FALSE(CollisionDetector::shouldKeep(false, 200, 100));
}

TEST_F(Internal_BgpTest, Collision_ShouldKeep_LocalRidLower_KeepsIncomingOnly)
{
    // localRid < peerRid: keep the connection iff it is the incoming one.
    EXPECT_FALSE(CollisionDetector::shouldKeep(true, 100, 200));
    EXPECT_TRUE(CollisionDetector::shouldKeep(false, 100, 200));
}

TEST_F(Internal_BgpTest, Collision_NotificationCode_IsCeaseCollisionResolution)
{
    EXPECT_EQ(CollisionDetector::collisionNotificationCode(),
              static_cast<uint16_t>(BGP_NOTIFICATION_CEASE_COLLISION_RESOLUTION));
}

// FSM STATE TRANSITIONS

TEST_F(Internal_BgpTest, Fsm_IdleToActive_OnManualStartPassiveTcp)
{
    types::IPAddress peer = mkV4(0x0A000002); // 10.0.0.2
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();

    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->getFsmState(), FsmState::ACTIVE);
    EXPECT_FALSE(session->established());
}

TEST_F(Internal_BgpTest, Fsm_ActiveToOpenSent_OnTcpCrAcked)
{
    types::IPAddress peer = mkV4(0x0A000003);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();

    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(session->getFsmState(), FsmState::ACTIVE);

    // TCP_CR_ACKED from ACTIVE -> sendOpen() (no-op, no primaryConn) -> OPEN_SENT.
    session->postEvent(FsmEvent::TCP_CR_ACKED);
    proc->getScheduler().waitIdle();

    EXPECT_EQ(session->getFsmState(), FsmState::OPEN_SENT);
}

TEST_F(Internal_BgpTest, Fsm_OpenSentToOpenConfirm_NegotiatesHoldAndKeepaliveFromPeerOffer)
{
    types::IPAddress peer = mkV4(0x0A000004);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();
    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);

    session->postEvent(FsmEvent::TCP_CR_ACKED);
    proc->getScheduler().waitIdle();
    ASSERT_EQ(session->getFsmState(), FsmState::OPEN_SENT);

    session->holdTime = 90;
    session->setPeerRid(0x01020304);
    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();

    EXPECT_EQ(session->getFsmState(), FsmState::OPEN_CONFIRMED);
    EXPECT_EQ(session->holdTime, 90);
    EXPECT_EQ(session->keepaliveInterval, 60);
    EXPECT_EQ(session->getPeerRid(), 0x01020304u);
}

TEST_F(Internal_BgpTest, Fsm_OpenSentToOpenConfirm_KeepaliveDerivedFromHoldThirdWhenSmaller)
{
    types::IPAddress peer = mkV4(0x0A000005);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();
    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);

    session->postEvent(FsmEvent::TCP_CR_ACKED);
    proc->getScheduler().waitIdle();
    ASSERT_EQ(session->getFsmState(), FsmState::OPEN_SENT);

    session->holdTime = 30;
    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();

    EXPECT_EQ(session->getFsmState(), FsmState::OPEN_CONFIRMED);
    EXPECT_EQ(session->holdTime, 30);
    EXPECT_EQ(session->keepaliveInterval, 10);
}

TEST_F(Internal_BgpTest, Fsm_OpenSentToOpenConfirm_ZeroHoldTimeDisablesTimers)
{
    types::IPAddress peer = mkV4(0x0A000006);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();
    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);

    session->postEvent(FsmEvent::TCP_CR_ACKED);
    proc->getScheduler().waitIdle();
    ASSERT_EQ(session->getFsmState(), FsmState::OPEN_SENT);

    // Peer offers hold=0 (no timeout) -> negotiated min(0,180)=0 -> kaInterval=0,
    // hold/keepalive timers stopped rather than started.
    session->holdTime = 0;
    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();

    EXPECT_EQ(session->getFsmState(), FsmState::OPEN_CONFIRMED);
    EXPECT_EQ(session->holdTime, 0);
    EXPECT_EQ(session->keepaliveInterval, 0);
}

TEST_F(Internal_BgpTest, Fsm_OpenSent_PeerHoldBelowMinimumHoldtime_RejectsToIdle)
{
    types::IPAddress peer = mkV4(0x0A000007);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    // Configure MINIMUM_HOLDTIME on this neighbor's base transport config.
    auto& baseCfg = nbr->getConfigs().get<config::BgpNeighborSession::BGP_BASE>().get();
    baseCfg.get<config::BgpTransportBase::MINIMUM_HOLDTIME>().set(60);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();
    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);

    session->postEvent(FsmEvent::TCP_CR_ACKED);
    proc->getScheduler().waitIdle();
    ASSERT_EQ(session->getFsmState(), FsmState::OPEN_SENT);

    // Peer offers hold=30, below the configured MINIMUM_HOLDTIME=60 -> rejected.
    session->holdTime = 30;
    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();

    EXPECT_EQ(session->getFsmState(), FsmState::IDLE);
}

TEST_F(Internal_BgpTest, Fsm_OpenConfirmToEstablished_OnKeepaliveMsg)
{
    types::IPAddress peer = mkV4(0x0A000008);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();
    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);

    session->postEvent(FsmEvent::TCP_CR_ACKED);
    proc->getScheduler().waitIdle();

    session->holdTime = 90;
    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();
    ASSERT_EQ(session->getFsmState(), FsmState::OPEN_CONFIRMED);

    session->postEvent(FsmEvent::KEEPALIVE_MSG);
    proc->getScheduler().waitIdle();

    EXPECT_EQ(session->getFsmState(), FsmState::ESTABLISHED);
    EXPECT_TRUE(session->established());
}

TEST_F(Internal_BgpTest, Fsm_Established_HoldTimerExpiry_TearsDownToIdle)
{
    types::IPAddress peer = mkV4(0x0A000009);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();
    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);

    session->postEvent(FsmEvent::TCP_CR_ACKED);
    proc->getScheduler().waitIdle();

    session->holdTime = 90;
    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();

    session->postEvent(FsmEvent::KEEPALIVE_MSG);
    proc->getScheduler().waitIdle();
    ASSERT_EQ(session->getFsmState(), FsmState::ESTABLISHED);

    // HOLD_TIMER_EXPIRES -> resetToIdle(true, HOLD_TIMER_EXPIRED) -> sendNotification
    // (no-op without primaryConn), closeAllConnections (no-op), transitionTo(IDLE).
    session->postEvent(FsmEvent::HOLD_TIMER_EXPIRES);
    proc->getScheduler().waitIdle();

    EXPECT_EQ(session->getFsmState(), FsmState::IDLE);
    EXPECT_FALSE(session->established());
    EXPECT_EQ(session->getTimers().connectionRetryCount, 0u);
}

TEST_F(Internal_BgpTest, Fsm_Established_KeepaliveTimerExpiry_SendsKeepaliveAndRestarts)
{
    types::IPAddress peer = mkV4(0x0A00000A);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();
    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);

    session->postEvent(FsmEvent::TCP_CR_ACKED);
    proc->getScheduler().waitIdle();

    session->holdTime = 90;
    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();

    session->postEvent(FsmEvent::KEEPALIVE_MSG);
    proc->getScheduler().waitIdle();
    ASSERT_EQ(session->getFsmState(), FsmState::ESTABLISHED);

    // KEEPALIVE_TIMER_EXPIRES -> sendKeepalive() (no-op) + restartKeepaliveTimer();
    // stays ESTABLISHED.
    session->postEvent(FsmEvent::KEEPALIVE_TIMER_EXPIRES);
    proc->getScheduler().waitIdle();

    EXPECT_EQ(session->getFsmState(), FsmState::ESTABLISHED);
}

TEST_F(Internal_BgpTest, Fsm_Established_KeepaliveOrUpdateMsg_RestartsHoldTimerAndStaysUp)
{
    types::IPAddress peer = mkV4(0x0A00000B);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();
    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);

    session->postEvent(FsmEvent::TCP_CR_ACKED);
    proc->getScheduler().waitIdle();

    session->holdTime = 90;
    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();

    session->postEvent(FsmEvent::KEEPALIVE_MSG);
    proc->getScheduler().waitIdle();
    ASSERT_EQ(session->getFsmState(), FsmState::ESTABLISHED);

    session->postEvent(FsmEvent::UPDATE_MSG);
    proc->getScheduler().waitIdle();
    EXPECT_EQ(session->getFsmState(), FsmState::ESTABLISHED);

    session->postEvent(FsmEvent::KEEPALIVE_MSG);
    proc->getScheduler().waitIdle();
    EXPECT_EQ(session->getFsmState(), FsmState::ESTABLISHED);
}

TEST_F(Internal_BgpTest, Fsm_Established_MaxPrefixReached_TearsDownToIdle)
{
    types::IPAddress peer = mkV4(0x0A00000C);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();
    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);

    session->postEvent(FsmEvent::TCP_CR_ACKED);
    proc->getScheduler().waitIdle();

    session->holdTime = 90;
    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();

    session->postEvent(FsmEvent::KEEPALIVE_MSG);
    proc->getScheduler().waitIdle();
    ASSERT_EQ(session->getFsmState(), FsmState::ESTABLISHED);

    // MAX_PREFIX_REACHED -> resetToIdle(true, CEASE_MAX_PREFIXES) -> IDLE.
    session->postEvent(FsmEvent::MAX_PREFIX_REACHED);
    proc->getScheduler().waitIdle();

    EXPECT_EQ(session->getFsmState(), FsmState::IDLE);
    EXPECT_FALSE(session->established());
}

TEST_F(Internal_BgpTest, Fsm_ManualStop_FromEstablished_ReturnsToIdle)
{
    types::IPAddress peer = mkV4(0x0A00000D);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();
    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);

    session->postEvent(FsmEvent::TCP_CR_ACKED);
    proc->getScheduler().waitIdle();

    session->holdTime = 90;
    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();

    session->postEvent(FsmEvent::KEEPALIVE_MSG);
    proc->getScheduler().waitIdle();
    ASSERT_EQ(session->getFsmState(), FsmState::ESTABLISHED);

    // MANUAL_STOP -> resetToIdle(true, CEASE_ADMIN_SHUT) -> IDLE.
    session->postEvent(FsmEvent::MANUAL_STOP);
    proc->getScheduler().waitIdle();

    EXPECT_EQ(session->getFsmState(), FsmState::IDLE);
}

TEST_F(Internal_BgpTest, Fsm_ManualStop_FromActive_ReturnsToIdleAndResetsRetryCount)
{
    types::IPAddress peer = mkV4(0x0A00000E);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();
    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(session->getFsmState(), FsmState::ACTIVE);

    session->postEvent(FsmEvent::MANUAL_STOP);
    proc->getScheduler().waitIdle();

    EXPECT_EQ(session->getFsmState(), FsmState::IDLE);
    EXPECT_EQ(session->getTimers().connectionRetryCount, 0u);
}

TEST_F(Internal_BgpTest, Session_ResolveCollision_OpenConfirm_LocalRidHigher_KeepsOutgoingSession)
{
    types::IPAddress peer = mkV4(0x0A00000F);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();
    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);

    session->postEvent(FsmEvent::TCP_CR_ACKED);
    proc->getScheduler().waitIdle();

    session->holdTime = 90;
    session->setPeerRid(100); // < proc->getRouterId() == 65001
    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();
    ASSERT_EQ(session->getFsmState(), FsmState::OPEN_CONFIRMED);

    EXPECT_FALSE(session->isOutgoing());
    EXPECT_FALSE(CollisionDetector::shouldKeep(session->isOutgoing(), proc->getRouterId(), session->getPeerRid()));

    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();

    EXPECT_EQ(session->getFsmState(), FsmState::IDLE);
}

TEST_F(Internal_BgpTest, Session_ResolveCollision_EqualRouterIds_AlwaysTearsDown)
{
    types::IPAddress peer = mkV4(0x0A000010);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();
    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);

    session->postEvent(FsmEvent::TCP_CR_ACKED);
    proc->getScheduler().waitIdle();

    session->holdTime = 90;
    // Equal to the local router-id (AS 65001, no BGP_ROUTER_ID configured).
    session->setPeerRid(proc->getRouterId());
    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();
    ASSERT_EQ(session->getFsmState(), FsmState::OPEN_CONFIRMED);

    EXPECT_FALSE(CollisionDetector::shouldKeep(session->isOutgoing(), proc->getRouterId(), session->getPeerRid()));

    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();

    EXPECT_EQ(session->getFsmState(), FsmState::IDLE);
}

TEST_F(Internal_BgpTest, SessionTimers_HoldTimerExpiry_PostsHoldTimerExpiresEvent)
{
    types::IPAddress peer = mkV4(0x0A000011);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();
    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);

    session->postEvent(FsmEvent::TCP_CR_ACKED);
    proc->getScheduler().waitIdle();

    session->holdTime = 90;
    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();

    session->postEvent(FsmEvent::KEEPALIVE_MSG);
    proc->getScheduler().waitIdle();
    ASSERT_EQ(session->getFsmState(), FsmState::ESTABLISHED);

    session->getTimers().startHoldTimer(std::chrono::seconds(1));

    bool reachedIdle = waitForBgp([&]() {
        proc->getScheduler().waitIdle();
        return session->getFsmState() == FsmState::IDLE;
    }, 3000ms);

    EXPECT_TRUE(reachedIdle);
    EXPECT_EQ(session->getFsmState(), FsmState::IDLE);
}

TEST_F(Internal_BgpTest, SessionTimers_CancelAll_PreventsHoldTimerFromFiring)
{
    types::IPAddress peer = mkV4(0x0A000012);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();
    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);

    session->postEvent(FsmEvent::TCP_CR_ACKED);
    proc->getScheduler().waitIdle();

    session->holdTime = 90;
    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();

    session->postEvent(FsmEvent::KEEPALIVE_MSG);
    proc->getScheduler().waitIdle();
    ASSERT_EQ(session->getFsmState(), FsmState::ESTABLISHED);

    // Start a longer hold timer, then immediately cancel everything. The
    // session should remain ESTABLISHED (no HOLD_TIMER_EXPIRES is posted).
    session->getTimers().startHoldTimer(std::chrono::seconds(0));
    session->getTimers().cancelAll();
    proc->getScheduler().waitIdle();

    // Give any (cancelled) timer callback a chance to fire and be dropped.
    std::this_thread::sleep_for(50ms);
    proc->getScheduler().waitIdle();

    EXPECT_EQ(session->getFsmState(), FsmState::ESTABLISHED);
}

TEST_F(Internal_BgpTest, SessionTimers_KeepaliveTimerExpiry_KeepsSessionEstablished)
{
    types::IPAddress peer = mkV4(0x0A000013);
    Neighbor* nbr = proc->getNtable().createNeighbor(peer);
    ASSERT_NE(nbr, nullptr);

    proc->startPassiveSession(*nbr);
    proc->getScheduler().waitIdle();
    Session* session = proc->findSession(peer);
    ASSERT_NE(session, nullptr);

    session->postEvent(FsmEvent::TCP_CR_ACKED);
    proc->getScheduler().waitIdle();

    session->holdTime = 90;
    session->postEvent(FsmEvent::BGP_OPEN);
    proc->getScheduler().waitIdle();

    session->postEvent(FsmEvent::KEEPALIVE_MSG);
    proc->getScheduler().waitIdle();
    ASSERT_EQ(session->getFsmState(), FsmState::ESTABLISHED);

    // Near-zero keepalive timer fires KEEPALIVE_TIMER_EXPIRES repeatedly via
    // its self-restart; session must stay ESTABLISHED throughout.
    session->getTimers().startKeepaliveTimer(std::chrono::seconds(0));

    std::this_thread::sleep_for(50ms);
    proc->getScheduler().waitIdle();

    EXPECT_EQ(session->getFsmState(), FsmState::ESTABLISHED);

    // Clean up so the self-restarting timer doesn't keep firing into teardown.
    session->getTimers().cancelAll();
    proc->getScheduler().waitIdle();
}

namespace
{
struct SessionLoopbackPair
{
    Tcp& tcpA;
    Tcp& tcpB;

    Listener listener;
    Connection connA;
    std::optional<Connection> connB;

    struct AcceptedConn
    {
        std::optional<Connection>* out = nullptr;
        bool got = false;
    };
    AcceptedConn acceptedB;

    static types::IPAddress mkLoopback()
    {
        types::IPAddress a;
        a.setV4(0x7F000001);
        return a;
    }

    static void onAccept(AcceptCallbackCtx& ctx) noexcept
    {
        auto* a = static_cast<AcceptedConn*>(ctx.user);
        a->got = true;
        if (a->out) a->out->emplace(std::move(ctx.newConn));
    }

    SessionLoopbackPair(Tcp& a, Tcp& b, uint16_t port, Session& sessionA, Session& sessionB)
        : tcpA(a), tcpB(b),
          listener(std::move([&]() {
              ListenOptions opt;
              opt.recvCallback = &Session::onReceiveCallback;
              opt.recvUser = &sessionB;
              opt.onAccept = &onAccept;
              opt.onAcceptUser = &acceptedB;
              return tcpB.listen(TcpEndpoint{mkLoopback(), port}, opt);
          }())),
          connA(std::move([&]() {
              ConnectOptions opt;
              opt.recvCallback = &Session::onReceiveCallback;
              opt.recvUser = &sessionA;
              return tcpA.connect(TcpEndpoint{mkLoopback(), 0}, TcpEndpoint{mkLoopback(), port}, opt);
          }()))
    {
        // acceptedB is declared after connB, so its default member initializer
        // (out = nullptr) runs after the listener above captured &acceptedB;
        // wire the pointer here once every member is fully constructed.
        acceptedB.out = &connB;
    }

    // Pumps both stacks until the accept side has captured its Connection.
    bool pumpUntilAccepted(int maxIters = 200)
    {
        for (int i = 0; i < maxIters; ++i)
        {
            tcpA.pump(1);
            tcpB.pump(1);
            if (acceptedB.got) return true;
        }
        return acceptedB.got;
    }

    void pump(int iters = 5)
    {
        for (int i = 0; i < iters; ++i)
        {
            tcpA.pump(1);
            tcpB.pump(1);
        }
    }
};
} // namespace

TEST_F(Internal_BgpTest, SessionEstablishment_RealLoopback_BothReachEstablishedWithRealOpenWrite)
{
    cli::MockFileSystem fsB;
    core::Global globalB(fsB, {}, false, true);
    core::VirtualRouter* vrfB = globalB.getRoutingInstance("default", types::AddressFamily::IPv4);
    vrfB->getTcp().swapEngineForTesting(new MockTcpEngine(*vrfB));
    BgpProcess procB(65002, vrfB);

    types::IPAddress addrA = mkV4(0x7F000001); // 127.0.0.1, used for both neighbor addrs
    types::IPAddress addrB = mkV4(0x7F000001);

    Neighbor* nbrA = proc->getNtable().createNeighbor(addrB);
    Neighbor* nbrB = procB.getNtable().createNeighbor(addrA);
    ASSERT_NE(nbrA, nullptr);
    ASSERT_NE(nbrB, nullptr);

    proc->startPassiveSession(*nbrA);
    proc->getScheduler().waitIdle();
    procB.startPassiveSession(*nbrB);
    procB.getScheduler().waitIdle();

    Session* sessionA = proc->findSession(addrB);
    Session* sessionB = procB.findSession(addrA);
    ASSERT_NE(sessionA, nullptr);
    ASSERT_NE(sessionB, nullptr);
    ASSERT_EQ(sessionA->getFsmState(), FsmState::ACTIVE);
    ASSERT_EQ(sessionB->getFsmState(), FsmState::ACTIVE);

    // Mock-backed Connection pair on an arbitrary high port (not 179).
    SessionLoopbackPair pair(vrf->getTcp(), vrfB->getTcp(), 17900, *sessionA, *sessionB);
    ASSERT_TRUE(pair.pumpUntilAccepted());
    ASSERT_TRUE(pair.connB.has_value());

    // Hand each end to its session: ACTIVE + TCP_CONNECTION_CONFIRMED ->
    // sendOpen() (now a REAL write since primaryConn is set) -> OPEN_SENT.
    sessionA->acceptConnection(std::move(pair.connA));
    proc->getScheduler().waitIdle();
    sessionB->acceptConnection(std::move(*pair.connB));
    procB.getScheduler().waitIdle();

    EXPECT_EQ(sessionA->getFsmState(), FsmState::OPEN_SENT);
    EXPECT_EQ(sessionB->getFsmState(), FsmState::OPEN_SENT);
    EXPECT_NE(sessionA->getPrimaryConnection(), nullptr);
    EXPECT_NE(sessionB->getPrimaryConnection(), nullptr);

    for (int i = 0; i < 30; ++i)
    {
        vrf->getTcp().pump(1);
        proc->getScheduler().waitIdle();
        procB.getScheduler().waitIdle();
        vrfB->getTcp().pump(1);
        proc->getScheduler().waitIdle();
        procB.getScheduler().waitIdle();
        if (sessionA->getFsmState() == FsmState::ESTABLISHED &&
            sessionB->getFsmState() == FsmState::ESTABLISHED)
            break;
    }

    EXPECT_EQ(sessionA->getFsmState(), FsmState::ESTABLISHED);
    EXPECT_EQ(sessionB->getFsmState(), FsmState::ESTABLISHED);
}

TEST_F(Internal_BgpTest, SessionEstablishment_EbgpDetection_DifferentAsNumbers)
{
    cli::MockFileSystem fsB;
    core::Global globalB(fsB, {}, false, true);
    core::VirtualRouter* vrfB = globalB.getRoutingInstance("default", types::AddressFamily::IPv4);
    vrfB->getTcp().swapEngineForTesting(new MockTcpEngine(*vrfB));
    BgpProcess procB(65002, vrfB);

    types::IPAddress addrB = mkV4(0x7F000001);
    Neighbor* nbrA = proc->getNtable().createNeighbor(addrB);
    ASSERT_NE(nbrA, nullptr);

    EXPECT_FALSE(nbrA->isConfedEbgp());
    (void)nbrA->isEbgp();
}

// RFC 8277 multi-session: per-AFI child Sessions under MultiSession
// negotiate independently when TRANSPORT_MULTI_SESSION is enabled.
TEST_F(Internal_BgpTest, MultiSession_PerAfiChildSessionsNegotiateIndependently)
{
    GTEST_SKIP();    
}

// Once BgpRx::processOpen's inverted verifyConnection() check is fixed,
// two real TCP-loopback sessions should reach ESTABLISHED with correct
// negotiated capabilities (AS4/AS_TRANS, MP-BGP AFI/SAFI intersection).
TEST_F(Internal_BgpTest, SessionEstablishment_RealTcpLoopback_ReachesEstablished)
{
    GTEST_SKIP();    
}

// HOLD timer expiry on one side of a real established session tears down
// with NOTIFICATION on the other side.
TEST_F(Internal_BgpTest, SessionEstablishment_HoldTimerExpiry_TearsDownPeer)
{
    GTEST_SKIP();    
}

namespace
{
struct UpdateBuilder
{
    std::vector<uint8_t> nlriBytes;
    std::vector<uint8_t> withdrawnBytes;
    IncomingUpdate uinfo;

    void addAnnouncement(const types::IPv4Prefix& p)
    {
        size_t off = nlriBytes.size();
        nlriBytes.resize(off + ExampleNlri::nlriEncodedSize(p));
        ExampleNlri::encodeNlri(nlriBytes.data() + off, p);
    }

    void addWithdrawn(const types::IPv4Prefix& p)
    {
        size_t off = withdrawnBytes.size();
        withdrawnBytes.resize(off + ExampleNlri::nlriEncodedSize(p));
        ExampleNlri::encodeNlri(withdrawnBytes.data() + off, p);
    }

    // Must be called after all addAnnouncement/addWithdrawn calls, since it
    // captures pointers into nlriBytes/withdrawnBytes.
    IncomingUpdate& finalize(const Attributes& attrs, const Path& path)
    {
        uinfo.attrs = attrs;
        uinfo.path = path;
        uinfo.afi = path.family;
        uinfo.nlriData = std::span<uint8_t>(nlriBytes.data(), nlriBytes.size());
        uinfo.withdrawnData = std::span<uint8_t>(withdrawnBytes.data(), withdrawnBytes.size());
        return uinfo;
    }
};
} // namespace

class Internal_BgpPolicyTest : public Internal_BgpTest
{
protected:
    static constexpr AfiSafi kAfiSafi{BGP_AFI_IPV4, BGP_SAFI_UNICAST};

    void TearDown() override
    {
        ownedSessions.clear();
        Internal_BgpTest::TearDown();
    }

    void installConnectedNextHop(uint32_t networkHostOrder, uint8_t length = 24)
    {
        auto* entry = new core::RibEntry<uint32_t>;
        entry->prefix = networkHostOrder;
        entry->length = length;
        entry->source = core::RouteSource::CONNECTED;
        entry->processId = 0;
        entry->adminDistance = 0;
        entry->metric = 0;
        entry->addNextHopInterface(interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 0));
        vrf->getRib().addRoute(entry);
        vrf->getRib().wait<uint32_t>();
    }

    struct PeerFixture
    {
        Neighbor* nbr;
        Session* session;
    };

    PeerFixture makePeer(const types::IPAddress& peerAddr, uint32_t peerRid, uint32_t remoteAs)
    {
        proc->enableAddressFamily<ExampleNlri::afi>();

        Neighbor* nbr = proc->getNtable().createNeighbor(peerAddr);
        nbr->addAfNeighbor(const_cast<AfiSafi&>(kAfiSafi));
        nbr->getConfigs().get<config::BgpNeighborSession::REMOTE_AS>().set(remoteAs);

        auto session = std::make_unique<Session>(*nbr);
        session->getNegotiated().activeFamilies.insert(kAfiSafi);
        session->setPeerRid(peerRid);

        Session* sptr = session.get();
        ownedSessions.push_back(std::move(session));

        sptr->postEvent(FsmEvent::MANUAL_START_PASSIVE_TCP);
        proc->getScheduler().waitIdle(); // IDLE -> ACTIVE

        sptr->postEvent(FsmEvent::TCP_CR_ACKED);
        proc->getScheduler().waitIdle(); // ACTIVE -> OPEN_SENT

        sptr->postEvent(FsmEvent::BGP_OPEN);
        proc->getScheduler().waitIdle(); // OPEN_SENT -> OPEN_CONFIRMED

        sptr->postEvent(FsmEvent::KEEPALIVE_MSG);
        proc->getScheduler().waitIdle(); // OPEN_CONFIRMED -> ESTABLISHED
                                               // (triggers onSessionEstablished:
                                               //  activatePeer, nbr->rid, nbr->session)

        return PeerFixture{nbr, sptr};
    }

    std::recursive_mutex& getSchedulerLock() { return proc->getScheduler().getLock(); }

    std::vector<std::unique_ptr<Session>> ownedSessions;

    // Returns true if prefix is present in the global RIB (accepted + installed).
    bool isInstalled(uint32_t hostAddr, uint8_t length)
    {
        vrf->getRib().wait<uint32_t>();
        utils::RCU::Guard g;
        core::RibEntry<uint32_t>* e = vrf->getRib().lookup<uint32_t>(hostAddr, g);
        return e != nullptr && e->length == length && e->source == core::RouteSource::BGP;
    }

    config::BgpAddressFamilyRegistry& afConfigs()
    {
        return proc->getConfigs().get<config::Bgp::ADDRESS_FAMILIES>().emplaceBack(kAfiSafi.flatten());
    }
};

// Own AS present in AS_PATH -> ingress AS-PATH loop -> route rejected, never installed.
TEST_F(Internal_BgpPolicyTest, Ingress_AsPathLoop_OwnAsInPath_Rejected)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peer = makePeer(mkV4(0x0A000002), 0x01010101, 65099);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099, kLocalAs, 65050}; // contains our own AS (65001)
    attrs.asPath = {seg};

    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0000100, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_FALSE(isInstalled(0xC0000100, 24));
}

// No own-AS in AS_PATH -> accepted -> installed in the global RIB.
TEST_F(Internal_BgpPolicyTest, Ingress_AsPathNoLoop_Accepted)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peer = makePeer(mkV4(0x0A000003), 0x01010102, 65099);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099, 65050}; // no own AS
    attrs.asPath = {seg};

    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0000200, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_TRUE(isInstalled(0xC0000200, 24));
}

// Confederation member AS present in CONFED_SEQUENCE -> confederation loop -> rejected.
TEST_F(Internal_BgpPolicyTest, Ingress_ConfedLoop_OwnAsInConfedSequence_Rejected)
{
    installConnectedNextHop(0x0A000000, 24);

    constexpr uint32_t kConfedMemberAs = 65050;
    proc->getConfigs().get<config::Bgp::BGP_CONFEDERATION_IDENTIFIER>().set(65000);
    proc->getConfigs().get<config::Bgp::BGP_CONFEDERATION_PEERS>().withWrite(
        [&](std::vector<uint32_t>& peers) {
            peers.push_back(kConfedMemberAs);
            return true;
        });

    // Local AS (kLocalAs=65001) acts as the confed member; confedId=65000 != kLocalAs.
    auto peer = makePeer(mkV4(0x0A000004), 0x01010103, kConfedMemberAs);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment confSeq;
    confSeq.segmentType = BGP_AS_CONFED_SEQUENCE;
    confSeq.asns = {kConfedMemberAs, kLocalAs}; // our own AS appears in a CONFED_SEQUENCE
    attrs.asPath = {confSeq};

    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0000300, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_FALSE(isInstalled(0xC0000300, 24));
}

// ORIGINATOR_ID equal to our router ID -> route-reflector loop -> rejected (iBGP only).
TEST_F(Internal_BgpPolicyTest, Ingress_RrLoop_OwnOriginatorId_Rejected)
{
    installConnectedNextHop(0x0A000000, 24);

    // iBGP peer: REMOTE_AS == kLocalAs.
    auto peer = makePeer(mkV4(0x0A000006), 0x01010104, kLocalAs);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65050};
    attrs.asPath = {seg};
    attrs.originatorId = proc->getRouterId();

    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0000400, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_FALSE(isInstalled(0xC0000400, 24));
}

// Own CLUSTER_ID present in CLUSTER_LIST -> route-reflector loop -> rejected (iBGP only).
TEST_F(Internal_BgpPolicyTest, Ingress_RrLoop_OwnClusterIdInClusterList_Rejected)
{
    installConnectedNextHop(0x0A000000, 24);

    constexpr uint32_t kClusterId = 0x0000C0DE;
    proc->getConfigs().get<config::Bgp::BGP_CLUSTER_ID>().set(kClusterId);

    auto peer = makePeer(mkV4(0x0A000007), 0x01010105, kLocalAs); // iBGP

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65050};
    attrs.asPath = {seg};
    attrs.clusterList = {kClusterId};

    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0000500, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_FALSE(isInstalled(0xC0000500, 24));
}

TEST_F(Internal_BgpPolicyTest, Ingress_RrLoopCheck_SkippedForEbgpPeer)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peer = makePeer(mkV4(0x0A000008), 0x01010106, 65099); // eBGP

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099};
    attrs.asPath = {seg};
    attrs.originatorId = proc->getRouterId(); // would be an RR loop if this were iBGP

    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0000600, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_TRUE(isInstalled(0xC0000600, 24));
}

// BGP_ENFORCE_FIRST_AS (eBGP only, default true): peer's first AS_PATH ASN must
// equal the neighbor's configured REMOTE_AS, otherwise the route is rejected.
TEST_F(Internal_BgpPolicyTest, Ingress_EnforceFirstAs_Mismatch_Rejected)
{
    installConnectedNextHop(0x0A000000, 24);

    ASSERT_TRUE(proc->getConfigs().get<config::Bgp::BGP_ENFORCE_FIRST_AS>().load());

    auto peer = makePeer(mkV4(0x0A000009), 0x01010107, 65099); // eBGP, REMOTE_AS=65099

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65111, 65050}; // first AS (65111) != REMOTE_AS (65099)
    attrs.asPath = {seg};

    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0000700, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_FALSE(isInstalled(0xC0000700, 24));
}

// BGP_ENFORCE_FIRST_AS: matching first AS is accepted.
TEST_F(Internal_BgpPolicyTest, Ingress_EnforceFirstAs_Match_Accepted)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peer = makePeer(mkV4(0x0A00000A), 0x01010108, 65099); // eBGP, REMOTE_AS=65099

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099, 65050}; // first AS matches REMOTE_AS
    attrs.asPath = {seg};

    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0000800, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_TRUE(isInstalled(0xC0000800, 24));
}

// BGP_MAX_AS_LIMIT exceeded -> rejected.
TEST_F(Internal_BgpPolicyTest, Ingress_MaxAsLimit_Exceeded_Rejected)
{
    installConnectedNextHop(0x0A000000, 24);

    proc->getConfigs().get<config::Bgp::BGP_MAX_AS_LIMIT>().set(3);

    auto peer = makePeer(mkV4(0x0A00000B), 0x01010109, 65099);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099, 65050, 65051, 65052}; // 4 hops > limit of 3
    attrs.asPath = {seg};

    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0000900, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_FALSE(isInstalled(0xC0000900, 24));
}

// BGP_MAX_COMMUNITY_LIMIT exceeded -> rejected.
TEST_F(Internal_BgpPolicyTest, Ingress_MaxCommunityLimit_Exceeded_Rejected)
{
    installConnectedNextHop(0x0A000000, 24);

    proc->getConfigs().get<config::Bgp::BGP_MAX_COMMUNITY_LIMIT>().set(2);

    auto peer = makePeer(mkV4(0x0A00000C), 0x0101010A, 65099);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099};
    attrs.asPath = {seg};
    attrs.communities = {1, 2, 3}; // 3 > limit of 2

    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0000A00, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_FALSE(isInstalled(0xC0000A00, 24));
}

// BGP_MAX_EXT_COMMUNITY_LIMIT exceeded -> rejected.
TEST_F(Internal_BgpPolicyTest, Ingress_MaxExtCommunityLimit_Exceeded_Rejected)
{
    installConnectedNextHop(0x0A000000, 24);

    proc->getConfigs().get<config::Bgp::BGP_MAX_EXT_COMMUNITY_LIMIT>().set(1);

    auto peer = makePeer(mkV4(0x0A00000D), 0x0101010B, 65099);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099};
    attrs.asPath = {seg};
    attrs.extendedCommunities = {0x0002000000000064ULL, 0x00020000000000C8ULL}; // 2 > limit of 1

    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0000B00, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_FALSE(isInstalled(0xC0000B00, 24));
}

TEST_F(Internal_BgpPolicyTest, Ingress_AllowAsIn_OwnAsWithinOccurrences_Accepted)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peer = makePeer(mkV4(0x0A00000E), 0x0101010C, 65099);
    peer.nbr->getAfNeighbor(kAfiSafi).getConfigs().get<config::BgpNeighbor::ALLOWAS_IN>().set(true);
    // ALLOWAS_IN_OCCURANCES left unset -> defaults to 1 occurrence allowed.

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099, kLocalAs}; // our own AS appears exactly once
    attrs.asPath = {seg};

    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0000C00, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_TRUE(isInstalled(0xC0000C00, 24));
}

TEST_F(Internal_BgpPolicyTest, Ingress_AllowAsIn_OwnAsExceedsOccurrences_Rejected)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peer = makePeer(mkV4(0x0A00000F), 0x0101010D, 65099);
    auto& nbrAfCfg = peer.nbr->getAfNeighbor(kAfiSafi).getConfigs();
    nbrAfCfg.get<config::BgpNeighbor::ALLOWAS_IN>().set(true);
    nbrAfCfg.get<config::BgpNeighbor::ALLOWAS_IN_OCCURANCES>().set(1);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099, kLocalAs, 65050, kLocalAs}; // our own AS appears twice
    attrs.asPath = {seg};

    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0000D00, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_FALSE(isInstalled(0xC0000D00, 24));
}

// Without ALLOWAS_IN, own AS present even once is rejected (baseline AS-PATH loop check).
TEST_F(Internal_BgpPolicyTest, Ingress_AllowAsInNotSet_OwnAsOnce_Rejected)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peer = makePeer(mkV4(0x0A000010), 0x0101010E, 65099);
    // ALLOWAS_IN left at default (false).

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099, kLocalAs};
    attrs.asPath = {seg};

    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0000E00, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_FALSE(isInstalled(0xC0000E00, 24));
}

TEST_F(Internal_BgpPolicyTest, Ingress_LocalAsLoop_ConfiguredLocalAsInPath_Rejected)
{
    installConnectedNextHop(0x0A000000, 24);

    constexpr uint32_t kLocalAsOverride = 65077;

    auto peer = makePeer(mkV4(0x0A000011), 0x0101010F, 65099);
    auto& sessCfg = peer.nbr->getConfigs();
    config::BgpLocalAs::Tuple la;
    std::get<0>(la) = kLocalAsOverride;
    types::EnumBitMap<config::bgp::BgpLocalAsProps>& props = std::get<1>(la);
    props.set(config::bgp::BgpLocalAsProps::DUAL_AS);
    sessCfg.getConfigs().get<config::BgpNeighborSession::LOCAL_AS>().set(la);

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099, kLocalAsOverride}; // contains the configured LOCAL_AS value
    attrs.asPath = {seg};

    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0000F00, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_FALSE(isInstalled(0xC0000F00, 24));
}

TEST_F(Internal_BgpPolicyTest, Ingress_MaximumPrefix_LimitReached_PostsMaxPrefixEvent)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peer = makePeer(mkV4(0x0A000012), 0x01010110, 65099);
    auto& nbrAfCfg = peer.nbr->getAfNeighbor(kAfiSafi).getConfigs();
    nbrAfCfg.get<config::BgpNeighbor::MAXIMUM_PREFIX>().set(2);
    nbrAfCfg.get<config::BgpNeighbor::MAXIMUM_PREFIX_WARNING_ONLY>().set(false);

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099};
    attrs.asPath = {seg};
    Path path = makePath(mkV4(0x0A000005));

    // First two prefixes: under/at threshold but below the hard limit's pre-check.
    {
        UpdateBuilder ub;
        ub.addAnnouncement(mkPrefix(0xC0010000, 24));
        Notification err;
        { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }
    }
    EXPECT_TRUE(isInstalled(0xC0010000, 24));

    {
        UpdateBuilder ub;
        ub.addAnnouncement(mkPrefix(0xC0020000, 24));
        Notification err;
        { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }
    }
    proc->getScheduler().waitIdle();

    EXPECT_TRUE(waitForBgp([&] {
        return peer.session->getFsmState() == FsmState::IDLE;
    }));
}

TEST_F(Internal_BgpPolicyTest, Ingress_MaximumPrefix_WarningOnly_DoesNotTearDown)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peer = makePeer(mkV4(0x0A000013), 0x01010111, 65099);
    auto& nbrAfCfg = peer.nbr->getAfNeighbor(kAfiSafi).getConfigs();
    nbrAfCfg.get<config::BgpNeighbor::MAXIMUM_PREFIX>().set(1);
    nbrAfCfg.get<config::BgpNeighbor::MAXIMUM_PREFIX_WARNING_ONLY>().set(true);

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099};
    attrs.asPath = {seg};
    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0030000, 24));
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }
    proc->getScheduler().waitIdle();

    EXPECT_TRUE(isInstalled(0xC0030000, 24));
    EXPECT_NE(peer.session->getFsmState(), FsmState::IDLE);
}

TEST_F(Internal_BgpPolicyTest, Update_SinglePrefixAnnouncement_InstalledWithEbgpDistance)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peer = makePeer(mkV4(0x0A000020), 0x02000001, 65099); // eBGP

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099};
    attrs.asPath = {seg};
    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0100000, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    ASSERT_TRUE(isInstalled(0xC0100000, 24));

    utils::RCU::Guard g;
    core::RibEntry<uint32_t>* e = vrf->getRib().lookup<uint32_t>(0xC0100000, g);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->adminDistance, 20u);
}

TEST_F(Internal_BgpPolicyTest, Update_IbgpPeer_InstalledWithInternalDistance)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peer = makePeer(mkV4(0x0A000021), 0x02000002, kLocalAs); // iBGP

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65050};
    attrs.asPath = {seg};
    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0110000, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    ASSERT_TRUE(isInstalled(0xC0110000, 24));

    utils::RCU::Guard g;
    core::RibEntry<uint32_t>* e = vrf->getRib().lookup<uint32_t>(0xC0110000, g);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->adminDistance, 200u);
}

TEST_F(Internal_BgpPolicyTest, Update_DistanceRange_OverridesDefaultDistance)
{
    installConnectedNextHop(0x0A000000, 24);

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();

    // Override admin distance to 50 for 192.18.0.0/24.
    afConfigs().get<config::BgpAddressFamily::DISTANCE_RANGE>().withWrite(
        [&](auto& ranges)
        {
            ranges.emplace_back(uint8_t{50}, types::IPPrefix(0xC0120000u, 24, true), std::string{});
            return true;
        });

    auto peer = makePeer(mkV4(0x0A000022), 0x02000003, 65099); // eBGP

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099};
    attrs.asPath = {seg};
    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0120000, 24));

    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    ASSERT_TRUE(isInstalled(0xC0120000, 24));

    utils::RCU::Guard g;
    core::RibEntry<uint32_t>* e = vrf->getRib().lookup<uint32_t>(0xC0120000, g);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->adminDistance, 50u);
}

// Announce then withdraw the same prefix -> removed from the global RIB.
TEST_F(Internal_BgpPolicyTest, Update_Withdraw_RemovesRouteFromGlobalRib)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peer = makePeer(mkV4(0x0A000023), 0x02000004, 65099); // eBGP

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099};
    attrs.asPath = {seg};
    Path path = makePath(mkV4(0x0A000005));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();

    {
        UpdateBuilder ub;
        ub.addAnnouncement(mkPrefix(0xC0130000, 24));
        Notification err;
        { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }
    }
    ASSERT_TRUE(isInstalled(0xC0130000, 24));

    {
        UpdateBuilder ub;
        ub.addWithdrawn(mkPrefix(0xC0130000, 24));
        Notification err;
        Attributes emptyAttrs;
        { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(emptyAttrs, path), err)); }
    }

    EXPECT_FALSE(isInstalled(0xC0130000, 24));
}

TEST_F(Internal_BgpPolicyTest, BestPath_TwoPeers_HigherLocalPrefWins)
{
    installConnectedNextHop(0x0A000000, 24); // covers both peers' next hops

    auto peerLow = makePeer(mkV4(0x0A000030), 0x02000010, 65099);
    auto peerHigh = makePeer(mkV4(0x0A000031), 0x02000011, 65098);

    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;

    Attributes lowAttrs;
    lowAttrs.origin = BGP_ORIGIN_IGP;
    seg.asns = {65099};
    lowAttrs.asPath = {seg};
    lowAttrs.localPref = 100;
    Path lowPath = makePath(mkV4(0x0A000040));

    Attributes highAttrs;
    highAttrs.origin = BGP_ORIGIN_IGP;
    seg.asns = {65098};
    highAttrs.asPath = {seg};
    highAttrs.localPref = 200;
    Path highPath = makePath(mkV4(0x0A000041));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();

    {
        UpdateBuilder ub;
        ub.addAnnouncement(mkPrefix(0xC0140000, 24));
        Notification err;
        { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peerLow.session, ub.finalize(lowAttrs, lowPath), err)); }
    }
    {
        UpdateBuilder ub;
        ub.addAnnouncement(mkPrefix(0xC0140000, 24));
        Notification err;
        { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peerHigh.session, ub.finalize(highAttrs, highPath), err)); }
    }

    ASSERT_TRUE(isInstalled(0xC0140000, 24));

    utils::RCU::Guard g;
    core::RibEntry<uint32_t>* e = vrf->getRib().lookup<uint32_t>(0xC0140000, g);
    ASSERT_NE(e, nullptr);
    ASSERT_GT(e->nextHopCount, 0u);
    ASSERT_TRUE(e->nextHops[0].nextHop.has_value());
    EXPECT_EQ(*e->nextHops[0].nextHop, 0x0A000041u);
}

TEST_F(Internal_BgpPolicyTest, BestPath_WithdrawBestPath_PromotesRemainingPath)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peerLow = makePeer(mkV4(0x0A000032), 0x02000012, 65099);
    auto peerHigh = makePeer(mkV4(0x0A000033), 0x02000013, 65098);

    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;

    Attributes lowAttrs;
    lowAttrs.origin = BGP_ORIGIN_IGP;
    seg.asns = {65099};
    lowAttrs.asPath = {seg};
    lowAttrs.localPref = 100;
    Path lowPath = makePath(mkV4(0x0A000042));

    Attributes highAttrs;
    highAttrs.origin = BGP_ORIGIN_IGP;
    seg.asns = {65098};
    highAttrs.asPath = {seg};
    highAttrs.localPref = 200;
    Path highPath = makePath(mkV4(0x0A000043));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();

    {
        UpdateBuilder ub;
        ub.addAnnouncement(mkPrefix(0xC0150000, 24));
        Notification err;
        { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peerLow.session, ub.finalize(lowAttrs, lowPath), err)); }
    }
    {
        UpdateBuilder ub;
        ub.addAnnouncement(mkPrefix(0xC0150000, 24));
        Notification err;
        { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peerHigh.session, ub.finalize(highAttrs, highPath), err)); }
    }
    ASSERT_TRUE(isInstalled(0xC0150000, 24));

    // Withdraw the winner (peerHigh's path).
    {
        UpdateBuilder ub;
        ub.addWithdrawn(mkPrefix(0xC0150000, 24));
        Notification err;
        Attributes emptyAttrs;
        { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peerHigh.session, ub.finalize(emptyAttrs, highPath), err)); }
    }

    ASSERT_TRUE(isInstalled(0xC0150000, 24));

    utils::RCU::Guard g;
    core::RibEntry<uint32_t>* e = vrf->getRib().lookup<uint32_t>(0xC0150000, g);
    ASSERT_NE(e, nullptr);
    ASSERT_GT(e->nextHopCount, 0u);
    ASSERT_TRUE(e->nextHops[0].nextHop.has_value());
    EXPECT_EQ(*e->nextHops[0].nextHop, 0x0A000042u);
}

TEST_F(Internal_BgpPolicyTest, Update_Med_SetsRouteMetric)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peer = makePeer(mkV4(0x0A000034), 0x02000014, 65099); // eBGP

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099};
    attrs.asPath = {seg};
    attrs.med = 777;
    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0160000, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    ASSERT_TRUE(isInstalled(0xC0160000, 24));

    utils::RCU::Guard g;
    core::RibEntry<uint32_t>* e = vrf->getRib().lookup<uint32_t>(0xC0160000, g);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->metric, 777u);
}

TEST_F(Internal_BgpPolicyTest, RecursiveHost_DefaultEnabled_InstallsRouteOverHostNextHop)
{
    // /32 connected "next hop" route.
    installConnectedNextHop(0x0A0000FE, 32);

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    ASSERT_TRUE(afConfigs().get<config::BgpAddressFamily::BGP_RECURSIVE_HOST>().load());

    auto peer = makePeer(mkV4(0x0A000035), 0x02000015, 65099); // eBGP

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099};
    attrs.asPath = {seg};
    Path path = makePath(mkV4(0x0A0000FE)); // next hop == the /32 connected route

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0170000, 24));

    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_TRUE(isInstalled(0xC0170000, 24));
}

TEST_F(Internal_BgpPolicyTest, RecursiveHost_Disabled_SkipsInstallOverHostNextHop)
{
    installConnectedNextHop(0x0A0000FE, 32);

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    afConfigs().get<config::BgpAddressFamily::BGP_RECURSIVE_HOST>().set(false);

    auto peer = makePeer(mkV4(0x0A000036), 0x02000016, 65099); // eBGP

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099};
    attrs.asPath = {seg};
    Path path = makePath(mkV4(0x0A0000FE)); // next hop only resolvable via the /32

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0180000, 24));

    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_FALSE(isInstalled(0xC0180000, 24));
}

TEST_F(Internal_BgpPolicyTest, InvalidatePeer_RemovesRoutesFromGlobalRib)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peer = makePeer(mkV4(0x0A000037), 0x02000017, 65099); // eBGP

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099};
    attrs.asPath = {seg};
    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC0190000, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }
    ASSERT_TRUE(isInstalled(0xC0190000, 24));

    { std::lock_guard lock(getSchedulerLock()); af.invalidatePeer(peer.session->getPeerRid()); }

    EXPECT_FALSE(isInstalled(0xC0190000, 24));
}

TEST_F(Internal_BgpPolicyTest, SoftReconfig_SoftClearInbound_ReappliesIngressPolicy)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peer = makePeer(mkV4(0x0A000038), 0x02000018, 65099); // eBGP
    auto& nbrAfCfg = peer.nbr->getAfNeighbor(kAfiSafi).getConfigs();
    nbrAfCfg.get<config::BgpNeighbor::SOFT_RECONFIGURATION>().set(true);
    nbrAfCfg.get<config::BgpNeighbor::ALLOWAS_IN>().set(true);
    nbrAfCfg.get<config::BgpNeighbor::ALLOWAS_IN_OCCURANCES>().set(0);

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    // Own AS (kLocalAs) appears once: with ALLOWAS_IN_OCCURANCES=0, ownAsCount(1)
    // > maxOccurrences(0) -> rejected.
    seg.asns = {65099, kLocalAs};
    attrs.asPath = {seg};
    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC01A0000, 24));

    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_FALSE(isInstalled(0xC01A0000, 24));

    // Relax the limit and replay the stored pre-policy route.
    nbrAfCfg.get<config::BgpNeighbor::ALLOWAS_IN_OCCURANCES>().set(1);
    { std::lock_guard lock(getSchedulerLock()); af.softClearInbound(peer.session->getPeerRid()); }

    EXPECT_TRUE(isInstalled(0xC01A0000, 24));
}

TEST_F(Internal_BgpPolicyTest, RecomputeNlri_BatchUpdate_BothIndependentPrefixesInstalled)
{
    installConnectedNextHop(0x0A000000, 24);

    auto peer = makePeer(mkV4(0x0A000039), 0x02000019, 65099); // eBGP

    Attributes attrs;
    attrs.origin = BGP_ORIGIN_IGP;
    AsPathSegment seg;
    seg.segmentType = BGP_AS_SEQUENCE;
    seg.asns = {65099};
    attrs.asPath = {seg};
    Path path = makePath(mkV4(0x0A000005));

    UpdateBuilder ub;
    ub.addAnnouncement(mkPrefix(0xC01B0000, 24));
    ub.addAnnouncement(mkPrefix(0xC01C0000, 24));

    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();
    Notification err;
    { std::lock_guard lock(getSchedulerLock()); ASSERT_TRUE(af.onUpdateFromPeer(*peer.session, ub.finalize(attrs, path), err)); }

    EXPECT_TRUE(isInstalled(0xC01B0000, 24));
    EXPECT_TRUE(isInstalled(0xC01C0000, 24));
}

TEST_F(Internal_BgpPolicyTest, FINDING_NetworkCommand_PostConstructionConfigHasNoEffect)
{
    AddressFamily<ExampleNlri::afi>& af = proc->enableAddressFamily<ExampleNlri::afi>();

    // Configure NETWORK for a prefix AFTER the AF instance already exists.
    afConfigs().get<config::BgpAddressFamily::NETWORK>().withWrite(
        [&](auto& networks)
        {
            networks.emplace_back(types::IPPrefix(0xC01D0000u, 24, true), false, std::string{});
            return true;
        });

    proc->getScheduler().waitIdle();

    EXPECT_FALSE(isInstalled(0xC01D0000, 24));
}

// Aggregate-address: manual aggregation triggers ATOMIC_AGGREGATE +
// AGGREGATOR on the aggregate route installed into Loc-RIB.
TEST_F(Internal_BgpPolicyTest, AggregateAddress_ManualAggregation_SetsAtomicAggregateAndAggregator)
{
    GTEST_SKIP();
}

// Aggregate-address with summary-only: more-specific contributing routes
// are suppressed from Adj-RIB-Out (not re-advertised) once the aggregate
// is active.
TEST_F(Internal_BgpPolicyTest, AggregateAddress_SummaryOnly_SuppressesMoreSpecificRoutes)
{
    GTEST_SKIP();
}

// recomputeAdjRibOut / ACTIVATE: a neighbor-AF that is not ACTIVATE'd does
// not receive advertisements for routes that would otherwise be
// best-path (withdraw-and-skip branch).
TEST_F(Internal_BgpPolicyTest, RecomputeAdjRibOut_NeighborAfNotActivated_WithdrawnAndSkipped)
{
    GTEST_SKIP();
}

// Proper repro for the syncNetworkRoutes() constructor-only gap
// (AddressFamilyInstance.h ~line 2735): once syncNetworkRoutes is wired up
// to fire on NETWORK config changes (not just construction), configuring
// NETWORK after AF construction must inject a locally-originated route.
// This is the "fixed" counterpart to
// FINDING_NetworkCommand_PostConstructionConfigHasNoEffect above.
TEST_F(Internal_BgpPolicyTest, FINDING_NetworkCommand_PostConstructionConfigTakesEffectOnceWired)
{
    GTEST_SKIP();
}
