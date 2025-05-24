#include "Dhcpv6Server.h"
#include <Interface.h>
#include <Functions.h>
#include <PacketStructure.h>
#include <Encryption.hpp>
#include <TimeManager.h>
#include <IPPacket.h>
#include <Configs.h>

Protocol::Dhcpv6Server::Dhcpv6Server(Global& global) : DhcpServerBase(global)
{
    dhcpUniqueIdentifier = generateUniqueIdentifier();
    startServer();
}

Protocol::Dhcpv6Server::~Dhcpv6Server()
{
    stopServer();
}

void Protocol::Dhcpv6Server::removeInterface(Interface* iface)
{
    std::lock_guard<std::mutex> lock(trackingMutex);
    for (auto it = clientReconfAccept.begin(); it != clientReconfAccept.end();)
    {
        if (iface == it->second.second)
        {
            it = clientReconfAccept.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void Protocol::Dhcpv6Server::startServer()
{
    stopFlag.store(false, std::memory_order_release);
    serverThread = std::thread(&Dhcpv6Server::dhcpHandler, this);
}

void Protocol::Dhcpv6Server::stopServer()
{
    stopFlag.store(true, std::memory_order_release);
    serverCV.notify_one();
    if (serverThread.joinable())
        serverThread.join();

    std::lock_guard<std::mutex> lock(configMutex);
    for (auto& [_, net] : dhcpNetworks)
    {
        if (net) delete net;
    }
    dhcpNetworks.clear();
}

void Protocol::Dhcpv6Server::handleDhcpPacket(const PacketInfo& packet, Interface* iface, bool multicast, const ByteString& localAddress)
{
    if ((packet.Layer5.empty() || !std::holds_alternative<Dhcpv6Header>(packet.Layer5[0])))
        return;
    
    const Dhcpv6Header& header = std::get<Dhcpv6Header>(packet.Layer5[0]);
    const ByteString& msgType = header.type;

    processReconfigAccept(header, localAddress, iface);

    switch (msgType[0].value)
    {
    case 0x01:
        processSolicit(header, iface, multicast, &localAddress);
        break;
    case 0x03:
        processRequest(header, iface, multicast, &localAddress);
        break;
    case 0x04:
        processConfirm(header, iface, multicast, &localAddress);
        break;
    case 0x05:
        processRenew(header, iface, multicast, &localAddress);
        break;
    case 0x06:
        processRebind(header, iface, multicast, &localAddress);
        break;
    case 0x08:
        processRelease(header, iface, multicast, &localAddress);
        break;
    case 0x09:
        processDecline(header, iface, multicast, &localAddress);
        break;
    case 0x0B:
        processInformationRequest(header, iface, multicast, &localAddress);
        break;
    }
}

void Protocol::Dhcpv6Server::dhcpHandler()
{
    std::unique_lock<std::mutex> lock(serverThreadMutex);

    while (!stopFlag.load(std::memory_order_relaxed))
    {
        // Wait until notified or after 1 second
        serverCV.wait_for(lock, std::chrono::seconds(1), [&]() {
            return stopFlag.load(std::memory_order_relaxed);
        });

        if (stopFlag.load(std::memory_order_relaxed)) break;

        for (auto& [_, net] : dhcpNetworks)
        {
            if (net && net->lease)
                net->lease->cleanupExpiredLeases();
            if (net && net->prefixLease)
                net->prefixLease->cleanupExpiredLeases();
        }
    }
}

ByteString Protocol::Dhcpv6Server::findMatchingNetwork(const ByteString& ip)
{
    for (const auto& [id, net] : dhcpNetworks)
    {
        ByteString network;
        {
            network = net->config->getNetwork();
        }
        if (Functions::compareNetworkWithIp(network, ip, net->config->getPrefixLen()))
            return id;
    }
    return {};
}

std::vector<Protocol::Dhcpv6::IANABlock> Protocol::Dhcpv6Server::extractIA_NA(const Dhcpv6Header& header, Interface* iface)
{
    std::vector<Dhcpv6::IANABlock> results;

    for (const auto& opt : header.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::IA_NA && opt.value.size() >= 12)
        {
            Dhcpv6::IANABlock block;
            block.iaid = opt.value.substr(0, 4);
            block.t1 = Functions::byteToNum(opt.value.substr(4, 4));
            block.t2 = Functions::byteToNum(opt.value.substr(8, 4));

            size_t offset = 12;
            while (offset + 4 <= opt.value.size())
            {
                ByteString code = opt.value.substr(offset, 2);
                uint16_t len = Functions::byteToNum(opt.value.substr(offset + 2, 2));
                if (offset + 4 + len > opt.value.size()) break;

                //TODO
                ByteString preferedLife;
                ByteString validLife;

                if (code == Variable::Dhcpv6::Options::IAAddr && len >= 24)
                {
                    block.addresses.push_back(opt.value.substr(offset + 4, 16));
                    preferedLife = opt.value.substr(offset + 20, 4);
                    validLife = opt.value.substr(offset + 24, 4);
                }
                
                offset += 4 + len;
            }

            ByteString networkID;
            if (!block.addresses.empty())
            {
                networkID = findMatchingNetwork(block.addresses[0]);
            }
            else
            {
                std::lock_guard<std::mutex> lock(configMutex);
                if (!dhcpNetworks.empty())
                {
                    for (const auto& network : dhcpNetworks)
                    {
                        if (network.second->config->interface == iface)
                        {
                            networkID = network.second->config->getNetworkID();
                        }
                    }
                }
            }

            auto it = dhcpNetworks.find(networkID);
            if (it != dhcpNetworks.end() && it->second->config->interface == iface)
            {
                std::lock_guard<std::mutex> lock(configMutex);
                block.network = it->second;
            }

            results.push_back(std::move(block));
        }
    }
    return results;
}

std::vector<Protocol::Dhcpv6::IAPDBlock> Protocol::Dhcpv6Server::extractIA_PD(const Dhcpv6Header& header, Interface* iface)
{
    std::vector<Dhcpv6::IAPDBlock> results;

    for (const auto& opt : header.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::IA_PD && opt.value.size() >= 12)
        {
            Dhcpv6::IAPDBlock block;
            block.iaid = opt.value.substr(0, 4);
            block.t1 = Functions::byteToNum(opt.value.substr(4, 4));
            block.t2 = Functions::byteToNum(opt.value.substr(8, 4));

            size_t offset = 12;
            while (offset + 4 <= opt.value.size())
            {
                ByteString code = opt.value.substr(offset, 2);
                uint16_t len = Functions::byteToNum(opt.value.substr(offset + 2, 2));
                if (offset + 4 + len > opt.value.size()) break;

                //TODO
                ByteString preferedLife;
                ByteString validLife;

                if (code == Variable::Dhcpv6::Options::IA_Prefix && len >= 28)
                {
                    preferedLife = opt.value.substr(offset + 4, 4);
                    validLife = opt.value.substr(offset + 8, 4);
                    uint8_t plen = static_cast<uint8_t>(opt.value[offset + 12]);

                    ByteString prefix = opt.value.substr(offset + 16, 16);
                    block.prefixes.emplace_back(prefix, plen);
                }

                offset += 4 + len;
            }

            ByteString networkID;
            if (!block.prefixes.empty())
            {
                networkID = findMatchingNetwork(block.prefixes[0].first);
            }
            else
            {
                std::lock_guard<std::mutex> lock(configMutex);
                if (!dhcpNetworks.empty())
                {
                    for (const auto& network : dhcpNetworks)
                    {
                        if (network.second->config->interface == iface)
                        {
                            networkID = network.second->config->getNetworkID();
                        }
                    }
                }
            }

            auto it = dhcpNetworks.find(networkID);
            if (it != dhcpNetworks.end() && it->second->config->interface == iface)
            {
                std::lock_guard<std::mutex> lock(configMutex);
                block.network = it->second;
            }

            results.push_back(std::move(block));
        }
    }
    return results;
}

std::vector<Protocol::Dhcpv6::IANABlock> Protocol::Dhcpv6Server::extractIA_TA(const Dhcpv6Header& header, Interface* iface)
{
    std::vector<Dhcpv6::IANABlock> results;

    for (const auto& opt : header.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::IA_TA && opt.value.size() >= 12)
        {
            Dhcpv6::IANABlock block;
            block.iaid = opt.value.substr(0, 4);
            block.t1 = 0;
            block.t2 = 0;

            size_t offset = 12;
            while (offset + 4 <= opt.value.size())
            {
                ByteString code = opt.value.substr(offset, 2);
                uint16_t len = Functions::byteToNum(opt.value.substr(offset + 2, 2));
                if (offset + 4 + len > opt.value.size()) break;

                //TODO
                ByteString preferedLife;
                ByteString validLife;

                if (code == Variable::Dhcpv6::Options::IAAddr && len >= 24)
                {
                    block.addresses.push_back(opt.value.substr(offset + 4, 16));
                    preferedLife = opt.value.substr(offset + 20, 4);
                    validLife = opt.value.substr(offset + 24, 4);
                }
                
                offset += 4 + len;
            }

            ByteString networkID;
            if (!block.addresses.empty())
            {
                networkID = findMatchingNetwork(block.addresses[0]);
            }
            else
            {
                std::lock_guard<std::mutex> lock(configMutex);
                if (!dhcpNetworks.empty())
                {
                    for (const auto& network : dhcpNetworks)
                    {
                        if (network.second->config->interface == iface)
                        {
                            networkID = network.second->config->getNetworkID();
                        }
                    }
                }
            }

            auto it = dhcpNetworks.find(networkID);
            if (it != dhcpNetworks.end() && it->second->config->interface == iface)
            {
                std::lock_guard<std::mutex> lock(configMutex);
                block.network = it->second;
            }

            results.push_back(std::move(block));
        }
    }

    return results;
}

