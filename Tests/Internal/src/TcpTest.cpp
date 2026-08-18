// Internal_TcpTest.cpp

#include <gtest/gtest.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <VirtualRouter.h>
#include <Global.h>
#include <MockFileSystem.hpp>
#include <IPAddress.h>

#include "tcp/Tcp.h"
#include "tcp/TcpTypes.hpp"
#include "tcp/Connection.h"
#include "tcp/Listener.h"
#include "tcp/tx/TxBuffer.h"
#include "tcp/tx/TxBufferPool.h"
#include "tcp/rx/RxBuffer.h"
#include "tcp/rx/RxConsumer.h"

using namespace transport::tcp;

namespace
{
types::IPAddress mkV4(uint32_t hostOrder)
{
    types::IPAddress a;
    a.setV4(hostOrder);
    return a;
}

types::IPAddress loopback() { return mkV4(0x7F000001); }

TcpEndpoint ep(uint32_t v4HostOrder, TcpPort port)
{
    return TcpEndpoint{mkV4(v4HostOrder), port};
}

// Every test uses a unique port so reruns and parallel suites never collide.
constexpr uint16_t kPortBase = 21500;

std::span<uint8_t> byteSpan(std::vector<uint8_t>& v)
{
    return std::span<uint8_t>(v.data(), v.size());
}

std::vector<uint8_t> bytes(const char* s)
{
    return std::vector<uint8_t>(s, s + std::strlen(s));
}

// RAW-SOCKET FAR END

// Blocking loopback connect; returns fd or -1.
int rawConnect(uint16_t port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) != 0)
    {
        ::close(fd);
        return -1;
    }
    return fd;
}

// Raw loopback listener the engine's connect side can dial into.
struct RawListener
{
    int lfd{-1};

    bool open(uint16_t port)
    {
        lfd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (lfd < 0) return false;

        int one = 1;
        (void)::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        sockaddr_in sin{};
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port);
        sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (::bind(lfd, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) != 0) return false;
        if (::listen(lfd, 8) != 0) return false;
        return true;
    }

    // Accepts one connection within timeoutMs; returns fd or -1.
    int acceptOne(int timeoutMs = 2000)
    {
        pollfd pfd{lfd, POLLIN, 0};
        if (::poll(&pfd, 1, timeoutMs) <= 0) return -1;
        return ::accept4(lfd, nullptr, nullptr, SOCK_CLOEXEC);
    }

    ~RawListener()
    {
        if (lfd >= 0) ::close(lfd);
    }
};

// Reads whatever arrives on fd within timeoutMs (may poll repeatedly).
std::vector<uint8_t> rawDrain(int fd, int timeoutMs = 500)
{
    std::vector<uint8_t> out;
    uint8_t tmp[4096];

    pollfd pfd{fd, POLLIN, 0};
    while (::poll(&pfd, 1, timeoutMs) > 0 && (pfd.revents & POLLIN))
    {
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        out.insert(out.end(), tmp, tmp + n);
        timeoutMs = 50; // subsequent chunks should arrive quickly
    }
    return out;
}

// CALLBACK SINKS

struct EventSink
{
    std::vector<TcpEvent> events;

    bool saw(TcpEventType t) const
    {
        for (const auto& e : events)
            if (e.type == t) return true;
        return false;
    }

    std::optional<TcpError> firstError() const
    {
        for (const auto& e : events)
            if (e.type == TcpEventType::ERROR) return e.error;
        return std::nullopt;
    }
};

void connEventRecord(ConnCallbackCtx& ctx) noexcept
{
    static_cast<EventSink*>(ctx.user)->events.push_back(ctx.ev);
}

struct RecvSink
{
    std::vector<uint8_t> data;
    size_t calls{0};
};

void recvAppendAll(RecvCallbackCtx& ctx) noexcept
{
    auto* sink = static_cast<RecvSink*>(ctx.user);
    ++sink->calls;
    auto span = ctx.consumer.get();
    sink->data.insert(sink->data.end(), span.begin(), span.end());
    ctx.consumer.commit(span.size());
}

struct AcceptSink
{
    size_t count{0};
    std::optional<Connection> conn;
    TcpSocketKey key{};
};

void acceptCapture(AcceptCallbackCtx& ctx) noexcept
{
    auto* sink = static_cast<AcceptSink*>(ctx.user);
    ++sink->count;
    sink->key = ctx.key;
    sink->conn.emplace(std::move(ctx.newConn));
}
} // namespace

TEST(Internal_TcpUnitTest, TxPool_AcquireGivesEmptyBuffer)
{
    PoolConfig cfg{};
    TxBufferPool pool(cfg);

    TxBuffer buf = pool.acquire();
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
    EXPECT_TRUE(buf.peek(0).empty());
}

TEST(Internal_TcpUnitTest, TxPool_BlockSizeClampedTo256)
{
    PoolConfig cfg{};
    cfg.blockSize = 64;
    TxBufferPool pool(cfg);
    EXPECT_EQ(pool.blockSize(), 256u);
}

TEST(Internal_TcpUnitTest, TxBuffer_ReserveCommitPeekConsume_SingleBlock)
{
    PoolConfig cfg{};
    TxBufferPool pool(cfg);
    TxBuffer buf = pool.acquire();

    auto span = buf.reserveSpan(5);
    ASSERT_GE(span.size(), 5u);
    std::memcpy(span.data(), "hello", 5);
    buf.commit(5);

    EXPECT_EQ(buf.size(), 5u);
    EXPECT_FALSE(buf.empty());

    auto head = buf.peek(0);
    ASSERT_EQ(head.size(), 5u);
    EXPECT_EQ(std::memcmp(head.data(), "hello", 5), 0);

    EXPECT_EQ(buf.consume(5), 5u);
    EXPECT_TRUE(buf.empty());
    EXPECT_TRUE(buf.peek(0).empty());
}

TEST(Internal_TcpUnitTest, TxBuffer_CommitClampsToBlockSlack)
{
    PoolConfig cfg{};
    TxBufferPool pool(cfg);
    TxBuffer buf = pool.acquire();

    auto span = buf.reserveSpan(1);
    ASSERT_FALSE(span.empty());

    // Committing more than the tail block's slack must clamp, not corrupt.
    buf.commit(pool.blockSize() * 10);
    EXPECT_LE(buf.size(), pool.blockSize());
}

