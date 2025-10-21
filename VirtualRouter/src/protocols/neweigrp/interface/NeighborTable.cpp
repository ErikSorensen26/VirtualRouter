// EigrpInterfaceNeighborManager.cpp

#include "EigrpInterfaceNeighborManager.h"

namespace Protocol
{
void EigrpInterface::setState(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, EigrpConfigs::NeighborState newState)
{
    // Make sure neighbor exists
    if (!neighbor) return; // Neighbor is null

    // Check for currect state in order to change state
    auto currentState = neighbor->neighborState.load(std::memory_order_relaxed);
    uint32_t seq = nextSequenceNumber.load(std::memory_order_relaxed);
    bool unicast = neighbor->unicast;

    if (neighbor->neighborState != EigrpConfigs::NeighborState::DOWN &&
        neighbor->neighborState != static_cast<EigrpConfigs::NeighborState>(static_cast<int>(newState) - 1)) return;

    if (currentState == newState) return;

    switch (newState)
    {
        case EigrpConfigs::NeighborState::INIT:
            Logger::getInstance().info(true) << "Neighbor in INIT state. Sending Hello." << std::endl;
            sendHelloPacket(neighbor, unicast);

            // Update INIT start time and start stuck detection thread
            neighbor->initStartTime = std::chrono::steady_clock::now();
            neighbor->stuckInInitCheckActive.store(true, std::memory_order_release);

            // Spawn a thread to check for "stuck in INIT"
            neighbor->stuckInInitTimerId = eigrpProcess.routingInstance->global.timeManager.addTimer(std::chrono::steady_clock::now() + std::chrono::seconds(neighbor->holdTime.load(std::memory_order_relaxed)), [this, neighbor, neighborIp]()
            {
                // Check if the neighbor is still in Initializing
                if (destroy.load(std::memory_order_acquire)) return;
                if (neighbor->neighborState < EigrpConfigs::NeighborState::LOADING)
                {
                    handleNeighborDown(neighbor, neighborIp);
                }
                else
                {
                    neighbor->stuckInInitCheckActive = false;
                }
            });

            // Update the neighbors state
            neighbor->neighborState.store(newState, std::memory_order_release);

            changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::TWOWAY);
            break;

        case EigrpConfigs::NeighborState::TWOWAY:
            Logger::getInstance().info(true) << "Neighbor in TWOWAY state." << std::endl;
            
            // Update the neighbors state
            neighbor->neighborState.store(newState, std::memory_order_release);
            
            // Start a seperate thread to monitor the second hello
            neighbor->twoWayThreadID.store(eigrpProcess.routingInstance->global.timeManager.addTimer(
                std::chrono::steady_clock::now() + std::chrono::milliseconds(300),
                [this, neighbor, neighborIp]()
                {
                    if (destroy.load(std::memory_order_acquire)) return;
                    changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::EXSTART);
                }
            ), std::memory_order_release);
            break;

        case EigrpConfigs::NeighborState::EXSTART:
            Logger::getInstance().info(true) << "Neighbor in EXSTART state. Waiting for role" << std::endl;
            
            // Send Null Update to initialize reliable communication
            Logger::getInstance().info(true) << "Sending Null Update to neighbor." << std::endl;
            
            bool nullSent;
            {
                std::lock_guard<std::mutex> initLock(neighbor->initFlagMutex);
                nullSent = neighbor->initFlags.nullSent;
            }

            if (!nullSent)
            {
                sendUpdateToNeighbor(neighbor, {}, EigrpConfigs::UpdateType::QUERY);
            }

            // Update the neighbors state
            neighbor->neighborState.store(newState, std::memory_order_release);