std::vector<ByteString> Protocol::Dhcpv6Server::extractORO(const Dhcpv6Header& header)
{
    std::vector<ByteString> results;
    for (const auto& opt : header.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::optionRequest)
        {
            for (size_t i = 0; i + 2 <= opt.value.size(); i += 2)
                results.push_back(opt.value.substr(i, 2));
        }
    }
    return results;
}

std::optional<ByteString> Protocol::Dhcpv6Server::processSolicit(const Dhcpv6Header& header, Interface* iface, bool multicast, const ByteString* localAddress)
{
    auto ianaBlocks = extractIA_NA(header, iface);
    auto iapdBlocks = extractIA_PD(header, iface);
    auto iataBlocks = extractIA_TA(header, iface);

    if (ianaBlocks.empty() && iapdBlocks.empty() && iataBlocks.empty()) return std::nullopt;

    auto oro = extractORO(header);
    bool rapidCommitRequest = handleRapidCommit(header.options);

    // Extract Client ID (DUID)
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid) || !trackElapsedTime(header, duid))
        return std::nullopt; // No leases will be given without a Client ID.

    if (rapidCommitRequest)
    {
        ByteString lastLease;
        // Allocate and commit all addresses/prefixes
        for (auto& block : ianaBlocks)
        {
            if (!block.network || !block.network->lease) continue;

            // If the client requested specific addresses, try to reserve them
            if (!block.addresses.empty())
            {
                std::vector<ByteString> confirmed;
                for (const auto& requestedIP : block.addresses)
                {
                    ByteString id = duid + block.iaid + requestedIP;
                    if (!block.network->pool->isAllocatedOrExcluded(requestedIP) && 
                        block.network->lease->allocateRequestedIP(
                            requestedIP, 
                            block.network->config->leaseTime.load(std::memory_order_relaxed),
                            block.network->config->t1Percentage.load(std::memory_order_relaxed),
                            block.network->config->t2Percentage.load(std::memory_order_relaxed), 
                            &id
                        ))
                    {
                        lastLease = requestedIP;
                        confirmed.push_back(requestedIP);
                    }
                }
                block.addresses = confirmed;
            }
            else
            {
                // No requested addresses, just allocate one
                ByteString id = duid + block.iaid;
                ByteString ip = block.network->lease->allocateIP(
                    block.network->config->leaseTime.load(std::memory_order_relaxed),
                    block.network->config->t1Percentage.load(std::memory_order_relaxed), 
                    block.network->config->t2Percentage.load(std::memory_order_relaxed),
                    &id, true
                );
                if (!ip.empty())
                {
                    lastLease = ip;
                    block.addresses.push_back(ip);
                }
                else
                {
                    //TODO
                }
            }

            if (!lastLease.empty())
            {
                std::lock_guard<std::mutex> lock(trackingMutex);
                recentLeases[duid] = lastLease;
            }
        }

        for (auto& block : iapdBlocks)
        {
            if (!block.network || !block.network->prefixLease) continue;

            if (!block.prefixes.empty())
            {
                std::vector<std::pair<ByteString, uint8_t>> confirmed;
                for (const auto& [prefix, length] : block.prefixes)
                {
                    ByteString id = duid + block.iaid + prefix;
                    if (!block.network->pool->isAllocatedOrExcluded(prefix) &&
                        block.network->prefixLease->allocateRequestedPrefix(
                            id, prefix, length,
                            block.network->config->leaseTime.load(std::memory_order_relaxed),
                            block.network->config->t1Percentage.load(std::memory_order_relaxed),
                            block.network->config->t2Percentage.load(std::memory_order_relaxed)
                        ))
                    {
                        block.prefixes.emplace_back(prefix, length);
                    }
                }
                block.prefixes = confirmed;
            }
            else
            {
                // No specified prefix requested, assign a default
                auto allocated = block.network->prefixLease->allocatePrefix(
                    duid + block.iaid,
                    block.network->config->defaultSubnetPrefix,
                    block.network->config->leaseTime.load(std::memory_order_relaxed),
                    block.network->config->t1Percentage.load(std::memory_order_relaxed),
                    block.network->config->t2Percentage.load(std::memory_order_relaxed),
                    true
                );
                if (!allocated.first.empty())
                {
                    block.prefixes.push_back(allocated);
                }
            }
        }

        for (auto& block : iataBlocks)
        {
            if (!block.network || !block.network->prefixLease) continue;
    
            if (!block.addresses.empty())
            {
                std::vector<ByteString> confirmed;
                for (const auto& requestedIP : block.addresses)
                {
                    ByteString id = duid + block.iaid + requestedIP;
                    if (!block.network->pool->isAllocatedOrExcluded(requestedIP) && 
                        block.network->lease->allocateRequestedIP(
                            requestedIP,
                            block.network->config->leaseTime.load(std::memory_order_relaxed),
                            block.network->config->t1Percentage.load(std::memory_order_relaxed),
                            block.network->config->t2Percentage.load(std::memory_order_relaxed),
                            &id
                        ))
                    {
                        confirmed.push_back(requestedIP);
                    }
                }
                block.addresses = confirmed;
            }
            else
            {
                // No requested addresses, just allocate one
                ByteString ip = block.network->lease->allocateIP(
                    block.network->config->leaseTime.load(std::memory_order_relaxed),
                    block.network->config->t1Percentage.load(std::memory_order_relaxed),
                    block.network->config->t2Percentage.load(std::memory_order_relaxed),
                    nullptr
                );
                if (!ip.empty())
                    block.addresses.push_back(ip);
            }
        }

        Dhcpv6Header reply = buildResponse(
            Variable::Dhcpv6::Type::reply,
            header.transactionID,
            duid,
            ianaBlocks,
            iapdBlocks,
            iataBlocks
        );

        if (multicast && dhcpConfigs.allowUnicast.load(std::memory_order_relaxed) && !ianaBlocks.empty())
        {
            auto block = ianaBlocks.begin();
            if (!block->addresses.empty())
            {
                addServerUnicast(reply, block->addresses.front(), iface);
            }
        }

        // Add arpid Commit option to server response
        reply.options.emplace_back(
            Variable::Dhcpv6::Options::rapidCommit,
            Functions::numToByte(0, 2),
            ByteString()
        );

        ByteString statusCode;
        std::string message;

        if (!ianaBlocks.empty() || iapdBlocks.empty() || iataBlocks.empty())
        {
            statusCode = Variable::Dhcpv6::Status::success;
            message = "Advertised addresses/prefixes";
        }
        else
        {
            statusCode = Variable::Dhcpv6::Status::noAddrsAvail;
            message = "No available addresses or prefix";
        }

        reply.options.push_back(buildStatusOption(statusCode, message));

        sendPacket(reply, iface, *localAddress);
        return std::nullopt;
    }

    for (auto& block : ianaBlocks)
    {
        if (!block.network || !block.network->lease) continue;

        if (!block.addresses.empty())
        {
            for (const auto& addr : block.addresses)
            {
                ByteString id = duid + block.iaid + addr;
                if (block.network->pool->allocateRequestedTempIP(addr, &id))
                {
                    block.addresses.push_back(addr);
                    // Schedule offer timeout
                    scheduleTimeout(
                        Dhcp::TimerType::IP_OFFER_TIMEOUT,
                        duid + block.iaid + addr,
                        addr,
                        block.network->config->getNetworkID(), 
                        dhcpConfigs.offerTimeout.load(std::memory_order_relaxed)
                    );
                    // Schedule a client request timeout
                    scheduleTimeout(
                        Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT,
                        duid + block.iaid + addr,
                        addr,
                        block.network->config->getNetworkID(),
                        dhcpConfigs.clientRequestTimeout.load(std::memory_order_relaxed)
                    );

                    // Set the request as outgoing
                    outgoingRequests.insert(addr);
                    //std::cout << addr << std::endl;
                }
            }
        }
        else
        {
            ByteString id = duid + block.iaid;
            ByteString offeredIP = block.network->pool->allocateTempIP(&id, true);
            if (!offeredIP.empty())
            {
                block.addresses.push_back(offeredIP);
                // Schedule offer timeout
                scheduleTimeout(
                    Dhcp::TimerType::IP_OFFER_TIMEOUT,
                    duid + block.iaid + offeredIP,
                    offeredIP, 
                    block.network->config->getNetworkID(), 
                    dhcpConfigs.offerTimeout.load(std::memory_order_relaxed)
                );
                // Schedule a client request timeout
                scheduleTimeout(
                    Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT,
                    duid + block.iaid + offeredIP,
                    offeredIP,
                    block.network->config->getNetworkID(),
                    dhcpConfigs.clientRequestTimeout.load(std::memory_order_relaxed)
                );

                // Set the request as outgoing
                outgoingRequests.insert(offeredIP);
                //std::cout << offeredIP.toHex() << std::endl;
            }
        }
    }

    for (auto& block : iapdBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;

        if (!block.prefixes.empty())
        {
            for (const auto& prefix : block.prefixes)
            {
                if (block.network->prefixPool->allocateSpecificPrefix(prefix.first, prefix.second, duid + block.iaid + prefix.first))
                {
                    block.prefixes.push_back(prefix);
                    // Schedule offer timeout
                    scheduleTimeout(
                        Dhcp::TimerType::PREFIX_OFFER_TIMEOUT,
                        duid + block.iaid + prefix.first,
                        prefix.first, 
                        block.network->config->getNetworkID(), 
                        dhcpConfigs.offerTimeout.load(std::memory_order_relaxed)
                    );
                    // Schedule a client request timeout
                    scheduleTimeout(
                        Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT,
                        duid + block.iaid + prefix.first,
                        prefix.first, 
                        block.network->config->getNetworkID(), 
                        dhcpConfigs.clientRequestTimeout.load(std::memory_order_relaxed)
                    );
                    
                    // Set the request as outgoing.
                    outgoingRequests.insert(prefix.first);
                }
            }
        }
        else
        {
            auto offered = block.network->prefixPool->allocateTempPrefix(duid + block.iaid, block.network->config->defaultSubnetPrefix.load(std::memory_order_relaxed), true);
            if (offered.first.empty())
            {
                block.prefixes.push_back(offered);
                // Schedule offer timeout
                scheduleTimeout(
                    Dhcp::TimerType::PREFIX_OFFER_TIMEOUT,
                    duid + block.iaid, 
                    offered.first, 
                    block.network->config->getNetworkID(), 
                    dhcpConfigs.offerTimeout.load(std::memory_order_relaxed)
                );
                // Schedule a client request timeout
                scheduleTimeout(
                    Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT,
                    duid + block.iaid, 
                    offered.first, 
                    block.network->config->getNetworkID(), 
                    dhcpConfigs.clientRequestTimeout.load(std::memory_order_relaxed)
                );
                
                // Set the request as outgoing.
                outgoingRequests.insert(offered.first);
                //std::cout << offered.first << std::endl;
            }
        }
    }

    for (auto& block : iataBlocks)
    {
        if (!block.network || !block.network->lease) continue;

        if (!block.addresses.empty())
        {
            for (const auto& addr : block.addresses)
            {
                if (block.network->pool->allocateRequestedTempIP(addr, nullptr))
                {
                    block.addresses.push_back(addr);
                    // Schedule offer timeout
                    scheduleTimeout(
                        Dhcp::TimerType::IP_OFFER_TIMEOUT,
                        duid + block.iaid,
                        addr,
                        block.network->config->getNetworkID(), 
                        dhcpConfigs.offerTimeout.load(std::memory_order_relaxed)
                    );
                    // Schedule a client request timeout
                    scheduleTimeout(
                        Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT,
                        duid + block.iaid,
                        addr,
                        block.network->config->getNetworkID(),
                        dhcpConfigs.clientRequestTimeout.load(std::memory_order_relaxed)
                    );

                    // Set the request as outgoing
                    outgoingRequests.insert(addr);
                    //std::cout << addr.toHex() << std::endl;
                }
            }
        }
        else
        {
            ByteString offeredIP = block.network->pool->allocateTempIP(nullptr);
            if (!offeredIP.empty())
            {
                block.addresses.push_back(offeredIP);
                // Schedule offer timeout
                scheduleTimeout(
                    Dhcp::TimerType::IP_OFFER_TIMEOUT,
                    duid + block.iaid, 
                    offeredIP, 
                    block.network->config->getNetworkID(), 
                    dhcpConfigs.offerTimeout.load(std::memory_order_relaxed)
                );
                // Schedule a client request timeout
                scheduleTimeout(
                    Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT,
                    duid + block.iaid,
                    offeredIP,
                    block.network->config->getNetworkID(),
                    dhcpConfigs.clientRequestTimeout.load(std::memory_order_relaxed)
                );

                // Set the request as outgoing
                outgoingRequests.insert(offeredIP);
                //std::cout << offeredIP.toHex() << std::endl;
            }
        }
    }
    

    Dhcpv6Header advertise = buildResponse(
        Variable::Dhcpv6::Type::advertise,
        header.transactionID,
        duid,
        ianaBlocks,
        iapdBlocks,
        iataBlocks
    );

    if (multicast && dhcpConfigs.allowUnicast.load(std::memory_order_relaxed) && !ianaBlocks.empty())
    {
        auto block = ianaBlocks.begin();
        if (!block->addresses.empty())
        {
            addServerUnicast(advertise, block->addresses.front(), iface);
        }
    }

    for (const auto& block : ianaBlocks)
    {
        if (!block.network || !block.network->config) continue;
        std::vector<Dhcpv6Header::Option> extras = buildOptions(block.network->config, oro);
        advertise.options.insert(advertise.options.end(), extras.begin(), extras.end());
    }

    for (const auto& block : iapdBlocks)
    {
        if (!block.network || !block.network->config) continue;
        std::vector<Dhcpv6Header::Option> extras = buildOptions(block.network->config, oro);
        advertise.options.insert(advertise.options.end(), extras.begin(), extras.end());
    }

    for (const auto& block : ianaBlocks)
    {
        if (block.network && block.network->config)
        {
            ByteString pref = ByteString(1, getServerPreference(block.network->config, duid));
            advertise.options.emplace_back(
                Variable::Dhcpv6::Options::preference,
                Functions::numToByte(1, 2),
                std::move(pref)
            );
            break;
        }
    }

    ByteString statusCode;
    std::string message;

    if (!ianaBlocks.empty() || iapdBlocks.empty() || iataBlocks.empty())
    {
        statusCode = Variable::Dhcpv6::Status::success;
        message = "Advertised addresses/prefixes";
    }
    else
    {
        statusCode = Variable::Dhcpv6::Status::noAddrsAvail;
        message = "No available addresses or prefix";
    }

    advertise.options.push_back(buildStatusOption(statusCode, message));

    if (!localAddress)
    {
        return advertise.encapsulate();
    }
    
    sendPacket(advertise, iface, *localAddress);
    return std::nullopt;
}