TEST(Internal_TcpUnitTest, TxBuffer_MultiBlockChainingPreservesByteOrder)
{
    PoolConfig cfg{};
    cfg.blockSize = 256;
    TxBufferPool pool(cfg);
    TxBuffer buf = pool.acquire();

    // Write 3 blocks' worth of a rolling pattern in odd-sized chunks.
    const size_t total = 256 * 3;
    std::vector<uint8_t> expect(total);
    for (size_t i = 0; i < total; ++i)
        expect[i] = static_cast<uint8_t>(i * 7 + 3);

    size_t written = 0;
    while (written < total)
    {
        size_t chunk = std::min<size_t>(97, total - written);
        auto span = buf.reserveSpan(chunk);
        ASSERT_GE(span.size(), chunk) << "at offset " << written;
        std::memcpy(span.data(), expect.data() + written, chunk);
        buf.commit(chunk);
        written += chunk;
    }
    EXPECT_EQ(buf.size(), total);

    // Drain via peek/consume; peek must never cross a block boundary.
    std::vector<uint8_t> got;
    while (!buf.empty())
    {
        auto head = buf.peek(0);
        ASSERT_FALSE(head.empty());
        ASSERT_LE(head.size(), pool.blockSize());
        got.insert(got.end(), head.begin(), head.end());
        ASSERT_EQ(buf.consume(head.size()), head.size());
    }

    EXPECT_EQ(got, expect);
}

TEST(Internal_TcpUnitTest, TxBuffer_PeekAtOffsetWithinAndAcrossBlocks)
{
    PoolConfig cfg{};
    cfg.blockSize = 256;
    TxBufferPool pool(cfg);
    TxBuffer buf = pool.acquire();

    std::vector<uint8_t> data(300);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<uint8_t>(i);

    size_t written = 0;
    while (written < data.size())
    {
        size_t chunk = std::min<size_t>(128, data.size() - written);
        auto span = buf.reserveSpan(chunk);
        ASSERT_GE(span.size(), chunk);
        std::memcpy(span.data(), data.data() + written, chunk);
        buf.commit(chunk);
        written += chunk;
    }

    // Offset inside the first block.
    auto s1 = buf.peek(10);
    ASSERT_FALSE(s1.empty());
    EXPECT_EQ(s1[0], data[10]);

    // Offset past the first block boundary.
    auto s2 = buf.peek(260);
    ASSERT_FALSE(s2.empty());
    EXPECT_EQ(s2[0], data[260]);

    // Offset beyond the data.
    EXPECT_TRUE(buf.peek(data.size()).empty());
}

TEST(Internal_TcpUnitTest, TxBuffer_ConsumeMoreThanHeldReturnsActual)
{
    PoolConfig cfg{};
    TxBufferPool pool(cfg);
    TxBuffer buf = pool.acquire();

    auto span = buf.reserveSpan(8);
    ASSERT_GE(span.size(), 8u);
    std::memcpy(span.data(), "12345678", 8);
    buf.commit(8);

    EXPECT_EQ(buf.consume(1000), 8u);
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.consume(1), 0u);
}

TEST(Internal_TcpUnitTest, TxBuffer_ReserveSpanHonorsMinBytesContract)
{
    // Contract (TxBuffer.h): "Writable span of at least minBytes, or empty
    // on allocation failure." A short non-empty span violates it.
    PoolConfig cfg{};
    cfg.blockSize = 256;
    TxBufferPool pool(cfg);
    TxBuffer buf = pool.acquire();

    auto span = buf.reserveSpan(1000);
    EXPECT_TRUE(span.empty() || span.size() >= 1000u)
        << "reserveSpan returned a short span of " << span.size()
        << " bytes for a 1000-byte request";
}

TEST(Internal_TcpUnitTest, TxBuffer_SpliceFromMovesChain)
{
    PoolConfig cfg{};
    TxBufferPool pool(cfg);

    TxBuffer a = pool.acquire();
    TxBuffer b = pool.acquire();

    auto sa = a.reserveSpan(3);
    ASSERT_GE(sa.size(), 3u);
    std::memcpy(sa.data(), "abc", 3);
    a.commit(3);

    auto sb = b.reserveSpan(3);
    ASSERT_GE(sb.size(), 3u);
    std::memcpy(sb.data(), "def", 3);
    b.commit(3);

    a.spliceFrom(&b);

    EXPECT_EQ(a.size(), 6u);
    EXPECT_TRUE(b.empty());

    std::vector<uint8_t> got;
    while (!a.empty())
    {
        auto head = a.peek(0);
        got.insert(got.end(), head.begin(), head.end());
        a.consume(head.size());
    }
    EXPECT_EQ(got, bytes("abcdef"));
}

TEST(Internal_TcpUnitTest, TxBuffer_SpliceFromDifferentPoolRefused)
{
    PoolConfig cfgA{};
    PoolConfig cfgB{};
    TxBufferPool poolA(cfgA);
    TxBufferPool poolB(cfgB);

    TxBuffer a = poolA.acquire();
    TxBuffer b = poolB.acquire();

    auto sb = b.reserveSpan(3);
    ASSERT_GE(sb.size(), 3u);
    std::memcpy(sb.data(), "xyz", 3);
    b.commit(3);

    a.spliceFrom(&b);

    // Cross-pool splice must be refused: neither side changes.
    EXPECT_TRUE(a.empty());
    EXPECT_EQ(b.size(), 3u);
}

TEST(Internal_TcpUnitTest, TxPool_SingleBlockPoolCanAllocate)
{
    PoolConfig cfg{};
    cfg.blockSize = 256;
    cfg.slabBlocks = 1;
    cfg.maxBlocks = 1;
    TxBufferPool pool(cfg);

    TxBuffer buf = pool.acquire();
    auto span = buf.reserveSpan(10);
    EXPECT_FALSE(span.empty())
        << "a fresh 1-block pool could not supply its single block";
}

TEST(Internal_TcpUnitTest, TxPool_BlocksReturnToFreeListAfterConsume)
{
    PoolConfig cfg{};
    cfg.blockSize = 256;
    cfg.slabBlocks = 2;
    cfg.maxBlocks = 2;
    TxBufferPool pool(cfg);

    for (int round = 0; round < 3; ++round)
    {
        TxBuffer buf = pool.acquire();
        auto span = buf.reserveSpan(16);
        ASSERT_FALSE(span.empty()) << "pool exhausted on round " << round
                                   << " -- consumed blocks were not recycled";
        std::memset(span.data(), 0xAB, 16);
        buf.commit(16);
        EXPECT_EQ(buf.consume(16), 16u);
        // buf destructor resets and must return all blocks to the pool.
    }
}

TEST(Internal_TcpUnitTest, TxBuffer_MoveTransfersChain)
{
    PoolConfig cfg{};
    TxBufferPool pool(cfg);

    TxBuffer a = pool.acquire();
    auto span = a.reserveSpan(4);
    ASSERT_GE(span.size(), 4u);
    std::memcpy(span.data(), "data", 4);
    a.commit(4);

    TxBuffer b = std::move(a);
    EXPECT_EQ(b.size(), 4u);
    EXPECT_TRUE(a.empty());

    TxBuffer c = pool.acquire();
    c = std::move(b);
    EXPECT_EQ(c.size(), 4u);
    EXPECT_TRUE(b.empty());

    auto head = c.peek(0);
    ASSERT_EQ(head.size(), 4u);
    EXPECT_EQ(std::memcmp(head.data(), "data", 4), 0);
}

