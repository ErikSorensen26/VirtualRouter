// ReliableRX.cpp

#include "ReliableTransport.h"
#include "TLVBuilder.h"
#include "EigrpInterface.h"
#include <Eigrp.h>
#include <PacketBuilder.hpp>

namespace Eigrp
{
void ReliableTransport::handleIncoming(const uint8_t* ipStart, const EigrpHeader& eigrpPacket, const uint8_t* neighborIp, bool multicast)
{
    IPAddress neigIp(neighborIp, af);

    // Check if passive
    if (iface.configs->isPassive.load(std::memory_order_relaxed))
        return;

    // Validate packet version
    if (eigrpPacket.raw->version != 0x02)
        return; // Version not valid

    RTPInfo hdrInfo(eigrpPacket, neigIp);
    parseEigrpOptions(eigrpPacket.getTrail().data(), eigrpPacket.getTrail().size(), hdrInfo.opts);

    const TLV16Option* authOpt = nullptr;
    for (const auto& opt : hdrInfo.opts)
    {
        if (opt.type == EIGRP_OPTION_AUTHENTICATION)
        {
            authOpt = &opt;
            break;
        }
    }

    size_t size = (eigrpPacket.buffer + EigrpHeader::fixedSize + eigrpPacket.getTrail().size()) - ipStart;
    if (!iface.getAuth().validateAuth(ipStart, size, authOpt));

    // Check for valid neighbor 
    Neighbor* neighbor = ntable->lookup(neigIp);
    hdrInfo.neighbor = neighbor;

    if (eigrpPacket.getOpcode() == EIGRP_TYPE_HELLO)
    {
        if (eigrpPacket.getAck() == 0)
        {
            processHello(hdrInfo, !multicast);
        }
        else if (neighbor)
        {
            processAck(*neighbor, eigrpPacket.getAck());
        }
    }
    else if (neighbor)
    {
        if (auto ack = eigrpPacket.getAck(); ack != 0)
            processAck(*neighbor, ack); // Piggyback ack

        switch (eigrpPacket.getOpcode())
        {
            case EIGRP_TYPE_UPDATE:
                processUpdate(hdrInfo);
                break;
            case EIGRP_TYPE_REPLY:
                processReply(hdrInfo);
                break;
            case EIGRP_TYPE_SIA_REPLY:
                processSIAReply(hdrInfo);
                break;
            case EIGRP_TYPE_QUERY:
                processQuery(hdrInfo);
                break;
            case EIGRP_TYPE_SIA_QUERY:
                processSIAQuery(hdrInfo);
                break;
            default:
                return;
        }
    }
}

void ReliableTransport::processHello(RTPInfo& info, bool unicast)
{
    auto receivedHello = info.eigrp;

    if (!ntable->validatePTP(info.neighborIp) || !verifyNeighborAS(receivedHello))
    {
        pendingPeerTermination.store(true, std::memory_order_release);
        return;
    }

    auto calcVersion = [&]() {
        bool sawVersion = false;
        uint16_t version = 0;

        for (const auto& v : info.opts)
        {
            if (v.type == EIGRP_OPTION_VERSION && v.valueSize >= 4)
            {
                version = readU16(v.value + 2);
                sawVersion = true;
                break;
            }
        }

        Neighbor::Version nver = Neighbor::Version::LEGACY;
        if (sawVersion && version >= static_cast<uint16_t>(Neighbor::Version::WIDE))
            nver = Neighbor::Version::WIDE;
        return nver;
    };

    // Safely access or create the neighbor
    if (!info.neighbor)
    {
        Neighbor::Version nver = calcVersion();
        info.neighbor = ntable->createNeighbor(info.neighborIp, nver);
        if (!info.neighbor) return;

        info.neighbor->srtt = 1.0;
        info.neighbor->rttvar = 0.5;
        info.neighbor->rto = 1.5;
        info.neighbor->setState(Neighbor::State::PENDING);
    }
    else if (unicast && info.neighbor && info.neighbor->version == Neighbor::Version::UNKNOWN)
    {
        Neighbor::Version nver = calcVersion();
        info.neighbor->version = nver;
        if (info.neighbor->getState() == Neighbor::State::DOWN)
            info.neighbor->setState(Neighbor::State::PENDING);
    }
    else if (!info.neighbor && unicast) return;

    auto* nbr = info.neighbor;

    bool parametersFound = false;
    uint32_t conditionalSeq = 0;
    bool conditionExemption = false;

    // Process TLVs
    for (const auto& opt : info.opts)
    {
        if (opt.type == EIGRP_OPTION_PARAMETER)
        {
            // Check for Peer Termination
            static const uint8_t legacyPeerTerm[6] = {0};
            if (std::memcmp(opt.value, legacyPeerTerm, 6) == 0)
            {
                ntable->onDown(*info.neighbor);
                return;
            }

            uint8_t parameters[6];
            TLVBuilder::calculateParameters(parameters, iface.getBase().getGlobalConfigMgr().getKValues());

            if (std::memcmp(parameters, opt.value, 6) != 0)
            {
                pendingPeerTermination.store(true, std::memory_order_release);
                return;
            }

            if (opt.length >= 8)
                info.neighbor->holdTime.store(readU16(opt.value + 6), std::memory_order_relaxed);

            parametersFound = true;
        }
        else if (opt.type == EIGRP_OPTION_MULTICAST_SEQUENCE && opt.valueSize == 4)
        {
            conditionalSeq = readU32(opt.value);
        }
        else if (opt.type == EIGRP_OPTION_SEQUENCE && opt.valueSize > 1)
        {
            const uint8_t addrLen = opt.value[0];
            if (addrLen != 4 && addrLen != 16)
                continue;
            
            const uint8_t* list = opt.value + 1;
            const size_t listLen = opt.valueSize - 1;

            uint8_t ourAddr[16];
            if (iface.getBase().getAF() == AddressFamily::IPv4)
                iface.getIface()->configs.ipv4.getPrimaryAddress(ourAddr);
            else
                writeU128(ourAddr, iface.getIface()->configs.ipv6.getLocalAddress());

            for (size_t off = 0; off + addrLen <= listLen; off += addrLen)
            {
                if (std::memcmp(ourAddr, list + off, addrLen) == 0)
                {
                    conditionExemption = true;
                    break;
                }
            }
        }
    }

    if (conditionalSeq != 0)
        info.neighbor->receivedConditions[conditionalSeq] = conditionExemption;

    info.neighbor->markHeard();
    iface.getTimers().startHoldTimer(*nbr);

    // Safely extract the neighbor state
    if (parametersFound)
    {
        if (!nbr->initInProgress.exchange(true, std::memory_order_acq_rel))
        {
            sendNullUpdate(*nbr);
        }
    }
}

bool ReliableTransport::validateSeqNum(RTPInfo& info, uint32_t recvSeq)
{
    if (recvSeq == 0)
        return false;
    
    const uint32_t lastSeqRecv = info.neighbor->lastSeqRecv.load(std::memory_order_relaxed);

    if (recvSeq <= lastSeqRecv)
    {
        sendAck(*info.neighbor, recvSeq);
        return false;
    }

    info.neighbor->lastSeqRecv.store(recvSeq, std::memory_order_relaxed);
    return true;
}

void ReliableTransport::checkInit(Neighbor& neighbor)
{
    if (neighbor.getState() == Neighbor::State::UP)
        return;

    const uint32_t recvInit = neighbor.recvInitSeq.load(std::memory_order_relaxed);
    const uint32_t sendInit = neighbor.sentInitSeq.load(std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(reliableMtx);
    const bool ourNullAcked = (sendInit != 0) && !reliablePackets.contains(sendInit);
    const bool theirNullSeen = (recvInit != 0);

    if (ourNullAcked && theirNullSeen && !neighbor.fullSent.load(std::memory_order_relaxed))
    {
        neighbor.setState(Neighbor::State::UP);
    }
}

bool ReliableTransport::processConditionalReceive(uint32_t seq, Neighbor& neighbor)
{
    if (auto crit = neighbor.receivedConditions.find(seq); crit != neighbor.receivedConditions.end())
    {
        bool exempt = crit->second;
        neighbor.receivedConditions.erase(seq);
        return !exempt;
    }
    else
    {
        pendingPeerTermination.store(true, std::memory_order_release);
        return false;
    }
}

void ReliableTransport::processUpdate(RTPInfo& info)
{
    auto& update = info.eigrp;
    Neighbor& nbr = *info.neighbor;
    Neighbor::State state = nbr.getState();

    // ignore self-originated or invalid AS packets
    if (!verifyNeighborAS(update))
        return;

    const uint32_t recvSeq = update.getSequence();

    if (info.eigrp.getFlagCondRecv() && !processConditionalReceive(recvSeq, *info.neighbor))
        return;

    if (!validateSeqNum(info, recvSeq))
        return;

    std::vector<TLV16Option> routeOpts;
    for (const auto& opt : info.opts)
    {
        routeOpts.push_back(opt);
    }

    bool resync = update.getFlagRestart() && update.getFlagInit() &&
        info.neighbor && state == Neighbor::State::UP;
    if (resync)
    {
        nbr.resyncInProgress.store(true, std::memory_order_release);
        iface.getTopController().onNeighborDown(*info.neighbor);
    }

    if (update.getFlagInit())
    {
        nbr.recvInitSeq.store(recvSeq, std::memory_order_relaxed);
        nbr.fullSent.store(false, std::memory_order_release);
        checkInit(*info.neighbor);
    }

    trackAck(nbr, recvSeq);

    if (state == Neighbor::State::UP)
    {
        std::vector<ReceivedRoute> routeBuffer;
        routeBuffer.reserve(routeOpts.size());
        bool nextHopSelf = iface.configs->nextHopSelf.load(std::memory_order_relaxed);
        for (const auto& opt : routeOpts)
        {
            if (auto route = TLVBuilder::decodeRoute(opt, iface.interfaceKey, af); route)
            {
                routeBuffer.emplace_back(std::move(*route));
            }
        }

        if (!routeBuffer.empty())
        {
            if (iface.recordDampeningEvent())
            {
                iface.getMetrics().addRouteMetrics(routeBuffer);
                iface.getTopController().processReceivedRoutes(routeBuffer, nbr);
            }
        }

        // Continue resync if needed
        if (resync)
        {
            sendFullTopology(*info.neighbor, Resync::REPLY);
        }
        if (nbr.resyncInProgress.load(std::memory_order_relaxed))
        {
            nbr.resyncInProgress.store(false, std::memory_order_release);
        }
    }

    attemptSendAck(nbr, recvSeq);
}

void ReliableTransport::processAck(Neighbor& neighbor, const uint32_t seq)
{
    auto& timers = iface.getTimers();

    if (neighbor.currentReliable.load(std::memory_order_relaxed) == seq)
    {
        if (auto uit = neighbor.reliablePackets.find(seq); uit != neighbor.reliablePackets.end())
        {
            timers.cancelRetransmissionTimer(uit->second.info);
            neighbor.reliablePackets.erase(uit);
            neighbor.currentReliable.store(0, std::memory_order_relaxed);
        }
        else if (auto mit = reliablePackets.find(seq); mit != reliablePackets.end())
        {
            mit->second.neighbors.erase(&neighbor);
            neighbor.activeConditions.erase(seq);
            if (mit->second.neighbors.empty())
                reliablePackets.erase(mit);
        }
        else return;

        neighbor.currentReliable.store(0, std::memory_order_release);

        if (!neighbor.reliableQueue.empty())
        {
            auto [nseq, multicast] = neighbor.reliableQueue.front();
            neighbor.reliableQueue.pop_front();

            if (multicast)
            {
                auto it = reliablePackets.find(nseq);
                if (it == reliablePackets.end()) return;
                auto nit = it->second.neighbors.find(&neighbor);
                if (nit == it->second.neighbors.end()) return;

                auto& info = nit->second;
                info.sendTime = std::chrono::steady_clock::now();
                info.retransmissionCount = 0;
                neighbor.currentReliable.store(nseq, std::memory_order_release);
                sendRetransmission(neighbor, it->second.packet);
                iface.getTimers().startRetransmissionTimer(&neighbor, it->second, info, seq);
            }
            else
            {
                auto it = neighbor.reliablePackets.find(nseq);
                if (it == neighbor.reliablePackets.end()) return;

                neighbor.currentReliable.store(nseq, std::memory_order_release);
                sendRetransmission(neighbor, it->second.packet);
                startUnicastReliable(neighbor, it->second, nseq);
            }
        }
    }

    if (seq == neighbor.sentInitSeq.load(std::memory_order_release) && neighbor.initComplete)
        sendFullTopology(neighbor);

    checkInit(neighbor);
}

void ReliableTransport::processQuery(RTPInfo& info)
{
    Neighbor* nbr = info.neighbor;
    if (!nbr || nbr->getState() != Neighbor::State::UP) return;

    const uint32_t recvSeq = info.eigrp.getSequence();

    if (info.eigrp.getFlagCondRecv() && !processConditionalReceive(recvSeq, *info.neighbor))
        return;

    if (!validateSeqNum(info, recvSeq))
        return;
    trackAck(*nbr, recvSeq);

    std::vector<ReceivedRoute> queriedRoutes;
    for (const auto& opt : info.opts)
    {
        if (auto route = TLVBuilder::decodeRoute(opt, iface.interfaceKey, af); route)
        {
            queriedRoutes.emplace_back(std::move(*route));
        }
    }

    iface.getMetrics().addRouteMetrics(queriedRoutes);
    iface.getTopController().processReceivedQueryRoutes(queriedRoutes, *info.neighbor, recvSeq);

    attemptSendAck(*nbr, recvSeq);
}

void ReliableTransport::processSIAQuery(RTPInfo& info)
{
    Neighbor* nbr = info.neighbor;
    if (!nbr || nbr->getState() == Neighbor::State::UP) return;

    const uint32_t seq = info.eigrp.getSequence();
    if (!validateSeqNum(info, seq))
        return;
    trackAck(*info.neighbor, seq);
    sendSIAReply(*info.neighbor);
    attemptSendAck(*info.neighbor, seq);
}

void ReliableTransport::processReply(RTPInfo& info)
{
    Neighbor* nbr = info.neighbor;
    if (!nbr || nbr->getState() != Neighbor::State::UP) return;

    const uint32_t seq = info.eigrp.getSequence();
    if (!validateSeqNum(info, seq))
        return;

    trackAck(*nbr, seq);

    std::vector<ReceivedRoute> receivedRoutes;
    for (const auto& opt : info.opts)
    {
        if (auto route = TLVBuilder::decodeRoute(opt, iface.interfaceKey, af); route)
        {
            receivedRoutes.emplace_back(std::move(*route));
        }
    }

    if (!receivedRoutes.empty())
        iface.getTopController().processReceivedActiveRoutes(receivedRoutes, *info.neighbor);

    attemptSendAck(*nbr, seq);
}

void ReliableTransport::processSIAReply(RTPInfo& info)
{
    if (!info.neighbor || info.neighbor->getState() != Neighbor::State::UP) return;

    const uint32_t seq = info.eigrp.getSequence();
    if (!validateSeqNum(info, seq))
        return;

    trackAck(*info.neighbor, seq);
    iface.getTopController().processSIAReply(*info.neighbor, seq);
    attemptSendAck(*info.neighbor, seq);
}
}