std::optional<ByteString> Protocol::Dhcpv6Server::processRequest(const Dhcpv6Header& header, Interface* iface, bool multicast, const ByteString* localAddress)
{
    auto ianaBlocks = extractIA_NA(header, iface);
    auto iapdBlocks = extractIA_PD(header, iface);
    auto iataBlocks = extractIA_TA(header, iface);
    if (ianaBlocks.empty() && iapdBlocks.empty()) return std::nullopt;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid) || !validateServerID(header) || !trackElapsedTime(header, duid))
        return std::nullopt; // No leases will be given if no Client ID is present

    // Validate Server Identifier
    ByteString expectedServerID = ByteString("\x00\x03", 2) + ByteString("\x00\x00\x00\x00\x00\x01", 6);
    if (!validateServerID(header)) return std::nullopt;

    for (auto& block : ianaBlocks)
    {
        ByteString lastLease;

        if (!block.network || !block.network->lease) continue;
        ByteString baseId = duid + block.iaid;

        std::vector<ByteString> committed;
        for (const ByteString& addr : block.addresses)
        {
            if (block.network->lease->isAllocated(addr))
            {
                block.network->lease->renewLease(addr);
                lastLease = addr;
                committed.push_back(addr);
            }
            else if (outgoingRequests.count(addr) && block.network->pool->isTemporarilyOffered(addr))
            {
                ByteString id = baseId + addr;
                ByteString reserved = block.network->lease->activateLeaseFromTemp(
                    addr,
                    block.network->config->leaseTime.load(std::memory_order_relaxed),
                    block.network->config->t1Percentage.load(std::memory_order_relaxed),
                    block.network->config->t2Percentage.load(std::memory_order_relaxed),
                    &id
                );
                if (!reserved.empty()) 
                {
                    lastLease = reserved;
                    committed.push_back(reserved);
                    cancelTimeout(Dhcp::TimerType::IP_OFFER_TIMEOUT, id + reserved, reserved);
                    cancelTimeout(Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT, id + reserved, reserved);
                    outgoingRequests.erase(reserved);
                }
                {
                    //TODO fail
                }
            }
        }

        if (!lastLease.empty())
        {
            recentLeases[duid] = lastLease;
        }

        block.addresses = committed;
    }

    // Handle IA_PD
    for (auto& block : iapdBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;
        ByteString baseId = duid + block.iaid;

        std::vector<std::pair<ByteString, uint8_t>> committed;
        for (const auto& [prefix, plen] : block.prefixes)
        {
            if (block.network->prefixLease->isAllocated(prefix))
            {
                block.network->prefixLease->renewPrefix(prefix);
                committed.emplace_back(prefix, plen);
            }
            else if (outgoingRequests.count(prefix) && block.network->prefixPool->isTemporarilyOffered(prefix))
            {
                auto activated = block.network->prefixLease->activateLeaseFromTemp(
                    baseId + prefix,
                    block.network->config->leaseTime.load(std::memory_order_relaxed),
                    block.network->config->t1Percentage.load(std::memory_order_relaxed),
                    block.network->config->t2Percentage.load(std::memory_order_relaxed)
                );
                if (!activated.first.empty())
                {
                    committed.push_back(activated);
                    cancelTimeout(Dhcp::TimerType::PREFIX_OFFER_TIMEOUT, baseId + prefix, activated.first);
                    cancelTimeout(Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT, baseId + prefix, activated.first);
                    outgoingRequests.erase(activated.first);
                }
            }
        }

        block.prefixes = committed;
    }

    // Handle IA_TA
    for (auto& block : iataBlocks)
    {
        if (!block.network || !block.network->lease) continue;

        std::vector<ByteString> committed;
        for (const ByteString& addr : block.addresses)
        {
            if (block.network->pool->isTemporarilyOffered(addr))
            {
                ByteString reserved = block.network->lease->activateLeaseFromTemp(
                    addr,
                    block.network->config->leaseTime.load(std::memory_order_relaxed),
                    block.network->config->t1Percentage.load(std::memory_order_relaxed),
                    block.network->config->t2Percentage.load(std::memory_order_relaxed),
                    nullptr
                );
                if (!reserved.empty()) committed.push_back(reserved);
            }
        }

        block.addresses = committed;
    }

    // Send REPLY with lease confirmation
    auto oro = extractORO(header);
    Dhcpv6Header reply = buildResponse(
        Variable::Dhcpv6::Type::reply,
        header.transactionID,
        duid,
        ianaBlocks,
        iapdBlocks,
        iataBlocks
    );

    if (multicast && dhcpConfigs.allowUnicast.load(std::memory_order_relaxed) && !ianaBlocks.empty())
    {
        auto block = ianaBlocks.begin();
        if (!block->addresses.empty())
        {
            addServerUnicast(reply, block->addresses.front(), iface);
        }
    }

    ByteString statusCode;
    std::string message;

    bool anyCommitted = false;
    for (const auto& block : ianaBlocks)
        if (!block.addresses.empty()) anyCommitted = true;
    for (const auto& block : iapdBlocks)
        if (!block.prefixes.empty()) anyCommitted = true;

    if (anyCommitted)
    {
        statusCode = Variable::Dhcpv6::Status::success;
        message = "Lease granted";
    }
    else
    {
        statusCode = Variable::Dhcpv6::Status::noBinding;
        message = "No binding found or failed to allocate";
    }

    reply.options.push_back(buildStatusOption(statusCode, message));

    if (!localAddress)
    {
        return reply.encapsulate();
    }

    sendPacket(reply, iface, *localAddress);
    return std::nullopt;
}

