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

    if (hdrInfo.opts.size() > 0 && hdrInfo.opts.back().type == Variable::Eigrp::Option::authentication)
        if (!iface.getAuth().validateAuth(ipStart, hdrInfo.opts.back()))
            return;

    // Check for valid neighbor 
    Neighbor* neighbor = ntable->lookup(neigIp);
    hdrInfo.neighbor = neighbor;

    if (eigrpPacket.getOpcode() == Variable::Eigrp::Type::hello)
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
            processAck(*neighbor, ack); // Implicit ack

        switch (eigrpPacket.getOpcode())
        {
            case Variable::Eigrp::Type::update:
                processUpdate(hdrInfo);
                break;
            case Variable::Eigrp::Type::reply:
                processReply(hdrInfo);
                break;
            case Variable::Eigrp::Type::siaReply:
                processSIAReply(hdrInfo);
                break;
            case Variable::Eigrp::Type::query:
                processQuery(hdrInfo);
                break;
            case Variable::Eigrp::Type::siaQuery:
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

    if (!ntable->validatePTP(info.neighborIp) || !verifyNeighborAS(receivedHello)) return;

    // Safely access or create the neighbor
    if (!info.neighbor && !unicast)
    {
        uint16_t version;
        for (const auto& v : info.opts)
            if (v.type == Variable::Eigrp::Option::version)
                version = readU16(v.value + 2);
        if (version != static_cast<uint8_t>(Neighbor::Version::LEGACY) || version != static_cast<uint8_t>(Neighbor::Version::WIDE)) return;

        info.neighbor = ntable->createNeighbor(info.neighborIp, static_cast<Neighbor::Version>(version));
        if (!info.neighbor) return;
        info.neighbor->srtt = 1.0;
        info.neighbor->rttvar = 0.5;
        info.neighbor->rto = 1.5;
        info.neighbor->setState(Neighbor::State::INIT);
    }
    else if (!info.neighbor && unicast) return;

    auto* nbr = info.neighbor;
    bool parametersFound = false;

    // Process TLVs
    for (const auto& opt : info.opts)
    {
        if (opt.type == Variable::Eigrp::Option::parameter)
        {
            // Check for Peer Termination
            if (std::memcmp(opt.value, Variable::Mac::broadcast, 5) == 0)
            {
                ntable->onDown(*info.neighbor);
                return;
            }

            uint8_t parameters[6];
            TLVBuilder::calculateParameters(parameters, iface.getBase().getGlobalConfigMgr().getKValues());

            if (std::memcmp(parameters, opt.value, 6) != 0) continue;

            if (opt.length >= 8)
                info.neighbor->holdTime.store(readU16(opt.value + 6), std::memory_order_relaxed);

            parametersFound = true;
        }
        else
        {
            // TODO handle peer termination opt
        }
    }

    info.neighbor->markHeard();

    iface.getTimers().restartHoldTimer(*nbr);

    // Safely extract the neighbor state
    const auto& state = nbr->getState();

    if (state == Neighbor::State::INIT && parametersFound)
    {
        info.neighbor->setState(Neighbor::State::TWOWAY);
        info.neighbor->eotRecv.store(false, std::memory_order_relaxed);
        info.neighbor->initInProgress.store(true, std::memory_order_relaxed);

        sendNullUpdate(*nbr);
        iface.getTimers().startInitTimer(*info.neighbor);
    }
}

bool ReliableTransport::validateSeqNum(RTPInfo& info, uint32_t recvSeq)
{
    if (recvSeq == 0)
        return false;
    
    const uint32_t lastSeqRecv = info.neighbor->lastSeqRecv.load(std::memory_order_relaxed);

    if (recvSeq <= lastSeqRecv)
    {
        sendAck(*info.neighbor, lastSeqRecv);
        return false;
    }

    info.neighbor->lastSeqRecv.store(recvSeq, std::memory_order_relaxed);
    return true;
}

void ReliableTransport::processUpdate(RTPInfo& info)
{
    auto& update = info.eigrp;
    Neighbor& nbr = *info.neighbor;

    // ignore self-originated or invalid AS packets
    if (!verifyNeighborAS(update))
        return;

    const uint32_t recvSeq = update.getSequence();
    if (!validateSeqNum(info, recvSeq))
        return;

    std::vector<TLV16Option> routeOpts;
    for (const auto& opt : info.opts)
    {
        routeOpts.push_back(opt);
    }

    if (update.getFlagInit())
    {
        if (routeOpts.empty())
        {
            nbr.initSeq.store(recvSeq, std::memory_order_relaxed);
        }

        iface.getTimers().cancelInitTimer(nbr);

        if (routeOpts.empty())
        {
            nbr.eotRecv.store(true, std::memory_order_release);
            nbr.setState(Neighbor::State::ESTABLISHED);
        }
    }

    if (update.getFlagCondRecv())
    {
        sendCondAck(nbr, recvSeq);
    }
    else
    {
        sendAck(nbr, recvSeq);
    }

    if (update.getFlagRestart())
    {
        ntable->onDown(*info.neighbor);
    }

    if (update.getFlagEndOfTable())
    {
        nbr.eotRecv.store(true, std::memory_order_release);
        iface.getTimers().cancelInitTimer(nbr);
        nbr.setState(Neighbor::State::ESTABLISHED);
        nbr.initInProgress.store(false, std::memory_order_relaxed);
    }

    if (nbr.getState() < Neighbor::State::LOADING)
        return;

    std::vector<ReceivedRoute> routeBuffer;
    routeBuffer.reserve(routeOpts.size());
    for (const auto& opt : routeOpts)
    {
        if (auto route = TLVBuilder::decodeRoute(opt, iface.interfaceKey); route)
            routeBuffer.emplace_back(std::move(*route));
    }

    if (!routeBuffer.empty())
    {
        if (iface.recordDampeningEvent())
        {
            iface.getMetrics().addRouteMetrics(routeBuffer);
            iface.getTopController().processReceivedRoutes(routeBuffer, nbr);

            // TODO implicit if no eot?
        }
    }
}