TEST(Internal_TcpUnitTest, TxPool_ConcurrentAcquireWriteRelease)
{
    // The pool free-list is documented lock-free & thread-safe; hammer it
    // from several threads, each with a private TxBuffer.
    PoolConfig cfg{};
    cfg.blockSize = 256;
    cfg.slabBlocks = 32;
    TxBufferPool pool(cfg);

    constexpr int kThreads = 4;
    constexpr int kIters = 500;
    std::atomic<int> failures{0};

    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t)
    {
        ts.emplace_back([&pool, &failures] {
            for (int i = 0; i < kIters; ++i)
            {
                TxBuffer buf = pool.acquire();
                auto span = buf.reserveSpan(64);
                if (span.size() < 64)
                {
                    ++failures;
                    continue;
                }
                std::memset(span.data(), 0x5A, 64);
                buf.commit(64);
                if (buf.consume(64) != 64)
                    ++failures;
            }
        });
    }
    for (auto& t : ts) t.join();

    EXPECT_EQ(failures.load(), 0);
}

TEST(Internal_TcpUnitTest, RxBuffer_RawModePassthrough)
{
    RxBuffer rx(42, 64);

    std::vector<uint8_t> in = bytes("ABCD");
    {
        RxConsumer cons = rx.consume(byteSpan(in));
        auto view = cons.get();
        ASSERT_EQ(view.size(), 4u);
        EXPECT_EQ(std::memcmp(view.data(), "ABCD", 4), 0);
        EXPECT_EQ(cons.getId(), 42u);
        cons.commit(view.size());
    }

    // Fully consumed -> stays in (or returns to) the zero-copy RAW mode.
    EXPECT_EQ(rx.getMode(), RxBuffer::Mode::RAW);
}

TEST(Internal_TcpUnitTest, RxBuffer_ConstructionLeavesBufferEmpty)
{
    RxBuffer rx(1, 2048);
    EXPECT_EQ(rx.size(), 0u);
}

TEST(Internal_TcpUnitTest, RxConsumer_CommitAdvancesView)
{
    RxBuffer rx(7, 64);

    std::vector<uint8_t> in = bytes("ABCD");
    {
        RxConsumer cons = rx.consume(byteSpan(in));
        cons.commit(1);
        auto view = cons.get();
        ASSERT_EQ(view.size(), 3u);
        EXPECT_EQ(view[0], 'B');

        cons.commit(1); // additive
        view = cons.get();
        ASSERT_EQ(view.size(), 2u);
        EXPECT_EQ(view[0], 'C');

        cons.commit(view.size());
    }
}

TEST(Internal_TcpUnitTest, RxBuffer_PartialCommitRetainsTail)
{
    RxBuffer rx(7, 64);

    std::vector<uint8_t> first = bytes("ABCD");
    {
        RxConsumer cons = rx.consume(byteSpan(first));
        cons.commit(2); // leave "CD" unconsumed
    }

    EXPECT_EQ(rx.getMode(), RxBuffer::Mode::BUFFERED);
    EXPECT_EQ(rx.size(), 2u) << "unconsumed tail was not retained";

    // The next receive must present the retained tail plus the new bytes.
    std::vector<uint8_t> second = bytes("EF");
    {
        RxConsumer cons = rx.consume(byteSpan(second));
        auto view = cons.get();
        ASSERT_EQ(view.size(), 4u) << "expected \"CDEF\"";
        EXPECT_EQ(std::memcmp(view.data(), "CDEF", 4), 0);
        cons.commit(view.size());
    }

    // Fully drained: back to RAW.
    EXPECT_EQ(rx.getMode(), RxBuffer::Mode::RAW);
}

TEST(Internal_TcpUnitTest, RxBuffer_ZeroCommitKeepsEverything)
{
    RxBuffer rx(7, 64);

    std::vector<uint8_t> first = bytes("AB");
    {
        RxConsumer cons = rx.consume(byteSpan(first));
        // commit nothing
    }

    std::vector<uint8_t> second = bytes("CD");
    {
        RxConsumer cons = rx.consume(byteSpan(second));
        auto view = cons.get();
        ASSERT_EQ(view.size(), 4u) << "expected \"ABCD\" re-presented";
        EXPECT_EQ(std::memcmp(view.data(), "ABCD", 4), 0);
        cons.commit(view.size());
    }
}

TEST(Internal_TcpUnitTest, Types_EndpointEqualityAndWildcard)
{
    TcpEndpoint a = ep(0x0A000001, 179);
    TcpEndpoint b = ep(0x0A000001, 179);
    TcpEndpoint c = ep(0x0A000001, 180);

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);

    TcpEndpoint wild{types::IPAddress{}, 179};
    EXPECT_TRUE(wild.isWildcardAddress());
    EXPECT_FALSE(a.isWildcardAddress());
}

TEST(Internal_TcpUnitTest, Types_SocketKeyFlippedAndEquality)
{
    TcpSocketKey k{ep(0x0A000001, 179), ep(0x0A000002, 40000)};
    TcpSocketKey f = k.flipped();

    EXPECT_EQ(f.local, k.remote);
    EXPECT_EQ(f.remote, k.local);
    EXPECT_NE(k, f);
    EXPECT_EQ(k, f.flipped());
}

TEST(Internal_TcpUnitTest, Types_SocketKeyMatchListener)
{
    TcpSocketKey k{ep(0x0A000001, 179), ep(0x0A000002, 40000)};

    // Exact address + port match.
    EXPECT_TRUE(k.matchListener(ep(0x0A000001, 179)));
    // Wildcard listener matches on port alone.
    EXPECT_TRUE(k.matchListener(TcpEndpoint{types::IPAddress{}, 179}));
    // Port mismatch always fails.
    EXPECT_FALSE(k.matchListener(ep(0x0A000001, 180)));
    // Address mismatch fails for a non-wildcard listener.
    EXPECT_FALSE(k.matchListener(ep(0x0A000009, 179)));
}

TEST(Internal_TcpUnitTest, Types_EndpointHashDistinguishes)
{
    std::hash<TcpEndpoint> h;
    EXPECT_NE(h(ep(0x0A000001, 179)), h(ep(0x0A000001, 180)));
    EXPECT_NE(h(ep(0x0A000001, 179)), h(ep(0x0A000002, 179)));
    EXPECT_EQ(h(ep(0x0A000001, 179)), h(ep(0x0A000001, 179)));
}

