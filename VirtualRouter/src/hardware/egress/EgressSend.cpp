// EgressSend.cpp

#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/ethernet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <stdexcept>

#include "EgressSend.h"
#include "hardware/Ifname.h"
#include "hardware/PacketSlot.hpp"
#include "interface/Interface.h"
#include "qos/egress/TxQueueOpts.hpp"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace hardware::egress
{

static inline size_t alignUp(size_t v, size_t align)
{
    return (v + align - 1) & ~(align - 1);
}

EgressSend::EgressSend(interface::Interface& iface, const qos::egress::TxQueueOpts& opts)
    : EgressBase(iface, opts)
{
    uint32_t ifindex = ifnametoindex(opts.ifname.c_str());
    if (ifindex <= 0)
        std::runtime_error("EgressSend: Invalid ifindex");

    frameCount = 64; // Hard coded (deal with it)
    const size_t rawPerFrame = packetSize + MTU_PADDING + sizeof(PacketSlot);

    frameStride = alignUp(rawPerFrame, 64);

    const size_t totalBytes = frameStride * frameCount;
    void* mem = nullptr;
    if (posix_memalign(&mem, 64, totalBytes) != 0 || !mem)
        throw std::bad_alloc();

    frameArea = static_cast<uint8_t*>(mem);

    initFreeRing(frameCount);

    sockFd = ::socket(AF_PACKET, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, htons(ETH_P_ALL));

    if (sockFd < 0)
    {
        std::free(frameArea);
        frameArea = nullptr;
        throw std::runtime_error("EgressSend: socket() failed: " + std::string(strerror(errno)));
    }

#ifdef PACKET_QDISC_BYPASS
    {
        int one = 1;
        ::setsockopt(sockFd, SOL_PACKET, PACKET_QDISC_BYPASS, &one, sizeof(one));
    }
#endif

    std::memset(&addr, 0, sizeof(addr));
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ALL);
    addr.sll_ifindex = ifindex;

    if (::bind(sockFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        int err = errno;
        ::close(sockFd);
        sockFd = -1;
        std::free(frameArea);
        frameArea = nullptr;
        throw std::runtime_error("EgressSend: bind() failed: " +
                                 std::string(std::strerror(err)));
    }
}

EgressSend::~EgressSend()
{
    if (sockFd >= 0)
        ::close(sockFd);
    if (frameArea)
        std::free(frameArea);
}

void EgressSend::mapFrame(uint32_t index, FrameHandle& out)
{
    if (!frameArea || index >= frameCount)
    {
        out.payload = nullptr;
        out.slot = nullptr;
        out.qid = qid;
        return;
    }

    uint8_t* base = frameArea + static_cast<size_t>(index) * frameStride;

    out.payload = base;
    out.slot = nullptr; // Egress Base will set this
    out.qid = qid;
}

bool EgressSend::send(uint32_t index, uint32_t length) noexcept
{
    if (!frameArea || sockFd < 0 || index >= frameCount)
    {
        if (index < frameCount)
            pushFree(index);
        return false;
    }

    uint8_t* payload = frameArea + static_cast<size_t>(index) * frameStride;

    const size_t maxBytes = packetSize + MTU_PADDING;

    size_t sendLen = length;
    if (sendLen > maxBytes)
        sendLen = maxBytes;

    if (sendLen == 0)
    {
        pushFree(index);
        return true;
    }

    ssize_t n;
    do
    {
        n = ::sendto(sockFd, payload, sendLen, MSG_NOSIGNAL | MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    } while (n < 0 && errno == EINTR);

    const bool ok = (n == static_cast<ssize_t>(sendLen));

    pushFree(index);

    return ok;
}

void EgressSend::cancel(uint32_t index)
{
    if (index < frameCount)
        pushFree(index);
}

} // namespace hardware
