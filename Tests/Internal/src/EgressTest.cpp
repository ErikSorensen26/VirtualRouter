// EgressTest.cpp — TX pipeline benchmark (requires a real or dummy network interface)
//
// Default interface: "dummy0"
//   Create with: ip link add dummy0 type dummy && ip link set dummy0 up
// To test loopback instead, change kTestIface to "lo".

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
#include "cli/session/Configs.h"

namespace {

// Change kTestIface to run on a different interface.
// "dummy0" measures pure software TX throughput (NIC drops frames instantly).
// "lo"     measures TX + loopback RX processing (slower).
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
    std::memset(buf, 0xFF, 6);                                  // dst = broadcast
    for (int i = 0; i < 6; ++i)
        buf[6 + i] = (srcMac >> (8 * (5 - i))) & 0xFF;         // src = our MAC
    buf[12] = 0x90; buf[13] = 0x00;                             // EtherType 0x9000 (private/test)
}

} // namespace

class EgressTest : public ::testing::Test
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

TEST_F(EgressTest, TxBenchmark)
{
    uint32_t ifidx = static_cast<uint32_t>(::if_nametoindex(kTestIface));
    if (ifidx == 0)
        GTEST_SKIP() << "Interface '" << kTestIface << "' not found — "
                        "create with: ip link add dummy0 type dummy && ip link set dummy0 up";

    uint64_t mac = readIfMac(kTestIface);
    printf("[EgressTest] interface=%s  ifindex=%u  mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
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
    printf("[EgressTest] TX threads started — running for %ds\n\n", kDurationSec);
    fflush(stdout);

    uint8_t testFrame[kFrameSize];
    buildTestFrame(testFrame, kFrameSize, mac);

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

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    iface->stopThreads();

    const double elapsed  = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - startTime).count();
    const double fps      = static_cast<double>(txSent) / elapsed;
    const double mbps     = fps * kFrameSize * 8.0 / 1e6;
    const double missRate = (txSent + txNoFrame) > 0
        ? 100.0 * static_cast<double>(txNoFrame) / static_cast<double>(txSent + txNoFrame)
        : 0.0;

    printf("\n[EgressTest] --- Results (%.1fs) ---\n", elapsed);
    printf("  TX sent:      %12lu frames\n", txSent);
    printf("  TX no-frame:  %12lu (%.1f%% miss rate)\n", txNoFrame, missRate);
    printf("  TX rate:      %12.0f fps  (%.1f Mbit/s @ %u B/frame)\n",
           fps, mbps, kFrameSize);
    fflush(stdout);

    EXPECT_GT(txSent, 0UL) << "No frames were sent";
}
