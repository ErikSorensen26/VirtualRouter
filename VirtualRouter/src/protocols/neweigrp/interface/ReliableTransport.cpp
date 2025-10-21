// ReliableTransport.cpp

#include "ReliableTransport.h"

namespace Protocol
{
void EigrpInterface::handleIncoming(const EigrpHeader& eigrpPacket, const uint8_t* neighborIp, bool multicast)
{
    EigrpConfigs::NeighborState neighborState;
    IPAddress neigIp(neighborIp, eigrpProcess.addressFamily);
    // Check if passive

    {
        if (configs->isPassive.load(std::memory_order_relaxed))
        {
            Logger::getInstance().info() << "Interface is passive. Incoming EIGRP packet ignored." << std::endl;
            return;
        }
    }

    // Validate packet version
    if (eigrpPacket.raw->version != 0x02)
    {
        // Version not valid
        return;
    }

    // Check for valid neighbor 
    EigrpConfigs::NeighborInfo* neighbor = nullptr;
    {
        // Neighbor mutex for save access
        std::shared_lock<std::shared_mutex> lock(neighborMutex);
        auto neighborIt = neighbors.find(neigIp);
        if (neighborIt != neighbors.end())
        {
            neighbor = neighborIt->second;
        }
    }

    if (neighbor && (eigrpPacket.getOpcode() != Variable::Eigrp::Type::hello || eigrpPacket.getAck() != 0))
    {
        neighborState = neighbor->neighborState.load(std::memory_order_relaxed);
    }

    // Change neighbor state if needed
    if (neighbor)
    {
        if (neighborState == EigrpConfigs::NeighborState::EXSTART)
        {
            changeNeighborState(neighbor, neigIp, EigrpConfigs::NeighborState::EXCHANGE);
        }
    }

    if (eigrpPacket.getOpcode() == Variable::Eigrp::Type::hello)
    {
        if (eigrpPacket.getAck() == 0)
        {
            processHello(neighbor, eigrpPacket, neigIp, !multicast);
        }
        else if (neighbor)
        {
            processAck(neighbor, eigrpPacket.getAck());
        }
    }
    else if (neighbor)
    {
        switch (eigrpPacket.getOpcode())
        {
            case Variable::Eigrp::Type::update:
                processUpdate(neighbor, eigrpPacket);
                break;
            case Variable::Eigrp::Type::reply:
                processReply(neighbor, neigIp, eigrpPacket);
                break;
            case Variable::Eigrp::Type::siaReply:
                processSIAReply(neighbor, neigIp, eigrpPacket);
                break;
            case Variable::Eigrp::Type::query:
                processQuery(neighbor, eigrpPacket, neigIp);
                break;
            case Variable::Eigrp::Type::siaQuery:
                processSIAQuery(neighbor, eigrpPacket, neigIp);
                break;
            default:
                return;
        }
    }
}

uint32_t EigrpInterface::getNextSequenceNumber() 
{
    return nextSequenceNumber.fetch_add(1, std::memory_order_acquire);
}

void EigrpInterface::processHello(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedHello, const IPAddress& neighborIp, bool unicast)
{
    // Neighor values if needed
    bool neighborAdded = false;
    
    // Checks if point-to-point is configured
    {
        if (configs->interfaceMode.load(std::memory_order_relaxed) == EigrpConfigs::Mode::POINT_TO_POINT)
        {
            // In point-to-point mode, ensure there's only one neighbor
            if (!neighbors.empty() && neighbors.find(neighborIp) == neighbors.end())
            {
                return;
            }
        }
    }

    // Validate the Autonomous System Number (ASN)
    if (receivedHello.getAutonomousSystem() != eigrpProcess.asNumber)
    {
        // Drop the packet - AS number mismatch
        return;
    }

    // Extract Hold Time
    uint16_t recievedHoldTime = configs->holdTime.load(std::memory_order_relaxed); // Default holdtime.

    // Safely access or create the neighbor
    if (!neighbor && !unicast)
    {
        std::unique_lock<std::shared_mutex> intLock(neighborMutex);
        neighbor = new EigrpConfigs::NeighborInfo(eigrpProcess.addressFamily, eigrpProcess.routingInstance->global.timeManager, neighborIp);

        neighbors[neighborIp] = neighbor;
        neighborAdded = true;
        std::lock_guard<std::mutex> globalLock(eigrpProcess.neighborMutex);
        eigrpProcess.allNeighbors[neighborIp] = neighbor;
    }
    else if (!neighbor && unicast)
    {
        return;
    }
    else if (neighbor && neighbor->holdTimerId.load(std::memory_order_relaxed) == 0) // If hold timer is not running
    {
        neighborAdded = true;
    }

    std::optional<TLV16Option> authOpt = std::nullopt;

    std::vector<TLV16Option> options;
    auto trail = receivedHello.getTrail();
    parseEigrpOptions(trail.data(), trail.size(), options);

    // Process TLVs
    for (const auto& opt : options)
    {
        if (opt.type == Variable::Eigrp::Option::parameter)
        {
            uint8_t parameters[6];
            eigrpProcess.calculateParameters(parameters, recievedHoldTime);
            if (std::memcmp(parameters, opt.value, 6) != 0) return; // Drop the packet if parameters mismatch

            // Extract Holdtime
            recievedHoldTime = readU16(opt.value + 6);

            // Check for Peer Termination
            if (std::memcmp(opt.value, /*Filled bytes->*/Variable::Mac::broadcast, 5))
            {
                handleNeighborDown(neighbor, neighborIp);
                return;
            }
        }
        else if (opt.type == Variable::Eigrp::Option::sequence)
        {
            if (std::memcmp(opt.value + 1, neighborIp.raw, opt.value[0]) != 0)
                return; // Drop packet if the IPs doesn't match
        }
        else if (opt.type == Variable::Eigrp::Option::multicastSequence)
        {
            neighbor->initSequence = readU32(opt.value);
        }
        else if (opt.type == Variable::Eigrp::Option::authentication)
        {
            authOpt = opt;
            break;
        }
    }

    // Validate Authentication
    if (neighbor && configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
    {
        if (!authOpt.has_value()) return;
        EigrpHeader tempHeader = receivedHello;
        //auto authTLV = generateAuthenticatedTLV(tempHeader); //TODO
        //if (authTLV.value != authOpt->value) return;
    }

    // Update neighbor fields and start/renew hold timers
    if (neighborAdded)
    {
        neighbor->holdTime = recievedHoldTime;
        neighbor->lastHeard = std::chrono::steady_clock::now();
        neighbor->lastReceivedSequenceNumber = 1;
        neighbor->srtt = 1.0;
        neighbor->rttvar = 0.5;
        neighbor->rto = 1.5;
    }

    // Cancel existing hold timer
    if (neighbor && neighbor->holdTimerId.load(std::memory_order_relaxed) != 0)
    {
        if (eigrpProcess.routingInstance->global.timeManager.cancelTimer(neighbor->holdTimerId))
        {
            changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::EXSTART);
        }
    }
    startHoldTimer(neighbor, neighborIp, neighbor->holdTime);

    // Safely extract the neighbor state
    EigrpConfigs::NeighborState neighborState = neighbor->neighborState.load(std::memory_order_relaxed);

    if (neighborState == EigrpConfigs::NeighborState::DOWN)
    {
        changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::INIT);
    }
    else if (neighborState == EigrpConfigs::NeighborState::TWOWAY)
    {
        neighbor->secondHelloReceived.store(true, std::memory_order_release);
        // Cancel existing Time if it exists and change states
        if (neighbor->twoWayThreadID.load(std::memory_order_relaxed) != 0)
        {
            eigrpProcess.routingInstance->global.timeManager.cancelTimer(neighbor->holdTimerId.load(std::memory_order_relaxed));
        }
    }
}

