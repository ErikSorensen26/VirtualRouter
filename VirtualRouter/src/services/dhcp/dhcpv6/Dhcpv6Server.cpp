#include "Dhcpv6Server.h"
#include <Interface.h>
#include <Functions.h>
#include <PacketStructure.h>
#include <Encryption.hpp>
#include <TimeManager.h>
#include <IPPacket.h>
#include <Configs.h>

Protocol::Dhcpv6Server::Dhcpv6Server()
{
    dhcpUniqueIdentifier = generateUniqueIdentifier();
    startServer();
}

Protocol::Dhcpv6Server::~Dhcpv6Server()
{
    stopServer();
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

void Protocol::Dhcpv6Server::handleDhcpPacket(const PacketInfo& packet, Interface* iface)
{
    if ((packet.Layer5.empty() || !std::holds_alternative<Dhcpv6Header>(packet.Layer5[0])))
        return;
    
    const Dhcpv6Header& header = std::get<Dhcpv6Header>(packet.Layer5[0]);
    const ByteString& msgType = header.type;

    switch (msgType[0])
    {
    case 0x01:
        processSolicit(header, iface);
        break;
    case 0x03:
        processRequest(header, iface);
        break;
    case 0x04:
        processConfirm(header, iface);
        break;
    case 0x05:
        processRenew(header, iface);
        break;
    case 0x06:
        processRebind(header, iface);
        break;
    case 0x08:
        processRelease(header, iface);
        break;
    case 0x09:
        processDecline(header, iface);
        break;
    case 0x0B:
        processInformationRequest(header, iface);
        break;
    case 0x0E:
        processEchoRequest(header, iface);
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
        if (Functions::compareNetworkWithIp(net->config->network, ip, net->config->subnetPrefix))
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

void Protocol::Dhcpv6Server::processSolicit(const Dhcpv6Header& header, Interface* iface)
{
    auto ianaBlocks = extractIA_NA(header, iface);
    auto iapdBlocks = extractIA_PD(header, iface);
    auto iataBlocks = extractIA_TA(header, iface);

    if (ianaBlocks.empty() && iapdBlocks.empty() && iataBlocks.empty()) return;

    auto oro = extractORO(header);
    bool rapidCommitRequest = handleRapidCommit(header.options);

    // Extract Client ID (DUID)
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid))
        return; // No leases will be given without a Client ID.

    if (rapidCommitRequest)
    {
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
                        block.network->lease->allocateRequestedIP(requestedIP, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage, &id))
                    {
                        confirmed.push_back(requestedIP);
                    }
                }
                block.addresses = confirmed;
            }
            else
            {
                // No requested addresses, just allocate one
                ByteString id = duid + block.iaid;
                ByteString ip = block.network->lease->allocateIP(block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage, &id, true);
                if (!ip.empty())
                {
                    block.addresses.push_back(ip);
                }
                else
                {
                    //TODO
                }
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
                    if (!block.network->pool->isAllocatedOrExcluded(prefix) &&
                        block.network->prefixLease->allocateRequestedPrefix(duid + block.iaid + prefix, prefix, length, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage))
                        block.prefixes.emplace_back(prefix, length);
                }
                block.prefixes = confirmed;
            }
            else
            {
                // No specified prefix requested, assign a default
                auto allocated = block.network->prefixLease->allocatePrefix(duid + block.iaid, block.network->config->defaultSubnetPrefix, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage, true);
                if (!allocated.first.empty())
                    block.prefixes.push_back(allocated);
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
                        block.network->lease->allocateRequestedIP(requestedIP, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage, &id))
                    {
                        confirmed.push_back(requestedIP);
                        //std::cout << requestedIP << std::endl;
                    }
                }
                block.addresses = confirmed;
            }
            else
            {
                // No requested addresses, just allocate one
                ByteString ip = block.network->lease->allocateIP(block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage, nullptr);
                if (!ip.empty())
                    block.addresses.push_back(ip);
                //std::cout << ip.toHex() << std::endl;
            }
        }

        Dhcpv6Header reply = buildResponse(
            Variable::Dhcpv6::Type::reply,
            header.transactionID,
            ianaBlocks,
            iapdBlocks,
            iataBlocks
        );

        // Add arpid Commit option to server response
        reply.options.push_back({
            Variable::Dhcpv6::Options::rapidCommit,
            Functions::numToByte(0, 2),
            {}
        });

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

        sendPacket(reply, iface);
        return;
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
            auto offered = block.network->prefixPool->allocateTempPrefix(duid + block.iaid, block.network->config->defaultSubnetPrefix, true);
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
        ianaBlocks,
        iapdBlocks,
        iataBlocks
    );

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
            Dhcpv6Header::Option prefOpt;
            prefOpt.option = Variable::Dhcpv6::Options::preference;
            prefOpt.length = Functions::numToByte(1, 2);
            prefOpt.value = ByteString(1, getServerPreference(block.network->config));
            advertise.options.push_back(prefOpt);
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

    sendPacket(advertise, iface);
}

