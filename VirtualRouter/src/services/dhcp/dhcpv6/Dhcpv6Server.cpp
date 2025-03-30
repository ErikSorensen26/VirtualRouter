#include "Dhcpv6Server.h"
#include <Interface.h>
#include <Functions.h>
#include <PacketStructure.h>
#include <Encryption.hpp>
#include <TimeManager.h>
#include <IPPacket.h>

Protocol::Dhcpv6Server::Dhcpv6Server()
{
    dhcpUniqueIdentifier = generateUniqueIdentifier();
    stopFlag.store(false, std::memory_order_release);
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
    if (serverThread.joinable())
        serverThread.join();

    std::lock_guard<std::mutex> lock(configMutex);
    for (auto& [_, net] : dhcpNetworks)
    {
        if (net) delete net;
    }
    dhcpNetworks.clear();
}

void Protocol::Dhcpv6Server::handleDhcpPacket(const PacketInfo& packet)
{
    if ((packet.Layer5.empty() || !std::holds_alternative<Dhcpv6Header>(packet.Layer5[0])))
        return;
    
    const Dhcpv6Header& header = std::get<Dhcpv6Header>(packet.Layer5[0]);
    const ByteString& msgType = header.type;

    switch (msgType[0])
    {
    case 0x01:
        processSolicit(header);
            break;
    case 0x03:
        processRequest(header);
            break;
    case 0x04:
        processConfirm(header);
            break;
    case 0x05:
        processRenew(header);
            break;
    case 0x06:
        processRebind(header);
            break;
    case 0x08:
        processRelease(header);
            break;
    case 0x09:
        processDecline(header);
            break;
    case 0x0B:
        processInformationRequest(header);
            break;
    case 0x0E:
        processEchoRequest(header);
            }
        }

void Protocol::Dhcpv6Server::dhcpHandler()
{
    while (!stopFlag.load(std::memory_order_relaxed))
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        for (auto& [_, net] : dhcpNetworks)
        {
            net->lease->cleanupExpiredLeases();
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

std::vector<Protocol::Dhcpv6::IANABlock> Protocol::Dhcpv6Server::extractIA_NA(const Dhcpv6Header& header)
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

                if (code == Variable::Dhcpv6::Options::IAAddr && len >= 16)
                    block.addresses.push_back(opt.value.substr(offset + 4, 16));
                
                offset += 4 + len;
            }

            if (!block.addresses.empty())
            {
                ByteString networkID = findMatchingNetwork(block.addresses[0]);
                {
                    std::lock_guard<std::mutex> lock(configMutex);
                    block.network = dhcpNetworks[findMatchingNetwork(block.addresses[0])];
                }
            };

            results.push_back(std::move(block));
        }
    }
    return results;
}

std::vector<Protocol::Dhcpv6::IAPDBlock> Protocol::Dhcpv6Server::extractIA_PD(const Dhcpv6Header& header)
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

                if (code == Variable::Dhcpv6::Options::IA_Prefix && len >= 17)
                {
                    uint8_t plen = static_cast<uint8_t>(opt.value[offset + 4]);
                    ByteString prefix = opt.value.substr(offset + 5, 16);
                    block.prefixes.emplace_back(prefix, plen);
                }

                offset += 4 + len;
            }

            if (!block.prefixes.empty())
            {
                ByteString networkID = findMatchingNetwork(block.prefixes[0].first);
                {
                    std::lock_guard<std::mutex> lock(configMutex);
                    block.network = dhcpNetworks[networkID];
                }
            }

            results.push_back(std::move(block));
        }
    }
    return results;
}