TEST(Internal_TcpUnitTest, Types_AdapterV4Roundtrip)
{
    types::IPAddress ip = mkV4(0xC0A80101); // 192.168.1.1
    sockaddr_storage ss{};
    uint32_t slen = 0;

    TcpIpAdapter::writeSocketaddr(ip, 8080, &ss, &slen);
    EXPECT_EQ(slen, sizeof(sockaddr_in));
    EXPECT_EQ(reinterpret_cast<sockaddr*>(&ss)->sa_family, AF_INET);

    types::IPAddress back{};
    TcpPort port{};
    TcpIpAdapter::readSockaddr(&ss, slen, back, port);

    EXPECT_EQ(port, 8080);
    EXPECT_TRUE(back == ip) << "IPv4 address did not survive the sockaddr roundtrip";
}

TEST(Internal_TcpUnitTest, Types_AdapterV6Roundtrip)
{
    types::IPAddress ip;
    ip.setV6((__uint128_t{0x20010DB800000000ull} << 64) | 0x1ull); // 2001:db8::1

    sockaddr_storage ss{};
    uint32_t slen = 0;

    TcpIpAdapter::writeSocketaddr(ip, 179, &ss, &slen);
    EXPECT_EQ(slen, sizeof(sockaddr_in6));
    EXPECT_EQ(reinterpret_cast<sockaddr*>(&ss)->sa_family, AF_INET6);

    types::IPAddress back{};
    TcpPort port{};
    TcpIpAdapter::readSockaddr(&ss, slen, back, port);

    EXPECT_EQ(port, 179);
    EXPECT_TRUE(back == ip) << "IPv6 address did not survive the sockaddr roundtrip";
}

TEST(Internal_TcpUnitTest, Types_AdapterAfDetection)
{
    EXPECT_EQ(TcpIpAdapter::af(mkV4(0x7F000001)), AF_INET);

    types::IPAddress v6;
    v6.setV6(__uint128_t{1});
    EXPECT_EQ(TcpIpAdapter::af(v6), AF_INET6);
}

TEST(Internal_TcpUnitTest, Types_TcpErrorOk)
{
    TcpError e{};
    EXPECT_TRUE(e.ok());

    TcpError bad{TcpErrc::CONNECTION_REFUSED, ECONNREFUSED};
    EXPECT_FALSE(bad.ok());
}

class Internal_TcpTest : public ::testing::Test
{
protected:
    cli::MockFileSystem fs;
    core::Global* global = nullptr;
    core::VirtualRouter* vrf = nullptr;

    void SetUp() override
    {
        utils::RCU::registerThread();
        core::GlobalProperties props(fs);
        props.enableDummies = true;
        props.enableRouting = false;
        props.threadPoolCapacity = (1 << 8);
        global = new core::Global(props);
        vrf = global->getRoutingInstance(DEFAULT_VRF, types::AddressFamily::IPv4);
    }

    void TearDown() override
    {
        delete global;
        utils::RCU::unregisterThread();
    }

    // Pumps until pred() is true or the budget is spent.
    template <typename Pred>
    static bool pumpUntil(Tcp& tcp, Pred pred, int maxIters = 500)
    {
        for (int i = 0; i < maxIters; ++i)
        {
            tcp.pump(1);
            if (pred()) return true;
        }
        return pred();
    }

    // Drains pull-mode events until pred(event) matches one, collecting all.
    template <typename Pred>
    static bool pollUntil(Tcp& tcp, std::vector<TcpEvent>& all, Pred pred, int maxIters = 500)
    {
        std::array<TcpEvent, 16> scratch{};
        for (int i = 0; i < maxIters; ++i)
        {
            size_t n = tcp.pollEvents(std::span<TcpEvent>(scratch), 1);
            for (size_t k = 0; k < n; ++k)
                all.push_back(scratch[k]);
            for (const auto& e : all)
                if (pred(e)) return true;
        }
        return false;
    }
};

TEST_F(Internal_TcpTest, Engine_ConnectCompletes_CallbackModel)
{
    const uint16_t port = kPortBase + 0;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Tcp tcp(*vrf);

    EventSink sink;
    ConnectOptions opt;
    opt.callback = &connEventRecord;
    opt.callbackUser = &sink;

    Connection conn = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), opt);
    ASSERT_TRUE(conn.ok());
    EXPECT_NE(conn.getId(), 0u);

    int peer = raw.acceptOne();
    ASSERT_GE(peer, 0);

    EXPECT_TRUE(pumpUntil(tcp, [&] { return sink.saw(TcpEventType::CONNECTED); }))
        << "no CONNECTED event was delivered for a completed loopback connect";

    ::close(peer);
}

TEST_F(Internal_TcpTest, Engine_ConnectCompletes_PullModel)
{
    const uint16_t port = kPortBase + 1;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Tcp tcp(*vrf);
    Connection conn = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});
    ASSERT_TRUE(conn.ok());

    int peer = raw.acceptOne();
    ASSERT_GE(peer, 0);

    std::vector<TcpEvent> events;
    EXPECT_TRUE(pollUntil(tcp, events, [&](const TcpEvent& e) {
        return e.type == TcpEventType::CONNECTED && e.id == conn.getId();
    })) << "pollEvents never reported CONNECTED";

    ::close(peer);
}

TEST_F(Internal_TcpTest, Engine_ListenerAcceptsAndFiresCallbacks)
{
    const uint16_t port = kPortBase + 2;

    Tcp tcp(*vrf);

    AcceptSink acceptSink;
    EventSink connSink;
    ListenOptions opt;
    opt.onAccept = &acceptCapture;
    opt.onAcceptUser = &acceptSink;
    opt.acceptConnCallback = &connEventRecord;
    opt.acceptedConnUser = &connSink;

    Listener lst = tcp.listen(ep(0x7F000001, port), opt);
    ASSERT_TRUE(lst.ok());
    EXPECT_NE(lst.getId(), 0u);

    int peer = rawConnect(port);
    ASSERT_GE(peer, 0) << "raw connect to the engine listener failed";

    EXPECT_TRUE(pumpUntil(tcp, [&] { return acceptSink.count > 0; }))
        << "onAccept was never invoked";
    EXPECT_EQ(acceptSink.count, 1u);
    ASSERT_TRUE(acceptSink.conn.has_value());
    EXPECT_TRUE(acceptSink.conn->ok());

    // The per-connection callback should have seen ACCEPTED as well.
    EXPECT_TRUE(connSink.saw(TcpEventType::ACCEPTED));

    // Accepted-side 4-tuple: local is the listener port, remote is the peer.
    EXPECT_EQ(acceptSink.key.local.port, port);
    EXPECT_TRUE(acceptSink.key.local.address == mkV4(0x7F000001));
    EXPECT_NE(acceptSink.key.remote.port, 0);

    ::close(peer);
}

TEST_F(Internal_TcpTest, Engine_ListenerPullModeProducesAcceptedEvents)
{
    const uint16_t port = kPortBase + 3;

    Tcp tcp(*vrf);
    Listener lst = tcp.listen(ep(0x7F000001, port), {});
    ASSERT_TRUE(lst.ok());

    int peer = rawConnect(port);
    ASSERT_GE(peer, 0);

    std::vector<TcpEvent> events;
    EXPECT_TRUE(pollUntil(tcp, events, [](const TcpEvent& e) {
        return e.type == TcpEventType::ACCEPTED && e.id != 0;
    })) << "pull-mode pollEvents never produced an ACCEPTED event";

    ::close(peer);
}