            // Move to EXCHANGE to start topology exchange
            changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::EXCHANGE);

            // Set the TWOWAY delay ID to 0 since the thread is done.
            neighbor->twoWayThreadID.store(0, std::memory_order_release);
            break;

        case EigrpConfigs::NeighborState::EXCHANGE:
            Logger::getInstance().info(true) << "Neighbor in EXCHANGE state. Sharing topology." << std::endl;

            bool slaveInit, masterInit, initUpdateReceived;
            EigrpConfigs::InitRole initRole;
            {
                std::lock_guard<std::mutex> initLock(neighbor->initFlagMutex);
                slaveInit = neighbor->initFlags.slaveInit;
                masterInit = neighbor->initFlags.masterInit;
                initUpdateReceived = neighbor->initFlags.initUpdateReceived;
                initRole = neighbor->initFlags.initRole;
            }
            
            if (slaveInit || masterInit)
            {
                neighbor->neighborState.store(newState, std::memory_order_release);
                changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::LOADING);
            }
            else if (initRole == EigrpConfigs::InitRole::MASTER)
            {
                if (!masterInit)
                {
                    {
                        std::lock_guard<std::mutex> initLock(neighbor->initFlagMutex);
                        neighbor->initFlags.masterInit = true;
                    }
                    
                    // Send Sequence Hello with the generated sequence number
                    Logger::getInstance().info(true) << "MASTER sending Sequence Hello with sequence number: " << seq << std::endl;
                    sendHelloPacket(neighbor, unicast, /*update=*/true, seq);

                    // Set state to loading
                    Logger::getInstance().info(true) << "MASTER sending full topology." << std::endl;

                    // Get all routes
                    auto allRotes = eigrpProcess.routingInstance->routingTable.getAllEigrpRoutes(eigrpProcess.addressFamily, eigrpProcess.asNumber);
                    std::vector<EigrpConfigs::RoutingUpdate> fullUpdate;
                    for (auto& route : allRotes)
                    {
                        fullUpdate.emplace_back(route, false);
                    }
                    // Send a conditional update to the neighbor
                    sendUpdateToNeighbor(neighbor, fullUpdate, EigrpConfigs::UpdateType::FULL, false, true, {neighborIp});

                    neighbor->neighborState.store(newState, std::memory_order_release);
                    changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::LOADING);
                }
            }
            else if (initRole == EigrpConfigs::InitRole::SLAVE && initUpdateReceived && !slaveInit)
            {
                // Send Null update to initialize reliable connections
                Logger::getInstance().info(true) << "Sending Null Update to neighbor." << std::endl;
                
                // Send Sequence Hello with the generated sequence number
                Logger::getInstance().info(true) << "Sending Sequence Hello with sequence number: " << seq << std::endl;
                sendHelloPacket(neighbor, /*unicast=*/unicast, /*update=*/true, seq);

                // Send your topology
                Logger::getInstance().info(true) << "Sending full topology." << std::endl;
                auto allRotes = eigrpProcess.routingInstance->routingTable.getAllEigrpRoutes(eigrpProcess.addressFamily, eigrpProcess.asNumber);
                std::vector<EigrpConfigs::RoutingUpdate> fullUpdate;
                for (auto& route : allRotes)
                {
                    fullUpdate.emplace_back(route, false);
                }
                // Send a conditional update to the neighbor
                sendUpdateToNeighbor(neighbor, fullUpdate, EigrpConfigs::UpdateType::FULL, false, true, {neighborIp});

                // Send a hello immediately after sending routes
                Logger::getInstance().info(true) << "Sending immediate Hello after full topology." << std::endl;
                sendHelloPacket(neighbor);

                neighbor->neighborState.store(newState, std::memory_order_release);
                changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::LOADING);
            }
            else if (initRole == EigrpConfigs::InitRole::SLAVE)
            {
                // Slave waits for masters topology
                Logger::getInstance().info(true) << "SLAVE waiting for Master's topology." << std::endl;
                return;
            }

            break;

        case EigrpConfigs::NeighborState::LOADING:
            Logger::getInstance().info(true) << "Neighbor in LOADING state.";

            neighbor->processAcks = true;

            // Update the neighbors state
            neighbor->neighborState.store(newState, std::memory_order_release);
            break;

        case EigrpConfigs::NeighborState::ESTABLISHED:
            Logger::getInstance().info(true) << "Neighbor in ESTABLISHED state. Adjacency fully formed." << std::endl;

            // Update the neighbors state
            neighbor->neighborState.store(newState, std::memory_order_release);
            break;
            
        default:
            break;
    }
}

void EigrpInterface::createNeighbor(const IPAddress& ipAddress, const uint8_t* macAddress, bool unicast)
{
    // Add neighbor only if it doesn't already exist
    std::unique_lock<std::shared_mutex> intLock(neighborMutex);

    auto it = neighbors.find(ipAddress);
    if (it == neighbors.end()) // Double check
    {
        auto* neighbor = new EigrpConfigs::NeighborInfo(
            eigrpProcess.addressFamily,
            eigrpProcess.routingInstance->global.timeManager,
            ipAddress,
            unicast
        );
        std::memcpy(neighbor->macAddress, macAddress, 6);
        neighbors[ipAddress] = neighbor;
        {
            std::lock_guard<std::mutex> globalLock(eigrpProcess.neighborMutex);
            eigrpProcess.allNeighbors[ipAddress] = neighbor;
        }
    }
}

void EigrpInterface::createNeighbor(const IPAddress& neighborIp)
{
    {
        std::unique_lock<std::shared_mutex> intLock(neighborMutex);

        // Check if neighbor already exists
        if (neighbors.find(neighborIp) != neighbors.end())
        {
            // Delete neighbor and replace it with a unicast neighbor if neighbor exists
            delete neighbors[neighborIp];
            neighbors[neighborIp] = nullptr;
            neighbors.erase(neighborIp);
            neighbors[neighborIp] = new EigrpConfigs::NeighborInfo(
                eigrpProcess.addressFamily,
                eigrpProcess.routingInstance->global.timeManager,
                neighborIp,
                true
            );
        }
        else
        {
            // Add the neighbor normally if not present
            neighbors[neighborIp] = new EigrpConfigs::NeighborInfo(eigrpProcess.addressFamily, eigrpProcess.routingInstance->global.timeManager, neighborIp, true);
        }

        std::lock_guard<std::mutex> globalLock(eigrpProcess.neighborMutex);
        eigrpProcess.allNeighbors[neighborIp] = neighbors[neighborIp];
    }

    // Disable mutlicast if enabled
    if (configs->multicastEnabled.load(std::memory_order_relaxed))
    {
        disableMulticast();
    }
}