std::optional<ByteString> Protocol::Dhcpv6Server::processRenew(const Dhcpv6Header& header, Interface* iface, bool multicast, const ByteString* localAddress)
{
    auto ianaBlocks = extractIA_NA(header, iface);
    auto iapdBlocks = extractIA_PD(header, iface);
    auto iataBlocks = extractIA_TA(header, iface);

    if (ianaBlocks.empty() && iapdBlocks.empty()) return std::nullopt;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid) || !validateServerID(header) || !trackElapsedTime(header, duid))
        return std::nullopt; // No leases will be given if no Client ID is present

    // Validate Server Identifier
    if (!validateServerID(header)) return std::nullopt;

    for (const auto& block : ianaBlocks)
    {
        if (!block.network || !block.network->lease) continue;

        for (const ByteString& addr : block.addresses)
            block.network->lease->renewLease(addr);
    }

    for (auto& block : iapdBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;

        for (const auto& [prefix, _] : block.prefixes)
            block.network->prefixLease->renewPrefix(prefix);
    }

    for (auto& block : iataBlocks)
    {
        if (!block.network || !block.network->lease) continue;

        for (const ByteString& addr : block.addresses)
            block.network->lease->renewLease(addr);
    }

    auto oro = extractORO(header);
    Dhcpv6Header reply = buildResponse(
        Variable::Dhcpv6::Type::reply,
        header.transactionID,
        duid,
        ianaBlocks,
        iapdBlocks,
        iataBlocks
    );

    if (multicast && dhcpConfigs.allowUnicast.load(std::memory_order_relaxed) && !ianaBlocks.empty())
    {
        auto block = ianaBlocks.begin();
        if (!block->addresses.empty())
        {
            addServerUnicast(reply, block->addresses.front(), iface);
        }
    }

    bool valid = false;
    for (const auto& block : ianaBlocks)
        if (!block.addresses.empty()) valid = true;
    for (const auto& block : iapdBlocks)
        if (!block.prefixes.empty()) valid = true;

    ByteString statusCode = valid ? Variable::Dhcpv6::Status::success
                                  : Variable::Dhcpv6::Status::noBinding;

    std::string msg = valid ? "Lease renewed"
                            : "No binding to renew";

    reply.options.push_back(buildStatusOption(statusCode, msg));

    if (!localAddress)
    {
        return reply.encapsulate();
    }

    sendPacket(reply, iface, *localAddress);

    return std::nullopt;
}

std::optional<ByteString> Protocol::Dhcpv6Server::processRebind(const Dhcpv6Header& header, Interface* iface, bool multicast, const ByteString* localAddress)
{
    auto ianaBlocks = extractIA_NA(header, iface);
    auto iapdBlocks = extractIA_PD(header, iface);

    if (ianaBlocks.empty() && iapdBlocks.empty()) return std::nullopt;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid) || !validateServerID(header) || !trackElapsedTime(header, duid))
        return std::nullopt; // No leases will be given if no Client ID is present

    for (const auto& block : ianaBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;
        for (const ByteString& addr : block.addresses)
            block.network->lease->renewLease(addr);
    }

    for (auto& block : iapdBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;
        for (const auto& [prefix, _] : block.prefixes)
            block.network->prefixLease->renewPrefix(prefix);
    }

    auto oro = extractORO(header);
    Dhcpv6Header reply = buildResponse(
        Variable::Dhcpv6::Type::reply,
        header.transactionID,
        duid,
        ianaBlocks,
        iapdBlocks,
        {}
    );

    if (multicast && dhcpConfigs.allowUnicast.load(std::memory_order_relaxed) && !ianaBlocks.empty())
    {
        auto block = ianaBlocks.begin();
        if (!block->addresses.empty())
        {
            addServerUnicast(reply, block->addresses.front(), iface);
        }
    }

    bool valid = false;
    for (const auto& block : ianaBlocks)
        if (!block.addresses.empty()) valid = true;
    for (const auto& block : iapdBlocks)
        if (!block.prefixes.empty()) valid = true;

    ByteString statusCode = valid ? Variable::Dhcpv6::Status::success
                                  : Variable::Dhcpv6::Status::noBinding;

    std::string msg = valid ? "Lease renewed"
                            : "No binding to renew";

    reply.options.push_back(buildStatusOption(statusCode, msg));

    if (!localAddress)
    {
        return reply.encapsulate();
    }

    sendPacket(reply, iface, *localAddress);
    return std::nullopt;
}

std::optional<ByteString> Protocol::Dhcpv6Server::processRelease(const Dhcpv6Header& header, Interface* iface, bool multicast, const ByteString* localAddress)
{
    auto ianaBlocks = extractIA_NA(header, iface);
    auto iapdBlocks = extractIA_PD(header, iface);

    if (ianaBlocks.empty() && iapdBlocks.empty()) return std::nullopt;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid) || !validateServerID(header))
        return std::nullopt; // No leases will be given if no Client ID is present

    for (const auto& block : ianaBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;
        for (const ByteString& addr : block.addresses)
            scheduleTimeout(
                Dhcp::TimerType::RELEASE_HOLD,
                duid + block.iaid + addr,
                addr,
                block.network->config->getNetworkID(), 
                dhcpConfigs.declineHoldTime.load(std::memory_order_relaxed)
            );
    }

    for (auto& block : iapdBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;
        for (const auto& [prefix, _] : block.prefixes)
            scheduleTimeout(
                Dhcp::TimerType::RELEASE_HOLD,
                duid + block.iaid + prefix,
                prefix,
                block.network->config->getNetworkID(),
                dhcpConfigs.declineHoldTime.load(std::memory_order_relaxed)
            );
    }

    Dhcpv6Header reply;
    reply.type = Variable::Dhcpv6::Type::reply;
    reply.transactionID = header.transactionID;

    if (multicast && dhcpConfigs.allowUnicast.load(std::memory_order_relaxed) && !ianaBlocks.empty())
    {
        auto block = ianaBlocks.begin();
        if (!block->addresses.empty())
        {
            addServerUnicast(reply, block->addresses.front(), iface);
        }
    }

    reply.options.emplace_back(
        Variable::Dhcpv6::Options::statusCode,
        Functions::numToByte(2, 2),
        ByteString("\x00\x00", 2) // Success
    );

    bool released = false;
    for (const auto& block : ianaBlocks)
        if (!block.addresses.empty()) released = true;
    for (const auto& block : iapdBlocks)
        if (!block.prefixes.empty()) released = true;

    ByteString statusCode = released ? Variable::Dhcpv6::Status::success
                                     : Variable::Dhcpv6::Status::noBinding;
    std::string msg = released ? "Lease released"
                               : "Nothing to release";

    reply.options.push_back(buildStatusOption(statusCode, msg));

    if (!localAddress)
    {
        return reply.encapsulate();
    }

    sendPacket(reply, iface, *localAddress);
    return std::nullopt;
}