TEST_F(Internal_TcpTest, Engine_WildcardBindAcceptsLoopback)
{
    const uint16_t port = kPortBase + 4;

    Tcp tcp(*vrf);

    AcceptSink acceptSink;
    ListenOptions opt;
    opt.onAccept = &acceptCapture;
    opt.onAcceptUser = &acceptSink;

    // 0.0.0.0:port
    Listener lst = tcp.listen(ep(0x00000000, port), opt);
    ASSERT_TRUE(lst.ok());

    int peer = rawConnect(port);
    ASSERT_GE(peer, 0) << "wildcard-bound listener did not accept on loopback";

    EXPECT_TRUE(pumpUntil(tcp, [&] { return acceptSink.count > 0; }));

    ::close(peer);
}

TEST_F(Internal_TcpTest, Engine_SocketKeysConsistentAcrossSides)
{
    const uint16_t port = kPortBase + 5;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Tcp tcp(*vrf);
    Connection conn = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});
    ASSERT_TRUE(conn.ok());

    int peer = raw.acceptOne();
    ASSERT_GE(peer, 0);
    tcp.pump(1);

    auto key = conn.socketKey();
    ASSERT_TRUE(key.has_value());

    EXPECT_TRUE(key->local.address == mkV4(0x7F000001));
    EXPECT_TRUE(key->remote.address == mkV4(0x7F000001));
    EXPECT_EQ(key->remote.port, port);
    EXPECT_NE(key->local.port, 0);

    // Cross-check the local port against what the raw peer sees.
    sockaddr_in sin{};
    socklen_t slen = sizeof(sin);
    ASSERT_EQ(::getpeername(peer, reinterpret_cast<sockaddr*>(&sin), &slen), 0);
    EXPECT_EQ(ntohs(sin.sin_port), key->local.port);

    ::close(peer);
}

TEST_F(Internal_TcpTest, Engine_EphemeralPortsWithinConfiguredRange)
{
    const uint16_t port = kPortBase + 6;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Config cfg{};
    cfg.ephemeralMin = 52000;
    cfg.ephemeralMax = 52015;
    Tcp tcp(*vrf, cfg);

    Connection c1 = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});
    Connection c2 = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});
    ASSERT_TRUE(c1.ok());
    ASSERT_TRUE(c2.ok());

    auto k1 = c1.socketKey();
    auto k2 = c2.socketKey();
    ASSERT_TRUE(k1.has_value());
    ASSERT_TRUE(k2.has_value());

    EXPECT_GE(k1->local.port, cfg.ephemeralMin);
    EXPECT_LE(k1->local.port, cfg.ephemeralMax);
    EXPECT_GE(k2->local.port, cfg.ephemeralMin);
    EXPECT_LE(k2->local.port, cfg.ephemeralMax);
    EXPECT_NE(k1->local.port, k2->local.port);
}

TEST_F(Internal_TcpTest, Engine_ConnectionIdsMonotonic)
{
    const uint16_t port = kPortBase + 7;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Tcp tcp(*vrf);
    Connection c1 = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});
    Connection c2 = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});

    EXPECT_LT(c1.getId(), c2.getId());
}

TEST_F(Internal_TcpTest, Engine_MaxConnectionsEnforced)
{
    const uint16_t port = kPortBase + 8;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Config cfg{};
    cfg.maxConnections = 1;
    Tcp tcp(*vrf, cfg);

    Connection c1 = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});
    ASSERT_TRUE(c1.ok());

    EXPECT_THROW(
        (void)tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {}),
        std::runtime_error);
}

TEST_F(Internal_TcpTest, Engine_CloseInvalidatesSocketKey)
{
    const uint16_t port = kPortBase + 9;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Tcp tcp(*vrf);
    Connection conn = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});
    ASSERT_TRUE(conn.ok());
    const ConnId id = conn.getId();

    EXPECT_TRUE(conn.socketKey().has_value());

    tcp.close(id);
    EXPECT_FALSE(conn.socketKey().has_value());

    // Double close must be a harmless no-op.
    tcp.close(id);
}

TEST_F(Internal_TcpTest, Engine_ConnectionDisconnectInvalidatesHandle)
{
    const uint16_t port = kPortBase + 10;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Tcp tcp(*vrf);
    Connection conn = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});
    ASSERT_TRUE(conn.ok());

    conn.disconnect();
    EXPECT_FALSE(conn.ok());
    EXPECT_FALSE(conn.socketKey().has_value());
}

TEST_F(Internal_TcpTest, Engine_MovedConnectionHandleStaysLive)
{
    const uint16_t port = kPortBase + 11;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Tcp tcp(*vrf);
    Connection a = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});
    ASSERT_TRUE(a.ok());
    const ConnId id = a.getId();

    Connection b = std::move(a);
    EXPECT_FALSE(a.ok());
    EXPECT_TRUE(b.ok());
    EXPECT_EQ(b.getId(), id);
    EXPECT_TRUE(b.socketKey().has_value()) << "move closed the connection";
}

TEST_F(Internal_TcpTest, Engine_MoveAssignClosesTargetKeepsSource)
{
    const uint16_t port = kPortBase + 12;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Tcp tcp(*vrf);
    Connection a = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});
    Connection b = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});
    const ConnId aId = a.getId();
    const ConnId bId = b.getId();

    b = std::move(a);

    EXPECT_FALSE(a.ok());
    EXPECT_TRUE(b.ok());
    EXPECT_EQ(b.getId(), aId);

    // b's original connection must have been closed by the assignment.
    EXPECT_FALSE(tcp.pollEvents({}, 0)); // no-op sanity pump
    Connection probe = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});
    EXPECT_GT(probe.getId(), bId);
}

TEST_F(Internal_TcpTest, Engine_ListenerDestructorClosesSocket)
{
    const uint16_t port = kPortBase + 13;

    Tcp tcp(*vrf);
    {
        Listener lst = tcp.listen(ep(0x7F000001, port), {});
        ASSERT_TRUE(lst.ok());

        int peer = rawConnect(port);
        ASSERT_GE(peer, 0);
        ::close(peer);
    } // Listener destroyed -> socket must be closed.

    int peer = rawConnect(port);
    EXPECT_LT(peer, 0) << "listener socket still accepting after handle destruction";
    if (peer >= 0) ::close(peer);
}

TEST_F(Internal_TcpTest, Engine_ListenerShutdownClosesSocket)
{
    // FINDING TARGET: Listener::shutdown() early-returns when `id != 0`,
    // i.e. for every *valid* listener, so shutdown() never does anything.
    const uint16_t port = kPortBase + 14;

    Tcp tcp(*vrf);
    Listener lst = tcp.listen(ep(0x7F000001, port), {});
    ASSERT_TRUE(lst.ok());

    lst.shutdown();

    int peer = rawConnect(port);
    EXPECT_LT(peer, 0) << "listener still accepting after shutdown()";
    if (peer >= 0) ::close(peer);
}