void Protocol::Dhcpv6Server::processRequest(const Dhcpv6Header& header, Interface* iface)
{
    auto ianaBlocks = extractIA_NA(header, iface);
    auto iapdBlocks = extractIA_PD(header, iface);
    auto iataBlocks = extractIA_TA(header, iface);
    if (ianaBlocks.empty() && iapdBlocks.empty()) return;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid) || !validateServerID(header))
        return; // No leases will be given if no Client ID is present

    // Validate Server Identifier
    ByteString expectedServerID = ByteString("\x00\x03", 2) + ByteString("\x00\x00\x00\x00\x00\x01", 6);
    if (!validateServerID(header)) return;

    for (auto& block : ianaBlocks)
    {
        if (!block.network || !block.network->lease) continue;
        ByteString id = duid + block.iaid;

        std::vector<ByteString> committed;
        for (const ByteString& addr : block.addresses)
        {
            if (block.network->lease->isAllocated(addr))
            {
                block.network->lease->renewLease(addr);
                committed.push_back(addr);
            }
            else if (outgoingRequests.count(addr) && block.network->pool->isTemporarilyOffered(addr))
            {
                ByteString reserved = block.network->lease->activateLeaseFromTemp(addr, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage, &id);
                if (!reserved.empty()) 
                {
                    committed.push_back(reserved);
                    cancelTimeout(Dhcp::TimerType::IP_OFFER_TIMEOUT, duid + block.iaid, reserved);
                    cancelTimeout(Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT, duid + block.iaid, reserved);
                    outgoingRequests.erase(reserved);
                }
            }
        }

        // If no requested address or all failed, allocate on dynamically
        if (committed.empty())
        {
            ByteString dynamic = block.network->lease->allocateIP(block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage, &id);
            if (!dynamic.empty()) committed.push_back(dynamic);
        }

        block.addresses = committed;
    }

    // Handle IA_PD
    for (auto& block : iapdBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;

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
                auto activated = block.network->prefixLease->activateLeaseFromTemp(duid + block.iaid, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage);
                if (!activated.first.empty())
                {
                    committed.push_back(activated);
                    cancelTimeout(Dhcp::TimerType::PREFIX_OFFER_TIMEOUT, duid + block.iaid, activated.first);
                    cancelTimeout(Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT, duid + block.iaid, activated.first);
                    outgoingRequests.erase(activated.first);
                }
            }
        }

        if (committed.empty())
        {
            auto dyn = block.network->prefixLease->allocatePrefix(duid + block.iaid, block.network->config->defaultSubnetPrefix, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage);
            if (!dyn.first.empty()) committed.push_back(dyn);
        }

        block.prefixes = committed;
    }

    // Handle IA_TA
    for (auto& block : iataBlocks)
    {
        if (!block.network || !block.network->lease) continue;
        ByteString id = duid + block.iaid;
        
        std::vector<ByteString> committed;
        for (const ByteString& addr : block.addresses)
        {
            if (block.network->lease->isAllocated(addr))
            {
                block.network->lease->renewLease(addr);
                committed.push_back(addr);
            }
            else if (block.network->pool->isTemporarilyOffered(addr))
            {
                ByteString reserved = block.network->lease->activateLeaseFromTemp(addr, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage, nullptr);
                if (!reserved.empty()) committed.push_back(reserved);
            }
        }

        if (committed.empty())
        {
            ByteString dynamic = block.network->lease->allocateIP(block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage, nullptr);
            if (!dynamic.empty()) committed.push_back(dynamic);
        }

        block.addresses = committed;
    }

    // Send REPLY with lease confirmation
    auto oro = extractORO(header);
    Dhcpv6Header reply = buildResponse(
        Variable::Dhcpv6::Type::reply,
        header.transactionID,
        ianaBlocks,
        iapdBlocks,
        iataBlocks
    );

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

    if (anyCommitted)
    {
        std::lock_guard<std::mutex> lock(reconfigMutex);
        auto it = activeReconfigs.find(duid);
        if (it != activeReconfigs.end())
        {
            TimeManager::getInstance().cancelTimer(it->second.timerID);
            activeReconfigs.erase(it);
        }
    }

    sendPacket(reply, iface);
}

