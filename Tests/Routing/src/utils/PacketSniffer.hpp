// PacketSniffer.hpp

#ifndef PACKET_SNIFFER_HPP
#define PACKET_SNIFFER_HPP

#include <pcap.h>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <string>
#include <iostream>
#include <utility>
#include <Decapsulation.h>
#include <functional>

class PacketSniffer {
public:
    explicit PacketSniffer(const std::string& interface)
        : iface(interface), handle(nullptr), running(false) {}

    ~PacketSniffer() {
        stop();
    }

    void start() {
        if (running) return;

        char errbuf[PCAP_ERRBUF_SIZE];
        handle = pcap_open_live(iface.c_str(), BUFSIZ, 1, 1000, errbuf);
        if (!handle) {
            std::cerr << "Failed to open pcap on interface " << iface << ": " << errbuf << std::endl;
            return;
        }

        running = true;
        captureThread = std::thread([this]() {
            pcap_loop(handle, 0, &PacketSniffer::packetHandler, reinterpret_cast<u_char*>(this));
        });
    }

    void stop() {
        if (!running) return;

        running = false;
        if (handle) {
            pcap_breakloop(handle);
            pcap_close(handle);
            handle = nullptr;
        }

        if (captureThread.joinable())
            captureThread.join();
    }

    bool hasPacket() {
        std::lock_guard<std::mutex> lock(queueMutex);
        return !packetQueue.empty();
    }

    PacketInfo getNextPacket() {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (packetQueue.empty()) return {};
        auto pkt = std::move(packetQueue.front());
        packetQueue.pop();
        return pkt;
    }

    template <typename HeaderType>
    std::function<bool()> createListenCondition(std::function<bool(const HeaderType&)> condition, int requiredMatches = 1)
    {
        const size_t listenerId = listenerIdCounter++;

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            listenerIndexMap[listenerId] = packetHistory.size();
        }

        int matchCount = 0;

        return [this, condition, requiredMatches, listenerId, matchCount]() mutable -> bool
        {
            std::lock_guard<std::mutex> lock(queueMutex);

            size_t& lastIndexSeen = listenerIndexMap[listenerId];
            bool matched = false;

            for (size_t i = lastIndexSeen; i < packetHistory.size(); ++i)
            {
                const PacketInfo& pkt = packetHistory[i];
                auto flattenedHeaders = flattenHeaders(pkt);
                for (const auto& header : flattenedHeaders)
                {
                    std::visit([&](const auto& layerVarient)
                    {
                        std::visit([&](const auto& actualHeader)
                        {
                            using ActualType = std::decay_t<decltype(actualHeader)>;
                            if constexpr (std::is_same_v<ActualType, HeaderType>)
                            {
                                if (condition(actualHeader))
                                {
                                    ++matchCount;
                                    if (matchCount > requiredMatches)
                                        matched = true;
                                }
                            }
                        }, layerVarient);
                    }, header);
                    if (matched) break;
                }
                if (matched) break;
            }

            lastIndexSeen = packetHistory.size();
            return matched;
        };
    }

private:
    static void packetHandler(u_char* user, const struct pcap_pkthdr* header, const u_char* packet) {
        auto* sniffer = reinterpret_cast<PacketSniffer*>(user);
        if (!sniffer || header->caplen == 0) return;

        ByteString byteData(packet, header->caplen);
        Packet decap(byteData, true);
        decap.decapsulate();

        std::lock_guard<std::mutex> lock(sniffer->queueMutex);

        sniffer->packetQueue.push(decap.packetInfo);

        sniffer->packetHistory.push_back(decap.packetInfo);
        if (sniffer->packetHistory.size() > sniffer->MAX_HISTORY)
            sniffer->packetHistory.pop_front();
    }

    std::string iface;
    pcap_t* handle;
    std::thread captureThread;
    std::atomic<bool> running;

    std::queue<PacketInfo> packetQueue;
    std::deque<PacketInfo> packetHistory;
    std::mutex queueMutex;
    const size_t MAX_HISTORY = 1000;

    std::unordered_map<size_t, size_t> listenerIndexMap;
    size_t listenerIdCounter;
};

#endif // PACKET_SNIFFER_HPP