void EigrpInterface::processUpdate(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedUpdate)
{
    // Get neighbor Ip
    IPAddress neighborIp;
    IPAddress interfaceIp;
    bool initComplete = neighbor->initComplete.load(std::memory_order_relaxed);
    bool processAcks = neighbor->processAcks.load(std::memory_order_relaxed);
    if (currentInterface->shutdownFlag.load(std::memory_order_relaxed))
    {
        interfaceIp = getInterfaceIp();
    }
    {
        std::shared_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
        neighborIp = neighbor->ipAddress;
    }
    
    if (neighborIp == interfaceIp)
    {
        return; // Neighbor IP invalid
    }

    // Extract sequence number
    uint32_t receivedSequenceNumber = receivedUpdate.getSequence();

    // Handle non-stop-forwarding
    if (neighbor->isGracfullyRestarting.load(std::memory_order_relaxed) && neighbor->lastReceivedSequenceNumber + 1 == receivedSequenceNumber)
    {
        eigrpProcess.routingInstance->global.timeManager.cancelTimer(neighbor->gracefulRestartTimerId.load(std::memory_order_relaxed));
        neighbor->gracefulRestartTimerId.store(0, std::memory_order_release);
    }

    // Update flags
    bool initReceived = false;
    bool endOfTable = false;
    bool conditionalReceive = false;
    if (receivedUpdate.getFlagRestart()) {}
    if (receivedUpdate.getFlagInit()) { neighbor->initSequence.store(receivedSequenceNumber); initReceived = true; }
    if (receivedUpdate.getFlagCondRecv()) { conditionalReceive = true; }
    if (receivedUpdate.getFlagEndOfTable()) { endOfTable = true; }

    // Validate sequence number
    {
        uint32_t lastReceivedSequenceNumber = neighbor->lastReceivedSequenceNumber.load(std::memory_order_relaxed);

        if (receivedSequenceNumber <= lastReceivedSequenceNumber && !processAcks)
        {
            sendAckToNeighbor(neighbor, neighborIp, neighbor->lastReceivedSequenceNumber);
        }
        else if (receivedSequenceNumber > lastReceivedSequenceNumber + 1)
        {
            // Buffer out of order packet
            std::unique_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
            neighbor->packetBuffer[receivedSequenceNumber] = EigrpConfigs::NeighborInfo::PacketBuffer{.neighborIp = neighborIp, .eigrp = receivedUpdate}; //TODO copy packet to buffer
        }

        // Validate init sequence
        if (initReceived)
        {
            neighbor->initSequence = receivedSequenceNumber;
        }
            
        neighbor->lastReceivedSequenceNumber = receivedSequenceNumber;
    }

    // Acknowledge packet
    sendAckToNeighbor(neighbor, neighborIp, receivedSequenceNumber);


    // Determine roles based on sequence numbers
    if (!initComplete)
    {
        std::lock_guard<std::mutex> initLock(neighbor->initFlagMutex);
        if (receivedSequenceNumber < nextSequenceNumber)
        {
            neighbor->initFlags.initRole = EigrpConfigs::InitRole::MASTER;
            Logger::getInstance().info(true) << "MASTER role has been chosen with sequence number: " << nextSequenceNumber << " and neighbors: " << receivedSequenceNumber << "." << std::endl;
        }
        else if (receivedSequenceNumber > nextSequenceNumber)
        {
            neighbor->initFlags.initRole = EigrpConfigs::InitRole::SLAVE;
            Logger::getInstance().info(true) << "SLAVE role has been chosen with sequence number: " << nextSequenceNumber << " and neighbors: " << receivedSequenceNumber << "." << std::endl;
        }
        else if (neighbor->lastReceivedSequenceNumber == nextSequenceNumber)
        {
            // Use router ID as a tie-breaker
            Logger::getInstance().info(true) << "Sequence numbers equal. Using Router ID as tie-breaker." << std::endl;
            if (eigrpProcess.getRouterID() > neighbor->routerID)
            {
                neighbor->initFlags.initRole = EigrpConfigs::InitRole::MASTER;
                Logger::getInstance().info(true) << "Tie-breaker determined: MASTER." << std::endl;
            }
            else
            {
                neighbor->initFlags.initRole = EigrpConfigs::InitRole::SLAVE;
                Logger::getInstance().info(true) << "Tie-breaker determined: SLAVE." << std::endl;
            }
        }
        neighbor->initComplete.store(true, std::memory_order_release);
        initComplete = true;
    }

    // Process Ack if present
    if (receivedUpdate.getSequence() != 0 && processAcks)
    {
        processAck(neighbor, receivedUpdate.getAck());
    }

    // Collect routes for batch processing
    {
        std::lock_guard<std::mutex> lock(bufferMutex);
        auto trail = receivedUpdate.getTrail();
        std::vector<TLV16Option> options;
        parseEigrpOptions(trail.data(), trail.size(), options);
        for (const auto &option : options)
        {
            if (option.type == Variable::Eigrp::Option::internalRoute ||
                option.type == Variable::Eigrp::Option::internalRouteV6 ||
                option.type == Variable::Eigrp::Option::externalRoute ||
                option.type == Variable::Eigrp::Option::externalRouteV6)
            {
                RoutingTable::Eigrp* route = decodeRoute(option.value, option.valueSize, (option.type == Variable::Eigrp::Option::externalRoute), false);
                route->nextHop = neighborIp;

                routeBuffer.emplace_back(route, route->delay == 0xFFFFFFFF);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(bufferMutex);
        if (!routeBuffer.empty())
        {
            recordRouteChange();
            updateRoutingTable(neighbor, neighborIp, routeBuffer);
            size_t neighborAmount;
            {
                std::lock_guard<std::mutex> globalLock(eigrpProcess.neighborMutex);
                neighborAmount = eigrpProcess.allNeighbors.size();
            }
            if (neighborAmount == 1 && !neighbor->isInit && !routeBuffer.empty())
            {
                // Reject routes if this is the only neighbor
                for (auto& route : routeBuffer)
                {
                    route.withdraw = true;
                }
                sendUpdateToNeighbor(neighbor, routeBuffer, EigrpConfigs::UpdateType::PARTIAL);
            }

            routeBuffer.clear();
        }
    }

    // Check and process buffered packets
    processBufferedPackets(neighbor);

    bool slaveInit;
    EigrpConfigs::InitRole initRole;
    {
        std::lock_guard<std::mutex> lock(neighbor->initFlagMutex);
        slaveInit = neighbor->initFlags.slaveInit;
        initRole = neighbor->initFlags.initRole;
    }

    // Safely access neighbor state
    if (initComplete && neighbor->neighborState == EigrpConfigs::NeighborState::EXSTART && initRole == EigrpConfigs::InitRole::SLAVE && !slaveInit)
    {
        changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::EXCHANGE);
    }
}

void EigrpInterface::processBufferedPackets(EigrpConfigs::NeighborInfo* neighbor)
{
    if (!neighbor) return; // neighbor is invalid
    
    uint32_t nextExpectedSequence = neighbor->lastReceivedSequenceNumber + 1;

    {
        std::lock_guard<std::mutex> lock(neighbor->bufferMutex);
        while (true)
        {
            auto packetIt = neighbor->packetBuffer.find(nextExpectedSequence);

            if (packetIt != neighbor->packetBuffer.end())
            {
                // Process buffered packet
                EigrpConfigs::NeighborInfo::PacketBuffer bufferedPacket = packetIt->second;
                neighbor->packetBuffer.erase(packetIt);
                neighbor->lastReceivedSequenceNumber = nextExpectedSequence;

                // Unlock before processing the packet
                processUpdate(neighbor, bufferedPacket.eigrp);
            }
            else if (isTimeoutForMissing(neighbor, nextExpectedSequence))
            {
                Logger::getInstance().info() << "Timeout for missing packet with sequence number: "
                                             << nextExpectedSequence << ". Moving forward." << std::endl;
                neighbor->lastReceivedSequenceNumber.store(nextExpectedSequence, std::memory_order_release);
            }
            else
            {
                break;
            }
            
            // Move to the next sequence number
            nextExpectedSequence++;
        }
    }
}

void EigrpInterface::processAck(EigrpConfigs::NeighborInfo* neighbor, const uint32_t sequenceNumber)
{
    // Ensure the neighbor is valid
    if (!neighbor && !neighbor->processAcks.load(std::memory_order_relaxed))
    {
        Logger::getInstance().warn() << "Invalid neighbor passed to processAck." << std::endl;
        return;
    }

    EigrpConfigs::NeighborInfo::ReliablePacketInfo reliablePacketCopy;
    {
        // Locate the acknowledgement packet in reliablePacket
        std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
        auto pktIt = neighbor->reliablePackets.find(sequenceNumber);

        if (pktIt == neighbor->reliablePackets.end())
        {
            return;
        }

        // Cancel the Retransmission Timer
        if (pktIt->second.timerId != 0)
        {
            eigrpProcess.routingInstance->global.timeManager.cancelTimer(pktIt->second.timerId);
        }

        // Copy data before unlocking
        reliablePacketCopy = pktIt->second;

        // Erase the packet while locked
        std::erase_if(neighbor->reliablePackets, [&](const auto& entry) {
            return entry.first == sequenceNumber;
        });
    }
    

    // Handle routes associated with the acknowledged packet
    for (const auto& route : reliablePacketCopy.packet.updatedRoutes)
    {
        IPPrefix key(route.route->network.raw, route.route->mask, eigrpProcess.addressFamily);

        {
            std::unique_lock<std::shared_mutex> routeLock(neighbor->neighborDataMutex);
            auto advertIt = neighbor->advertisedRoutes.find(key);

            if (route.withdraw)
            {
                // Fully remove routes marked for removal
                if (advertIt != neighbor->advertisedRoutes.end() && advertIt->second.removePending)
                {
                    neighbor->advertisedRoutes.erase(advertIt);
                }
            }
            else
            {
                // Update or add routes
                if (advertIt == neighbor->advertisedRoutes.end())
                {
                    neighbor->advertisedRoutes[key] = {route.route, true, false, false};
                }
                else
                {
                    advertIt->second.active = true;
                    advertIt->second.pendingUpdate = false;
                }
            }
        }
    }

    // Update RTT and RTO Estimates if Necessary
    updateRTTEstimate(neighbor, sequenceNumber);
}

void EigrpInterface::processQuery(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedQuery, const IPAddress& neighborIp)
{
    if (!neighbor) return; // Neighbor does not exist

    uint32_t receivedSequenceNumber = receivedQuery.getSequence();

    // Ensure correct last received sequence tracking
    if (receivedSequenceNumber < neighbor->lastReceivedSequenceNumber.load(std::memory_order_relaxed))
        return; // Ignoring duplicate sequence number;

    std::vector<RoutingTable::Eigrp*> queriedRoutes;

    auto trail = receivedQuery.getTrail();
    std::vector<TLV16Option> options;
    parseEigrpOptions(trail.data(), trail.size(), options);

    for (const auto& option : options)
    {
        if (option.type == Variable::Eigrp::Option::internalRoute ||
            option.type == Variable::Eigrp::Option::internalRouteV6)
            queriedRoutes.push_back(decodeRoute(option.value, option.valueSize, false, false));
        else if (option.type == Variable::Eigrp::Option::externalRoute ||
            option.type == Variable::Eigrp::Option::externalRouteV6)
            queriedRoutes.push_back(decodeRoute(option.value, option.valueSize, true, false));
    }
    
    sendAckToNeighbor(neighbor, neighborIp, receivedQuery.getSequence());

    std::vector<RoutingTable::Eigrp*> knownQueryRoutes;
    std::vector<RoutingTable::Eigrp*> knownRoutes;
    std::vector<RoutingTable::Eigrp*> unknownQueryRoutes;

    for (const auto& queriedRoute : queriedRoutes)
    {
        // Check if the queried route exists
        auto existingRoute = eigrpProcess.routingInstance->routingTable.getEigrpRoute(queriedRoute->network.raw, queriedRoute->mask, eigrpProcess.addressFamily, eigrpProcess.asNumber);

        // Check summary routes
        if (!existingRoute)
        {
            std::shared_lock<std::shared_mutex> lock(configs->configsMutex);
            for (const auto& summaryRoute : configs->summaryRoutes) {
                if (Functions::isSubnetOf(queriedRoute->network.raw, queriedRoute->mask, summaryRoute.summary->network.raw, summaryRoute.summary->mask, eigrpProcess.addressFamily))
                    existingRoute = summaryRoute.summary;
            }
        }

        if (existingRoute && existingRoute->nextHop != neighborIp)
        {
            knownQueryRoutes.push_back(queriedRoute);
            knownRoutes.push_back(existingRoute);
        }
        else
        {
            unknownQueryRoutes.push_back(queriedRoute);
        }
    }

    if (!knownQueryRoutes.empty() && !knownRoutes.empty())
    {
        // Route is know, send a reply immediately
        sendReplyToNeighbor(neighbor, neighborIp, knownQueryRoutes, knownRoutes, receivedSequenceNumber);
        for (auto route : knownQueryRoutes) { delete route; }
        return;
    }

    // Indicates no neighbors are available
    bool noNeighbors = true;
    {
        std::shared_lock<std::shared_mutex> intLock(eigrpProcess.interfaceMutex);
        for (const auto& [_, interface] : eigrpProcess.eigrpInterfaceList)
        {
            for (const auto& [ip, otherNeighbor] : interface->neighbors)
            {
                if (ip == neighborIp) continue;

                noNeighbors = false;

                interface->sendQueryToNeighbor(otherNeighbor, ip, {unknownQueryRoutes});
            }
        };
    }

    // If no neighbors are available, send an empty reply
    if (noNeighbors)
    {
        for (auto* route : queriedRoutes) { route->delay = std::numeric_limits<uint32_t>::max(); }
        sendReplyToNeighbor(neighbor, neighborIp, queriedRoutes, queriedRoutes, receivedSequenceNumber);
        for (auto* route : queriedRoutes) { 
            delete route; 
        }
        return;
    }

    // Store this query in our global tracker
    for (auto query : unknownQueryRoutes)
    {
        IPPrefix queryKey(query->network.raw, query->mask, eigrpProcess.addressFamily);

        if (!eigrpProcess.configs.activeDisabled)
        {
            startActiveTimer(query);
        }
    }
    
    // Delete remaining queried routes
    for (auto route : queriedRoutes) { 
        delete route; 
    }
    queriedRoutes.clear();
}

void EigrpInterface::processSIAQuery(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedQuery, const IPAddress& neighborIp)
{
    if (!neighbor) return; // Neighbor does not exist

    uint32_t receivedSequenceNumber = receivedQuery.getSequence();

    if (receivedSequenceNumber < neighbor->lastReceivedSequenceNumber.load(std::memory_order_relaxed))
        return;

    sendAckToNeighbor(neighbor, neighborIp, receivedSequenceNumber);

    sendSIAReplyToNeighbor(neighbor, neighborIp, receivedSequenceNumber);
}

void EigrpInterface::processReply(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const EigrpHeader& receivedReply)
{
    if (!neighbor) return; // Neighbor invalid

    uint32_t receivedSequenceNumber = receivedReply.getSequence();

    if (receivedSequenceNumber < neighbor->lastReceivedSequenceNumber.load(std::memory_order_relaxed))
        return;

    neighbor->lastReceivedSequenceNumber.store(receivedSequenceNumber, std::memory_order_release);

    // Send ack for the received reply
    sendAckToNeighbor(neighbor, neighborIp, receivedSequenceNumber);

    std::vector<RoutingTable::Eigrp*> receivedRoutes;

    auto trail = receivedReply.getTrail();
    std::vector<TLV16Option> options;
    parseEigrpOptions(trail.data(), trail.size(), options);
    for (const auto& option : options)
    {
        if (option.type == Variable::Eigrp::Option::internalRoute ||
            option.type == Variable::Eigrp::Option::internalRouteV6)
        {
            receivedRoutes.push_back(decodeRoute(option.value, option.valueSize, false, false));
        }
        else if (option.type == Variable::Eigrp::Option::externalRoute ||
            option.type == Variable::Eigrp::Option::externalRouteV6)
        {
            receivedRoutes.push_back(decodeRoute(option.value, option.valueSize, true, true));
        }
    }

    if (receivedRoutes.empty()) return;

    for (RoutingTable::Eigrp* route : receivedRoutes)
    {
        IPPrefix key(route->network.raw, route->mask, eigrpProcess.addressFamily);
        auto it = eigrpProcess.outstandingReplies.find(key);
        if (it == eigrpProcess.outstandingReplies.end()) {
            delete route; // no query outstanding for this prefix
            continue;
        }

        auto& queryInfo = it->second;
        auto pendingIt = queryInfo.pendingQueries.find(neighborIp);
        if (pendingIt == queryInfo.pendingQueries.end() ||
            pendingIt->second.sequenceNumber != receivedReply.getAck()) {
            delete route;
            continue;
        }

        // cancel SIA timer
        eigrpProcess.routingInstance->global.timeManager.cancelTimer(pendingIt->second.siaTimerId);
        queryInfo.pendingQueries.erase(pendingIt);

        RoutingTable::Eigrp* current = eigrpProcess.routingInstance->routingTable.getEigrpRoute(
            route->network.raw, route->mask, eigrpProcess.addressFamily, eigrpProcess.asNumber);

        if (route->delay != std::numeric_limits<uint32_t>::max() &&
            isFeasibleSuccessor(route, current)) {
            queryInfo.feasibleRoutes.push_back({neighbor, neighborIp, route, false});
        } else {
            queryInfo.feasibleRoutes.push_back({neighbor, neighborIp, queryInfo.route, /*poisen*/true});
            delete route; // only keep queryInfo.route poisoned
        }

        // finish processing this outstanding reply if all queries resolved
        if (queryInfo.pendingQueries.empty()) {
            RoutingTable::Eigrp* bestRoute = nullptr;
            bool remove = false;

            if (!queryInfo.feasibleRoutes.empty()) {
                std::sort(queryInfo.feasibleRoutes.begin(), queryInfo.feasibleRoutes.end(),
                    [](const auto& a, const auto& b) {
                        return std::get<2>(a)->feasibleDistance < std::get<2>(b)->feasibleDistance;
                    });
                bestRoute = std::get<2>(queryInfo.feasibleRoutes.front());
            } else {
                remove = true;
            }

            // Send reply back to originator of query (if any)
            if (queryInfo.originNeighbor.v6 != 0) {
                std::shared_lock lock(eigrpProcess.interfaceMutex);
                for (const auto& [_, iface] : eigrpProcess.eigrpInterfaceList) {
                    if (iface->neighbors.count(queryInfo.originNeighbor)) {
                        iface->sendReplyToNeighbor(
                            iface->neighbors[queryInfo.originNeighbor], queryInfo.originNeighbor,
                            { bestRoute ? bestRoute : queryInfo.route },
                            { bestRoute ? bestRoute : queryInfo.route },
                            queryInfo.sequenceNumber
                        );
                    }
                }
            }

            if (remove) {
                RoutingTable::Eigrp* current = eigrpProcess.routingInstance->routingTable.getEigrpRoute(
                    queryInfo.route->network.raw, queryInfo.route->mask,
                    eigrpProcess.addressFamily, eigrpProcess.asNumber);
                if (current != queryInfo.route)
                    delete queryInfo.route;
                eigrpProcess.routingInstance->routingTable.removeEigrp(
                    queryInfo.route->network.raw, queryInfo.route->mask,
                    eigrpProcess.addressFamily, eigrpProcess.asNumber);
            }

            for (const auto& r : queryInfo.feasibleRoutes)
                updateRoutingTable(std::get<0>(r), std::get<1>(r), {{std::get<2>(r), std::get<3>(r)}});

            cancelActiveTimer(queryInfo.route->network, queryInfo.route->mask);

            for (const auto& pq : queryInfo.pendingQueries)
                eigrpProcess.routingInstance->global.timeManager.cancelTimer(pq.second.siaTimerId);

            eigrpProcess.outstandingReplies.erase(it);
        }
    }

}

void EigrpInterface::processSIAReply(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const EigrpHeader& receivedReply)
{
    if (!neighbor) return; // Neighbor invalid

    uint32_t receivedSequenceNumber = receivedReply.getSequence();

    if (receivedSequenceNumber < neighbor->lastReceivedSequenceNumber.load(std::memory_order_relaxed))
        return;

    // Send ack for the received reply
    sendAckToNeighbor(neighbor, neighborIp, receivedSequenceNumber);

    auto now = std::chrono::steady_clock::now();

    for (auto& [key, queryInfo] : eigrpProcess.outstandingReplies)
    {
        if (queryInfo.pendingQueries.count(neighborIp))
        {
            auto& outgoing = queryInfo.pendingQueries[neighborIp];

            if (outgoing.sequenceNumber != receivedReply.getAck())
                continue; // Not matching sequence number

            eigrpProcess.routingInstance->global.timeManager.cancelTimer(outgoing.siaTimerId);
            startSIATimer(queryInfo.route, neighborIp, outgoing);

            outgoing.lastSIARefreshTime = now;
        }
    }
}

void EigrpInterface::sendHelloPacket(EigrpConfigs::NeighborInfo* neighbor, bool unicast, bool update, uint32_t sequenceNumber)
{
    if (destroy.load(std::memory_order_seq_cst))
    {
        return;
    }


    if (configs->isPassive.load(std::memory_order_relaxed))
    {
        Logger::getInstance().info() << "Interface is passive. Hello packet not sent." << std::endl;
        return;
    }

    uint8_t const* targetIp;
    
    {
        if (neighbor && unicast)
        {
            std::shared_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
            targetIp = neighbor->ipAddress.raw;
        }
        else if (configs->multicastEnabled.load(std::memory_order_relaxed))
        {
            targetIp = getMulticast();
        }
    }

    PacketBuilder eigrpHello(currentInterface);

    eigrpProcess.eigrpHello(eigrpHello, *this, targetIp, sequenceNumber, false, update);

    auto interface = currentInterface;
    if (!interface || interface->shutdownFlag.load(std::memory_order_acquire) || destroy.load(std::memory_order_acquire))
    {
        return;
    }

    IPPacket::BuildIP build = {
        .iface = currentInterface,
        .packetInfo = eigrpHello,
        .destIp = targetIp,
        .DSCP = configs->DSCP.load(std::memory_order_relaxed),
        .protocolType = Variable::IP::eigrp
    };

    eigrpProcess.addressFamily == AddressFamily::IPv4
        ? IPPacket::buildIpv4(build)
        : IPPacket::buildIpv6(build);
}

void EigrpInterface::sendAckToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, uint32_t sequenceNumber)
{
    // Validate neighbor
    if (!neighbor && neighbor->processAcks.load(std::memory_order_release))
    {
        Logger::getInstance().warn() << "Invalid neighbor passed to processReply." << std::endl;
        return;
    }

    // Add the sequence number to pensing ACKs if not already present
    {
        std::unique_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
        neighbor->pendingAcks.insert(sequenceNumber);

        // if ACK processing is disabled, return early
        if (!neighbor->processAcks.load(std::memory_order_relaxed))
        {
            return;
        }

        // Process pending ACKs
        for (auto seq : neighbor->pendingAcks)
        {
            PacketBuilder eigrpAckPacketStructure(currentInterface);

            eigrpProcess.eigrpHello(eigrpAckPacketStructure, *this, neighborIp.raw, seq, /*ack=*/true);

            // Send the assembled ACK packet if it contains data
            if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
            {
                auto sourceIP = getInterfaceIp();
                IPPacket::BuildIP build = {
                    .iface = currentInterface,
                    .packetInfo = eigrpAckPacketStructure,
                    .destIp = neighborIp.raw,
                    .sourceIp = sourceIP.raw,
                    .DSCP = configs->DSCP.load(std::memory_order_relaxed),
                    .protocolType = Variable::IP::eigrp,
                };

                eigrpProcess.addressFamily == AddressFamily::IPv4
                    ? IPPacket::buildIpv4(build)
                    : IPPacket::buildIpv6(build);
            }
        }
        neighbor->pendingAcks.clear();
    }
}