void Protocol::Dhcpv6Server::processRenew(const Dhcpv6Header& header, Interface* iface)
{
    auto ianaBlocks = extractIA_NA(header, iface);
    auto iapdBlocks = extractIA_PD(header, iface);
    auto iataBlocks = extractIA_TA(header, iface);

    if (ianaBlocks.empty() && iapdBlocks.empty()) return;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid) || !validateServerID(header))
        return; // No leases will be given if no Client ID is present

    // Validate Server Identifier
    if (!validateServerID(header)) return;

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
        ianaBlocks,
        iapdBlocks,
        iataBlocks
    );

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

    sendPacket(reply, iface);
}

void Protocol::Dhcpv6Server::processRebind(const Dhcpv6Header& header, Interface* iface)
{
    auto ianaBlocks = extractIA_NA(header, iface);
    auto iapdBlocks = extractIA_PD(header, iface);

    if (ianaBlocks.empty() && iapdBlocks.empty()) return;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid) || !validateServerID(header))
        return; // No leases will be given if no Client ID is present

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
        ianaBlocks,
        iapdBlocks,
        {}
    );

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

    sendPacket(reply, iface);
}

void Protocol::Dhcpv6Server::processRelease(const Dhcpv6Header& header, Interface* iface)
{
    auto ianaBlocks = extractIA_NA(header, iface);
    auto iapdBlocks = extractIA_PD(header, iface);

    if (ianaBlocks.empty() && iapdBlocks.empty()) return;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid) || !validateServerID(header))
        return; // No leases will be given if no Client ID is present

    for (const auto& block : ianaBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;
        for (const ByteString& addr : block.addresses)
            scheduleTimeout(
                Dhcp::TimerType::RELEASE_HOLD,
                duid + block.iaid,
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
                duid + block.iaid,
                prefix,
                block.network->config->getNetworkID(),
                dhcpConfigs.declineHoldTime.load(std::memory_order_relaxed)
            );
    }

    Dhcpv6Header reply;
    reply.type = Variable::Dhcpv6::Type::reply;
    reply.transactionID = header.transactionID;

    reply.options.push_back({
        Variable::Dhcpv6::Options::statusCode,
        Functions::numToByte(2, 2),
        ByteString("\x00\x00", 2) // Success
    });

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

    sendPacket(reply, iface);
}