void EigrpInterface::deleteNeighbor(const IPAddress& neighborIp)
{
    // Find the neighbor and remove it if present
    EigrpConfigs::NeighborInfo* neighbor = nullptr;
    {
        std::unique_lock<std::shared_mutex> lock(neighborMutex);
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt != neighbors.end() && neighborIt->second->unicast)
        {
            neighbor = neighborIt->second;
        }
    }

    // Remove the neighbor if found and is in unciast
    if (neighbor)
    {
        handleNeighborDown(neighbor, neighborIp);
    }

    // Enable multicast if neighbor are empty
    {
        std::shared_lock<std::shared_mutex> lock(neighborMutex);
        if (neighbors.empty())
        {
            enableMulticast();
        }
    }
}

EigrpConfigs::NeighborInfo* EigrpInterface::lookup(const IPAddress& neighborIp)
{
    auto it = neighbors.find(neighborIp);
    if (it != neighbors.end())
    {
        return it->second;
    }
    return nullptr;
}

void EigrpInterface::onDown(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp)
{
    if (neighbors.find(neighborIp) == neighbors.end() || !neighbor) return;
    
    // Cancel timers
    if (neighbor->holdTimerId != 0)
    {
        eigrpProcess.routingInstance->global.timeManager.cancelTimer(neighbor->holdTimerId);
        neighbor->holdTimerId = 0;
    }
    for (const auto &timerEntry : neighbor->retransmissionTimers)
    {
        eigrpProcess.routingInstance->global.timeManager.cancelTimer(timerEntry.second);
    }

    // Clear reliable packets and sequence tracking
    {
        std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
        neighbor->retransmissionTimers.clear();
        neighbor->reliablePackets.clear();
    }

    // Collect affected routes
    std::vector<IPPrefix> affectedRoutes;
    for (auto& [destination, entry] : eigrpProcess.topologyTable->getTopologyEntries())
    {
        // If this neighbor was advertising the route
        if (entry->routesByNeighbor.find(neighborIp) != entry->routesByNeighbor.end())
        {
            affectedRoutes.push_back(destination);
        }
    }

    // Remove the neighbor rotues from topology
    eigrpProcess.topologyTable->handleNeighborDown(neighborIp);

    // Trigger Active for routes with no feasible successor
    std::vector<RoutingTable::Eigrp*> activeRoutes;
    for (const auto& prefix : affectedRoutes)
    {
        // Find the best remaining successor
        auto bestRoute = eigrpProcess.topologyTable->findBestRoute(prefix);
        if (!bestRoute.has_value())
        {
            // If no valid successor is remaining
            RoutingTable::Eigrp* route = eigrpProcess.routingInstance->routingTable.getEigrpRoute(prefix.addr, prefix.prefixLength, eigrpProcess.addressFamily, eigrpProcess.asNumber);
            if (route)
            {
                route->stuckInActive = true;
                activeRoutes.push_back(route);
            }
        }
        else
        {
            // Reinstall best route if available
            updateRoutingTableForDestination(prefix);
        }
    }

    // Remove neighbor from neighbor list
    {
        std::unique_lock<std::shared_mutex> intLock(neighborMutex);
        if (neighbors.find(neighborIp) == neighbors.end())
        {
            return; // Neighbor already removed
        }
        neighbors.erase(neighborIp);
    }
    {
        std::lock_guard<std::mutex> globalLock(eigrpProcess.neighborMutex);
        eigrpProcess.allNeighbors.erase(neighborIp);
    }

    if (!neighbor->unicast)
    {
        delete neighbor;
        neighbor = nullptr;
    }

    // Launch queries for routes that entered Active
    if (!activeRoutes.empty())
    {
        sendQueryToNeighbors(activeRoutes);
    }
}

void EigrpInterface::onRestart(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp)
{
    // Validate neighbor
    if (!neighbor)
    {
        return; //neighbor does not exist
    }

    // Cancel existing timers
    neighbor->clearTimers();

    {
        std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
        neighbor->reliablePackets.clear();
        eigrpProcess.topologyTable->removeRoutesFromNeighbor(neighborIp);
    }

    // Reinitialize neighbor state
    {
        neighbor->neighborState.store(EigrpConfigs::NeighborState::DOWN, std::memory_order_release);
        neighbor->initComplete.store(false, std::memory_order_release);
    }
    changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::DOWN);
}

void EigrpInterface::startGracefulRestart(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp)
{
    auto expireTime = std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess.configs.purgeTime.load(std::memory_order_relaxed));
    uint32_t gracefulTimerId = eigrpProcess.routingInstance->global.timeManager.addTimer(expireTime, [&]() {
        handleNeighborRestart(neighbor, neighborIp);
    });
    neighbor->gracefulRestartTimerId.store(gracefulTimerId, std::memory_order_release);
}
}