TEST_F(Internal_TcpTest, Engine_ListenerDisconnectClosesAcceptedConn)
{
    const uint16_t port = kPortBase + 15;

    Tcp tcp(*vrf);

    AcceptSink acceptSink;
    ListenOptions opt;
    opt.onAccept = &acceptCapture;
    opt.onAcceptUser = &acceptSink;

    Listener lst = tcp.listen(ep(0x7F000001, port), opt);
    ASSERT_TRUE(lst.ok());

    int peer = rawConnect(port);
    ASSERT_GE(peer, 0);
    ASSERT_TRUE(pumpUntil(tcp, [&] { return acceptSink.count > 0; }));
    ASSERT_TRUE(acceptSink.conn.has_value());

    const ConnId cid = acceptSink.conn->getId();
    lst.disconnect(cid);

    // The raw peer should observe the close (EOF on read).
    pollfd pfd{peer, POLLIN, 0};
    ASSERT_GT(::poll(&pfd, 1, 2000), 0) << "peer never saw the close";
    uint8_t tmp[16];
    EXPECT_EQ(::recv(peer, tmp, sizeof(tmp), 0), 0) << "expected EOF after listenerDisconnect";

    ::close(peer);
}

TEST_F(Internal_TcpTest, Engine_TakenAcceptedHandleClosesOnDestruction)
{
    // Ownership: a Connection handle moved out of AcceptCallbackCtx owns the socket.
    const uint16_t port = kPortBase + 16;

    Tcp tcp(*vrf);

    AcceptSink acceptSink;
    ListenOptions opt;
    opt.onAccept = &acceptCapture;
    opt.onAcceptUser = &acceptSink;

    Listener lst = tcp.listen(ep(0x7F000001, port), opt);
    int peer = rawConnect(port);
    ASSERT_GE(peer, 0);
    ASSERT_TRUE(pumpUntil(tcp, [&] { return acceptSink.count > 0; }));
    ASSERT_TRUE(acceptSink.conn.has_value());

    acceptSink.conn.reset(); // owning handle destroyed -> connection must close

    pollfd pfd{peer, POLLIN, 0};
    ASSERT_GT(::poll(&pfd, 1, 2000), 0) << "peer never saw the close";
    uint8_t tmp[8];
    EXPECT_EQ(::recv(peer, tmp, sizeof(tmp), 0), 0) << "expected EOF after handle destruction";

    ::close(peer);
}

TEST_F(Internal_TcpTest, Engine_UntakenAcceptedConnStaysListenerOwned)
{
    // Without an onAccept capture the listener keeps ownership and data still flows.
    const uint16_t port = kPortBase + 24;

    Tcp tcp(*vrf);

    RecvSink sink;
    ListenOptions opt;
    opt.recvCallback = &recvAppendAll;
    opt.recvUser = &sink;

    Listener lst = tcp.listen(ep(0x7F000001, port), opt);
    int peer = rawConnect(port);
    ASSERT_GE(peer, 0);

    ASSERT_EQ(::send(peer, "ping", 4, MSG_NOSIGNAL), 4);
    EXPECT_TRUE(pumpUntil(tcp, [&] { return sink.data.size() >= 4; }))
        << "listener-owned connection did not deliver data";
    EXPECT_EQ(sink.data, bytes("ping"));

    ::close(peer);
}

TEST_F(Internal_TcpTest, Data_RecvCallbackDeliversBytes)
{
    const uint16_t port = kPortBase + 17;

    Tcp tcp(*vrf);

    RecvSink recvSink;
    EventSink evSink;
    ListenOptions opt;
    opt.recvCallback = &recvAppendAll;
    opt.recvUser = &recvSink;
    opt.acceptConnCallback = &connEventRecord;
    opt.acceptedConnUser = &evSink;

    Listener lst = tcp.listen(ep(0x7F000001, port), opt);
    int peer = rawConnect(port);
    ASSERT_GE(peer, 0);

    const auto payload = bytes("hello tcp engine");
    ASSERT_EQ(::send(peer, payload.data(), payload.size(), MSG_NOSIGNAL),
              static_cast<ssize_t>(payload.size()));

    pumpUntil(tcp, [&] { return recvSink.data.size() >= payload.size(); });

    EXPECT_EQ(recvSink.data, payload) << "inbound bytes were not delivered to the recv callback";
    EXPECT_FALSE(evSink.saw(TcpEventType::PEER_CLOSED))
        << "spurious PEER_CLOSED while the peer was still open";

    ::close(peer);
}

TEST_F(Internal_TcpTest, Data_RecvCallbackPartialCommitCarriesOver)
{
    // Commit only part of each delivery; the tail must be re-presented
    // together with the next segment (RxBuffer BUFFERED accumulation).
    const uint16_t port = kPortBase + 18;

    Tcp tcp(*vrf);

    struct PartialSink
    {
        std::vector<uint8_t> committed;
    } sink;

    ListenOptions opt;
    opt.recvCallback = +[](RecvCallbackCtx& ctx) noexcept {
        auto* s = static_cast<PartialSink*>(ctx.user);
        auto span = ctx.consumer.get();
        // Consume in fixed 4-byte records; leave any remainder unconsumed.
        size_t whole = (span.size() / 4) * 4;
        s->committed.insert(s->committed.end(), span.begin(), span.begin() + whole);
        ctx.consumer.commit(whole);
    };
    opt.recvUser = &sink;

    Listener lst = tcp.listen(ep(0x7F000001, port), opt);
    int peer = rawConnect(port);
    ASSERT_GE(peer, 0);

    // 6 bytes now (one whole record + 2 leftover)...
    ASSERT_EQ(::send(peer, "AAAABB", 6, MSG_NOSIGNAL), 6);
    pumpUntil(tcp, [&] { return sink.committed.size() >= 4; }, 200);

    // ...then 2 more to complete the second record.
    ASSERT_EQ(::send(peer, "BB", 2, MSG_NOSIGNAL), 2);
    pumpUntil(tcp, [&] { return sink.committed.size() >= 8; }, 200);

    EXPECT_EQ(sink.committed, bytes("AAAABBBB"))
        << "partial-record tail was lost between recv events";

    ::close(peer);
}