void Protocol::Dhcpv6Server::processDecline(const Dhcpv6Header& header, Interface* iface)
{
    auto ianaBlocks = extractIA_NA(header, iface);
    auto iapdBlocks = extractIA_PD(header, iface);

    if (ianaBlocks.empty() && iapdBlocks.empty()) return;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid) || !validateServerID(header))
        return; // No leases will be given if no Client ID is present

    {
        std::lock_guard<std::mutex> lock(declineMutex);

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
                        return; // Rate limit exceeded
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
                        return; // Rate limit exceeded
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
                duid + block.iaid,
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
                duid + block.iaid,
                prefix,
                block.network->config->getNetworkID(),
                dhcpConfigs.declineHoldTime.load(std::memory_order_relaxed)
            );
    }

    Dhcpv6Header reply;
    reply.type = Variable::Dhcpv6::Type::reply;
    reply.transactionID = header.transactionID;

    reply.options.push_back({
        Variable::Dhcpv6::Options::statusCode,
        Functions::numToByte(2, 2),
        ByteString("\x00\x00", 2) // Success
    });

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

    sendPacket(reply, iface);
}

void Protocol::Dhcpv6Server::processConfirm(const Dhcpv6Header& header, Interface* iface)
{
    auto ianaBlocks = extractIA_NA(header, iface);
    auto iapdBlocks = extractIA_PD(header, iface);

    if (ianaBlocks.empty() && iapdBlocks.empty()) return;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid) || !validateServerID(header))
        return; // No leases will be given if no Client ID is present

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
            if (!Functions::compareNetworkWithIp(cfg->network, addr, cfg->subnetPrefix))
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
            if (!Functions::compareNetworkWithIp(cfg->network, prefix, cfg->subnetPrefix))
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

    if (allOnLink)
    {
        std::lock_guard<std::mutex> lock(reconfigMutex);
        auto it = activeReconfigs.find(duid);
        if (it != activeReconfigs.end())
        {
            TimeManager::getInstance().cancelTimer(it->second.timerID);
            activeReconfigs.erase(it);
        }
    }

    sendPacket(reply, iface);
}

void Protocol::Dhcpv6Server::processInformationRequest(const Dhcpv6Header& header, Interface* iface)
{
    auto oro = extractORO(header);

    // Build minimal reply with requested info (no AI_NA needed)
    Dhcpv6Header reply;
    reply.type = Variable::Dhcpv6::Type::reply;
    reply.transactionID = header.transactionID;

    for (const auto& [_, net] : dhcpNetworks)
    {
        std::vector<Dhcpv6Header::Option> opts = buildOptions(net->config, oro);
        reply.options.insert(reply.options.end(), opts.begin(), opts.end());
        break; // We only need one matching config
    }

    sendPacket(reply, iface);
}

void Protocol::Dhcpv6Server::processEchoRequest(const Dhcpv6Header& header, Interface* iface)
{
    ByteString duid = extractDUID(header);
    if (duid.empty()) return;
    if (!validateAuthentication(header, duid)) return;

    for (const auto& opt : header.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::serverID)
        {
            // Invalid - discard
            return;
        }
    }

    Dhcpv6Header reply;
    reply.type = Variable::Dhcpv6::Type::echoReply;
    reply.transactionID = header.transactionID;

    // Server Identifier (required)
    Dhcpv6Header::Option serverID;
    serverID.option = Variable::Dhcpv6::Options::serverID;
    serverID.length = Functions::numToByte(dhcpUniqueIdentifier.size(), 2);
    serverID.value = dhcpUniqueIdentifier;
    reply.options.push_back(serverID);

    // Authentication (if enabled)
    if (authenticationEnabled.load(std::memory_order_relaxed))
    {
        reply.options.push_back(buildAuthenticationOption());
    }

    auto oro = extractORO(header);
    for (const auto& [_, net] : dhcpNetworks)
    {
        std::vector<Dhcpv6Header::Option> opts = buildOptions(net->config, oro);
        reply.options.insert(reply.options.end(), opts.begin(), opts.end());
        break;
    }

    sendPacket(reply, iface);
}