std::optional<ByteString> Protocol::Dhcpv6Server::processDecline(const Dhcpv6Header& header, Interface* iface, bool multicast, const ByteString* localAddress)
{
    auto ianaBlocks = extractIA_NA(header, iface);
    auto iapdBlocks = extractIA_PD(header, iface);

    if (ianaBlocks.empty() && iapdBlocks.empty()) return std::nullopt;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid) || !validateServerID(header) || !trackElapsedTime(header, duid))
        return std::nullopt; // No leases will be given if no Client ID is present

    {
        std::lock_guard<std::mutex> lock(trackingMutex);

        for (const auto& block : ianaBlocks)
        {
            for (const auto& addr : block.addresses)
            {
                auto now = std::chrono::steady_clock::now();

                auto it = declineTimestamps.find(duid + block.iaid);
                if (it != declineTimestamps.end())
                {
                    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
                    if (duration > 10)
                    {
                        return std::nullopt; // Rate limit exceeded
                    }
                }

                declineTimestamps[addr] = now;
            }
        }

        for (const auto& block : iapdBlocks)
        {
            for (const auto& prefix : block.prefixes)
            {
                auto now = std::chrono::steady_clock::now();

                auto it = declineTimestamps.find(prefix.first);
                if (it != declineTimestamps.end())
                {
                    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
                    if (duration > 10)
                    {
                        return std::nullopt; // Rate limit exceeded
                    }
                }
                
                declineTimestamps[prefix.first] = now;
            }
        }
    }

    for (const auto& block : ianaBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;
        for (const ByteString& addr : block.addresses)
            scheduleTimeout(
                Dhcp::TimerType::DECLINE_HOLD, 
                duid + block.iaid + addr,
                addr,
                block.network->config->getNetworkID(),
                dhcpConfigs.declineHoldTime.load(std::memory_order_relaxed)
            );
    }

    for (auto& block : iapdBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;
        for (const auto& [prefix, _] : block.prefixes)
            scheduleTimeout(
                Dhcp::TimerType::DECLINE_HOLD,
                duid + block.iaid + prefix,
                prefix,
                block.network->config->getNetworkID(),
                dhcpConfigs.declineHoldTime.load(std::memory_order_relaxed)
            );
    }

    Dhcpv6Header reply;
    reply.type = Variable::Dhcpv6::Type::reply;
    reply.transactionID = header.transactionID;

    reply.options.emplace_back(
        Variable::Dhcpv6::Options::statusCode,
        Functions::numToByte(2, 2),
        ByteString("\x00\x00", 2) // Success
    );

    if (multicast && dhcpConfigs.allowUnicast.load(std::memory_order_relaxed) && !ianaBlocks.empty())
    {
        auto block = ianaBlocks.begin();
        if (!block->addresses.empty())
        {
            addServerUnicast(reply, block->addresses.front(), iface);
        }
    }

    bool declined = false;
    for (const auto& block : ianaBlocks)
        if (!block.addresses.empty()) declined = true;
    for (const auto& block : iapdBlocks)
        if (!block.prefixes.empty()) declined = true;

    ByteString statusCode = declined ? Variable::Dhcpv6::Status::success
                                     : Variable::Dhcpv6::Status::noBinding;
    std::string msg = declined ? "Address declined"
                               : "Nothing to decline";

    reply.options.push_back(buildStatusOption(statusCode, msg));

    if (!localAddress)
    {
        return reply.encapsulate();
    }

    sendPacket(reply, iface, *localAddress);
    return std::nullopt;
}

std::optional<ByteString> Protocol::Dhcpv6Server::processConfirm(const Dhcpv6Header& header, Interface* iface, bool multicast, const ByteString* localAddress)
{
    auto ianaBlocks = extractIA_NA(header, iface);
    auto iapdBlocks = extractIA_PD(header, iface);

    if (ianaBlocks.empty() && iapdBlocks.empty()) return std::nullopt;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid) || !validateServerID(header) || !trackElapsedTime(header, duid))
        return std::nullopt; // No leases will be given if no Client ID is present

    Dhcpv6Header reply;
    reply.type = Variable::Dhcpv6::Type::reply;
    reply.transactionID = header.transactionID;

    bool allOnLink = true;

    // Validate IA_NA addresses
    for (const auto& block : ianaBlocks)
    {
        if (!block.network || !block.addresses.empty()) continue;

        for (const auto& addr : block.addresses)
        {
            const auto& cfg = block.network->config;
            if (!Functions::compareNetworkWithIp(cfg->getNetwork(), addr, cfg->getPrefixLen()))
            {
                allOnLink = false;
                break;
            }
        }
        if (!allOnLink) break;
    }

    // Validate IA_PD prefixes
    for (const auto& block : iapdBlocks)
    {
        if (!block.network || block.prefixes.empty()) continue;

        for (const auto& [prefix, plen] : block.prefixes)
        {
            const auto& cfg = block.network->config;
            if (!Functions::compareNetworkWithIp(cfg->getNetwork(), prefix, cfg->getPrefixLen()))
            {
                allOnLink = false;
                break;
            }
        }
        if (!allOnLink) break;
    }

    ByteString statusCode = allOnLink
        ? Variable::Dhcpv6::Status::success
        : Variable::Dhcpv6::Status::notOnLink;

    std::string statusMsg = allOnLink ? "Prefix is on-link" : "Prefix not on-link";
    reply.options.push_back(buildStatusOption(statusCode, statusMsg));

    if (multicast && dhcpConfigs.allowUnicast.load(std::memory_order_relaxed) && !ianaBlocks.empty())
    {
        auto block = ianaBlocks.begin();
        if (!block->addresses.empty())
        {
            addServerUnicast(reply, block->addresses.front(), iface);
        }
    }

    if (!localAddress)
    {
        return reply.encapsulate();
    }

    sendPacket(reply, iface, *localAddress);
    return std::nullopt;
}

std::optional<ByteString> Protocol::Dhcpv6Server::processInformationRequest(const Dhcpv6Header& header, Interface* iface, bool multicast, const ByteString* localAddress)
{
    auto oro = extractORO(header);

    // Build minimal reply with requested info (no AI_NA needed)
    Dhcpv6Header reply;
    reply.type = Variable::Dhcpv6::Type::reply;
    reply.transactionID = header.transactionID;

    ByteString duid = extractDUID(header);

    if (!trackElapsedTime(header, duid)) return std::nullopt;

    for (const auto& [_, net] : dhcpNetworks)
    {
        std::vector<Dhcpv6Header::Option> opts = buildOptions(net->config, oro);
        reply.options.insert(reply.options.end(), opts.begin(), opts.end());
        break; // We only need one matching config
    }

    if (!localAddress)
    {
        return reply.encapsulate();
    }

    sendPacket(reply, iface, *localAddress);
    return std::nullopt;
}

void Protocol::Dhcpv6Server::processRelayForward(const Dhcpv6RelayHeader& relay, Interface* iface)
{
    std::vector<Dhcpv6RelayHeader> relayChain;

    auto clientMessage = processRelayChain(relay, relayChain);
    if (!clientMessage.has_value())
    {
        return;
    }

    // Step 1: handle the message and get reply to send
    std::optional<ByteString> dhcpHeader;
    Dhcpv6RelayHeader relayHeader;
    switch (clientMessage->type.data()[0])
    {
        case 0x01: dhcpHeader = processSolicit(*clientMessage, iface, false, nullptr); break;
        case 0x03: dhcpHeader = processRequest(*clientMessage, iface, false, nullptr); break;
        case 0x04: dhcpHeader = processConfirm(*clientMessage, iface, false, nullptr); break;
        case 0x05: dhcpHeader = processRenew(*clientMessage, iface, false, nullptr); break;
        case 0x06: dhcpHeader = processRebind(*clientMessage, iface, false, nullptr); break;
        case 0x08: dhcpHeader = processRelease(*clientMessage, iface, false, nullptr); break;
        case 0x09: dhcpHeader = processDecline(*clientMessage, iface, false, nullptr); break;
        case 0x0B: dhcpHeader = processInformationRequest(*clientMessage, iface, false, nullptr); break;
    }

    if (!dhcpHeader.has_value())
        return;

    // Step 2: Re-encapsulate throught the relay chain in reverse
    for (auto it = relayChain.rbegin(); it != relayChain.rend(); ++it)
    {
        Dhcpv6RelayHeader replyRelay;
        replyRelay.msgType = Variable::Dhcpv6::Type::relayReply;
        replyRelay.hopCount = it->hopCount;
        replyRelay.linkAddress = it->linkAddress;
        replyRelay.peerAddress = it->peerAddress;

        // Add relay-msg option
        replyRelay.options.emplace_back(
            Variable::Dhcpv6::Options::relayMsg,
            Functions::numToByte(dhcpHeader.value().size(), 2),
            dhcpHeader.value()
        );

        // Copy other options
        for (const auto& opt : it->options)
        {
            if (opt.option != Variable::Dhcpv6::Options::relayMsg)
            {
                replyRelay.options.push_back(opt);
            }
        }
        

        // Encapsulate
        auto encapReply = replyRelay.encapsulate();
        if (encapReply.has_value())
            dhcpHeader = std::move(encapReply.value());

        relayHeader = std::move(replyRelay);
    }

    // Step 3: Send final packet from interface
    sendRelayPacket(relayHeader, iface);
}

std::optional<Dhcpv6Header> Protocol::Dhcpv6Server::processRelayChain(
    const Dhcpv6RelayHeader& relay,
    std::vector<Dhcpv6RelayHeader>& relayChain
)
{
    relayChain.push_back(relay);

    for (const auto& opt : relay.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::relayMsg)
        {
            const ByteString& inner = opt.value;
            if (inner.empty())
                return {}; // Malformed

            ByteString msgType = inner.substr(0, 1);

            if (msgType == Variable::Dhcpv6::Type::relayForward)
            {
                // Recurse into inner relay
                Dhcpv6RelayHeader innerRelay;
                if (!innerRelay.decapsulate(inner))
                    return std::nullopt;

                return processRelayChain(innerRelay, relayChain);
            }
            else
            {
                Dhcpv6Header header;
                if (!header.decapsulate(inner))
                    return std::nullopt;

                return header;
            }
        }
    }

    return std::nullopt;
}