std::vector<Protocol::Dhcpv6::IANABlock> Protocol::Dhcpv6Server::extractIA_TA(const Dhcpv6Header& header)
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

                if (code == Variable::Dhcpv6::Options::IAAddr && len >= 16)
                    block.addresses.push_back(opt.value.substr(offset + 4, 16));

                offset += 4 + len;
            }

            if (!block.addresses.empty())
            {
                ByteString networkID = findMatchingNetwork(block.addresses[0]);
                {
                    std::lock_guard<std::mutex> lock(configMutex);
                    block.network = dhcpNetworks[networkID];
                }
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

void Protocol::Dhcpv6Server::processSolicit(const Dhcpv6Header& header)
{
    auto ianaBlocks = extractIA_NA(header);
    auto iapdBlocks = extractIA_PD(header);
    auto iataBlocks = extractIA_TA(header);

    if (ianaBlocks.empty() && iapdBlocks.empty() && iataBlocks.empty()) return;

    auto oro = extractORO(header);
    bool rapidCommitRequest = handleRapidCommit(header.options);

    // Extract Client ID (DUID)
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid))
        return; // No leases will be given without a Client ID.

    Interface* iface = nullptr;

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
                    if (!block.network->pool->isAllocatedOrExcluded(requestedIP) && 
                        block.network->lease->allocateRequestedIP(duid, requestedIP, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage))
                    {
                        confirmed.push_back(requestedIP);
                    }
                }
                block.addresses = confirmed;
            }
            else
            {
                // No requested addresses, just allocate one
                ByteString ip = block.network->lease->allocateIP(duid, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage);
                if (!ip.empty())
                    block.addresses.push_back(ip);
            }

            if (!iface && block.network->config->interface)
                iface = block.network->config->interface;
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
                        block.network->prefixLease->allocateRequestedPrefix(duid, prefix, length, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage))
                        block.prefixes.emplace_back(prefix, length);
                }
                block.prefixes = confirmed;
            }
            else
            {
                // No specified prefix requested, assign a default
                auto allocated = block.network->prefixLease->allocatePrefix(duid, block.network->config->subnetPrefix, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage);
                if (!allocated.first.empty())
                    block.prefixes.push_back(allocated);
            }

            if (!iface && block.network->config->interface)
                iface = block.network->config->interface;
        }

        for (auto& block : iataBlocks)
        {
            if (!block.network || !block.network->lease) continue;

            ByteString ip = block.network->lease->allocateIP(duid, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage);
            if (!ip.empty()) block.addresses.push_back(ip);

            if (!iface && block.network->config->interface)
                iface = block.network->config->interface;
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

        if (block.addresses.empty())
        {
            ByteString offeredIP = block.network->pool->allocateTempIP(duid);
            if (!offeredIP.empty())
            {
                block.addresses.push_back(offeredIP);
                // Schedule offer timeout
                scheduleTimeout(
                    Dhcp::TimerType::IP_OFFER_TIMEOUT,
                    duid, 
                    offeredIP, 
                    block.network->config->getNetworkID(), 
                    dhcpConfigs.offerTimeout.load(std::memory_order_relaxed)
                );
                // Schedule a client request timeout
                scheduleTimeout(
                    Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT,
                    duid,
                    offeredIP,
                    block.network->config->getNetworkID(),
                    dhcpConfigs.clientRequestTimeout.load(std::memory_order_relaxed)
                );

                // Set the request as outgoing
                outgoingRequests.insert(offeredIP);
            }
        }

        if (!iface && block.network->config->interface)
            iface = block.network->config->interface;
    }

    for (auto& block : iapdBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;

        if (block.prefixes.empty())
        {
            auto offered = block.network->prefixPool->allocateTempPrefix(duid, block.network->config->subnetPrefix);
            if (offered.first.empty())
            {
                block.prefixes.push_back(offered);
                // Schedule offer timeout
                scheduleTimeout(
                    Dhcp::TimerType::PREFIX_OFFER_TIMEOUT,
                    duid, 
                    offered.first, 
                    block.network->config->getNetworkID(), 
                    dhcpConfigs.offerTimeout.load(std::memory_order_relaxed)
                );
                // Schedule a client request timeout
                scheduleTimeout(
                    Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT,
                    duid, 
                    offered.first, 
                    block.network->config->getNetworkID(), 
                    dhcpConfigs.clientRequestTimeout.load(std::memory_order_relaxed)
                );
                
                // Set the request as outgoing.
                outgoingRequests.insert(offered.first);
            }
        }

        if (!iface && block.network->config->interface)
            iface = block.network->config->interface;
    }

    Dhcpv6Header advertise = buildResponse(
        Variable::Dhcpv6::Type::advertise,
        header.transactionID,
        ianaBlocks,
        iapdBlocks,
        {}
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

void Protocol::Dhcpv6Server::processRequest(const Dhcpv6Header& header)
{
    auto ianaBlocks = extractIA_NA(header);
    auto iapdBlocks = extractIA_PD(header);
    auto iataBlocks = extractIA_TA(header);
    if (ianaBlocks.empty() && iapdBlocks.empty()) return;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid))
        return; // No leases will be given if no Client ID is present

    Interface* iface = nullptr;

    // Validate Server Identifier
    ByteString expectedServerID = ByteString("\x00\x03", 2) + ByteString("\x00\x00\x00\x00\x00\x01", 6);
    if (!validateServerID(header)) return;

    for (auto& block : ianaBlocks)
    {
        if (!block.network || !block.network->lease) continue;

        std::vector<ByteString> committed;
        for (const ByteString& addr : block.addresses)
        {
            if (block.network->lease->isAllocated(addr))
            {
                block.network->lease->renewLease(addr);
                committed.push_back(addr);
            }
            else if (outgoingRequests.contains(addr) && block.network->pool->isTemporarilyOffered(addr))
            {
                ByteString reserved = block.network->lease->activateLeaseFromTemp(duid, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage);
                if (!reserved.empty()) 
                {
                    committed.push_back(reserved);
                    cancelTimeout(Dhcp::TimerType::IP_OFFER_TIMEOUT, duid, reserved);
                    cancelTimeout(Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT, duid, reserved);
                    outgoingRequests.erase(reserved);
                }
            }
        }

        // If no requested address or all failed, allocate on dynamically
        if (committed.empty())
        {
            ByteString dynamic = block.network->lease->allocateIP(duid, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage);
            if (!dynamic.empty()) committed.push_back(dynamic);
        }

        block.addresses = committed;

        if (!iface && block.network->config->interface)
            iface = block.network->config->interface;
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
            else if (outgoingRequests.contains(prefix) && block.network->prefixPool->isTemporarilyOffered(prefix))
            {
                auto activated = block.network->prefixLease->activateLeaseFromTemp(duid, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage);
                if (!activated.first.empty())
                {
                    committed.push_back(activated);
                    cancelTimeout(Dhcp::TimerType::PREFIX_OFFER_TIMEOUT, duid, activated.first);
                    cancelTimeout(Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT, duid, activated.first);
                    outgoingRequests.erase(activated.first);
                }
            }
        }

        if (committed.empty())
        {
            auto dyn = block.network->prefixLease->allocatePrefix(duid, block.network->config->subnetPrefix, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage);
            if (!dyn.first.empty()) committed.push_back(dyn);
        }

        block.prefixes = committed;

        if (!iface && block.network->config->interface)
            iface = block.network->config->interface;
    }

    // Handle IA_TA
    for (auto& block : iataBlocks)
    {
        if (!block.network || !block.network->lease) continue;
        
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
                ByteString reserved = block.network->lease->activateLeaseFromTemp(duid, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage);
                if (!reserved.empty()) committed.push_back(reserved);
            }
        }

        if (committed.empty())
        {
            ByteString dynamic = block.network->lease->allocateIP(duid, block.network->config->leaseTime, block.network->config->t1Percentage, block.network->config->t2Percentage);
            if (!dynamic.empty()) committed.push_back(dynamic);
        }

        block.addresses = committed;

        if (!iface && block.network->config->interface)
            iface = block.network->config->interface;
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