void Protocol::Dhcpv6Server::processRelayForward(const Dhcpv6RelayHeader& relay, Interface* iface)
{
    // Find relay-msg option
    for (const auto& opt : relay.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::relayMsg)
        {
            Dhcpv6Header inner;
            if (!inner.decapsulate(opt.value)) return;

            // Build fake PacketInfo to reuse existing handler
            PacketInfo innerPkt;
            innerPkt.Layer5.push_back(inner);

            // Store relay context for later reply
            {
                std::lock_guard<std::mutex> lock(relayMutex);
                pendingRelays[inner.transactionID] = relay;
            }

            handleDhcpPacket(innerPkt, iface); // Decapsulate and reuse logic
            return;
        }
    }
}

void Protocol::Dhcpv6Server::sendRelayReply(const ByteString& transactionID, const Dhcpv6Header& response)
{
    Dhcpv6RelayHeader relay;
    {
        std::lock_guard<std::mutex> lock(relayMutex);
        auto it = pendingRelays.find(transactionID);
        if (it == pendingRelays.end()) return;
        relay = it->second;
        pendingRelays.erase(it);
    }

    auto serialized = response.encapsulate();
    if (!serialized.has_value()) return;

    Dhcpv6RelayHeader::Option relayOpt;
    relayOpt.option = Variable::Dhcpv6::Options::relayMsg;
    relayOpt.length = Functions::numToByte(serialized->size(), 2);
    relayOpt.value = serialized.value();

    relay.options = { relayOpt };

    // Send back on interface corresponding to the link-address
    Interface* iface = nullptr;

    {
        std::lock_guard<std::mutex> lock(configMutex);
        for (const auto& [_, net] : dhcpNetworks)
        {
            if (net->config && net->config->interface &&
                Functions::compareNetworkWithIp(net->config->network, relay.linkAddress, net->config->subnetPrefix))
            {
                iface = net->config->interface;
                break;
            }
        }
    }

    if (!iface) return;

    sendRelayPacket(relay, iface);
}

void Protocol::Dhcpv6Server::sendReconfigure()
{
    ByteString secret;
    {
        std::lock_guard<std::mutex> lock(authMutex);
        secret = authConfig.sharedSecret;
    }
    std::lock_guard<std::mutex> lock(configMutex);
    for (const auto& [_, net] : dhcpNetworks)
    {
        if (!net || !net->config || !net->config->interface) continue;

        // Iterate over allocated leases
        auto activeLeases = net->lease->getActiveLeases();
        for (const auto& [ip, lease] : activeLeases)
        {
            // Build RECONFIGURE message
            Dhcpv6Header reconfig;
            reconfig.type = Variable::Dhcpv6::Type::reconfigure;
            reconfig.transactionID = generateDhcpTransid();

            // Create or replace state
            {
                std::lock_guard<std::mutex> configMutex(reconfigMutex);
                Dhcpv6::ReconfigureState state;
                state.duid = lease.clientID;
                state.attempts = 1;
                state.iface = net->config->interface;
                state.transactionID = reconfig.transactionID;

                // First attempt is immediate so no need to schedule yet
                activeReconfigs[lease.clientID] = state;
            }

            // Add server ID
            reconfig.options.push_back({
                Variable::Dhcpv6::Options::serverID,
                Functions::numToByte(dhcpUniqueIdentifier.size(), 2),
                dhcpUniqueIdentifier
            });

            // Add Client ID (from lease)
            reconfig.options.push_back({
                Variable::Dhcpv6::Options::clientID,
                Functions::numToByte(lease.clientID.size(), 2),
                lease.clientID
            });

            // Reconfigure message type: 5 = Renew
            reconfig.options.push_back({
                Variable::Dhcpv6::Options::reconfigureMessage,
                Functions::numToByte(1, 2),
                ByteString("\x0b", 1) // RENEW
            });

            // Authentication Option (if configured)
            if (authenticationEnabled.load(std::memory_order_relaxed))
                reconfig.options.push_back(buildAuthenticationOption());

            sendPacket(reconfig, net->config->interface);

            scheduleReconfigureRetry(lease.clientID);
        }
    }
}