void Protocol::Dhcpv6Server::processReconfigAccept(const Dhcpv6Header& header, const ByteString& localAddress, Interface*& iface)
{
    {
        std::lock_guard<std::mutex> lock(trackingMutex);

        ByteString clientID = nullptr;
        bool hasReconfigAccept = false;

        for (const auto& opt : header.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::reconfAccept)
            {
                hasReconfigAccept = opt.value.empty();
            }
            else if (opt.option == Variable::Dhcpv6::Options::clientID)
            {
                clientID = opt.value;
            }
        }

        if (hasReconfigAccept && !clientID.empty())
        {
            clientReconfAccept[clientID] = {localAddress, iface};
        }
    }
}

void Protocol::Dhcpv6Server::sendReconfigure(const ByteString& duid, Dhcpv6::ReconfigReason reason, bool isRelay)
{
    if (!rkapAuthenticationEnabled.load(std::memory_order_relaxed)) return;

    auto getClient = [&](const ByteString& duid) -> std::optional<std::pair<ByteString, Interface*>> {
        std::lock_guard<std::mutex> lock(configMutex);
        auto it = clientReconfAccept.find(duid);
        if (it == clientReconfAccept.end()) return std::nullopt;
        return it->second;
    };

    if (auto client = getClient(duid))
    {
        ByteString validIp;

        // Step 1: see if most recent lease is valid.
        auto it = recentLeases.find(client->first);
        if (it != recentLeases.end())
        {
            // Find lease
            auto network = dhcpNetworks[findMatchingNetwork(it->second)];
            if (network->lease->isAllocated(it->second))
            {
                validIp = it->second;
            }
            else
            {
                recentLeases.erase(it);
            }
        }

        buildReconfigPacket(duid, validIp.empty() ? Variable::Multicast::Dhcp::serverToAllv6 : validIp, client.value(), reason, isRelay);
    }
}

void Protocol::Dhcpv6Server::buildReconfigPacket(const ByteString& clientID, const ByteString& leaseIp, const std::pair<ByteString, Interface*>& ifacePair, Dhcpv6::ReconfigReason reason, bool isRelay)
{
    // Build RECONFIGURE message
    Dhcpv6Header reconfig;
    reconfig.type = Variable::Dhcpv6::Type::reconfigure;
    reconfig.transactionID = generateDhcpTransid();

    // Add server ID
    reconfig.options.emplace_back(
        Variable::Dhcpv6::Options::serverID,
        Functions::numToByte(dhcpUniqueIdentifier.size(), 2),
        dhcpUniqueIdentifier
    );

    // Add Client ID (from lease)
    reconfig.options.emplace_back(
        Variable::Dhcpv6::Options::clientID,
        Functions::numToByte(clientID.size(), 2),
        clientID
    );

    // Reconfigure message type: 5 = Renew
    reconfig.options.emplace_back(
        Variable::Dhcpv6::Options::reconfigureMessage,
        Functions::numToByte(1, 2),
        ByteString(1, static_cast<uint8_t>(reason)) // RENEW
    );

    // Authentication Option (if configured)
    if (delayedAuthenticationEnabled.load(std::memory_order_relaxed))
        addAuthenticationOption(reconfig, clientID);

    if (isRelay)
    {
        // Look up relay chain
        std::optional<Dhcpv6::RelayClient> relay;
        {
            std::lock_guard<std::mutex> lock(trackingMutex);
            auto it = relayClients.find(clientID);
            if (it != relayClients.end())
                relay = it->second;
        }

        if (!relay.has_value()) return;

        // Encapsulate DHCPv6 header
        auto inner = reconfig.encapsulate();
        if (!inner.has_value()) return;

        Dhcpv6RelayHeader relayReply;

        // Re-wrap the message through each hop (in reverse order)
        for (auto it = relay->relayChain.rbegin(); it != relay->relayChain.rend(); ++it)
        {
            relayReply.msgType = Variable::Dhcpv6::Type::relayReply;
            relayReply.hopCount = it->hopCount;
            relayReply.linkAddress = it->linkAddress;
            relayReply.peerAddress = it->peerAddress;

            relayReply.options.clear();
            relayReply.options.emplace_back(
                Variable::Dhcpv6::Options::relayMsg,
                Functions::numToByte(inner->size(), 2),
                *inner
            );

            for (const auto& opt : it->options)
            {
                if (opt.option != Variable::Dhcpv6::Options::relayMsg)
                    relayReply.options.push_back(opt);
            }

            auto encapsulated = relayReply.encapsulate();
            if (!encapsulated.has_value()) return;

            inner = encapsulated;
        }

        // Send relay-encapsulated packet
        if (inner.has_value())
        {
            PacketInfo pkt;
            pkt.Layer5.push_back(relayReply);
            IPPacket::buildUdp(
                ifacePair.second,
                pkt,
                relayReply.peerAddress,
                nullptr,
                nullptr,
                0,
                dhcpConfigs.hopCountLimit.load(std::memory_order_relaxed),
                Variable::Ethernet::ipv6,
                Variable::Udp::dhcpv6Server,
                Variable::Udp::dhcpv6Client
            );
        }
    }
    else
    {
        // Regular (non-relay) sending
        PacketInfo pkt;
        pkt.Layer5.push_back(std::move(reconfig));

        IPPacket::buildUdp(
            ifacePair.second,
            pkt,
            leaseIp,
            nullptr,
            nullptr,
            0,
            dhcpConfigs.hopCountLimit.load(std::memory_order_relaxed),
            Variable::Ethernet::ipv6,
            Variable::Udp::dhcpv6Server,
            Variable::Udp::dhcpv6Client
        );
    }
}

Dhcpv6Header Protocol::Dhcpv6Server::buildResponse(
    const ByteString& type,
    const ByteString& transactionID,
    const ByteString& duid,
    const std::vector<Dhcpv6::IANABlock>& ianaBlocks,
    const std::vector<Dhcpv6::IAPDBlock>& iapdBlocks,
    const std::vector<Dhcpv6::IANABlock>& iataBlocks
)
{
    Dhcpv6Header header;
    header.type = type;
    header.transactionID = transactionID;

    std::optional<bool> hasIANA = std::nullopt;
    std::optional<bool> hasIAPD = std::nullopt;
    std::optional<bool> hasIATA = std::nullopt;
    
    auto buildIAStatus = [&](const ByteString& code, const std::string& msg) -> ByteString {
        ByteString val = code + ByteString(msg);
        ByteString length = Functions::numToByte(val.size(), 2);
        return ByteString(Variable::Dhcpv6::Options::statusCode) + length + val;
    };

    // IA_NA
    for (const auto& block : ianaBlocks)
    {
        if (!block.network || !block.network->config || dhcpNetworks.find(block.network->config->getNetworkID()) == dhcpNetworks.end()) continue;

        ByteString value = block.iaid;
        value += Functions::numToByte(block.t1, 4);
        value += Functions::numToByte(block.t2, 4);

        for (const auto& addr : block.addresses)
        {

            ByteString addrVal = addr;

            addrVal += Functions::numToByte(
                static_cast<uint32_t>(block.network->config->leaseTime.load(std::memory_order_relaxed)
                * block.network->config->t1Percentage.load(std::memory_order_relaxed)), 4);

            addrVal += Functions::numToByte(
                static_cast<uint32_t>(block.network->config->leaseTime.load(std::memory_order_relaxed)
                * block.network->config->t2Percentage.load(std::memory_order_relaxed)), 4);

            value += Variable::Dhcpv6::Options::IAAddr;
            value += Functions::numToByte(addrVal.size(), 2);
            value += addrVal;
        }

        if (block.addresses.empty()) 
        {
            hasIANA = false;
            ByteString status = buildIAStatus(Variable::Dhcpv6::Status::noAddrsAvail, "No address available");
            value += status;
        }
        else
        {
            hasIANA = true;
        }

        // IA_NA
        header.options.emplace_back(
            Variable::Dhcpv6::Options::IA_NA,
            Functions::numToByte(value.size(), 2),
            std::move(value)
        );
    }

    // IA_PD
    for (const auto& block : iapdBlocks)
    {
        if (!block.network || !block.network->config || dhcpNetworks.find(block.network->config->getNetworkID()) == dhcpNetworks.end()) continue;

        ByteString value = block.iaid;
        value += Functions::numToByte(block.t1, 4);
        value += Functions::numToByte(block.t2, 4);

        for (const auto& [prefix, prefixLen] : block.prefixes)
        {
            ByteString pval;

            pval += Functions::numToByte(static_cast<uint32_t>(
                block.network->config->leaseTime.load(std::memory_order_relaxed)
                * block.network->config->t1Percentage.load(std::memory_order_relaxed)), 4);

            pval += Functions::numToByte(
                static_cast<uint32_t>(block.network->config->leaseTime.load(std::memory_order_relaxed)
                * block.network->config->t2Percentage.load(std::memory_order_relaxed)), 4);

            pval += Functions::numToByte(prefixLen, 1);
            pval += ByteString(3, '\x00'); // Reserved
            pval += prefix.substr(0, 16);

            value += Variable::Dhcpv6::Options::IA_Prefix;
            value += Functions::numToByte(pval.size(), 2);
            value += pval;
        }

        if (block.prefixes.empty())
        {
            hasIAPD = false;
            ByteString status = buildIAStatus(Variable::Dhcpv6::Status::noPrefixAvail, "No prefix available");
            value += status;
        }
        else
        {
            hasIAPD = true;
        }

        header.options.emplace_back(
            Variable::Dhcpv6::Options::IA_PD,
            Functions::numToByte(value.size(), 2),
            std::move(value)
        );
    }

    // IA_TA
    for (const auto& block : iataBlocks)
    {
        if (!block.network || !block.network->config || dhcpNetworks.find(block.network->config->getNetworkID()) == dhcpNetworks.end()) continue;

        ByteString value = block.iaid;
        value += Functions::numToByte(0, 4); // T1
        value += Functions::numToByte(0, 4);

        for (const auto& addr : block.addresses)
        {
            ByteString addrVal = addr;

            addrVal += Functions::numToByte(
                static_cast<uint32_t>(block.network->config->leaseTime.load(std::memory_order_relaxed)
                * block.network->config->t1Percentage.load(std::memory_order_relaxed)), 4);

            addrVal += Functions::numToByte(
                static_cast<uint32_t>(block.network->config->leaseTime.load(std::memory_order_relaxed)
                * block.network->config->t2Percentage.load(std::memory_order_relaxed)), 4);

            value += Variable::Dhcpv6::Options::IAAddr;
            value += Functions::numToByte(addrVal.size(), 2);
            value += addrVal;
        }

        if (block.addresses.empty())
        {
            hasIATA = false;
            ByteString status = buildIAStatus(Variable::Dhcpv6::Status::noAddrsAvail, "No temporary address available");
            value += status;
        }
        else
        {
            hasIATA = true;
        }

        header.options.emplace_back(
            Variable::Dhcpv6::Options::IA_TA,
            Functions::numToByte(value.size(), 2),
            std::move(value)
        );
    }

    bool noAddr = true;
    if ((hasIANA.has_value() && hasIANA.value()) ||
        (hasIAPD.has_value() && hasIAPD.value()) ||
        (hasIATA.has_value() && hasIATA.value()) ||
        (!hasIANA.has_value() && !hasIAPD.has_value() && hasIATA.has_value()))
    {
        noAddr = false;
    }

    if (noAddr)
    {
        ByteString status = Variable::Dhcpv6::Status::noAddrsAvail + ByteString("No address available");
        header.options.emplace_back(
            Variable::Dhcpv6::Options::statusCode,
            Functions::numToByte(status.size(), 2),
            status
        );
    }

    // Server ID
    header.options.emplace_back(
        Variable::Dhcpv6::Options::serverID,
        Functions::numToByte(dhcpUniqueIdentifier.size(), 2),
        dhcpUniqueIdentifier
    );

    if (!duid.empty())
    {
        header.options.emplace_back(
            Variable::Dhcpv6::Options::clientID,
            Functions::numToByte(duid.size(), 2),
            duid
        );
    }

    if (delayedAuthenticationEnabled.load(std::memory_order_relaxed))
        addAuthenticationOption(header, duid);

    return header;
}

