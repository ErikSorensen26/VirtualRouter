// Ingress.hpp

#ifndef INGRESS_HPP
#define INGRESS_HPP

#include <linux/if_xdp.h>
#include <linux/if_ether.h>
#include <linux/if_link.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdexcept>
#include <thread>
#include <functional>
#include <cstring>
#include <cerrno>
#include <atomic>

class Ingress
{
public:
    using PacketCallback = std::function<void(const uint8_t* data, uint32_t len)>;

    Ingress(const char* ifname, uint32_t qid, PacketCallback cb,
            uint32_t frameCount = 4096, uint32_t frameSize = 2048)
      : callback(std::move(cb)),
        frameCount(frameCount),
        frameSize(frameSize),
        umemSize(uint64_t(frameCount) * frameSize),
        qid(qid)
    {
        setupSocket();
        setupUmem();
        mapRings();
        bindSocket(ifname);
        fillInitialFrames();
        startPoll();
    }

    ~Ingress()
    {
        running.store(false);
        if (poller.joinable()) poller.join();
        if (xskFd >= 0) close(xskFd);
        if (epfd >= 0) close(epfd);
        if (umemArea) munmap(umemArea, umemSize);
    }

private:
    PacketCallback callback;
    uint32_t frameCount, frameSize;
    uint64_t umemSize;
    int xskFd{-1}, epfd{-1};
    void* umemArea{};
    int* fillRingMapA{}, *rxRingMap{};
    uint32_t* fillRingDesc{};
    uint32_t* fillRingProducer{}, *fillRingConsumer{};
    struct xdp_desc* rxRingDesc;
    uint32_t* rxRingProducer{}, *rxRingConsumer{};
    struct xdp_mmap_offsets off{};
    std::thread poller;
    std::atomic<bool> running{true};
    uint32_t qid;

    void ensure(int r, const char* msg)
    {
        if (r < 0) throw std::runtime_error(std::string(msg) + ": " + strerror(errno));
    }

    void setupSocket()
    {
        xskFd = socket(AF_XDP, SOCK_RAW | SOCK_NONBLOCK, 0);
        ensure(xskFd, "socket(AF_XDP)");
    }

    void setupUmem()
    {
        ensure(posix_memalign(&umemArea, frameSize, umemSize), "posix_memalign");
        struct xdp_umem_reg umr {
            .addr = (uintptr_t)umemArea,
            .len = (uint32_t)umemSize,
            .chunk_size = frameSize,
            .headroom = 0,
            .flags = 0
        };
        ensure(setsockopt(xskFd, SOL_XDP, XDP_UMEM_REG, &umr, sizeof(umr)), "XDP_UMEM_REG");
        socklen_t sz = sizeof(off);
        ensure(setsockopt(xskFd, SOL_XDP, XDP_MMAP_OFFSETS, &off, sz), "XDP_MMAP_OFFSETS");
    }

    void mapRings()
    {
        size_t fillSize = off.fr.desc + frameCount + sizeof(uint32_t);
        void* fillRingMap = mmap(nullptr, fillSize, PROT_READ|PROT_WRITE, MAP_SHARED, xskFd, XDP_UMEM_PGOFF_FILL_RING);
        if (fillRingMap == MAP_FAILED)
            throw std::runtime_error("mmap(FILL_RING) failed");

        fillRingProducer = (uint32_t*)((uint8_t*)fillRingMap + off.fr.producer);
        fillRingConsumer = (uint32_t*)((uint8_t*)fillRingMap + off.fr.consumer);
        fillRingDesc     = (uint32_t*)((uint8_t*)fillRingMap + off.fr.desc);

        size_t rxSize = off.rx.desc + frameCount + sizeof(struct xdp_desc);
        void* rxRingMap = mmap(nullptr, rxSize, PROT_READ|PROT_WRITE, MAP_SHARED, xskFd, XDP_PGOFF_RX_RING);
        if (rxRingMap == MAP_FAILED)
            throw std::runtime_error("mmap(RX_RING) failed");

        rxRingProducer = (uint32_t*)((uint8_t*)rxRingMap + off.rx.producer);
        rxRingConsumer = (uint32_t*)((uint8_t*)rxRingMap + off.rx.producer);
        rxRingDesc     = (struct xdp_desc*)((uint8_t*)rxRingMap + off.rx.desc);
    }

    void bindSocket(const char* ifname)
    {
        struct sockaddr_xdp sxdp = {};
        sxdp.sxdp_family = AF_XDP;
        sxdp.sxdp_ifindex = if_nametoindex(ifname);
        sxdp.sxdp_queue_id = qid;
        sxdp.sxdp_flags = XDP_USE_NEED_WAKEUP;
        ensure(bind(xskFd, (sockaddr*)&sxdp, sizeof(sxdp)), "bind(AF_XDP)");
    }

    void fillInitialFrames()
    {
        for (uint32_t i = 0; i < frameCount; ++i)
            fillRingDesc[i] = i;
        *fillRingProducer = frameCount;
    }

    void startPoll()
    {
        epfd = epoll_create1(0);
        ensure(epfd, "epoll_create1");

        epoll_event ev{EPOLLIN, {.fd = xskFd}};
        ensure(epoll_ctl(epfd, EPOLL_CTL_ADD, xskFd, &ev), "epoll_ctl");

        poller = std::thread([this] { this->pollLoop(); });
    }

    void pollLoop()
    {
        epoll_event ev;
        while(running.load())
        {
            int n = epoll_wait(epfd, &ev, 1, 1000);
            if (n > 0)
                processRx();
        }
    }

    void processRx()
    {
        uint32_t cons = *rxRingConsumer;
        uint32_t prod = *rxRingProducer;
        while (cons != prod)
        {
            struct xdp_desc& desc = rxRingDesc[cons];
            uint64_t offset = desc.addr * frameSize;
            const uint8_t* data = reinterpret_cast<uint8_t*>(umemArea) + offset;

            callback(data, desc.len);

            // Recycle this buffer back into fill ring
            uint32_t fillIndex = *fillRingProducer % frameCount;
            fillRingDesc[fillIndex] = desc.addr;
            ++(*fillRingProducer);

            if (++cons >= frameCount) cons = 0;
        }
        *rxRingConsumer = cons;
        sendto(xskFd, nullptr, 0, 0, nullptr, 0); // Kick kernel if needed
    }
};

#endif // INGRESS_HPP