void Protocol::Dhcpv6Server::processRenew(const Dhcpv6Header& header)
{
    auto ianaBlocks = extractIA_NA(header);
    auto iapdBlocks = extractIA_PD(header);
    auto iataBlocks = extractIA_TA(header);

    if (ianaBlocks.empty() && iapdBlocks.empty()) return;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid))
        return; // No leases will be given if no Client ID is present

    Interface* iface = nullptr;

    // Validate Server Identifier
    if (!validateServerID(header)) return;

    for (const auto& block : ianaBlocks)
    {
        if (!block.network || !block.network->lease) continue;

        for (const ByteString& addr : block.addresses)
            block.network->lease->renewLease(addr);

        if (!iface && block.network->config->interface)
            iface = block.network->config->interface;
    }

    for (auto& block : iapdBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;

        for (const auto& [prefix, _] : block.prefixes)
            block.network->prefixLease->renewPrefix(prefix);

        if (!iface && block.network->config->interface)
            iface = block.network->config->interface;
    }

    for (auto& block : iataBlocks)
    {
        if (!block.network || !block.network->lease) continue;

        for (const ByteString& addr : block.addresses)
            block.network->lease->renewLease(addr);

        if (!iface && block.network->config->interface)
            iface = block.network->config->interface;
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

void Protocol::Dhcpv6Server::processRebind(const Dhcpv6Header& header)
{
    auto ianaBlocks = extractIA_NA(header);
    auto iapdBlocks = extractIA_PD(header);

    if (ianaBlocks.empty() && iapdBlocks.empty()) return;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid))
        return; // No leases will be given if no Client ID is present

    Interface* iface = nullptr;

    for (const auto& block : ianaBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;
        for (const ByteString& addr : block.addresses)
            block.network->lease->renewLease(addr);

        if (!iface && block.network->config->interface)
            iface = block.network->config->interface;
    }

    for (auto& block : iapdBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;
        for (const auto& [prefix, _] : block.prefixes)
            block.network->prefixLease->renewPrefix(prefix);

        if (!iface && block.network->config->interface)
            iface = block.network->config->interface;
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

void Protocol::Dhcpv6Server::processRelease(const Dhcpv6Header& header)
{
    auto ianaBlocks = extractIA_NA(header);
    auto iapdBlocks = extractIA_PD(header);

    if (ianaBlocks.empty() && iapdBlocks.empty()) return;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid))
        return; // No leases will be given if no Client ID is present

    Interface* iface = nullptr;

    for (const auto& block : ianaBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;
        for (const ByteString& addr : block.addresses)
            scheduleTimeout(
                Dhcp::TimerType::RELEASE_HOLD,
                duid,
                addr,
                block.network->config->getNetworkID(),
                dhcpConfigs.declineHoldTime.load(std::memory_order_relaxed)
            );

        if (!iface && block.network->config->interface)
            iface = block.network->config->interface;
    }

    for (auto& block : iapdBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;
        for (const auto& [prefix, _] : block.prefixes)
            scheduleTimeout(
                Dhcp::TimerType::RELEASE_HOLD,
                duid,
                prefix,
                block.network->config->getNetworkID(),
                dhcpConfigs.declineHoldTime.load(std::memory_order_relaxed)
            );

        if (!iface && block.network->config->interface)
            iface = block.network->config->interface;
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

void Protocol::Dhcpv6Server::processDecline(const Dhcpv6Header& header)
{
    auto ianaBlocks = extractIA_NA(header);
    auto iapdBlocks = extractIA_PD(header);

    if (ianaBlocks.empty() && iapdBlocks.empty()) return;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid))
        return; // No leases will be given if no Client ID is present

    Interface* iface = nullptr;

    for (const auto& block : ianaBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;
        for (const ByteString& addr : block.addresses)
            scheduleTimeout(
                Dhcp::TimerType::DECLINE_HOLD, 
                duid,
                addr,
                block.network->config->getNetworkID(),
                dhcpConfigs.declineHoldTime.load(std::memory_order_relaxed)
            );

        if (!iface && block.network->config->interface)
            iface = block.network->config->interface;
    }

    for (auto& block : iapdBlocks)
    {
        if (!block.network || !block.network->prefixLease) continue;
        for (const auto& [prefix, _] : block.prefixes)
            scheduleTimeout(
                Dhcp::TimerType::DECLINE_HOLD,
                duid,
                prefix,
                block.network->config->getNetworkID(),
                dhcpConfigs.declineHoldTime.load(std::memory_order_relaxed)
            );

        if (!iface && block.network->config->interface)
            iface = block.network->config->interface;
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

void Protocol::Dhcpv6Server::processConfirm(const Dhcpv6Header& header)
{
    auto ianaBlocks = extractIA_NA(header);
    auto iapdBlocks = extractIA_PD(header);

    if (ianaBlocks.empty() && iapdBlocks.empty()) return;

    // Extract the Clients ID
    ByteString duid = extractDUID(header);
    if (duid.empty() || !validateAuthentication(header, duid))
        return; // No leases will be given if no Client ID is present

    {
        std::lock_guard<std::mutex> lock(declineMutex);
        auto now = std::chrono::steady_clock::now();

        auto it = declineTimestamps.find(duid);
        if (it != declineTimestamps.end())
        {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
            if (duration > 10)
            {
                return; // Rate limit exceeded
            }
        }

        declineTimestamps[duid] = now;
    }

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

    Interface* iface = nullptr;
    if (!ianaBlocks.empty() && ianaBlocks[0].network)
        iface = ianaBlocks[0].network->config->interface;
    else if (!iapdBlocks.empty() && iapdBlocks[0].network)
        iface = iapdBlocks[0].network->config->interface;

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

void Protocol::Dhcpv6Server::processInformationRequest(const Dhcpv6Header& header)
{
    auto oro = extractORO(header);

    // Build minimal reply with requested info (no AI_NA needed)
    Dhcpv6Header reply;
    reply.type = Variable::Dhcpv6::Type::reply;
    reply.transactionID = header.transactionID;

    Interface* iface = nullptr;

    for (const auto& [_, net] : dhcpNetworks)
    {
        std::vector<Dhcpv6Header::Option> opts = buildOptions(net->config, oro);
        reply.options.insert(reply.options.end(), opts.begin(), opts.end());
        iface = net->config->interface;
        break; // We only need one matching config
    }

    sendPacket(reply, iface);
}

void Protocol::Dhcpv6Server::processEchoRequest(const Dhcpv6Header& header)
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

    // Pick any matching interface to send from
    Interface* iface = nullptr;
    {
        std::lock_guard<std::mutex> lock(configMutex);
        for (const auto& [_, net] : dhcpNetworks)
        {
            if (net && net->config && net->config->interface)
            {
                iface = net->config->interface;
                break;
            }
        }
    }

    auto oro = extractORO(header);
    for (const auto& [_, net] : dhcpNetworks)
    {
        std::vector<Dhcpv6Header::Option> opts = buildOptions(net->config, oro);
        reply.options.insert(reply.options.end(), opts.begin(), opts.end());
        iface = net->config->interface;
        break;
    }

    sendPacket(reply, iface);
}

void Protocol::Dhcpv6Server::processRelayForward(const Dhcpv6RelayHeader& relay)
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

            handleDhcpPacket(innerPkt); // Decapsulate and reuse logic
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
            ByteString length = Functions::numToByte(24, 2);

            value += Variable::Dhcpv6::Options::IAAddr;
            value += length + addrVal;
        }

        if (block.addresses.empty()) 
        {
            ByteString status = buildIAStatus(Variable::Dhcpv6::Status::noAddrsAvail, "No address available");
            value += status;
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
            pval += prefix;

            ByteString length = Functions::numToByte(pval.size(), 2);
            value += Variable::Dhcpv6::Options::IA_Prefix;
            value += length + pval;
        }

        if (block.prefixes.empty())
        {
            ByteString status = buildIAStatus(Variable::Dhcpv6::Status::noPrefixAvail, "No prefix available");
            value += status;
        }

        Dhcpv6Header::Option ia_pd;
        ia_pd.option = Variable::Dhcpv6::Options::IA_PD;
        ia_pd.length = Functions::numToByte(value.size(), 2);
        ia_pd.value = value;
        header.options.push_back(ia_pd);
    }

    // ID_TA
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
            ByteString length = Functions::numToByte(24, 2);

            value += Variable::Dhcpv6::Options::IAAddr;
            value += length + addrVal;
        }

        if (block.addresses.empty())
        {
            ByteString status = buildIAStatus(Variable::Dhcpv6::Status::noAddrsAvail, "No temporary address available");
            value += status;
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
            for (const auto& dns : config->dnsServer)
            {
                Dhcpv6Header::Option dnsOpt;
                dnsOpt.option = opt;
                dnsOpt.length = Functions::numToByte(16, 2);
                dnsOpt.value = dns;
                options.push_back(dnsOpt);
            }
        }

        else if (opt == Variable::Dhcpv6::Options::domainName && !config->domainName.empty())
        {
            Dhcpv6Header::Option dom;
            dom.option = opt;
            dom.length = Functions::numToByte(config->domainName.size(), 2);
            dom.value = config->domainName;
            options.push_back(dom);
        }

        else if (opt == Variable::Dhcpv6::Options::ntpServer && !config->ntpServer.empty())
        {
            Dhcpv6Header::Option ntp;
            ntp.option = opt;
            ntp.length = Functions::numToByte(16, 2);
            ntp.value = config->ntpServer;
            options.push_back(ntp);
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
    return false;
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
    PacketInfo pkt;
    pkt.Layer5.push_back(std::move(header));

    if (iface)
    {
        IPPacket::buildUdp(iface, pkt, Variable::IPv6::source, &Variable::IPv6::source, &Variable::Mac::broadcast, 0, dhcpConfigs.hopCountLimit.load(), Variable::Ethernet::ipv6, Variable::Udp::dhcpv6Server, Variable::Udp::dhcpv6Client);
        }
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
