// IngressTest.cpp — RX pipeline benchmark
//
// Sends frames into loopback via the TX path and measures how fast the
// TPACKET_V3 ingress ring delivers them through processIngress().
//
// Requires loopback (lo) — loopback is always available.

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <cstdio>
#include <thread>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if_arp.h>

#include <Logger.h>
#include <RCU.hpp>
#include <Global.h>
#include "interface/Interface.h"
#include "interface/configs/InterfaceType.hpp"
#include "hardware/HardwareManager.h"
#include "hardware/Ifname.h"
#include "hardware/PacketSlot.hpp"
#include "qos/egress/TxDistributor.h"
#include "cli/runtime/Configs.h"

namespace {

static constexpr const char* kTestIface   = "lo";
static constexpr int         kDurationSec = 10;
static constexpr uint32_t    kFrameSize   = 512;

static uint64_t readIfMac(const char* ifname)
{
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return 0;
    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    uint64_t mac = 0;
    if (::ioctl(s, SIOCGIFHWADDR, &ifr) == 0)
    {
        const auto* m = reinterpret_cast<const uint8_t*>(ifr.ifr_hwaddr.sa_data);
        mac = (uint64_t(m[0]) << 40) | (uint64_t(m[1]) << 32)
            | (uint64_t(m[2]) << 24) | (uint64_t(m[3]) << 16)
            | (uint64_t(m[4]) <<  8) |  uint64_t(m[5]);
    }
    ::close(s);
    return mac;
}

static void buildTestFrame(uint8_t* buf, uint32_t len, uint64_t srcMac)
{
    std::memset(buf, 0xAB, len);
    std::memset(buf, 0xFF, 6);
    for (int i = 0; i < 6; ++i)
        buf[6 + i] = (srcMac >> (8 * (5 - i))) & 0xFF;
    buf[12] = 0x90; buf[13] = 0x00;
}

} // namespace

class IngressTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        utils::Logger::getInstance().initialize(false, false);
        utils::RCU::registerThread();
    }

    void TearDown() override
    {
        utils::RCU::unregisterThread();
    }
};

TEST_F(IngressTest, RxBenchmark)
{
    uint32_t ifidx = static_cast<uint32_t>(::if_nametoindex(kTestIface));
    if (ifidx == 0)
        GTEST_SKIP() << "Interface '" << kTestIface << "' not found";

    uint64_t mac = readIfMac(kTestIface);
    printf("[IngressTest] interface=%s  ifindex=%u  mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
           kTestIface, ifidx,
           uint8_t(mac >> 40), uint8_t(mac >> 32), uint8_t(mac >> 24),
           uint8_t(mac >> 16), uint8_t(mac >>  8), uint8_t(mac));
    fflush(stdout);

    hardware::HwIfaceInfo hwInfo{ ifidx, std::string(kTestIface), mac, 1'000'000'000ULL };

    cli::FileSystem fs;
    core::Global global(fs, {}, /*enableRouting=*/false, /*test=*/true);

    interface::Interface* iface = global.addInterface(
        interface::InterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, /*id=*/0.0f), hwInfo, /*debug=*/false);
    ASSERT_NE(iface, nullptr) << "addInterface failed";

    iface->startThreads();
    printf("[IngressTest] TX/RX threads started — running for %ds\n\n", kDurationSec);
    fflush(stdout);

    uint8_t testFrame[kFrameSize];
    buildTestFrame(testFrame, kFrameSize, mac);

    const uint64_t rxStart = iface->rxFrames.load(std::memory_order_relaxed);

    const auto startTime = std::chrono::steady_clock::now();
    const auto deadline  = startTime + std::chrono::seconds(kDurationSec);
    uint64_t txSent    = 0;
    uint64_t txNoFrame = 0;
    uint32_t flowHash  = 0;

    while (std::chrono::steady_clock::now() < deadline)
    {
        if (!iface->tx)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        hardware::FrameHandle frame{};
        if (!iface->tx->getFrame(frame, qos::egress::TxDistPolicy::ROUND_ROBIN, flowHash))
        {
            ++txNoFrame;
            ++flowHash;
            std::this_thread::yield();
            continue;
        }

        std::memcpy(frame.payload, testFrame, kFrameSize);
        frame.slot->len      = kFrameSize;
        frame.slot->flowHash = flowHash;

        iface->tx->send(frame);
        ++txSent;
        ++flowHash;

        if (txSent % 1000000 == 0)
        {
            putchar('.');
            fflush(stdout);
        }
    }

    // Let the ingress thread drain what's in flight.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    iface->stopThreads();

    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - startTime).count();

    const uint64_t rxReceived = iface->rxFrames.load(std::memory_order_relaxed) - rxStart;
    const double   rxFps      = static_cast<double>(rxReceived) / elapsed;
    const double   rxMbps     = rxFps * kFrameSize * 8.0 / 1e6;
    const double   txFps      = static_cast<double>(txSent) / elapsed;
    const double   dropRate   = txSent > 0
        ? 100.0 * static_cast<double>(txSent - std::min(rxReceived, txSent)) / static_cast<double>(txSent)
        : 0.0;

    printf("\n[IngressTest] --- Results (%.1fs) ---\n", elapsed);
    printf("  TX sent:      %12lu frames  (%.0f fps)\n", txSent,    txFps);
    printf("  RX received:  %12lu frames  (%.0f fps  %.1f Mbit/s @ %u B/frame)\n",
           rxReceived, rxFps, rxMbps, kFrameSize);
    printf("  Drop rate:    %12.1f%%\n", dropRate);
    printf("  TX no-frame:  %12lu\n", txNoFrame);
    fflush(stdout);

    EXPECT_GT(rxReceived, 0UL) << "No frames were received";
}