TEST_F(Internal_TcpTest, Data_PeerShutdownDeliversPeerClosed)
{
    const uint16_t port = kPortBase + 19;

    Tcp tcp(*vrf);

    EventSink evSink;
    ListenOptions opt;
    opt.acceptConnCallback = &connEventRecord;
    opt.acceptedConnUser = &evSink;
    // Deliberately no recvCallback: PEER_CLOSED should come from EPOLLRDHUP.

    Listener lst = tcp.listen(ep(0x7F000001, port), opt);
    int peer = rawConnect(port);
    ASSERT_GE(peer, 0);

    pumpUntil(tcp, [&] { return evSink.saw(TcpEventType::ACCEPTED); }, 200);

    ::shutdown(peer, SHUT_WR);

    EXPECT_TRUE(pumpUntil(tcp, [&] { return evSink.saw(TcpEventType::PEER_CLOSED); }))
        << "no PEER_CLOSED after the peer shut down its write side";

    ::close(peer);
}

TEST_F(Internal_TcpTest, Data_ConnectionRefusedReportsError)
{
    // Nothing listens on this port.
    const uint16_t port = kPortBase + 20;

    Tcp tcp(*vrf);

    EventSink sink;
    ConnectOptions opt;
    opt.callback = &connEventRecord;
    opt.callbackUser = &sink;

    std::optional<Connection> conn;
    try
    {
        conn.emplace(tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), opt));
    }
    catch (const std::runtime_error&)
    {
        // Acceptable: the kernel refused synchronously.
        SUCCEED();
        return;
    }

    ASSERT_TRUE(pumpUntil(tcp, [&] { return sink.saw(TcpEventType::ERROR); }))
        << "no ERROR event for a refused connect";

    auto err = sink.firstError();
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->code, TcpErrc::CONNECTION_REFUSED)
        << "errno was " << err->osErrno;
}

TEST_F(Internal_TcpTest, Data_ReserveWithoutCommitSendsNothing)
{
    // Documents the two-phase TX contract: reserveSpan() alone must not
    // enqueue bytes; only committed data may reach the wire.
    const uint16_t port = kPortBase + 21;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Tcp tcp(*vrf);
    Connection conn = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});
    int peer = raw.acceptOne();
    ASSERT_GE(peer, 0);
    tcp.pump(1);

    auto span = conn.reserveSpan(16);
    if (!span.empty())
        std::memset(span.data(), 0x42, 16);

    EXPECT_EQ(conn.pendingTxBytes(), 0u);
    EXPECT_EQ(conn.flush(), 0u);

    auto got = rawDrain(peer, 100);
    EXPECT_TRUE(got.empty()) << "uncommitted bytes leaked onto the wire";

    ::close(peer);
}

TEST_F(Internal_TcpTest, Data_FlushOnFreshConnectionIsSafe)
{
    const uint16_t port = kPortBase + 22;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Tcp tcp(*vrf);
    Connection conn = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});
    EXPECT_EQ(conn.flush(), 0u);
    EXPECT_EQ(conn.pendingTxBytes(), 0u);
}

TEST_F(Internal_TcpTest, Data_CommitFlushDeliversToPeer)
{
    const uint16_t port = kPortBase + 25;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Tcp tcp(*vrf);

    EventSink sink;
    ConnectOptions opt;
    opt.callback = &connEventRecord;
    opt.callbackUser = &sink;

    Connection conn = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), opt);
    int peer = raw.acceptOne();
    ASSERT_GE(peer, 0);
    ASSERT_TRUE(pumpUntil(tcp, [&] { return sink.saw(TcpEventType::CONNECTED); }));

    const auto payload = bytes("committed bytes reach the wire");
    auto span = conn.reserveSpan(payload.size());
    ASSERT_GE(span.size(), payload.size());
    std::memcpy(span.data(), payload.data(), payload.size());
    conn.commit(payload.size());

    EXPECT_EQ(conn.pendingTxBytes(), payload.size());
    EXPECT_EQ(conn.flush(), payload.size());
    EXPECT_EQ(conn.pendingTxBytes(), 0u);

    EXPECT_EQ(rawDrain(peer), payload);

    ::close(peer);
}

TEST_F(Internal_TcpTest, Data_WriteHelperSpansMultipleBlocks)
{
    const uint16_t port = kPortBase + 26;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Tcp tcp(*vrf);
    Connection conn = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});
    int peer = raw.acceptOne();
    ASSERT_GE(peer, 0);
    tcp.pump(1);

    std::vector<uint8_t> payload(10240);
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<uint8_t>(i * 13 + 1);

    ASSERT_EQ(conn.write(std::span<const uint8_t>(payload)), payload.size());
    EXPECT_EQ(conn.pendingTxBytes(), payload.size());

    std::vector<uint8_t> got;
    for (int i = 0; i < 100 && got.size() < payload.size(); ++i)
    {
        conn.flush();
        tcp.pump(1);
        auto chunk = rawDrain(peer, 50);
        got.insert(got.end(), chunk.begin(), chunk.end());
    }

    EXPECT_EQ(got, payload);

    ::close(peer);
}

TEST_F(Internal_TcpTest, Data_PullReadDrainsReadableConnection)
{
    const uint16_t port = kPortBase + 27;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Tcp tcp(*vrf);
    Connection conn = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), {});
    int peer = raw.acceptOne();
    ASSERT_GE(peer, 0);

    const auto payload = bytes("pull model data");
    ASSERT_EQ(::send(peer, payload.data(), payload.size(), MSG_NOSIGNAL),
              static_cast<ssize_t>(payload.size()));

    std::vector<TcpEvent> events;
    ASSERT_TRUE(pollUntil(tcp, events, [&](const TcpEvent& e) {
        return e.type == TcpEventType::READABLE && e.id == conn.getId();
    })) << "no READABLE event for pending inbound data";

    std::vector<uint8_t> got;
    uint8_t buf[8]; // deliberately small: forces multiple read() calls
    while (size_t n = conn.read(std::span<uint8_t>(buf, sizeof(buf))))
        got.insert(got.end(), buf, buf + n);

    EXPECT_EQ(got, payload);

    ::close(peer);
}

TEST_F(Internal_TcpTest, Data_ReadByIdFromAcceptedEvent)
{
    const uint16_t port = kPortBase + 28;

    Tcp tcp(*vrf);
    Listener lst = tcp.listen(ep(0x7F000001, port), {}); // pull model: no callbacks

    int peer = rawConnect(port);
    ASSERT_GE(peer, 0);

    std::vector<TcpEvent> events;
    ASSERT_TRUE(pollUntil(tcp, events, [](const TcpEvent& e) {
        return e.type == TcpEventType::ACCEPTED;
    }));
    ConnId cid = 0;
    for (const auto& e : events)
        if (e.type == TcpEventType::ACCEPTED) cid = e.id;
    ASSERT_NE(cid, 0u);

    const auto payload = bytes("id-addressed read");
    ASSERT_EQ(::send(peer, payload.data(), payload.size(), MSG_NOSIGNAL),
              static_cast<ssize_t>(payload.size()));

    events.clear();
    ASSERT_TRUE(pollUntil(tcp, events, [&](const TcpEvent& e) {
        return e.type == TcpEventType::READABLE && e.id == cid;
    }));

    std::vector<uint8_t> got;
    uint8_t buf[64];
    while (size_t n = tcp.read(cid, std::span<uint8_t>(buf, sizeof(buf))))
        got.insert(got.end(), buf, buf + n);

    EXPECT_EQ(got, payload);

    ::close(peer);
}