std::vector<Dhcpv6Header::Option> Protocol::Dhcpv6Server::buildOptions(Dhcp::DhcpNetworkConfig* config, const std::vector<ByteString>& oro)
{
    std::vector<Dhcpv6Header::Option> options;

    {
        std::shared_lock<std::shared_mutex> lock(config->configMutex);

        for (const ByteString& opt : oro)
        {
            if (opt == Variable::Dhcpv6::Options::dnsServer && !config->dnsServer.empty())
            {
                ByteString dnsServer;
                for (const auto& dns : config->dnsServer)
                {
                    if (dns.size() == 16)
                    {
                        dnsServer += dns;
                    }
                }
                if (!dnsServer.empty())
                {
                    options.emplace_back(
                        Variable::Dhcpv6::Options::dnsServer,
                        Functions::numToByte(dnsServer.size(), 2),
                        dnsServer
                    );
                }
            }

            else if (opt == Variable::Dhcpv6::Options::domainSearch && !config->domainName.empty())
            {
                ByteString domainNames;
                for (const auto& name : config->domainName)
                {
                    domainNames += encodeDnsName(name);
                }
                if (!domainNames.empty())
                {
                    options.emplace_back(
                        Variable::Dhcpv6::Options::domainSearch,
                        Functions::numToByte(domainNames.size(), 2),
                        std::move(domainNames)
                    );
                }
            }

            else if (opt == Variable::Dhcpv6::Options::ntpServer && !config->ntpServer.empty())
            {
                ByteString ntpServers;
                for (const auto& ntp : config->ntpServer)
                {
                    if (ntp.size() == 16)
                    {
                        ntpServers += ntp;
                    }
                }
                if (!ntpServers.empty())
                {
                    options.emplace_back(
                        Variable::Dhcpv6::Options::ntpServer,
                        Functions::numToByte(ntpServers.size(), 2),
                        ntpServers
                    );
                }
            }
        }
    }

    return options;
}

void Protocol::Dhcpv6Server::addDelayedAuthKey(uint64_t id, const ByteString& secret, std::chrono::seconds lifetime)
{
    std::lock_guard<std::mutex> lock(authMutex);
    authConfig.delayedKeys.emplace_back(
        id,
        secret,
        std::chrono::steady_clock::now(),
        lifetime
    );
}

void Protocol::Dhcpv6Server::addRKAPAuthKey(const ByteString& secret, std::chrono::seconds lifetime)
{
    std::lock_guard<std::mutex> lock(authMutex);
    authConfig.rkapKeys.emplace_back(
        0,
        secret,
        std::chrono::steady_clock::now(),
        lifetime
    );
}

void Protocol::Dhcpv6Server::cleanupExpiredKeys()
{
    std::lock_guard<std::mutex> lock (authMutex);
    authConfig.rkapKeys.erase(
        std::remove_if(authConfig.rkapKeys.begin(), authConfig.rkapKeys.end(),
            [](const auto& key) { return key.isExpired(); }),
        authConfig.rkapKeys.end()
    );

    authConfig.delayedKeys.erase(
        std::remove_if(authConfig.delayedKeys.begin(), authConfig.delayedKeys.end(),
            [](const auto& key) { return key.isExpired(); }),
        authConfig.delayedKeys.end()
    );
}

bool Protocol::Dhcpv6Server::addAuthenticationOption(Dhcpv6Header& header, const ByteString& duid)
{
    if (!delayedAuthenticationEnabled.load(std::memory_order_relaxed)) return false;

    ByteString secret;
    Protocol::Dhcpv6::AuthConfig::AuthProtocol protocol;
    Protocol::Dhcpv6::AuthConfig::AuthAlgorithm algorithm;
    Protocol::Dhcpv6::AuthConfig::AuthRDM rdm;
    uint64_t counter = 0;

    {
        std::lock_guard<std::mutex> lock(authMutex);

        // Determin Protocol
        uint8_t type = header.type[0];
        if (Variable::Dhcpv6::Type::reconfigure[0] == type)
        {
            if (authConfig.rkapKeys.empty()) return false;
            secret = authConfig.rkapKeys.front().secret;
            protocol = Protocol::Dhcpv6::AuthConfig::AuthProtocol::RKAP;
            algorithm = Protocol::Dhcpv6::AuthConfig::AuthAlgorithm::HMACMD5;
            rdm = Protocol::Dhcpv6::AuthConfig::AuthRDM::TIMESTAMP;
            counter = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        else
        {
            auto it = authConfig.clientToKey.find(duid);
            auto key = getKey(it->second);
            if (authConfig.delayedKeys.empty() || it == authConfig.clientToKey.end() || !key.has_value()) return false;
            secret = key->secret;
            protocol = Protocol::Dhcpv6::AuthConfig::AuthProtocol::DELAYED;
            algorithm = Protocol::Dhcpv6::AuthConfig::AuthAlgorithm::HMACSHA1;
            rdm = Protocol::Dhcpv6::AuthConfig::AuthRDM::MONO;
            counter = authConfig.replayCounter[duid];
        }
    }

    // 1. Construct auth option with zeroes MAC
    ByteString authValue;
    authValue += Functions::numToByte(static_cast<uint8_t>(protocol), 1);
    authValue += Functions::numToByte(static_cast<uint8_t>(algorithm), 1);
    authValue += Functions::numToByte(static_cast<uint8_t>(rdm), 1);
    authValue += Functions::numToByte(counter, 8); // Replay counter
    authValue += ByteString(16, 0x00); // Placeholder MAC

    Dhcpv6Header::Option authOption;
    authOption.option = Variable::Dhcpv6::Options::auth;
    authOption.length = Functions::numToByte (authValue.size(), 2);
    authOption.value = authValue;
    header.options.push_back(authOption);

    // 2. Get full message with zeroes MAC
    auto encapsulated = header.encapsulate();
    if (!encapsulated.has_value()) return false;
    
    ByteString raw = encapsulated.value();

    ByteString mac;
    if (protocol == Protocol::Dhcpv6::AuthConfig::AuthProtocol::DELAYED)
    {
        mac = Authentication::generateHMAC(raw, secret, "SHA1");
    }
    else if (protocol == Protocol::Dhcpv6::AuthConfig::AuthProtocol::RKAP)
    {
        mac = Authentication::generateHMAC(raw, secret, "MD5");
    }

    // 3. Replace MAC at offset
    if (mac.size() == 16)
        authOption.value.replace(11, 16, mac);
    header.options.back() = authOption;
    return true;
}

bool Protocol::Dhcpv6Server::validateAuthentication(const Dhcpv6Header& header, const ByteString& duid)
{
    for (const auto& opt : header.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::auth && opt.value.size() >= 13 + 16)
        {
            Dhcpv6::AuthConfig::AuthProtocol protocol = static_cast<Protocol::Dhcpv6::AuthConfig::AuthProtocol>(opt.value[0].value);
            auto algorithm = static_cast<Protocol::Dhcpv6::AuthConfig::AuthAlgorithm>(opt.value[1].value);
            auto rdm = static_cast<Protocol::Dhcpv6::AuthConfig::AuthRDM>(opt.value[2].value);
            uint64_t counter = Functions::byteToNum(opt.value.substr(3, 8));
            ByteString receivedMAC = opt.value.substr(11, 16);

            // Validate counter
            if (counter <= authConfig.replayCounter[duid]) return false;

            // Re-encapsulate with MAC zeroed
            Dhcpv6Header headerCopy = header;
            for (auto& o : headerCopy.options)
            {
                if (o.option == Variable::Dhcpv6::Options::auth)
                {
                    o.value.replace(11, 16, ByteString(16, 0x00));
                    break;
                }
            }

            auto rawEncap = headerCopy.encapsulate();
            if (!rawEncap.has_value()) return false;

            auto validateAuth = ([&](const Dhcpv6::AuthConfig::Key key) -> bool {
                ByteString mac = (protocol == Dhcpv6::AuthConfig::AuthProtocol::DELAYED)
                    ? Authentication::generateHMAC(rawEncap.value(), key.secret, "SHA1")
                    : Authentication::generateHMAC(rawEncap.value(), key.secret, "MD5");
                return mac == receivedMAC;
            });

            std::lock_guard<std::mutex> lock(authMutex);
            bool validated = false;

            {
                auto clientIt = authConfig.clientToKey.find(duid);
                if (clientIt != authConfig.clientToKey.end())
                {
                    auto key = getKey(clientIt->second);
                    if (!key.has_value()) return false;
                    if (validateAuth(key.value()))
                    {
                        validated = true;
                    }
                }
                else
                {
                    for (const auto& key : authConfig.delayedKeys)
                    {
                        if (validateAuth(key))
                        {
                            authConfig.clientToKey[duid] = key.keyId;
                            validated = true;
                        }
                    }
                }

                if (!validated) return false;
            }

            authConfig.replayCounter[duid] = counter;
            return true;
        }
    }
    return !delayedAuthenticationEnabled.load(std::memory_order_relaxed);
}