void Protocol::Dhcpv6Server::scheduleReconfigureRetry(const ByteString& duid)
{
    std::lock_guard<std::mutex> lock(reconfigMutex);
    auto it = activeReconfigs.find(duid);
    if (it == activeReconfigs.end()) return;

    Dhcpv6::ReconfigureState& state = it->second;

    if (++state.attempts > dhcpConfigs.recMaxRc.load(std::memory_order_relaxed))
    {
        activeReconfigs.erase(it);
        return;
    }

    // Schedule next attempt
    state.timerID = TimeManager::getInstance().addTimer(
        std::chrono::steady_clock::now() + std::chrono::seconds(dhcpConfigs.recTimeout),
        [this, duid]() { retryReconfigure(duid); }
    );
}

void Protocol::Dhcpv6Server::retryReconfigure(const ByteString& duid)
{
    std::lock_guard<std::mutex> lock(reconfigMutex);
    auto it = activeReconfigs.find(duid);
    if (it == activeReconfigs.end()) return;

    Dhcpv6::ReconfigureState& state = it->second;
    if (!state.iface) return;

    // Build RECONFIGURE message
    Dhcpv6Header reconfig;
    reconfig.type = Variable::Dhcpv6::Type::reconfigure;
    reconfig.transactionID = state.transactionID;

    reconfig.options.push_back({
        Variable::Dhcpv6::Options::serverID,
        Functions::numToByte(dhcpUniqueIdentifier.size(), 2),
        dhcpUniqueIdentifier
    });

    reconfig.options.push_back({
        Variable::Dhcpv6::Options::clientID,
        Functions::numToByte(duid.size(), 2),
        duid
    });

    reconfig.options.push_back({
        Variable::Dhcpv6::Options::reconfigureMessage,
        Functions::numToByte(1, 2),
        ByteString("\x0b", 1)
    });

    if (authenticationEnabled.load(std::memory_order_relaxed))
        reconfig.options.push_back(buildAuthenticationOption());

    sendPacket(reconfig, state.iface);

    // Schedule next retry if needed
    scheduleReconfigureRetry(duid);
}

Dhcpv6Header Protocol::Dhcpv6Server::buildResponse(const ByteString& type, const ByteString& transactionID, const std::vector<Dhcpv6::IANABlock>& ianaBlocks, const std::vector<Dhcpv6::IAPDBlock>& iapdBlocks, const std::vector<Dhcpv6::IANABlock>& iataBlocks)
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
            addrVal += Functions::numToByte(static_cast<uint32_t>(block.network->config->leaseTime * block.network->config->t1Percentage), 4);
            addrVal += Functions::numToByte(static_cast<uint32_t>(block.network->config->leaseTime * block.network->config->t2Percentage), 4);

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
        Dhcpv6Header::Option ia_na;
        ia_na.option = Variable::Dhcpv6::Options::IA_NA;
        ia_na.length = Functions::numToByte(value.size(), 2);
        ia_na.value = value;
        header.options.push_back(ia_na);
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
            pval += Functions::numToByte(static_cast<uint32_t>(block.network->config->leaseTime * block.network->config->t1Percentage), 4);
            pval += Functions::numToByte(static_cast<uint32_t>(block.network->config->leaseTime * block.network->config->t2Percentage), 4);
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

        Dhcpv6Header::Option ia_pd;
        ia_pd.option = Variable::Dhcpv6::Options::IA_PD;
        ia_pd.length = Functions::numToByte(value.size(), 2);
        ia_pd.value = value;
        header.options.push_back(ia_pd);
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
            addrVal += Functions::numToByte(static_cast<uint32_t>(block.network->config->leaseTime * block.network->config->t1Percentage), 4);
            addrVal += Functions::numToByte(static_cast<uint32_t>(block.network->config->leaseTime * block.network->config->t2Percentage), 4);

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

        Dhcpv6Header::Option ia_ta;
        ia_ta.option = Variable::Dhcpv6::Options::IA_TA;
        ia_ta.length = Functions::numToByte(value.size(), 2);
        ia_ta.value = value;
        header.options.push_back(ia_ta);
    }

    if (authenticationEnabled.load(std::memory_order_relaxed))
    {
        ByteString authData;
        Dhcpv6::AuthConfig auth;
        {
            std::lock_guard<std::mutex> lock(authMutex);
            auth = authConfig;
            ++authConfig.replayCounter;
        }

        uint16_t protocol = auth.protocol;
        uint16_t algorithm = auth.algorithm;

        authData += Functions::numToByte(protocol, 2);
        authData += Functions::numToByte(algorithm, 2);
        authData += Functions::numToByte(auth.rdm, 1);
        authData += Functions::numToByte(auth.replayCounter, 8);

        ByteString mac;
        if (auth.protocol == 1)
        {
            mac = Authentication::generateMD5(authData, auth.sharedSecret);
        } // TODO add other encryptions

        Dhcpv6Header::Option authOpt;
        authOpt.option = Variable::Dhcpv6::Options::auth;
        authOpt.length = Functions::numToByte(authData.size() + mac.size(), 2);
        authOpt.value = authData + mac;
        {
            authConfig.replayCounter++;
        }
        header.options.push_back(authOpt);
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
        header.options.push_back({
            Variable::Dhcpv6::Options::statusCode,
            Functions::numToByte(status.size(), 2),
            status
        });
    }

    // Server ID
    Dhcpv6Header::Option serverID;
    serverID.option = Variable::Dhcpv6::Options::serverID;
    serverID.length = Functions::numToByte(8, 2);
    serverID.value = dhcpUniqueIdentifier;
    header.options.push_back(serverID);

    return header;
}