void EigrpInterface::sendUpdateToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const std::vector<EigrpConfigs::RoutingUpdate> &routes, EigrpConfigs::UpdateType updateType, bool restart, bool conditional, std::vector<IPAddress> conditionalNeighbors)
{
    // Determine target IP based on communication mode
    IPAddress interfaceIp = getInterfaceIp();
    uint8_t const* targetIp;
    {
        if (neighbor)
        {
            targetIp = neighbor->ipAddress.raw;
        }
        else 
        {
            // Make sure interface has neigbors
            if (neighbors.empty())
            {
                return; // No neighbors to sent routes.
            }
            targetIp = getMulticast();
        }

        // Save sequence number for NULL Update
        if (neighbor && updateType == EigrpConfigs::UpdateType::QUERY)
        {
            neighbor->nullUpdateSequence.store(getNextSequenceNumber(), std::memory_order_release);
            std::lock_guard<std::mutex> lock(neighbor->initFlagMutex);
            neighbor->initFlags.nullSent = true;
        }
    }

    // Filter routes based on stub configuration and split horizon
    std::vector<EigrpConfigs::RoutingUpdate> filteredRoutes;
    for (const auto& routeUpdate : routes)
    {
        auto* route = routeUpdate.route;
        if (route->delay == 0xFFFFFFFF)
            continue;

        bool isSummarized = std::any_of(configs->summaryRoutes.begin(), configs->summaryRoutes.end(),
            [&](const auto& summary) { return Functions::isSubnetOf(route->network.raw, route->mask, summary.summary->network.raw, summary.summary->mask, eigrpProcess.addressFamily); });
        
        if (isSummarized) continue;

        // Stub Test
        // If the process is running in stub mode, only allow routes that are permitted.
        if (eigrpProcess.isStub() && 
            !((route->routeType == RoutingTable::Eigrp::RouteType::CONNECTED && eigrpProcess.advertiseConnected()) ||
            (route->routeType == RoutingTable::Eigrp::RouteType::STATIC && eigrpProcess.advertiseStatic()) ||
            (route->routeType == RoutingTable::Eigrp::RouteType::SUMMARY && eigrpProcess.advertiseSummary()) ||
            (route->routeType == RoutingTable::Eigrp::RouteType::EXTERNAL && eigrpProcess.advertiseRedistributed())))
            continue;

        // Split Horizon Test
        if (configs->splitHorizon.load(std::memory_order_relaxed))
        {
            std::shared_lock<std::shared_mutex> lock(neighborMutex);
            auto it = neighbors.find(route->nextHop);
            if (it != neighbors.end() && (route->nextHop == it->first || Functions::compareNetworkWithIp(route->network.raw, it->second->ipAddress.raw, route->mask, eigrpProcess.addressFamily)))
            {
                continue;
            }
            if (Functions::compareNetworkWithIp(route->network.raw, interfaceIp.raw, route->mask, eigrpProcess.addressFamily))
            {
                continue;
            }
        }
        // Check if route requires an update
        IPPrefix key(route->network.raw, route->mask, eigrpProcess.addressFamily);
        bool needsUpdate = true;

        for (const auto& [address, updateNeighbor] : neighbors)
        {
            auto advertIt = updateNeighbor->advertisedRoutes.find(key);
            if (advertIt != updateNeighbor->advertisedRoutes.end() &&
                !advertIt->second.pendingUpdate &&
                !advertIt->second.removePending)
            {
                needsUpdate = false;
                break;
            }
        }

        if (!needsUpdate)
            continue;

        // Passed all filters, include in output
        filteredRoutes.push_back(routeUpdate);
    }

    if ((updateType == EigrpConfigs::UpdateType::PARTIAL) && filteredRoutes.empty())
    {
        return;
    }

    uint32_t bandwidthMetric = (10000000 / currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed));
    uint32_t delay = currentInterfaceInfo->delay.load(std::memory_order_relaxed);
    auto it = filteredRoutes.begin();

    // Set if this is a withdraw update
    bool isConditional = false;
    bool endOfTable = false;
    bool isInit = false;

    bool authentication = configs->authKey.fullyEnabled.load(std::memory_order_relaxed);
    bool stub = eigrpProcess.isStub();

    PacketBuilder eigrpPacket(currentInterface);

    do
    {
        // Create the EIGRP Update packet structure
        eigrpPacket.clear(); // Clear previous packet if there was one

        // Reserve header space.
        eigrpProcess.addressFamily == AddressFamily::IPv4
            ? IPPacket::reserveIpv4(currentInterface, eigrpPacket)
            : IPPacket::reserveIpv6(currentInterface, eigrpPacket);
        eigrpPacket.reserveHeader(HeaderType::EIGRP, 0);

        // Validate header structure
        BuildEntry* nextHeader = eigrpPacket.nextBuildHeader();
        if (unlikely(!nextHeader)) return;

        // Set the eigrp buffer for building
        EigrpHeader eigrp;
        eigrp.setBuffer(nextHeader->buffer);
        uint8_t* trail = eigrp.getTrailData();
        
        // Calculate remaining space available for routes
        uint16_t mtuSize = eigrpProcess.addressFamily == AddressFamily::IPv4
            ? currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed)
            : currentInterface->configs.ipv6.mtu.load(std::memory_order_relaxed);
        uint16_t maxRouteSize;
        maxRouteSize = mtuSize - (eigrpPacket.bufferOffset + EigrpHeader::fixedSize + (authentication ? 128 : 0) + (stub ? 6 : 0));

        // Get the next sequence number
        uint32_t sequenceNumber = getNextSequenceNumber();

        // Options
        TLV16BufferManager options(trail, eigrpPacket.getMaxHeaderSize(mtuSize));

        // Add routes to the packet
        for (; it != filteredRoutes.end() && options.size() + 64 < maxRouteSize; ++it)
        {
            if (it->route->routeType == RoutingTable::Eigrp::RouteType::EXTERNAL)
            {
                auto buffer = options.getNextValBuf();
                if (!buffer) continue;
                size_t len = encodeExternalRouteOption(buffer, it->route, bandwidthMetric, delay, it->withdraw);
                options.append(
                    (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Eigrp::Option::externalRoute : Variable::Eigrp::Option::externalRouteV6,
                    len + 4,
                    nullptr,
                    len
                );
            }
            else
            {
                auto buffer = options.getNextValBuf();
                if (!buffer) continue;
                size_t len = encodeRouteOption(buffer, it->route, bandwidthMetric, delay, it->withdraw);
                options.append(
                    (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Eigrp::Option::internalRoute : Variable::Eigrp::Option::internalRouteV6,
                    len + 4,
                    nullptr,
                    len
                );
            }
        }
        
        // Set flags based on update type and stage
        endOfTable = (it == filteredRoutes.end());
        if (updateType == EigrpConfigs::UpdateType::QUERY)
        {
            isInit = true;
            isConditional = false;
            endOfTable = false;
        }

        if (conditional)
        {
            isConditional = true;
        }

        eigrpProcess.eigrpUpdate(eigrp, sequenceNumber, /*init=*/isInit, /*conditional=*/isConditional, /*restart=*/restart, /*endOfTable*/endOfTable/* && filteredRoutes.size() != 1*/);
        isInit = false;
        
        // Handle Acks and Authentication
        if (neighbor)
        {
            {
                std::unique_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
                if (!neighbor->pendingAcks.empty())
                {
                    eigrp.setAck(*neighbor->pendingAcks.begin());
                    neighbor->pendingAcks.erase(neighbor->pendingAcks.begin());
                }
            }

            // Add stub option
            if (eigrpProcess.isStub())
            {
                encodeStubOption(options.getNextValBuf(), eigrpProcess.configs.stubConfig);
                options.append(
                    Variable::Eigrp::Option::stub,
                    6, nullptr, 2
                );
            }
            // Add authentication TLV if enabled
            if (configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
            {
                auto opt = options.getNextValBuf(0);
                uint8_t authSize = generateAuthenticatedTLV(opt);
                options.append(Variable::Eigrp::Option::authentication, authSize, nullptr, authSize);
            }
        }

        eigrpPacket.addTLVSize(options.size());

        // Assemble and send the packet
        if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
        {
            IPPacket::BuildIP build = {
                .iface = currentInterface,
                .packetInfo = eigrpPacket,
                .destIp = targetIp,
                .DSCP = configs->DSCP.load(std::memory_order_relaxed),
                .protocolType = Variable::IP::eigrp
            };

            eigrpProcess.addressFamily == AddressFamily::IPv4
                ? IPPacket::buildIpv4(build)
                : IPPacket::buildIpv6(build);
        }
        
        if (!neighbor || (neighbor && neighbor->processAcks))
        {
            if (conditional)
            {
                eigrp.setFlagCondRecv(true);
                std::shared_lock<std::shared_mutex> lock(neighborMutex);
                for (const auto& [address, neighborPtr] : neighbors)
                {
                    std::vector<EigrpConfigs::RoutingUpdate> neighborSpecificRoutes;
                    for (const auto& route : filteredRoutes)
                    {
                        IPPrefix key(route.route->network.raw, route.route->mask, eigrpProcess.addressFamily);

                        {
                            std::unique_lock<std::shared_mutex> neighborLock(neighborPtr->neighborDataMutex);
                            auto advertIt = neighborPtr->advertisedRoutes.find(key);
                            
                            if (advertIt == neighborPtr->advertisedRoutes.end() || advertIt->second.pendingUpdate || advertIt->second.removePending)
                            {
                                neighborSpecificRoutes.emplace_back(route);
                            }
                        }
                    }
                    if (!neighborSpecificRoutes.empty())
                    {
                        setupReliablePacket(
                            neighborPtr,
                            address,
                            EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(
                                eigrpProcess.addressFamily,
                                nextHeader->buffer,
                                nextHeader->length,
                                address,
                                neighborSpecificRoutes
                            ), 
                            sequenceNumber
                        );
                    }
                }
            }
            else
            {
                for (const auto& [address, neighborPtr] : neighbors)
                {
                    setupReliablePacket(
                        neighborPtr,
                        address,
                        EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(
                            eigrpProcess.addressFamily,
                            nextHeader->buffer,
                            nextHeader->length,
                            address,
                            filteredRoutes
                        ),
                        sequenceNumber
                    );
                }
            }
        }
    }
    while (!endOfTable && updateType == EigrpConfigs::UpdateType::FULL);
}

void EigrpInterface::sendQueryToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, std::vector<RoutingTable::Eigrp*> failedRoutes)
{
    uint32_t bandwidthMetric = (10000000 / currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed));
    size_t it = 0;

    bool authentication = configs->authKey.fullyEnabled.load(std::memory_order_relaxed);

    PacketBuilder eigrpQueryPacketStructure(currentInterface);

    // Calculate remaining space available for routes
    uint16_t mtuSize = eigrpProcess.addressFamily == AddressFamily::IPv4
        ? currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed)
        : currentInterface->configs.ipv6.mtu.load(std::memory_order_relaxed);
    uint16_t maxRouteSize = mtuSize - (eigrpQueryPacketStructure.bufferOffset + EigrpHeader::fixedSize + (authentication ? 128 : 0));

    do
    {
        // Clear Eigrp Packet
        eigrpQueryPacketStructure.clear();

        // Reserve header space.
        eigrpProcess.addressFamily == AddressFamily::IPv4
            ? IPPacket::reserveIpv4(currentInterface, eigrpQueryPacketStructure)
            : IPPacket::reserveIpv6(currentInterface, eigrpQueryPacketStructure);
        eigrpQueryPacketStructure.reserveHeader(HeaderType::EIGRP, EigrpHeader::fixedSize);

        // Validate header structure
        BuildEntry* nextHeader = eigrpQueryPacketStructure.nextBuildHeader();

        // Set the eigrp buffer for building
        EigrpHeader eigrp;
        eigrp.setBuffer(nextHeader->buffer);
        uint8_t* trail = eigrp.getTrailData();

        // Increment sequence number for this route/query
        uint32_t sequenceNumber = getNextSequenceNumber();

        // Options
        TLV16BufferManager options(trail, maxRouteSize);
        std::vector<RoutingTable::Eigrp*> sentRoutes;

        // Construct the Query option
        for (; it < failedRoutes.size() && options.size() + 64 < maxRouteSize; it++)
        { 
            if (failedRoutes[it]->routeType == RoutingTable::Eigrp::RouteType::EXTERNAL)
            {
                auto buffer = options.getNextValBuf();
                if (!buffer) continue;
                size_t len = encodeExternalRouteOption(
                    buffer,
                    failedRoutes[it],
                    bandwidthMetric,
                    0xFFFFFFFF,
                    true
                );
                options.append(
                    (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Eigrp::Option::externalRoute : Variable::Eigrp::Option::externalRouteV6,
                    len + 4,
                    nullptr,
                    len
                );
            }
            else
            {
                auto buffer = options.getNextValBuf();
                if (!buffer) continue;
                size_t len = encodeRouteOption(
                    buffer,
                    failedRoutes[it],
                    bandwidthMetric,
                    0xFFFFFFFF,
                    true
                );
                options.append(
                    (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Eigrp::Option::internalRoute : Variable::Eigrp::Option::internalRouteV6,
                    len + 4,
                    nullptr,
                    len
                );
            }
            sentRoutes.push_back(failedRoutes[it]);
        }
        
        if (sentRoutes.empty()) return; // Loop prevention

        // Set other EIGRP header feilds
        eigrp.raw->version = 2;
        eigrp.setOpcode(Variable::Eigrp::Type::query);
        std::memset(eigrp.raw->checksum, 0, 2);
        eigrp.setFlagInit(false);
        eigrp.setFlagCondRecv(false);
        eigrp.setFlagRestart(false);
        eigrp.setFlagEndOfTable(false);
        eigrp.setSequence(sequenceNumber);
        eigrp.setAck(0);
        eigrp.setVirtualRouterId(eigrpProcess.getVirtualRouterID());
        eigrp.setAutonomousSystem(eigrpProcess.asNumber);

        // Generate and append Authentication TLV if enabled for this neighbor
        if (configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
        {
            auto opt = options.getNextValBuf(0);
            uint8_t authSize = generateAuthenticatedTLV(opt);
            options.append(Variable::Eigrp::Option::authentication, authSize, nullptr, authSize);
        }

        eigrpQueryPacketStructure.addTLVSize(options.size());

        // Convert to raw packet ByteString
        if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
        {
            IPPacket::BuildIP build = {
                .iface = currentInterface,
                .packetInfo = eigrpQueryPacketStructure,
                .destIp = neighborIp.raw,
                .DSCP = configs->DSCP.load(std::memory_order_relaxed),
                .protocolType = Variable::IP::eigrp
            };

            eigrpProcess.addressFamily == AddressFamily::IPv4
                ? IPPacket::buildIpv4(build)
                : IPPacket::buildIpv6(build);
        }

        // Store the packet for possible retransmission (relieable delivery)
        std::vector<EigrpConfigs::RoutingUpdate> formattedRoutes;
        for (const auto& route : sentRoutes)
        {
            formattedRoutes.emplace_back(route, true);
        }
        setupReliablePacket(
            neighbor,
            neighborIp,
            EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(
                eigrpProcess.addressFamily,
                nextHeader->buffer,
                nextHeader->length,
                neighborIp,
                formattedRoutes
            ),
            sequenceNumber
        );

        for (RoutingTable::Eigrp* route : sentRoutes)
        {
            IPPrefix key(route->network.raw, route->mask, eigrpProcess.addressFamily);

            if (eigrpProcess.outstandingReplies.find(key) == eigrpProcess.outstandingReplies.end())
            {
                EigrpConfigs::ActiveRoute& active = eigrpProcess.outstandingReplies[key];
                active.route = route;
                active.originNeighbor = neighborIp;
            }

            EigrpConfigs::ActiveRoute& active = eigrpProcess.outstandingReplies[key];

            EigrpConfigs::OutgoingQuery outgoing;
            outgoing.sequenceNumber = sequenceNumber;
            outgoing.lastSIARefreshTime = std::chrono::steady_clock::now();
            startSIATimer(route, neighborIp, outgoing);

            active.pendingQueries[neighborIp] = std::move(outgoing);
        }
    }
    while (it < failedRoutes.size());
}

void EigrpInterface::sendSIAQueryToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp)
{
    //TODO FINISH THIS
    sendQueryToNeighbor(neighbor, neighborIp, {});
}

void EigrpInterface::sendQueryToNeighbors(std::vector<RoutingTable::Eigrp*> failedRoutes)
{
    if (failedRoutes.empty()) return;

    for (const auto& [ip, neighbor] : neighbors)
    {
        sendQueryToNeighbor(neighbor, ip, failedRoutes);
    }
}

void EigrpInterface::sendReplyToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, std::vector<RoutingTable::Eigrp*> queryRoutes, std::vector<RoutingTable::Eigrp*> existingRoutes, uint32_t ackNumber)
{
    uint32_t bandwidthMetric = (10000000 / currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed));
    uint32_t delay = currentInterfaceInfo->delay.load(std::memory_order_relaxed);
    size_t it = 0;

    bool authentication = configs->authKey.fullyEnabled.load(std::memory_order_relaxed);

    PacketBuilder eigrpQueryPacketStructure(currentInterface);
        
    // Calculate remaining space available for routes
    uint16_t mtuSize = eigrpProcess.addressFamily == AddressFamily::IPv4
        ? currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed)
        : currentInterface->configs.ipv6.mtu.load(std::memory_order_relaxed);
    uint16_t maxRouteSize = mtuSize - (eigrpQueryPacketStructure.bufferOffset + EigrpHeader::fixedSize + (authentication ? 128 : 0));

    do
    {
        // Clear Eigrp Packet
        eigrpQueryPacketStructure.clear();

        // Reserve header space.
        eigrpProcess.addressFamily == AddressFamily::IPv4
            ? IPPacket::reserveIpv4(currentInterface, eigrpQueryPacketStructure)
            : IPPacket::reserveIpv6(currentInterface, eigrpQueryPacketStructure);
        eigrpQueryPacketStructure.reserveHeader(HeaderType::EIGRP, EigrpHeader::fixedSize);

        // Validate header structure
        BuildEntry* nextHeader = eigrpQueryPacketStructure.nextBuildHeader();

        // Set the eigrp buffer for building
        EigrpHeader eigrp;
        eigrp.setBuffer(nextHeader->buffer);
        uint8_t* trail = eigrp.getTrailData();

        // Increment sequence number for this route/query
        uint32_t sequenceNumber = getNextSequenceNumber();

        // Options
        TLV16BufferManager options(trail, maxRouteSize);
        std::vector<RoutingTable::Eigrp*> sentRoutes;

        // Construct the Query option
        for (; it < queryRoutes.size() && options.size() + 64 < maxRouteSize; it++)
        {
            RoutingTable::Eigrp combinedRoute = *existingRoutes[it];
            combinedRoute.network = queryRoutes[it]->network;
            combinedRoute.mask = queryRoutes[it]->mask;
            if (configs->nextHopSelf.load(std::memory_order_relaxed))
                combinedRoute.nextHop = getInterfaceIp();

            if (combinedRoute.routeType == RoutingTable::Eigrp::RouteType::EXTERNAL)
            {
                auto buffer = options.getNextValBuf();
                if (!buffer) continue;
                size_t len = encodeExternalRouteOption(
                    buffer,
                    &combinedRoute,
                    bandwidthMetric,
                    delay
                );
                options.append(
                    (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Eigrp::Option::externalRoute : Variable::Eigrp::Option::externalRouteV6,
                    len + 4,
                    nullptr,
                    len
                );
            }
            else
            {
                auto buffer = options.getNextValBuf();
                if (!buffer) continue;
                size_t len = encodeRouteOption(
                    buffer,
                    &combinedRoute,
                    bandwidthMetric,
                    delay
                );
                options.append(
                    (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Eigrp::Option::internalRoute : Variable::Eigrp::Option::internalRouteV6,
                    len + 4,
                    nullptr,
                    len
                );
            }
        }
        
        // Set other EIGRP header feilds
        eigrp.raw->version = 2;
        eigrp.setOpcode(Variable::Eigrp::Type::reply);
        std::memset(eigrp.raw->checksum, 0, 2); // Will be calculated later
        eigrp.setFlagInit(false);
        eigrp.setFlagCondRecv(false);
        eigrp.setFlagRestart(false);
        eigrp.setFlagEndOfTable(false);
        eigrp.setSequence(sequenceNumber);
        eigrp.setAck(ackNumber);
        eigrp.setVirtualRouterId(eigrpProcess.getVirtualRouterID());
        eigrp.setAutonomousSystem(eigrpProcess.asNumber);

        // Generate and append Authentication TLV if enabled for this neighbor
        if (configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
        {
            auto opt = options.getNextValBuf(0);
            uint8_t authSize = generateAuthenticatedTLV(opt);
            options.append(Variable::Eigrp::Option::authentication, authSize, nullptr, authSize);
        }

        eigrpQueryPacketStructure.addTLVSize(options.size());

        // Convert to raw packet ByteString
        if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
        {
            IPPacket::BuildIP build = {
                .iface = currentInterface,
                .packetInfo = eigrpQueryPacketStructure,
                .destIp = neighborIp.raw,
                .DSCP = configs->DSCP.load(std::memory_order_relaxed),
                .protocolType = Variable::IP::eigrp
            };

            eigrpProcess.addressFamily == AddressFamily::IPv4
                ? IPPacket::buildIpv4(build)
                : IPPacket::buildIpv6(build);
        }

        // Store the packet for possible retransmission (relieable delivery)
        eigrp.setAck(0);
        setupReliablePacket(
            neighbor,
            neighborIp,
            EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(
                eigrpProcess.addressFamily,
                nextHeader->buffer,
                nextHeader->length,
                neighborIp
            ),
            sequenceNumber
        );
    }
    while (it < queryRoutes.size());
}

void EigrpInterface::sendSIAReplyToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, uint32_t querySequence)
{
    // Validate neighbor
    if (!neighbor) return;
    
    PacketBuilder eigrpPacket(currentInterface);

    // Reserve header space.
    eigrpProcess.addressFamily == AddressFamily::IPv4
        ? IPPacket::reserveIpv4(currentInterface, eigrpPacket)
        : IPPacket::reserveIpv6(currentInterface, eigrpPacket);
    eigrpPacket.reserveHeader(HeaderType::EIGRP, 0);

    // Validate header structure
    BuildEntry* nextHeader = eigrpPacket.nextBuildHeader();

    // Set the eigrp buffer for building
    EigrpHeader eigrp;
    eigrp.setBuffer(nextHeader->buffer);

    uint32_t currentSeqNum = getNextSequenceNumber();

    eigrp.raw->version = 2;
    eigrp.setOpcode(Variable::Eigrp::Type::siaReply);
    std::memset(eigrp.raw->checksum, 0, 2);
    eigrp.setFlagInit(false);
    eigrp.setFlagCondRecv(false);
    eigrp.setFlagRestart(false);
    eigrp.setFlagEndOfTable(false);
    eigrp.setSequence(currentSeqNum);
    eigrp.setAck(querySequence);
    eigrp.setVirtualRouterId(eigrpProcess.getVirtualRouterID());
    eigrp.setAutonomousSystem(eigrpProcess.asNumber);

    size_t mtu = eigrpProcess.addressFamily == AddressFamily::IPv4
        ? currentInterfaceInfo->ipv4.mtu.load(std::memory_order_relaxed)
        : currentInterfaceInfo->ipv6.mtu.load(std::memory_order_relaxed);

    TLV16BufferManager options(eigrp.getTrail().data(), eigrpPacket.getMaxHeaderSize(mtu));
    // Generate and append Authentication TLV if enabled for this neighbor
    if (configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
    {
        auto opt = options.getNextValBuf(0);
        uint8_t authSize = generateAuthenticatedTLV(opt);
        options.append(Variable::Eigrp::Option::authentication, authSize, nullptr, authSize);
    }

    eigrpPacket.addTLVSize(options.size());

    if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
    {
        IPPacket::BuildIP build = {
            .iface = currentInterface,
            .packetInfo = eigrpPacket,
            .destIp = neighborIp.raw,
            .DSCP = configs->DSCP.load(std::memory_order_relaxed),
            .protocolType = Variable::IP::eigrp
        };

        eigrpProcess.addressFamily == AddressFamily::IPv4
            ? IPPacket::buildIpv4(build)
            : IPPacket::buildIpv6(build);
    }

    // Store the packet for possible retransmission (relieable delivery)
    eigrp.setAck(0);
    setupReliablePacket(
        neighbor,
        neighborIp,
        EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(
            eigrpProcess.addressFamily,
            nextHeader->buffer,
            nextHeader->length,
            neighborIp
        ),
        currentSeqNum
    );
}
}