std::optional<Protocol::Dhcpv6::AuthConfig::Key> Protocol::Dhcpv6Server::getKey(uint64_t keyID)
{
    cleanupExpiredKeys();
    std::lock_guard<std::mutex> lock(authMutex);
    for (const auto& key : authConfig.delayedKeys)
    {
        if (key.keyId == keyID)
        {
            return key;
        }
    }
    return std::nullopt;
}

bool Protocol::Dhcpv6Server::validateStatusCode(const std::vector<Dhcpv6Header::Option>& options)
{
    for (const auto& option : options)
    {
        if (option.option == Variable::Dhcpv6::Options::statusCode)
        {
            if (option.value.size() < 2) return false;
            uint16_t status = static_cast<uint16_t>(Functions::byteToNum(option.value));
            
            // Store status code for client
            ByteString clientID;
            for (const auto& opt : options)
            {
                if (opt.option == Variable::Dhcpv6::Options::clientID)
                {
                    clientID = opt.value;
                    break;
                }
            }

            if (!clientID.empty())
            {
                clientStatusCodes[clientID] = status;
            }

            // Check if status is success (0) or other acceptable codes
            return status == 0 || status == 1 || status == 2;
        }
    }
    return true; // No status code is valid
}

bool Protocol::Dhcpv6Server::handleRapidCommit(const std::vector<Dhcpv6Header::Option>& options)
{
    // Check if Rapid Commit option is present
    for (const auto& option : options)
    {
        if (option.option == Variable::Dhcpv6::Options::rapidCommit)
        {
            return option.value.empty();
        }
    }
    return false;
}

bool Protocol::Dhcpv6Server::validateRelayMessage(const std::vector<Dhcpv6Header::Option>& options)
{
    bool foundRelayMsg;

    for (const auto& opt : options)
    {
        if (opt.option == Variable::Dhcpv6::Options::relayMsg)
            foundRelayMsg = true;
    }

    return foundRelayMsg;
}

uint8_t Protocol::Dhcpv6Server::getServerPreference(const Dhcp::DhcpNetworkConfig* config, const ByteString& duid)
{
    if (!config) return 0;
    
    uint16_t elapsed = 0;
    {
        std::lock_guard<std::mutex> lock(trackingMutex);
        auto it = clientElapsedTime.find(duid);
        if (it != clientElapsedTime.end())
            elapsed = it->second;
    }

    uint8_t base = config->serverPreference.load(std::memory_order_relaxed);
    uint8_t boost = std::min<uint16_t>(elapsed / 10, 155);
    return std::min<uint16_t>(base + boost, 255);
}

void Protocol::Dhcpv6Server::sendPacket(Dhcpv6Header& header, Interface* iface, const ByteString& destination)
{
    if (!iface || iface->shutdownFlag.load(std::memory_order_relaxed)) return;

    PacketInfo pkt;
    pkt.Layer5.push_back(std::move(header));

    IPPacket::buildUdp(
        iface,
        pkt,
        destination,
        nullptr,
        nullptr,
        0,
        dhcpConfigs.hopCountLimit.load(std::memory_order_relaxed),
        Variable::Ethernet::ipv6,
        Variable::Udp::dhcpv6Server,
        Variable::Udp::dhcpv6Client
    );
}

void Protocol::Dhcpv6Server::sendRelayPacket(Dhcpv6RelayHeader& header, Interface* iface)
{
    ByteString peerAddress = header.peerAddress;

    PacketInfo pkt;
    pkt.Layer5.push_back(std::move(header));

    if (iface)
    {
        IPPacket::buildUdp(
            iface,
            pkt,
            header.peerAddress,
            nullptr,
            nullptr,
            0,
            dhcpConfigs.hopCountLimit.load(std::memory_order_relaxed),
            Variable::Ethernet::ipv6,
            Variable::Udp::dhcpv6Server,
            Variable::Udp::dhcpv6Client
        );
    }
}

ByteString Protocol::Dhcpv6Server::generateUniqueIdentifier()
{
    // DUID-LL (Type 3) + Hardware type (1 = Ethernet) + MAC
    ByteString duid;
    duid += Functions::numToByte(3, 2); // DUID Type: 3 = DUID-LL
    duid += Functions::numToByte(1, 2); // Hardware Type: 1 = Ethernet

    // Pick any interface MAC address (for now first interface)
    for (const auto& [_, net] : dhcpNetworks)
    {
        if (net && net->config && net->config->interface)
        {
            duid += net->config->interface->configs.getMac();
            break;
        }
    }
    if (Configs::macAddressList.GigabitEthernet.size() == 0)
    {
        throw std::runtime_error("No MAC address available for DUID");
    }

    duid += ByteString(Configs::macAddressList.GigabitEthernet.front());

    return duid;
}

Dhcpv6Header::Option Protocol::Dhcpv6Server::buildStatusOption(const ByteString& code, const std::string& message)
{
    Dhcpv6Header::Option statusOpt;
    statusOpt.option = Variable::Dhcpv6::Options::statusCode;
    statusOpt.length = Functions::numToByte(2 + message.size(), 2);
    statusOpt.value = code + message;
    return statusOpt;
}

bool Protocol::Dhcpv6Server::validateServerID(const Dhcpv6Header& header)
{
    for (const auto& opt : header.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::serverID && 
            opt.value == dhcpUniqueIdentifier)
        {
            return true;
        }
    }
    return false;
}

ByteString Protocol::Dhcpv6Server::extractDUID(const Dhcpv6Header& header)
{
    for (const auto& opt : header.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::clientID)
        {
            return opt.value;
        }
    }
    return {};
}

bool Protocol::Dhcpv6Server::trackElapsedTime(const Dhcpv6Header& header, const ByteString& duid)
{
    for (const auto& opt : header.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::elapsedTime && opt.value.size() == 2)
        {
            uint16_t elapsed = static_cast<uint16_t>(Functions::byteToNum(opt.value));
            std::lock_guard<std::mutex> lock(trackingMutex);
            clientElapsedTime[duid] = elapsed;
            return true;
        }
    }
    return false;
}

void Protocol::Dhcpv6Server::addServerUnicast(Dhcpv6Header& header, const ByteString& leaseIp, Interface* iface)
{
    ByteString ip;
    if (Functions::isGlobalUnicast(leaseIp))
    {
        ip = iface->configs.ipv6.getGlobalUnicast();
    }
    else if (Functions::isLocalUnicast(leaseIp))
    {
        ip = iface->configs.ipv6.getLocalUnicast();
    }
    if (!ip.empty() && ip.size() == 16)
    {
        header.options.emplace_back(
            Variable::Dhcpv6::Options::unicast,
            Functions::numToByte(ip.size(), 2),
            ip
        );
    }
}