TEST_F(Internal_TcpTest, Conn_ShutdownWriteHalfClose)
{
    const uint16_t port = kPortBase + 29;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Tcp tcp(*vrf);

    RecvSink sink;
    ConnectOptions opt;
    opt.recvCallback = &recvAppendAll;
    opt.recvUser = &sink;

    Connection conn = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), opt);
    int peer = raw.acceptOne();
    ASSERT_GE(peer, 0);
    tcp.pump(1);

    conn.shutdown(TcpShutdown::WRITE);

    // The peer must see EOF...
    pollfd pfd{peer, POLLIN, 0};
    ASSERT_GT(::poll(&pfd, 1, 2000), 0) << "peer never saw the FIN";
    uint8_t tmp[8];
    EXPECT_EQ(::recv(peer, tmp, sizeof(tmp), 0), 0);

    // ...while peer->engine data still flows on the open read half.
    ASSERT_EQ(::send(peer, "still open", 10, MSG_NOSIGNAL), 10);
    EXPECT_TRUE(pumpUntil(tcp, [&] { return sink.data.size() >= 10; }))
        << "read half died with the write half";
    EXPECT_EQ(sink.data, bytes("still open"));

    ::close(peer);
}

TEST_F(Internal_TcpTest, Conn_StateReportsEstablishedThenClosed)
{
    const uint16_t port = kPortBase + 30;
    RawListener raw;
    ASSERT_TRUE(raw.open(port));

    Tcp tcp(*vrf);

    EventSink sink;
    ConnectOptions opt;
    opt.callback = &connEventRecord;
    opt.callbackUser = &sink;

    Connection conn = tcp.connect(ep(0x7F000001, 0), ep(0x7F000001, port), opt);
    int peer = raw.acceptOne();
    ASSERT_GE(peer, 0);
    ASSERT_TRUE(pumpUntil(tcp, [&] { return sink.saw(TcpEventType::CONNECTED); }));

    EXPECT_EQ(conn.state(), TcpState::ESTABLISHED);

    tcp.close(conn.getId());
    EXPECT_EQ(conn.state(), TcpState::CLOSED);

    ::close(peer);
}

TEST_F(Internal_TcpTest, Engine_CloseAcceptedConnectionById)
{
    const uint16_t port = kPortBase + 31;

    Tcp tcp(*vrf);

    AcceptSink acceptSink;
    ListenOptions opt;
    opt.onAccept = &acceptCapture;
    opt.onAcceptUser = &acceptSink;

    Listener lst = tcp.listen(ep(0x7F000001, port), opt);
    int peer = rawConnect(port);
    ASSERT_GE(peer, 0);
    ASSERT_TRUE(pumpUntil(tcp, [&] { return acceptSink.count > 0; }));
    ASSERT_TRUE(acceptSink.conn.has_value());

    tcp.close(acceptSink.conn->getId()); // Tcp::close now works for accepted connections
    EXPECT_FALSE(acceptSink.conn->socketKey().has_value());

    pollfd pfd{peer, POLLIN, 0};
    ASSERT_GT(::poll(&pfd, 1, 2000), 0);
    uint8_t tmp[8];
    EXPECT_EQ(::recv(peer, tmp, sizeof(tmp), 0), 0) << "expected EOF after close-by-id";

    ::close(peer);
}

namespace
{
struct ReentrantSink
{
    std::vector<uint8_t> data;
    bool closed{false};
};

void recvCloseSelf(RecvCallbackCtx& ctx) noexcept
{
    auto* s = static_cast<ReentrantSink*>(ctx.user);
    auto span = ctx.consumer.get();
    s->data.insert(s->data.end(), span.begin(), span.end());
    ctx.consumer.commit(span.size());
    ctx.tcp.close(ctx.id); // close our own connection from inside the callback
    s->closed = true;
}
} // namespace

TEST_F(Internal_TcpTest, Reentrancy_CloseFromRecvCallbackIsSafe)
{
    const uint16_t port = kPortBase + 32;

    Tcp tcp(*vrf);

    ReentrantSink sink;
    ListenOptions opt;
    opt.recvCallback = &recvCloseSelf;
    opt.recvUser = &sink;

    Listener lst = tcp.listen(ep(0x7F000001, port), opt);
    int peer = rawConnect(port);
    ASSERT_GE(peer, 0);

    ASSERT_EQ(::send(peer, "bye", 3, MSG_NOSIGNAL), 3);
    ASSERT_TRUE(pumpUntil(tcp, [&] { return sink.closed; }));
    EXPECT_EQ(sink.data, bytes("bye"));

    // The close from inside the callback must have taken effect...
    pollfd pfd{peer, POLLIN, 0};
    ASSERT_GT(::poll(&pfd, 1, 2000), 0);
    uint8_t tmp[8];
    EXPECT_EQ(::recv(peer, tmp, sizeof(tmp), 0), 0) << "expected EOF after in-callback close";

    // ...and the engine must still be healthy.
    tcp.pump(1);

    ::close(peer);
}

TEST_F(Internal_TcpTest, Engine_ListenBindFailureThrows)
{
    Tcp tcp(*vrf);
    // 8.8.8.8 is not a local address; the bind must fail loudly, not silently listen elsewhere.
    EXPECT_THROW((void)tcp.listen(ep(0x08080808, kPortBase + 33), {}), std::runtime_error);
}

// ~TcpEngine() must close every fd it still tracks, even when caller handles were leaked
TEST_F(Internal_TcpTest, Engine_DestructorClosesLeakedHandles)
{
    const uint16_t port = kPortBase + 23;

    auto* tcp = new Tcp(*vrf);
    // placement-new: the handle is deliberately never destroyed (its dtor would touch the deleted engine)
    alignas(Listener) static unsigned char lstBuf[sizeof(Listener)];
    auto* lst = new (lstBuf) Listener(tcp->listen(ep(0x7F000001, port), {}));
    int peer = rawConnect(port);
    ASSERT_GE(peer, 0);
    tcp->pump(1);

    // Destroy the engine with the listener (and its accepted connection)
    // still registered; the destructor must close every fd exactly once.
    delete tcp;

    // The peer must observe the close.
    pollfd pfd{peer, POLLIN, 0};
    EXPECT_GT(::poll(&pfd, 1, 2000), 0);
    uint8_t tmp[8];
    EXPECT_LE(::recv(peer, tmp, sizeof(tmp), 0), 0);

    ::close(peer);
    // `lst` is intentionally leaked: its destructor would dereference the
    // destroyed engine. The test's subject is ~TcpEngine's own cleanup.
    (void)lst;
}