void ReliableTransport::processAck(Neighbor& neighbor, const uint32_t seq)
{
    if (neighbor.getState() == Neighbor::State::TWOWAY)
    {
        neighbor.setState(Neighbor::State::LOADING);
        sendFullTopology(neighbor);
    }

    auto& timers = iface.getTimers();

    if (currentReliable.load(std::memory_order_relaxed) == seq)
    {
        auto rit = reliableQueue.find(seq);
        if (rit == reliableQueue.end()) return;

        auto it = rit->second.second.neighbors.find(&neighbor);
        if (it == rit->second.second.neighbors.end()) return;

        iface.getMetrics().updateRTTEstimate(neighbor, it->second.sendTime, seq);

        timers.cancelRetransmissionTimer(it->second);
        rit->second.second.neighbors.erase(it);

        if (rit->second.second.neighbors.empty())
        {
            reliableQueue.erase(rit);
            currentReliable.store(0, std::memory_order_release);
            if (!reliableQueue.empty())
            {
                auto nextIt = reliableQueue.begin();
                currentReliable.store(nextIt->first, std::memory_order_release);
                for (auto& nbr : nextIt->second.second.neighbors)
                {
                    timers.startRetransmissionTimer(nbr.first, nextIt->second.second, nbr.second, nextIt->first);
                }
            }
        }
    }
    else if (neighbor.currentReliable.load(std::memory_order_relaxed) == seq)
    {
        auto it = neighbor.reliableQueue.find(seq);
        if (it == neighbor.reliableQueue.end()) return;

        timers.cancelRetransmissionTimer(it->second.info);
        neighbor.reliableQueue.erase(it);
        neighbor.currentReliable.store(0, std::memory_order_release);

        if (!neighbor.reliableQueue.empty())
        {
            auto nextIt = neighbor.reliableQueue.begin();
            neighbor.currentReliable.store(nextIt->first, std::memory_order_release);
            timers.startRetransmissionTimer(&neighbor, nextIt->second, nextSeq);
        }
    }
}

void ReliableTransport::processQuery(RTPInfo& info)
{
    Neighbor* nbr = info.neighbor;
    if (!nbr) return;
    const uint32_t recvSeq = info.eigrp.getSequence();

    const uint32_t lastSeq = nbr->lastSeqRecv.load(std::memory_order_relaxed);
    if (recvSeq <= lastSeq)
    {
        sendAck(*nbr, lastSeq);
        return;
    }

    nbr->lastSeqRecv.store(recvSeq, std::memory_order_relaxed);
    sendAck(*nbr, recvSeq);

    std::vector<ReceivedRoute> queriedRoutes;
    for (const auto& opt : info.opts)
    {
        if (auto route = TLVBuilder::decodeRoute(opt, iface.interfaceKey))
            queriedRoutes.emplace_back(std::move(*route));
    }

    iface.getMetrics().addRouteMetrics(queriedRoutes);
    iface.getTopController().processReceivedQueryRoutes(queriedRoutes, *info.neighbor, recvSeq);
}

void ReliableTransport::processSIAQuery(RTPInfo& info)
{
    Neighbor* nbr = info.neighbor;
    if (!nbr) return;

    const uint32_t seq = info.eigrp.getSequence();

    if (seq < nbr->lastSeqRecv.load(std::memory_order_seq_cst))
        return;

    nbr->lastSeqRecv.store(seq, std::memory_order_release);
    sendAck(*info.neighbor, seq);
    sendSIAReply(*info.neighbor, seq);
}

void ReliableTransport::processReply(RTPInfo& info)
{
    Neighbor* nbr = info.neighbor;
    if (!nbr) return;

    const uint32_t seq = info.eigrp.getSequence();
    if (seq < nbr->lastSeqRecv)
        return;

    nbr->lastSeqRecv.store(seq, std::memory_order_release);

    sendAck(*nbr, seq);

    std::vector<ReceivedRoute> receivedRoutes;
    for (const auto& opt : info.opts)
    {
        if (auto route = TLVBuilder::decodeRoute(opt, iface.interfaceKey); route)
            receivedRoutes.emplace_back(std::move(*route));
    }

    if (!receivedRoutes.empty())
        iface.getTopController().processReceivedActiveRoutes(receivedRoutes, *info.neighbor);
}

void ReliableTransport::processSIAReply(RTPInfo& info)
{
    if (!info.neighbor) return;

    const uint32_t seq = info.eigrp.getSequence();
    if (seq < info.neighbor->lastSeqRecv.load(std::memory_order_relaxed))
        return;

    info.neighbor->lastSeqRecv.store(seq, std::memory_order_release);
    sendAck(*info.neighbor, seq);

    iface.getTopController().processSIAReply(*info.neighbor, seq);
}
}