std::vector<Dhcpv6Header::Option> Protocol::Dhcpv6Server::buildOptions(const DhcpNetworkConfig* config, const std::vector<ByteString>& oro)
{
    std::vector<Dhcpv6Header::Option> options;

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
                options.push_back({
                    Variable::Dhcpv6::Options::dnsServer,
                    Functions::numToByte(dnsServer.size(), 2),
                    dnsServer
                });
            }
        }

        else if (opt == Variable::Dhcpv6::Options::domainName && !config->domainName.empty())
        {
            ByteString domain = config->domainName;
            if (!domain.empty())
            {
                options.push_back({
                    Variable::Dhcpv6::Options::domainName,
                    Functions::numToByte(domain.size(), 2),
                    domain
                });
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
                options.push_back({
                    Variable::Dhcpv6::Options::ntpServer,
                    Functions::numToByte(ntpServers.size(), 2),
                    ntpServers
                });
            }
        }
    }

    return options;
}

void Protocol::Dhcpv6Server::configureAuthentication(ByteString secret, uint16_t protocol, uint16_t algorithm, uint8_t rdm)
{
    {
        std::lock_guard<std::mutex> lock(authMutex);
        authConfig.sharedSecret = std::move(secret);
        authConfig.protocol = protocol;
        authConfig.algorithm = algorithm;
        authConfig.rdm = rdm;
    }
}

Dhcpv6Header::Option Protocol::Dhcpv6Server::buildAuthenticationOption()
{
    Dhcpv6Header::Option opt;
    opt.option = Variable::Dhcpv6::Options::auth;

    ByteString reply;
    ByteString authWithoutMAC;
    {
        reply = Functions::numToByte(authConfig.replayCounter, 8);
        authWithoutMAC += Functions::numToByte(authConfig.protocol, 2);
        authWithoutMAC += Functions::numToByte(authConfig.algorithm, 2);
        authWithoutMAC += Functions::numToByte(authConfig.rdm, 1);
        authWithoutMAC += reply;
    }
    ByteString mac = Authentication::generateMD5(authWithoutMAC, authConfig.sharedSecret);

    opt.length = Functions::numToByte(authWithoutMAC.size() + mac.size(), 2);
    opt.value = authWithoutMAC + mac;
    {
        authConfig.replayCounter++;
    }
    return opt;
}

bool Protocol::Dhcpv6Server::validateAuthentication(const Dhcpv6Header& header, const ByteString& duid)
{
    Dhcpv6::AuthConfig auth;
    {
        std::lock_guard<std::mutex> lock(authMutex);
        auth = authConfig;
    }
    for (const auto& opt : header.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::auth && opt.value.size() >= 13 + 16)
        {
            uint16_t protocol = static_cast<uint16_t>(Functions::byteToNum(opt.value.substr(0, 2)));
            uint16_t algorithm = static_cast<uint16_t>(Functions::byteToNum(opt.value.substr(2, 2)));
            uint8_t rdm = opt.value[4];
            uint64_t counter = Functions::byteToNum(opt.value.substr(5, 8));
            ByteString receivedMAC = opt.value.substr(13, 16);

            if (protocol != auth.protocol || algorithm != auth.algorithm || rdm != auth.rdm)
                return false;

            // Reply protection
            if (auth.replayCounters.count(duid) && counter <= auth.replayCounters[duid])
                return false;

            ByteString authData = opt.value.substr(0, 13);
            ByteString computedMAC;
            if (protocol == 1)
            {
                computedMAC = Authentication::generateMD5(authData, auth.sharedSecret);
            } // Add others

            if (computedMAC != receivedMAC)
                return false;
    
            {
                std::lock_guard<std::mutex> lock(authMutex);
                authConfig.replayCounters[duid] = counter;
                return true;
            }
        }
    }
    return true;
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
            return true;
        }
    }
    return false;
}

bool Protocol::Dhcpv6Server::validateRelayMessage(const std::vector<Dhcpv6Header::Option>& options)
{
    bool foundRelayMsg;
    uint8_t hopCount = 0;

    for (const auto& opt : options)
    {
        if (opt.option == Variable::Dhcpv6::Options::relayMsg)
            foundRelayMsg = true;
        else if (opt.option == Variable::Dhcpv6::Options::hopcount && !opt.value.empty())
            hopCount = opt.value[0];
    }

    return foundRelayMsg && hopCount <= dhcpConfigs.hopCountLimit.load(std::memory_order_relaxed);
}

uint8_t Protocol::Dhcpv6Server::getServerPreference(const DhcpNetworkConfig* config)
{
    if (!config)
    {
        return 0;
    }
    else
    {
        std::lock_guard<std::mutex> lock(configMutex);
        return config->serverPreference;
    }
}

void Protocol::Dhcpv6Server::sendPacket(Dhcpv6Header& header, Interface* iface)
{
    if (!iface || iface->shutdownFlag.load(std::memory_order_relaxed)) return;

    PacketInfo pkt;
    pkt.Layer5.push_back(std::move(header));

    IPPacket::buildUdp(iface, pkt, Variable::IPv6::source, &Variable::IPv6::source, &Variable::Mac::broadcast, 0, dhcpConfigs.hopCountLimit.load(), Variable::Ethernet::ipv6, Variable::Udp::dhcpv6Server, Variable::Udp::dhcpv6Client);
}

void Protocol::Dhcpv6Server::sendRelayPacket(Dhcpv6RelayHeader& header, Interface* iface)
{
    ByteString peerAddress = header.peerAddress;

    PacketInfo pkt;
    pkt.Layer5.push_back(std::move(header));

    if (iface)
    {
        IPPacket::buildUdp(iface, pkt, Variable::IPv6::source, &Variable::IPv6::source, &peerAddress, 0, dhcpConfigs.hopCountLimit.load(std::memory_order_relaxed), Variable::Ethernet::ipv6, Variable::Udp::dhcpv6Server, Variable::Udp::dhcpv6Client);
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
        if (net && net->config && net->config->interface && net->config->interface->Get())
        {
            duid += net->config->interface->configs.macAddress;
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

Dhcpv6Header::Option Protocol::Dhcpv6Server::buildStatusOption(const ByteString& code, const std::string& message)
{
    Dhcpv6Header::Option statusOpt;
    statusOpt.option = Variable::Dhcpv6::Options::statusCode;
    statusOpt.length = Functions::numToByte(2 + message.size(), 2);
    statusOpt.value = code + message;
    return statusOpt;
}
