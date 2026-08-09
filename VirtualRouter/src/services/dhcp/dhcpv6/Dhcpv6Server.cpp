//TODO echo unknown option & duid generation

#include "Dhcpv6Server.h"
#include "interface/Interface.h"
#include "infrastructure/IPPacket.h"
#include "udp/Udp.h"
#include "processing/PacketBuilder.hpp"

namespace services::dhcp
{

/*
 * oro cannot have:
 * clientid
 * serverid
 * iana
 * iata
 * iapd
 * iaaddr
 * iaprefix
 */

#define ADD_DELAYED_AUTH                                            \
    auto auth = authManager.addDelayedAuthOption(tlv, send.clientID);  \
    bool validAuth;                                                 \
    if (auth.has_value())                                           \
    {                                                               \
        if (!auth.value().digest) return false;                     \
        validAuth = true;                                           \
    }

#define ADD_DELAYED_DIGEST                                          \
    if (validAuth)                                                  \
        authManager.addDelayedAuthDigest(dhcp, tlv, auth.value());

void Dhcpv6Server::handlePacket(packet::Dhcpv6Header& dhcp, interface::Interface& iface, bool multicast, const uint8_t* clientIp)
{
    Dhcpv6PacketBuild build(&iface);
    Dhcpv6PacketSend send = {
        .iface = iface,
        .multicast = multicast,
        .clientAddress = clientIp,
        .build = build
    };
    Dhcpv6PacketReceive receive = {
        .dhcpHeader = dhcp,
        .send = send
    };
    __uint128_t networkAddress = iface.configs.ipv6.getLocalAddress().addr;
    handleDhcpPacket(receive, networkAddress);
}

bool Dhcpv6Server::handleDhcpPacket(Dhcpv6PacketReceive& packet, types::IPv6Address networkAddress)
{
    auto& dhcp = packet.dhcpHeader;
    auto& options = packet.options;
    auto& iface = packet.send.iface;

    auto trail = dhcp.getTrail();
    if (!parseDhcpv6Options(trail.data(), trail.size(), options)) return false;

    DhcpNetwork* network = matchAddressToPool( networkAddress, iface.configs.key.getId());

    uint8_t dhcpType = dhcp.getType();

    bool isBinding = dhcpType == DHCPV6_TYPE_RENEW
                  || dhcpType == DHCPV6_TYPE_REBIND
                  || dhcpType == DHCPV6_TYPE_RELEASE
                  || dhcpType == DHCPV6_TYPE_DECLINE;

    Duid serverID;
    Dhcpv6IAOptions ia;

    packet::TLV16Option* authOpt = nullptr;
    bool reconfigAccept = false;

    for (auto& opt : options)
    {
        if (opt.type == DHCPV6_OPTION_CLIENT_ID)
        {
            packet.send.clientID = { opt.value, opt.valueSize };
            break;
        }
    }

    if (!packet.send.clientID.data)
        return false;

    for (auto& opt : options)
    {
        switch (opt.type)
        {
            case DHCPV6_OPTION_SERVER_ID:
                serverID = { opt.value, opt.valueSize };
                break;
            case DHCPV6_OPTION_RECONFIG_ACCEPT:
                reconfigAccept = true;
                break;
            case DHCPV6_OPTION_OPTION_REQUEST:
            {
                packet.send.oro = opt.value;
                packet.send.oroSize = opt.valueSize;
                break;
            }
            case DHCPV6_OPTION_IA_NA:
            {
                auto iana = extractIA_NA(opt, network, packet.send.clientID, isBinding);
                if (iana.has_value()) ia.ianaBlocks.push_back(iana.value());
                break;
            }
            case DHCPV6_OPTION_IA_TA:
            {
                auto iata = extractIA_TA(opt, network, isBinding);
                if (iata.has_value()) ia.iataBlocks.push_back(iata.value());
                break;
            }
            case DHCPV6_OPTION_IA_PD:
            {
                auto iapd = extractIA_PD(opt, network, packet.send.clientID, isBinding);
                if (iapd.has_value()) ia.iapdBlocks.push_back(iapd.value());
                break;
            }
            case DHCPV6_OPTION_AUTHENTICATION:
                authOpt = &opt;
                break;
            default:
                break;
        }
    }

    if (!packet.send.clientID.data) return false;
    ClientID& clientID = packet.send.clientID;

    // Auth
    if (authManager.getSettings().rkapEnabled.load(std::memory_order_relaxed))
    {
        if (dhcpType == DHCPV6_TYPE_RENEW ||
            dhcpType == DHCPV6_TYPE_REBIND ||
            dhcpType == DHCPV6_TYPE_INFORMATION_REQUEST)
        {
            auto reconfigInfo = activeReconfigs.find(clientID);
            if (reconfigInfo != activeReconfigs.end())
            {
                const auto& info = reconfigInfo->second;
                if (dhcpType != static_cast<uint8_t>(info.reason) ||
                    iface.configs.key != info.interfaceKey ||
                    utils::read<uint32_t, 3>(dhcp.getTransId()) != info.transactionID ||
                    !authManager.validateRkapDigest(dhcp, authOpt, clientID, info.secret))
                    return false;

                timeManager.cancelTimer(reconfigInfo->second.timerID);
                activeReconfigs.erase(clientID);
            }
            else if (!authManager.validateDelayedAuth(dhcp, authOpt, clientID))
                return false;
        }
        else if (!authManager.validateDelayedAuth(dhcp, authOpt, clientID))
            return false;
    }
    else if (!authManager.validateDelayedAuth(dhcp, authOpt, clientID))
        return false;

    std::optional<Dhcpv6SendType> send;

    switch (dhcpType)
    {
        case DHCPV6_TYPE_SOLICIT:
            send = processSolicit(packet, ia);
            break;
        case DHCPV6_TYPE_REQUEST:
            send = processRequest(packet, ia, serverID);
            break;
        case DHCPV6_TYPE_RENEW:
            send = processRenew(packet, ia, serverID);
            break;
        case DHCPV6_TYPE_REBIND:
            send = processRebind(packet, ia);
            break;
        case DHCPV6_TYPE_RELEASE:
            send = processRelease(packet, ia, serverID);
            break;
        case DHCPV6_TYPE_DECLINE:
            send = processDecline(packet, ia, serverID);
            break;
        case DHCPV6_TYPE_CONFIRM:
            send = processConfirm(packet, ia);
            break;
        case DHCPV6_TYPE_INFORMATION_REQUEST:
            send = processInformationRequest(packet);
            break;
        default:
            break;
    }

    if (!send.has_value()) return false;

    if (!reconfigAccepts.count(clientID))
    {
        bool hasLease = false;
        for (const auto& ip : ia.ianaBlocks)
            if (ip.status.code == Dhcpv6StatusCode::Success)
                { hasLease = true; break; }
        if (!hasLease)
            for (const auto& ip : ia.iataBlocks)
                if (ip.status.code == Dhcpv6StatusCode::Success)
                    { hasLease = true; break; }
        if (!hasLease)
            for (const auto& ip : ia.iapdBlocks)
                if (ip.status.code == Dhcpv6StatusCode::Success)
                    { hasLease = true; break; }

        if (hasLease)
        {
            if (((configs.requireReconfigureAccept.load(std::memory_order_relaxed) || iface.configs.dhcpv6.configs->automatic.load(std::memory_order_relaxed)) && 
                reconfigAccept && clientID.data && send.value().type != DHCPV6_TYPE_ADVERTISE) ||
                configs.reconfigureAll.load(std::memory_order_relaxed) || iface.configs.dhcpv6.configs->reconfigureAll.load(std::memory_order_relaxed))
            {
                reconfigAccepts[clientID] = {
                    .clientAddress = networkAddress ? 0 : utils::read<__uint128_t>(packet.send.clientAddress),
                    .interfaceKey = iface.configs.key.getId()
                };
            }
        }
    }

    switch(send.value().type)
    {
        case DHCPV6_TYPE_ADVERTISE:
            if (!sendAdvertise(packet.send, ia, dhcp.getTransId())) return false;
            break;
        case DHCPV6_TYPE_REPLY:
            if (!sendReply(packet.send, &ia, dhcp.getTransId(), send.value().property)) return false;
            break;
        case DHCPV6_TYPE_CONFIRM:
            if (!sendConfirmReply(packet.send, ia, dhcp.getTransId(), send.value().property)) return false;
            break;
        case DHCPV6_TYPE_INFORMATION_REQUEST:
            if (!sendReply(packet.send, nullptr, dhcp.getTransId(), send.value().property)) return false;
            break;
    }

    return true;
}

void Dhcpv6Server::buildResponse(
    packet::Dhcpv6Header& dhcp,
    ClientID& clientID,
    packet::TLV16BufferManager& tlv,
    Dhcpv6IAOptions* ia,
    const uint8_t* oro,
    size_t oroSize
)
{
    if (ia)
    {
        uint32_t validLifetime = ia->network->configs.validLifetime.load(std::memory_order_relaxed);
        uint32_t preferredLifetime = ia->network->configs.preferredLifetime.load(std::memory_order_relaxed);

        for (const auto& block : ia->ianaBlocks)
        {
            // Calculate total size
            size_t totalSize = 12; // IAID + T1 + T2
            for (const auto& entry : block.addresses)
                totalSize += 28 + (entry.status.code != Dhcpv6StatusCode::Success ? (6 + entry.status.msg.size()) : 0);

            // Reserve option slot
            auto iaHdr = tlv.getNextValBuf(totalSize);
            if (!iaHdr) continue; // Does not fit

            size_t offset = 0;
            
            std::memcpy(iaHdr, block.iaid, 4);
            utils::write<uint32_t>(iaHdr + 4, static_cast<uint32_t>(preferredLifetime / 2));
            utils::write<uint32_t>(iaHdr + 8, static_cast<uint32_t>((preferredLifetime * 8) / 10));
            offset += 12;
            
            for ( auto& entry : block.addresses)
            {
                bool hasStatus = entry.status.code != Dhcpv6StatusCode::Success;

                // Option type and length
                utils::write<uint16_t>(iaHdr + offset, DHCPV6_OPTION_IAADDR);
                utils::write<uint16_t>(iaHdr + offset + 2, 24 + (hasStatus ? (6 + entry.status.msg.size()) : 0));
                offset += 4;

                utils::write<__uint128_t>(iaHdr + offset, entry.address);
                utils::write<uint32_t>(iaHdr + offset + 16, entry.staticPreferred == 0 ? preferredLifetime : entry.staticPreferred);
                utils::write<uint32_t>(iaHdr + offset + 20, entry.staticValid == 0 ? validLifetime : entry.staticValid);
                offset += 24;

                if (hasStatus)
                {
                    utils::write<uint16_t>(iaHdr + offset, DHCPV6_OPTION_STATUS_CODE);
                    utils::write<uint16_t>(iaHdr + offset + 2, 2 + entry.status.msg.size());
                    offset += 4;

                    utils::write<uint16_t>(iaHdr + offset, static_cast<uint16_t>(entry.status.code));
                    if (entry.status.msg.size() > 0)
                    {
                        std::memcpy(iaHdr + offset + 2, reinterpret_cast<const uint8_t*>(entry.status.msg.data()), entry.status.msg.size());
                    }
                    offset += 2 + entry.status.msg.size();
                }
            }
        }

        for (const auto& block : ia->iataBlocks)
        {
            // Calculate total size
            size_t totalSize = 4; // IAID + T1 + T2
            for (const auto& entry : block.addresses)
                totalSize += 28 + (entry.status.code != Dhcpv6StatusCode::Success ? (6 + entry.status.msg.size()) : 0);

            // Reserve option slot
            auto iaHdr = tlv.getNextValBuf(totalSize);
            if (!iaHdr) continue; // Does not fit

            size_t offset = 0;
            
            std::memcpy(iaHdr, block.iaid, 4);
            offset += 4;
            
            for ( auto& entry : block.addresses)
            {
                bool hasStatus = entry.status.code != Dhcpv6StatusCode::Success;

                // Option type and length
                utils::write<uint16_t>(iaHdr + offset, DHCPV6_OPTION_IAADDR);
                utils::write<uint16_t>(iaHdr + offset + 2, 24 + (hasStatus ? (6 + entry.status.msg.size()) : 0));
                offset += 4;

                utils::write<__uint128_t>(iaHdr + offset, entry.address);
                utils::write<uint32_t>(iaHdr + offset + 16, preferredLifetime);
                utils::write<uint32_t>(iaHdr + offset + 20, validLifetime);
                offset += 24;

                if (hasStatus)
                {
                    utils::write<uint16_t>(iaHdr + offset, DHCPV6_OPTION_STATUS_CODE);
                    utils::write<uint16_t>(iaHdr + offset + 2, 2 + entry.status.msg.size());
                    offset += 4;

                    {
                        std::memcpy(iaHdr + offset + 2, reinterpret_cast<const uint8_t*>(entry.status.msg.data()), entry.status.msg.size());
                    }
                    offset += 2 + entry.status.msg.size();
                }
            }
        }

        for (const auto& block : ia->iapdBlocks)
        {
            uint32_t preferredLifetime = std::numeric_limits<uint32_t>::max();

            // Calculate total size
            size_t totalSize = 12; // IAID + T1 + T2
            for (const auto& entry : block.prefixes)
                totalSize += 29 + (entry.status.code != Dhcpv6StatusCode::Success ? (6 + entry.status.msg.size()) : 0);

            // Reserve option slot
            auto iaHdr = tlv.getNextValBuf(totalSize);
            if (!iaHdr) continue; // Does not fit

            size_t offset = 0;
            
            std::memcpy(iaHdr, block.iaid, 4);
            // T1 & T2 will be added later
            offset += 12;
            
            for ( auto& entry : block.prefixes)
            {
                bool hasStatus = entry.status.code != Dhcpv6StatusCode::Success;

                size_t prefixPreferredLifetime = entry.staticPreferred == 0 ? entry.pool ? entry.pool->validLifetime.load(std::memory_order_relaxed) : 0 : entry.staticPreferred;
                if (preferredLifetime > prefixPreferredLifetime) preferredLifetime = prefixPreferredLifetime;

                // Option type and length
                utils::write<uint16_t>(iaHdr + offset, DHCPV6_OPTION_IAADDR);
                utils::write<uint16_t>(iaHdr + offset + 2, 24 + (hasStatus ? (6 + entry.status.msg.size()) : 0));
                offset += 4;

                iaHdr[offset] = entry.prefix.prefixLength;
                utils::write<__uint128_t>(iaHdr + offset + 1, entry.prefix.addr);
                utils::write<uint32_t>(iaHdr + offset + 17, entry.staticPreferred == 0 ? entry.pool ? entry.pool->validLifetime.load(std::memory_order_relaxed) : 0 : entry.staticPreferred);
                utils::write<uint32_t>(iaHdr + offset + 21, entry.staticValid == 0 ? prefixPreferredLifetime : entry.staticValid);
                offset += 25;

                if (hasStatus)
                {
                    utils::write<uint16_t>(iaHdr + offset, DHCPV6_OPTION_STATUS_CODE);
                    utils::write<uint16_t>(iaHdr + offset + 2, 2 + entry.status.msg.size());
                    offset += 4;

                    utils::write<uint16_t>(iaHdr + offset, static_cast<uint16_t>(entry.status.code));
                    if (entry.status.msg.size() > 0)
                    {
                        std::memcpy(iaHdr + offset + 2, reinterpret_cast<const uint8_t*>(entry.status.msg.data()), entry.status.msg.size());
                    }
                    offset += 2 + entry.status.msg.size();
                }
            }

            // T1 and T2
            utils::write<uint32_t>(iaHdr + 4, static_cast<uint32_t>(preferredLifetime / 2));
            utils::write<uint32_t>(iaHdr + 8, static_cast<uint32_t>((preferredLifetime * 8) / 10));
        }
    }

    auto addServerList = [&](uint16_t type, std::vector<types::IPv6Address>& servers)
    {
        if (!servers.empty())
        {
            size_t size = servers.size() * 16;
            auto value = tlv.getNextValBuf(size);
            if (!value) return;
            size_t offset = 0;
            for (const auto& dns : servers)
            {
                utils::write<__uint128_t>(value + offset, dns.addr);
                offset += 16;
            }
            tlv.append(type, size, nullptr, size);
        }
    };

    auto addNames = [&](uint16_t type, std::vector<std::string>& names)
    {
        if (!names.empty())
        {
            size_t size = 0;
            for (const auto& name : names)
            {
                size_t encoded = 0;
                size_t pos = 0;
                while (pos < name.size())
                {
                    size_t next = name.find('.', pos);
                    if (next == std::string::npos) next = name.size();
                    size_t len = next - pos;
                    encoded += 1 + len;
                    pos = next + 1;
                }
                size += encoded + 1;
            }
            auto value = tlv.getNextValBuf(size);
            if (!value) return;
            size_t offset = 0;
            for (const auto& name : names)
            {
                // Encode DNS-style labels
                size_t pos = 0, next;
                while ((next = name.find('.', pos)) != std::string::npos)
                {
                    uint8_t len = next - pos;
                    value[offset] = len;
                    std::memcpy(value + offset + 1, name.data() + pos, len);
                    offset += len + 1;
                    pos = next + 1;
                }
                // Last label
                if (pos < name.size())
                {
                    uint8_t len = name.size() - pos;
                    value[offset] = len;
                    std::memcpy(value + offset + 1, name.data() + pos, len);
                    offset += len + 1;
                }
                value[offset] = 0x00;
                offset += 1;
            }
            tlv.append(type, size, nullptr, size);
        }
    };

    auto addFQDN = [&]() {
        const auto& fqdn = ia->network->configs.fqdn;
        if (fqdn.empty()) return;

        size_t encoded = 0;
        size_t pos = 0;

        // Compute DNS-encoded size
        while (pos < fqdn.size())
        {
            size_t next = fqdn.find('.', pos);
            if (next == std::string::npos) next = fqdn.size();
            size_t len = next - pos;
            encoded += 1 + len;
            pos = next + 1;
        }
        encoded += 1;

        size_t total = 1 + encoded;
        uint8_t* value = tlv.getNextValBuf(total);
        if (!value) return;
        value[0] = 0b00000110; // S=1, 0=1, N=0

        size_t offset = 1;
        pos = 0;
        while (pos < fqdn.size())
        {
            size_t next = fqdn.find('.', pos);
            if (next == std::string::npos) next = fqdn.size();
            size_t len = next - pos;
            value[offset] = len;
            std::memcpy(value + offset + 1, fqdn.data() + pos, len);
            offset += len + 1;
            pos = next + 1;
        }
        value[offset] = 0x00;

        tlv.append(DHCPV6_OPTION_FQDN, total, nullptr, total);
    };

    if (oro && oroSize != 0)
    {
        for (int i = 0; i < oroSize; i += 2)
        {
            switch (utils::read<uint16_t>(oro + i))
            {
                case DHCPV6_OPTION_DNS_SERVERS:
                {
                    addServerList(DHCPV6_OPTION_DNS_SERVERS, ia->network->configs.dnsServers);
                    break;
                }
                case DHCPV6_OPTION_DOMAIN_SEARCH:
                {
                    addNames(DHCPV6_OPTION_DOMAIN_SEARCH, ia->network->configs.domainSearch);
                    break;
                }
                case DHCPV6_OPTION_NTP_SERVERS:
                {
                    addServerList(DHCPV6_OPTION_NTP_SERVERS, ia->network->configs.ntpServers);
                    break;
                }
                case DHCPV6_OPTION_FQDN:
                {
                    addFQDN();
                    break;
                }
                case DHCPV6_OPTION_INFO_REFRESH_TIME:
                {
                    auto value = tlv.getNextValBuf(4);
                    if (!value) continue;
                    utils::write<uint32_t>(value, ia->network->configs.isRefreshTime.load(std::memory_order_relaxed) ? ia->network->configs.refreshTime.load(std::memory_order_relaxed) : configs.refreshTime.load(std::memory_order_relaxed));
                    tlv.append(DHCPV6_OPTION_INFO_REFRESH_TIME, 4, nullptr, 4);
                    break;
                }
                case DHCPV6_OPTION_SOL_MAX_RT:
                {
                    auto value = tlv.getNextValBuf(4);
                    if (!value) continue;
                    utils::write<uint32_t>(value, ia->network->configs.isSolMaxRt.load(std::memory_order_relaxed) ? ia->network->configs.solMaxRt.load(std::memory_order_relaxed) : configs.solMaxRt.load(std::memory_order_relaxed));
                    tlv.append(DHCPV6_OPTION_SOL_MAX_RT, 4, nullptr, 4);
                    break;
                }
                case DHCPV6_OPTION_INF_MAX_RT:
                {
                    auto value = tlv.getNextValBuf(4);
                    if (!value) continue;
                    utils::write<uint32_t>(value, ia->network->configs.isInfMaxRt.load(std::memory_order_relaxed) ? ia->network->configs.infMaxRt.load(std::memory_order_relaxed) : configs.infMaxRt.load(std::memory_order_relaxed));
                    tlv.append(DHCPV6_OPTION_INF_MAX_RT, 4, nullptr, 4);
                    break;
                }
                case DHCPV6_OPTION_VENDOR_OPTS:
                {
                    //TODO
                    break;
                }
                case DHCPV6_OPTION_VENDOR_CLASS_ID:
                {
                    //TODO
                    break;
                }
            }
        }
    }
}

std::optional<Dhcpv6SendType> Dhcpv6Server::processSolicit(Dhcpv6PacketReceive& packet, Dhcpv6IAOptions& ia)
{
    if (ia.empty() || !packet.send.multicast)
        return std::nullopt;
    
    interface::InterfaceConfigs& ifaceConf = packet.send.iface.configs;

    bool rapidCommit = false;
    for (const auto& opt : packet.options)
    {
        if (opt.type == DHCPV6_OPTION_RAPID_COMMIT &&
            opt.valueSize == 0 && (ifaceConf.dhcpv6.configs->rapidCommit.load(std::memory_order_relaxed) || configs.rapidCommit.load(std::memory_order_relaxed)))
        {
            rapidCommit = true;
        }
    }

    if (rapidCommit)
    {
        if (ia.network)
        {
            uint32_t validLifetime = ia.network->configs.validLifetime.load(std::memory_order_relaxed);

            for (auto& block : ia.ianaBlocks)
            {
                const IAKey iaKey{ packet.send.clientID, utils::read<uint32_t>(block.iaid), IAType::IA_NA };

                if (block.addresses.empty())
                {
                    if (ia.network && !addStaticLease(block, iaKey))
                    {
                        auto addrs = ia.network->leaseManager->createLease(iaKey, validLifetime);
                        if (addrs.empty())
                            block.status = { Dhcpv6StatusCode::NoAddrsAvail };
                        else
                            for (const auto& addr : addrs)
                                block.addresses.push_back({addr, { Dhcpv6StatusCode::Success } });
                    }
                }
                else if (ia.network->leaseManager)
                {
                    addStaticLease(block, iaKey);
                    for (auto& entry : block.addresses)
                    {
                        {
                            entry.status = ia.network->leaseManager->createLeaseFromRequest(
                                entry.address, iaKey, validLifetime
                            );
                        }
                    }
                }
            }
            for (auto& block : ia.iataBlocks)
            {
                const IAKey iaKey = { packet.send.clientID, utils::read<uint32_t>(block.iaid), IAType::IA_TA };

                if (block.addresses.empty())
                {
                    auto addrs = ia.network->leaseManager->createLease(iaKey, validLifetime);
                    if (addrs.empty())
                        block.status = { Dhcpv6StatusCode::NoAddrsAvail };
                    else
                        for (const auto& addr : addrs)
                            block.addresses.push_back( {addr, { Dhcpv6StatusCode::Success } });
                }
                else
                {
                    for (auto& entry : block.addresses)
                    {
                        entry.status = ia.network->leaseManager->createLeaseFromRequest(
                            entry.address, iaKey, validLifetime
                        );
                    }
                }
            }
            for (auto& block : ia.iapdBlocks)
            {
                const IAKey iaKey = { packet.send.clientID, utils::read<uint32_t>(block.iaid) };

                auto* pool = selectPrefixPool(ia.network, nullptr);

                if (block.prefixes.empty())
                {
                    if (!pool)
                    {
                        block.status = { Dhcpv6StatusCode::NoPrefixAvail };
                        continue;
                    }

                    if (!addStaticPrefix(block, iaKey))
                    {
                        
                        auto result = pool->leaseManager->createPrefix(iaKey, pool->validLifetime.load(std::memory_order_relaxed));
                        if (result.second.empty())
                        {
                            block.status = { Dhcpv6StatusCode::NoPrefixAvail };
                            continue;
                        }

                        for (const auto& prefix : result.second)
                            block.prefixes.push_back({ prefix, result.first, pool });
                    }
                }
                else
                {
                    addStaticPrefix(block, iaKey);

                    for (auto& entry : block.prefixes)
                    {
                        if (!entry.pool)
                        {
                            entry.status = { Dhcpv6StatusCode::NoPrefixAvail };
                            continue;
                        }

                        if (entry.prefix.addr == 0)
                        {
                            auto lease = entry.pool->leaseManager->createPrefix(
                                iaKey, entry.prefix.prefixLength, entry.pool->validLifetime.load(std::memory_order_relaxed)
                            );
                            entry.prefix = lease.first;
                            entry.status = lease.second;
                        }
                        else
                        {
                            entry.status = entry.pool->leaseManager->createPrefixFromRequest(
                                entry.prefix, iaKey, entry.pool->validLifetime.load(std::memory_order_relaxed)
                            );
                        }
                    }
                }
            }
        }

        return Dhcpv6SendType{ DHCPV6_TYPE_REPLY, true };
    }

    if (ia.network)
    {
        uint32_t validLifetime = ia.network->configs.validLifetime.load(std::memory_order_relaxed);
        uint32_t preferredLifetime = ia.network->configs.preferredLifetime.load(std::memory_order_relaxed);

        for (auto& block : ia.ianaBlocks)
        {
            const IAKey iaKey{ packet.send.clientID, utils::read<uint32_t>(block.iaid), IAType::IA_NA };
            size_t success = 0;

            if (ia.network && addStaticAdvertisedLease(block, iaKey))
                success++;

            if (auto addrs = ia.network->pool->getIAID(iaKey); addrs.has_value())
            {
                for (const auto& addr : addrs.value())
                {
                    block.addresses.push_back({addr, { Dhcpv6StatusCode::Success } });
                    success++;
                }
            }
            else if (!block.addresses.empty())
            {
                for (auto& entry : block.addresses)
                {
                    entry.status = ia.network->pool->allocateRequestedAdvertised(
                        { iaKey.duid, iaKey.iaid, entry.address, IAType::IA_NA }, validLifetime
                    );
                    if (entry.status.code == Dhcpv6StatusCode::Success)
                        success++;
                }
            }

            if (success == 0)
            {
                auto addr = ia.network->pool->allocateAdvertised(iaKey, validLifetime);
                if (addr != 0)
                    block.addresses.push_back({addr, { Dhcpv6StatusCode::Success } });
                else
                    block.status = { Dhcpv6StatusCode::NoAddrsAvail };
            }
        }
        for (auto& block : ia.iataBlocks)
        {
            const IAKey iaKey{ packet.send.clientID, utils::read<uint32_t>(block.iaid), IAType::IA_TA };
            size_t success = 0;

            if (auto addrs = ia.network->pool->getIAID(iaKey); addrs.has_value())
            {
                for (const auto& addr : addrs.value())
                {
                    block.addresses.push_back({addr, { Dhcpv6StatusCode::Success } });
                    success++;
                }
            }
            else if (!block.addresses.empty())
            {
                for (auto& entry : block.addresses)
                {
                    entry.status = ia.network->pool->allocateRequestedAdvertised(
                        { packet.send.clientID, utils::read<uint32_t>(block.iaid), entry.address, IAType::IA_TA }, preferredLifetime
                    );
                    if (entry.status.code == Dhcpv6StatusCode::Success)
                        success++;
                }
            }

            if (success == 0)
            {
                auto addr = ia.network->pool->allocateAdvertised(iaKey, validLifetime);
                if (addr != 0)
                    block.addresses.push_back({addr, { Dhcpv6StatusCode::Success } });
                else
                    block.status = { Dhcpv6StatusCode::NoAddrsAvail };
            }
        }
        for (auto& block : ia.iapdBlocks)
        {
            const IAKey iaKey = { packet.send.clientID, utils::read<uint32_t>(block.iaid) };
            size_t success = 0;

            std::vector<std::pair<types::IPv6Prefix, PrefixPoolConfig&>> advertised;

            if (ia.network && addStaticAdvertisedPrefix(block, iaKey))
                success++;

            for (const auto& pd : ia.network->prefixPools)
            {
                auto pds = pd->pool->getIAID(iaKey);
                if (pds.has_value())
                {
                    for (const auto& p : pds.value())
                        advertised.emplace_back(p, *pd);
                }
            }

            if (!advertised.empty())
            {
                for (const auto& prefix : advertised)
                {
                    block.prefixes.push_back({ prefix.first, { Dhcpv6StatusCode::Success }, &prefix.second });
                    success++;
                }
            }
            else if (!block.prefixes.empty())
            {
                for (auto& entry : block.prefixes)
                {
                    if (!entry.pool)
                    {
                        entry.status = { Dhcpv6StatusCode::NoPrefixAvail };
                        continue;
                    }

                    if (entry.prefix.addr == 0 && entry.prefix.prefixLength != 0)
                    {
                        auto lease = entry.pool->pool->allocateAdvertisedPrefix(
                            iaKey, entry.prefix.prefixLength, entry.pool->validLifetime.load(std::memory_order_relaxed)
                        );
                        entry.prefix = lease.first;
                        entry.status = lease.second;
                        if (lease.second.code == Dhcpv6StatusCode::Success)
                            success++;
                    }
                    else if (entry.prefix.addr != 0)
                    {
                        auto newPrefix = entry.pool->pool->allocateRequestedAdvertisedPrefix(
                            { iaKey.duid, iaKey.iaid, entry.prefix.addr, entry.prefix.prefixLength},
                            entry.pool->validLifetime.load(std::memory_order_relaxed)
                        );
                        entry.prefix.prefixLength = newPrefix.first;
                        entry.status = newPrefix.second;
                        if (newPrefix.second.code == Dhcpv6StatusCode::Success)
                            success++;
                    }
                }
            }

            if (success == 0)
            {
                auto* pool = selectPrefixPool(ia.network, nullptr);
                if (!pool)
                {
                    block.status = { Dhcpv6StatusCode::NoPrefixAvail };
                    continue;
                }

                auto lease = pool->pool->allocateAdvertisedPrefix(
                    iaKey, 0, pool->validLifetime.load(std::memory_order_relaxed)
                );
                
                if (lease.first.addr != 0)
                    block.prefixes.push_back({ lease.first, lease.second, pool });
                else
                    block.status = { Dhcpv6StatusCode::NoPrefixAvail };
            }
        }
    }

    return Dhcpv6SendType{ DHCPV6_TYPE_ADVERTISE };
}

std::optional<Dhcpv6SendType> Dhcpv6Server::processRequest(Dhcpv6PacketReceive& packet, Dhcpv6IAOptions& ia, Duid& serverID)
{
    if (ia.empty() || serverID != dhcpUniqueIdentifier || /*auth*/false)
        return std::nullopt;

    if (ia.network)
    {
        uint32_t validLifetime = ia.network->configs.validLifetime.load(std::memory_order_relaxed);

        for (auto& block : ia.ianaBlocks)
        {
            const IAKey iaKey{ packet.send.clientID, utils::read<uint32_t>(block.iaid), IAType::IA_NA };

            if (block.addresses.empty())
            {
                block.status = { Dhcpv6StatusCode::NoAddrsAvail };
                continue;
            }

            for (auto& entry : block.addresses)
            {
                entry.status = ia.network->leaseManager->createLeaseFromAdvertised(
                    entry.address, iaKey, entry.staticValid == 0 ? validLifetime : entry.staticValid
                );
            }
        }
        for (auto& block : ia.iataBlocks)
        {
            const IAKey iaKey = { packet.send.clientID, utils::read<uint32_t>(block.iaid), IAType::IA_TA };

            if (block.addresses.empty())
            {
                block.status = { Dhcpv6StatusCode::NoAddrsAvail };
                continue;
            }


            for (auto& entry : block.addresses)
            {
                entry.status = ia.network->leaseManager->createLeaseFromAdvertised(
                    entry.address, iaKey, validLifetime
                );
            }
        }
        for (auto& block : ia.iapdBlocks)
        {
            const IAKey iaKey = { packet.send.clientID, utils::read<uint32_t>(block.iaid) };

            if (block.prefixes.empty())
            {
                block.status = { Dhcpv6StatusCode::NoPrefixAvail };
                continue;
            }

            for (auto& entry : block.prefixes)
            {
                if (!entry.pool || entry.prefix.addr == 0 || entry.prefix.prefixLength == 0)
                {
                    entry.status = { Dhcpv6StatusCode::NoPrefixAvail };
                    continue;
                }

                entry.status = entry.pool->leaseManager->createPrefixFromAdvertised(
                    entry.prefix, iaKey, entry.staticValid == 0 ? entry.pool->validLifetime.load(std::memory_order_relaxed) : entry.staticValid
                );
            }
        }
    }

    return Dhcpv6SendType{ DHCPV6_TYPE_REPLY, false };
}

std::optional<Dhcpv6SendType> Dhcpv6Server::processConfirm(Dhcpv6PacketReceive& packet, Dhcpv6IAOptions& ia)
{
    if (ia.empty() || !packet.send.multicast || /*auth*/false)
        return std::nullopt;

    bool allOnLink = true;
    const auto& pool = ia.network->pool;

    // Validate IA_NA addresses
    for (const auto& block : ia.ianaBlocks)
    {
        for (const auto& addr : block.addresses)
        {
            if (!pool->withinRange(addr.address))
            {
                allOnLink = false;
                break;
            }
        }
        if (!allOnLink) break;
    }
    if (allOnLink)
    {
        for (const auto& block : ia.iataBlocks)
        {
            for (const auto& addr : block.addresses)
            {
                if (!pool->withinRange(addr.address))
                {
                    allOnLink = false;
                    break;
                }
            }
            if (!allOnLink) break;
        }
    }


    return Dhcpv6SendType{ DHCPV6_TYPE_CONFIRM, allOnLink };
}

std::optional<Dhcpv6SendType> Dhcpv6Server::processRenew(Dhcpv6PacketReceive& packet, Dhcpv6IAOptions& ia, Duid& serverID)
{
    if (ia.empty() || serverID != dhcpUniqueIdentifier || /*auth*/false)
        return std::nullopt;

    if (ia.network)
    {
        uint32_t validLifetime = ia.network->configs.validLifetime.load(std::memory_order_relaxed);

        for (auto& block : ia.ianaBlocks)
        {
            const IAKey iaKey{ packet.send.clientID, utils::read<uint32_t>(block.iaid), IAType::IA_NA };

            for (auto& entry : block.addresses)
            {
                if (entry.address.isUnspecified())
                {
                    entry.status = { Dhcpv6StatusCode::NoBinding };
                    continue;
                }

                entry.status = ia.network->leaseManager->renewLease(
                    entry.address, iaKey, entry.staticValid == 0 ? validLifetime : entry.staticValid
                );
            }

            if (block.addresses.empty())
                block.status = { Dhcpv6StatusCode::NoBinding };
        }
        for (auto& block : ia.iataBlocks)
        {
            const IAKey iaKey{ packet.send.clientID, utils::read<uint32_t>(block.iaid), IAType::IA_TA };

            for (auto& entry : block.addresses)
            {
                if (entry.address.isUnspecified())
                {
                    entry.status = { Dhcpv6StatusCode::NoBinding };
                    continue;
                }

                entry.status = ia.network->leaseManager->renewLease(
                    entry.address, iaKey, validLifetime
                );
            }

            if (block.addresses.empty())
                block.status = { Dhcpv6StatusCode::NoAddrsAvail };
        }
        for (auto& block : ia.iapdBlocks)
        {
            const IAKey iaKey = { packet.send.clientID, utils::read<uint32_t>(block.iaid) };

            for (auto& entry : block.prefixes)
            {
                if (!entry.pool || entry.prefix.addr == 0 || entry.prefix.prefixLength == 0)
                {
                    entry.status = { Dhcpv6StatusCode::NoBinding };
                    continue;
                }

                entry.status = entry.pool->leaseManager->renewPrefix(
                    entry.prefix,
                    iaKey,
                    entry.staticValid == 0 ? entry.pool->validLifetime.load(std::memory_order_relaxed) : entry.staticValid
                );
            }

            if (block.prefixes.empty())
                block.status = { Dhcpv6StatusCode::NoBinding };
        }
    }

    return Dhcpv6SendType{ DHCPV6_TYPE_REPLY, false };
}

std::optional<Dhcpv6SendType> Dhcpv6Server::processRebind(Dhcpv6PacketReceive& packet, Dhcpv6IAOptions& ia)
{
    if (ia.empty() || !packet.send.multicast || /*auth*/false)
        return std::nullopt;

    if (ia.network)
    {
        const uint32_t validLifetime = ia.network->configs.validLifetime.load(std::memory_order_relaxed);

        for (auto& block : ia.ianaBlocks)
        {
            const IAKey iaKey = { packet.send.clientID, utils::read<uint32_t>(block.iaid), IAType::IA_NA };

            for (auto& entry : block.addresses)
            {
                if (entry.address.isUnspecified())
                {
                    entry.status = { Dhcpv6StatusCode::NoBinding };
                    continue;
                }

                entry.status = ia.network->leaseManager->rebindLease(
                    entry.address, iaKey, entry.staticValid == 0 ? validLifetime : entry.staticValid
                );
            }
            if (block.addresses.empty())
                block.status = { Dhcpv6StatusCode::NoBinding };
        }
        for (auto& block : ia.iapdBlocks)
        {
            const IAKey iaKey = { packet.send.clientID, utils::read<uint32_t>(block.iaid), IAType::IA_TA };

            for (auto& entry : block.prefixes)
            {
                if (!entry.pool || entry.prefix.addr == 0 || entry.prefix.prefixLength == 0)
                {
                    entry.status = { Dhcpv6StatusCode::NoBinding };
                    continue;
                }

                entry.status = entry.pool->leaseManager->rebindPrefix(
                    entry.prefix,
                    iaKey,
                    entry.staticValid == 0 ? entry.pool->validLifetime.load(std::memory_order_relaxed) : entry.staticValid
                );
            }

            if (block.prefixes.empty())
                block.status = { Dhcpv6StatusCode::NoBinding };
        }
    }

    // IATA must not be processed in rebind
    ia.iataBlocks.clear();

    return Dhcpv6SendType{ DHCPV6_TYPE_REPLY, false };
}

std::optional<Dhcpv6SendType> Dhcpv6Server::processRelease(Dhcpv6PacketReceive& packet, Dhcpv6IAOptions& ia, Duid& serverID)
{
    if (ia.empty() || packet.send.multicast || serverID != dhcpUniqueIdentifier || /*auth*/false)
        return std::nullopt;

    if (ia.network)
    {
        for (auto& block : ia.ianaBlocks)
        {
            const IAKey iaKey = { packet.send.clientID, utils::read<uint32_t>(block.iaid), IAType::IA_NA };

            for (auto& entry : block.addresses)
            {
                if (entry.address.isUnspecified())
                {
                    entry.status = { Dhcpv6StatusCode::NoBinding };
                    continue;
                }

                entry.status = ia.network->leaseManager->releaseLease(
                    entry.address,
                    iaKey
                );
            }

            if (block.addresses.empty())
                block.status = { Dhcpv6StatusCode::NoAddrsAvail };

        }
        for (auto& block : ia.iataBlocks)
        {
            const IAKey iaKey = { packet.send.clientID, utils::read<uint32_t>(block.iaid), IAType::IA_TA };

            for (auto& entry : block.addresses)
            {
                if (entry.address.isUnspecified())
                {
                    entry.status = { Dhcpv6StatusCode::NoBinding };
                }

                entry.status = ia.network->leaseManager->releaseLease(
                    entry.address,
                    iaKey
                );
            }

            if (block.addresses.empty())
                block.status = { Dhcpv6StatusCode::NoAddrsAvail };
        }
        for (auto& block : ia.iapdBlocks)
        {
            const IAKey iaKey = { packet.send.clientID, utils::read<uint32_t>(block.iaid) };

            for (auto& entry : block.prefixes)
            {
                if (!entry.pool || entry.prefix.addr == 0 || entry.prefix.prefixLength == 0)
                {
                    entry.status = { Dhcpv6StatusCode::NoPrefixAvail };
                    continue;
                }

                entry.status = entry.pool->leaseManager->releasePrefix(
                    entry.prefix,
                    iaKey
                );
            }

            if (block.prefixes.empty())
                block.status = { Dhcpv6StatusCode::NoPrefixAvail };
        }
    }

    return Dhcpv6SendType{ DHCPV6_TYPE_REPLY, false };
}

std::optional<Dhcpv6SendType> Dhcpv6Server::processDecline(Dhcpv6PacketReceive& packet, Dhcpv6IAOptions& ia, Duid& serverID)
{
    if (ia.empty() || packet.send.multicast || serverID != dhcpUniqueIdentifier || /*auth*/false)
        return std::nullopt;

    if (ia.network)
    {
        for (auto& block : ia.ianaBlocks)
        {
            const IAKey iaKey = { packet.send.clientID, utils::read<uint32_t>(block.iaid), IAType::IA_NA };

            if (block.addresses.empty())
            {
                block.status = { Dhcpv6StatusCode::NoBinding };
                continue;
            }

            for (auto& entry : block.addresses)
            {
                if (entry.address.isUnspecified())
                {
                    entry.status = { Dhcpv6StatusCode::NoBinding };
                    continue;
                }

                entry.status = ia.network->leaseManager->declineLease(
                    entry.address,
                    iaKey
                );
            }
        }
        for (auto& block : ia.iataBlocks)
        {
            const IAKey iaKey = { packet.send.clientID, utils::read<uint32_t>(block.iaid), IAType::IA_TA };

            if (block.addresses.empty())
            {
                block.status = { Dhcpv6StatusCode::NoBinding };
                continue;
            }

            for (auto& entry : block.addresses)
            {
                if (entry.address.isUnspecified())
                {
                    entry.status = { Dhcpv6StatusCode::NoBinding };
                    continue;
                }

                entry.status = ia.network->leaseManager->declineLease(
                    entry.address,
                    iaKey
                );
            }
        }
        for (auto& block : ia.iapdBlocks)
        {
            const IAKey iaKey = { packet.send.clientID, utils::read<uint32_t>(block.iaid) };

            for (auto& entry : block.prefixes)
            {
                if (!entry.pool || entry.prefix.addr == 0 || entry.prefix.prefixLength == 0)
                {
                    entry.status = { Dhcpv6StatusCode::NoPrefixAvail };
                    continue;
                }

                entry.status = entry.pool->leaseManager->declinePrefix(
                    entry.prefix,
                    iaKey
                );
            }

            if (block.prefixes.empty())
                block.status = { Dhcpv6StatusCode::NoBinding };
        }
    }

    return Dhcpv6SendType{ DHCPV6_TYPE_REPLY, false };
}

std::optional<Dhcpv6SendType> Dhcpv6Server::processInformationRequest(Dhcpv6PacketReceive& packet)
{
    if (!packet.send.oro || packet.send.oroSize == 0 ||/*auth*/false)
        return std::nullopt;
    
    sendReply(packet.send, nullptr, packet.dhcpHeader.getTransId(), false);
    return Dhcpv6SendType{ DHCPV6_TYPE_INFORMATION_REQUEST };
}

bool Dhcpv6Server::processRelayForward(const packet::Dhcpv6RelayHeader& relay, interface::Interface& iface, const uint8_t* clientAddress, const uint8_t* relayIp)
{
    Dhcpv6PacketBuild build(&iface);

    // Reserve below headers
    transport::udp::reserveUDP(build.builder, types::AddressFamily::IPv6);
    build.builder.reserveHeader(packet::HeaderType::DHCPV6_RELAY, packet::Dhcpv6RelayHeader::fixedSize);

    // Process and add relay chain
    std::vector<RelayLink> relayChain;
    packet::Dhcpv6Header dhcp;
    size_t chainSize = processRelayChain(relay, dhcp, relayChain);
    if (chainSize == 0)
        return false;

    // Add new dhcpv6 header index
    build.builder.bufferOffset += chainSize;
    size_t dhcpOffset = build.builder.bufferOffset;

    auto header = build.builder.reserveHeader(packet::HeaderType::DHCPV6, packet::Dhcpv6Header::fixedSize);
    if (!header) return false;
    build.dhcp.setBuffer(header->buffer);

    __uint128_t networkAddress = utils::read<__uint128_t>(relayChain.back().relay.getPeerAddress());

    // build packet

    Dhcpv6PacketSend send = {
        .iface = iface,
        .multicast = false,
        .relay = true,
        .clientAddress = clientAddress,
        .build = build
    };

    Dhcpv6PacketReceive packet = {
        .dhcpHeader = dhcp,
        .send = send
    };

    if (!handleDhcpPacket(packet, networkAddress))
        return false;

    size_t offset = relay.buffer - build.builder.getBuffer();
    uint8_t* buffer = build.builder.getBuffer();
    uint8_t* end = buffer + build.builder.bufferOffset;

    for (auto it = relayChain.rbegin(); it != relayChain.rend(); ++it)
    {
        if (offset > dhcpOffset) return false;
        std::memcpy(buffer + offset, it->relay.buffer, it->relay.fixedSize);
        buffer[offset] = DHCPV6_TYPE_RELAY_REPLY;
        offset += it->relay.fixedSize;
        for (auto opt : it->options)
        {
            utils::write<uint16_t>(buffer + offset, opt.type);
            utils::write<uint16_t>(buffer + offset + 2, static_cast<uint16_t>(end - (buffer + offset + 4)));
            std::memcpy(buffer + offset + 4, opt.value, opt.valueSize);
            offset += 4 + opt.valueSize;
        }
        
        utils::write<uint16_t>(buffer + offset, DHCPV6_OPTION_RELAY_MSG);
        utils::write<uint16_t>(buffer + offset + 2, static_cast<uint16_t>(end - (buffer + offset + 4)));
        offset += 4;
    }

    if (offset != dhcpOffset) return false;

    interface::Interface* interface = &iface;
    infrastructure::ippacket::BuildIP ip = {
        .iface = interface,
        .packetInfo = build.builder,
        .destIp = types::IPAddress(relayIp, types::AddressFamily::IPv6),
        .hopLimit = 64,
        .protocolType = IP_UDP
    };

    transport::udp::buildUdp(types::AddressFamily::IPv6, ip, UDP_DHCPV6_SERVER, UDP_DHCPV6_CLIENT);
    return true;
}

size_t Dhcpv6Server::processRelayChain(const packet::Dhcpv6RelayHeader& relay, packet::Dhcpv6Header& dhcp, std::vector<RelayLink>& chain)
{
    const uint8_t* current = relay.buffer;
    const uint8_t* end = relay.buffer + relay.fixedSize + relay.getTrail().size();

    const uint8_t* relayMsg = nullptr;
    size_t relayMsgLen = 0;

    size_t chainSize = 0;

    std::vector<packet::TLV16Option> options;

    while (true)
    {
        if (current + 34 > end || current[0] != DHCPV6_TYPE_RELAY_FORWARD)
            return 0;

        // Store the parsed relay
        chain.push_back({});
        chainSize += 34;
        auto& parsed = chain.back();
        size_t relaySize = end - current;
        if (!parsed.relay.parse(const_cast<uint8_t*>(current), relaySize, relaySize))
            return 0;

        // Parse options
        auto trail = parsed.relay.getTrail();
        if (!parseDhcpv6Options(trail.data(), trail.size(), options))
            return 0;

        // Locate the relay-msg
        bool foundRelayMsg = false;
        for (auto& opt : options)
        {
            if (opt.type == DHCPV6_OPTION_RELAY_MSG)
            {
                if (foundRelayMsg) return 0;
                relayMsg = opt.value;
                relayMsgLen = opt.valueSize;
                foundRelayMsg = true;
                chainSize += 4; // Option & Length
            }
            else
            {
                parsed.options.push_back(opt); // store other options for later
                chainSize += opt.valueSize + 4;
            }
        }

        options.clear();
        if (!foundRelayMsg || !relayMsg || relayMsgLen == 0)
            return 0;

        // Determine if inner message is another relay or a DHCP message
        if (relayMsg[0] == 12 && chainSize < 1500) // Another relay-forward
        {
            current = relayMsg;
            continue;
        }
        else
        {
            // Otherwise this is the real DHCPv6 message
            dhcp.setBuffer(const_cast<uint8_t*>(relayMsg));
            return chainSize;
        }
    }
}

// Packet Sending

bool Dhcpv6Server::sendAdvertise(Dhcpv6PacketSend& send, Dhcpv6IAOptions& ia, const uint8_t* transId)
{
    processing::PacketBuilder& builder = send.build.builder;
    if (!send.relay)
    {
        transport::udp::reserveUDP(builder, types::AddressFamily::IPv6);
        builder.reserveHeader(packet::HeaderType::DHCPV6, packet::Dhcpv6Header::fixedSize);
    }

    interface::Interface* interface = &send.iface;
    send.build.maxSize = interface->configs.ipv6.mtu.load(std::memory_order_relaxed) - builder.bufferOffset;
    if (send.relay) return true;

    // build udp
    infrastructure::ippacket::BuildIP ip = {
        .iface = interface,
        .packetInfo = send.build.builder,
        .destIp = send.clientAddress ? types::IPAddress(send.clientAddress, types::AddressFamily::IPv6) : types::IPAddress(DHCPV6_SERVER_TO_ALL, types::AddressFamily::IPv6),
        .hopLimit = 1,
        .protocolType = IP_UDP
    };

    transport::udp::buildUdp(types::AddressFamily::IPv6, ip, UDP_DHCPV6_SERVER, UDP_DHCPV6_CLIENT);
    return true;
}

bool Dhcpv6Server::sendReply(Dhcpv6PacketSend& send, Dhcpv6IAOptions* ia, const uint8_t* transId, bool rapidCommit)
{
    processing::PacketBuilder& builder = send.build.builder;
    if (!send.relay)
    {
        transport::udp::reserveUDP(builder, types::AddressFamily::IPv6);
        builder.reserveHeader(packet::HeaderType::DHCPV6, packet::Dhcpv6Header::fixedSize);
    }

    interface::Interface* interface = &send.iface;
    send.build.maxSize = interface->configs.ipv6.mtu.load(std::memory_order_relaxed) - builder.bufferOffset;
    buildReply(send.build, send, ia, transId, rapidCommit);
    if (send.relay) return true;

    // build udp
    infrastructure::ippacket::BuildIP ip = {
        .iface = interface,
        .packetInfo = send.build.builder,
        .destIp = send.clientAddress ? types::IPAddress(send.clientAddress, types::AddressFamily::IPv6) : types::IPAddress(DHCPV6_SERVER_TO_ALL, types::AddressFamily::IPv6),
        .hopLimit = 1,
        .protocolType = IP_UDP
    };

    transport::udp::buildUdp(types::AddressFamily::IPv6, ip, UDP_DHCPV6_SERVER, UDP_DHCPV6_CLIENT);
    return true;
}

bool Dhcpv6Server::sendConfirmReply(Dhcpv6PacketSend& send, Dhcpv6IAOptions& ia, const uint8_t* transId, bool success)
{
    processing::PacketBuilder& builder = send.build.builder;
    if (!send.relay)
    {
        transport::udp::reserveUDP(builder, types::AddressFamily::IPv6);
        builder.reserveHeader(packet::HeaderType::DHCPV6, packet::Dhcpv6Header::fixedSize);
    }

    interface::Interface* interface = &send.iface;
    send.build.maxSize = interface->configs.ipv6.mtu.load(std::memory_order_relaxed) - builder.bufferOffset;
    buildConfirmReply(send.build, send, ia, transId, success);
    if (send.relay) return true;

    // build udp
    infrastructure::ippacket::BuildIP ip = {
        .iface = interface,
        .packetInfo = send.build.builder,
        .destIp = send.clientAddress ? types::IPAddress(send.clientAddress, types::AddressFamily::IPv6) : types::IPAddress(DHCPV6_SERVER_TO_ALL, types::AddressFamily::IPv6),
        .hopLimit = 1,
        .protocolType = IP_UDP
    };

    transport::udp::buildUdp(types::AddressFamily::IPv6, ip, UDP_DHCPV6_SERVER, UDP_DHCPV6_CLIENT);
    return true;
}

bool Dhcpv6Server::sendReconfigure(Dhcpv6PacketSend& send, ReconfigReason reason)
{
    if (!authManager.getSettings().rkapEnabled.load(std::memory_order_relaxed))
        return false;
    auto clientIt = reconfigAccepts.find(send.clientID);
    if (clientIt == reconfigAccepts.end())
        return false;

    processing::PacketBuilder& builder = send.build.builder;
    if (!send.relay)
    {
        transport::udp::reserveUDP(builder, types::AddressFamily::IPv6);
        builder.reserveHeader(packet::HeaderType::DHCPV6, packet::Dhcpv6Header::fixedSize);
    }

    interface::Interface* interface = &send.iface;
    send.build.maxSize = interface->configs.ipv6.mtu.load(std::memory_order_relaxed) - builder.bufferOffset;
    buildReconfigure(send.build, send, reason);

    // build udp
    infrastructure::ippacket::BuildIP ip = {
        .iface = interface,
        .packetInfo = send.build.builder,
        .destIp = send.clientAddress ? types::IPAddress(send.clientAddress, types::AddressFamily::IPv6) : types::IPAddress(DHCPV6_SERVER_TO_ALL, types::AddressFamily::IPv6),
        .hopLimit = 1,
        .protocolType = IP_UDP
    };

    transport::udp::buildUdp(types::AddressFamily::IPv6, ip, UDP_DHCPV6_SERVER, UDP_DHCPV6_CLIENT);
    return true;
}

// Packet Builders

bool Dhcpv6Server::buildAdvertise(Dhcpv6PacketBuild& build, Dhcpv6PacketSend& send, Dhcpv6IAOptions& ia, const uint8_t* transId)
{
    auto& dhcp = build.dhcp;
    auto& builder = build.builder;

    dhcp.setType(DHCPV6_TYPE_ADVERTISE);
    dhcp.setTransId(transId);

    // Add TLV options directly to trailing span
    packet::TLV16BufferManager tlv(dhcp.getTrailData(), build.maxSize);

    // Add Client identifier
    tlv.append(DHCPV6_OPTION_CLIENT_ID, send.clientID.size, send.clientID.data, send.clientID.size);

    // Add Server identifier
    tlv.append(DHCPV6_OPTION_SERVER_ID, dhcpUniqueIdentifier.size, dhcpUniqueIdentifier.data, dhcpUniqueIdentifier.size);

    ADD_DELAYED_AUTH

    // Add server preference
    uint8_t preference = send.iface.configs.dhcpv6.configs->preferenceValue.load(std::memory_order_relaxed);
    if (preference != 0)
        tlv.append(DHCPV6_OPTION_PREFERENCE, 1, &preference, 1);

    // Send
    if (send.multicast && configs.allowUnicast.load(std::memory_order_relaxed) && !ia.ianaBlocks.empty())
    {
        auto block = ia.ianaBlocks.begin();
        if (!block->addresses.empty())
        {
            addServerUnicast(tlv, block->addresses.front().address, send.iface);
        }
    }

    // Add IA options
    buildResponse(dhcp, send.clientID, tlv, &ia, send.oro, send.oroSize);

    builder.addTLVSize(tlv.size());

    ADD_DELAYED_DIGEST
    return true;
}

bool Dhcpv6Server::buildReply(Dhcpv6PacketBuild& build, Dhcpv6PacketSend& send, Dhcpv6IAOptions* ia, const uint8_t* transId, bool rapidCommit)
{
    auto& dhcp = build.dhcp;
    auto& builder = build.builder;

    dhcp.setType(DHCPV6_TYPE_REPLY);
    dhcp.setTransId(transId);

    // Add TLV options directly to trailing span
    packet::TLV16BufferManager tlv(dhcp.getTrailData(), build.maxSize);

    // Add Client identifier
    tlv.append(DHCPV6_OPTION_CLIENT_ID, send.clientID.size, send.clientID.data, send.clientID.size);

    // Add Server identifier
    tlv.append(DHCPV6_OPTION_SERVER_ID, dhcpUniqueIdentifier.size, dhcpUniqueIdentifier.data, dhcpUniqueIdentifier.size);

    ADD_DELAYED_AUTH

    // Send
    if (send.multicast && configs.allowUnicast.load(std::memory_order_relaxed) && !ia->ianaBlocks.empty())
    {
        auto block = ia->ianaBlocks.begin();
        if (!block->addresses.empty())
        {
            addServerUnicast(tlv, block->addresses.front().address, send.iface);
        }
    }

    // Add IA options
    buildResponse(dhcp, send.clientID, tlv, ia, send.oro, send.oroSize);

    if (rapidCommit)
        tlv.append(DHCPV6_OPTION_RAPID_COMMIT, 0, nullptr, 0);

    builder.addTLVSize(tlv.size());

    ADD_DELAYED_DIGEST

    return true;
}

bool Dhcpv6Server::buildConfirmReply(Dhcpv6PacketBuild& build, Dhcpv6PacketSend& send, Dhcpv6IAOptions& ia, const uint8_t* transId, bool allOnLink)
{
    auto& dhcp = build.dhcp;
    auto& builder = build.builder;

    dhcp.setType(DHCPV6_TYPE_REPLY);
    dhcp.setTransId(transId);

    // Add TLV options directly to trailing span
    packet::TLV16BufferManager tlv(dhcp.getTrailData(), build.maxSize);

    // Add Client identifier
    tlv.append(DHCPV6_OPTION_CLIENT_ID, send.clientID.size, send.clientID.data, send.clientID.size);

    // Add Server identifier
    tlv.append(DHCPV6_OPTION_SERVER_ID, dhcpUniqueIdentifier.size, dhcpUniqueIdentifier.data, dhcpUniqueIdentifier.size);

    ADD_DELAYED_AUTH

    if (allOnLink)
    {
        uint8_t successStatus[2];
        utils::write<uint16_t>(successStatus, 0);
        tlv.append(DHCPV6_OPTION_STATUS_CODE, 2, successStatus, 2);
    }
    else
    {
        uint8_t unsuccess[10];
        utils::write<uint16_t>(unsuccess, static_cast<uint16_t>(Dhcpv6StatusCode::NotOnLink));
        utils::write<uint64_t>(unsuccess + 2, 0x626164206C696E6B); // "Bad link"
        tlv.append(DHCPV6_OPTION_STATUS_CODE, 10, unsuccess, 10);
    }

    // Add IA options
    buildResponse(dhcp, send.clientID, tlv, &ia, send.oro, send.oroSize);

    builder.addTLVSize(tlv.size());

    ADD_DELAYED_DIGEST
    return true;
}

bool Dhcpv6Server::buildReconfigure(Dhcpv6PacketBuild& build, Dhcpv6PacketSend& send, ReconfigReason reason)
{
    if (activeReconfigs.count(send.clientID)) return false;

    auto& dhcp = build.dhcp;
    auto& builder = build.builder;

    dhcp.setType(DHCPV6_TYPE_RECONFIGURE);
    generateDhcpTransid(dhcp.raw->transId);

    packet::TLV16BufferManager tlv(dhcp.getTrailData(), build.maxSize);

    // Add Server ID
    tlv.append(DHCPV6_OPTION_SERVER_ID, dhcpUniqueIdentifier.size, dhcpUniqueIdentifier.data, dhcpUniqueIdentifier.size);

    // Add ClientID
    tlv.append(DHCPV6_OPTION_CLIENT_ID, send.clientID.size, send.clientID.data, send.clientID.size);

    // Add Auth (RKAP)
    auto key = authManager.addRkapAuthOption(tlv, send.clientID);
    if (!key.has_value()) return false;

    // Add reconfig message
    uint8_t reconfigType = static_cast<uint8_t>(reason);
    tlv.append(DHCPV6_OPTION_RECONFIG_MESSAGE, 1, &reconfigType, 1);

    builder.addTLVSize(tlv.size());

    activeReconfigs[send.clientID] = ReconfigureState {
        .reason = reason,
        .secret = key.value(),
        .interfaceKey = send.iface.configs.key.getId(),
        .timerID = timeManager.addTimer(
            std::chrono::steady_clock::now() + std::chrono::seconds(configs.reconfigureTimeout.load(std::memory_order_relaxed)),
            [this, clientID = send.clientID](uint32_t) {
                activeReconfigs.erase(clientID);
            }
        ),
        .transactionID = utils::read<uint32_t, 3>(dhcp.getTransId())
    };

    return true;
}

std::optional<IANABlock> Dhcpv6Server::extractIA_NA(const packet::TLV16Option& option, DhcpNetwork* network, Duid& clientID, bool isBinding)
{
    if (option.type == DHCPV6_OPTION_IA_NA && option.valueSize >= 12)
    {
        IANABlock iana;
        iana.iaid = option.value;

        if (!network)
        {
            iana.status = { isBinding ? Dhcpv6StatusCode::NoBinding : Dhcpv6StatusCode::NoAddrsAvail };
            return iana;
        }

        std::vector<packet::TLV16Option> options;
        parseDhcpv6Options(option.value + 12, option.valueSize - 12, options);

        for (const auto& iaaddr : options)
        {
            if (iaaddr.type == DHCPV6_OPTION_IAADDR && iaaddr.valueSize >= 24)
            {
                if (utils::read<uint32_t>(iaaddr.value + 16) > utils::read<uint32_t>(iaaddr.value + 20))
                {
                    iana.addresses.clear();
                    iana.status = { Dhcpv6StatusCode::UnspecFail };
                    return iana;
                }

                Dhcpv6StatusMessage status = { Dhcpv6StatusCode::None };
                uint32_t preferred = 0;
                uint32_t valid = 0;

                if (iaaddr.valueSize > 24)
                {
                    std::vector<packet::TLV16Option> iaaddrOpts;
                    parseDhcpv6Options(iaaddr.value + 24, iaaddr.valueSize - 24, iaaddrOpts);
                    for (const auto& iaaddrOpt : iaaddrOpts)
                    {
                        if (iaaddrOpt.type == DHCPV6_OPTION_STATUS_CODE && iaaddrOpt.valueSize >= 2)
                        {
                            status = {
                                static_cast<Dhcpv6StatusCode>(utils::read<uint16_t>(iaaddrOpt.value)),
                                std::string(reinterpret_cast<const char*>(iaaddrOpt.value + 2), iaaddrOpt.valueSize - 2)
                            };
                            break;
                        }
                    }
                }

                __uint128_t ianaAddr = utils::read<__uint128_t>(iaaddr.value);
                if (auto it = configs.staticIANAConfigs.find({ clientID, utils::read<uint32_t>(iana.iaid), ianaAddr, IAType::IA_NA }); it != configs.staticIANAConfigs.end())
                {
                    preferred = it->second.preferred;
                    valid = it->second.valid;
                }

                iana.addresses.push_back({ ianaAddr, { status }, preferred, valid });
            }
            else if (iaaddr.type == DHCPV6_OPTION_STATUS_CODE && iaaddr.valueSize >= 2 && iana.status.code == Dhcpv6StatusCode::None)
            {
                iana.status = {
                    static_cast<Dhcpv6StatusCode>(utils::read<uint16_t>(iaaddr.value)),
                    std::string(reinterpret_cast<const char*>(iaaddr.value + 2), iaaddr.valueSize - 2)
                };
            }
        } 
        return iana;
    }
    return std::nullopt;
}

std::optional<IATABlock> Dhcpv6Server::extractIA_TA(const packet::TLV16Option& option, DhcpNetwork* network, bool isBinding)
{
    if (option.type == DHCPV6_OPTION_IA_TA && option.valueSize >= 12)
    {
        IATABlock iata;
        iata.iaid = option.value;

        if (!network)
        {
            iata.status = { isBinding ? Dhcpv6StatusCode::NoBinding : Dhcpv6StatusCode::NoAddrsAvail };
            return iata;
        }

        std::vector<packet::TLV16Option> options;
        parseDhcpv6Options(option.value + 12, option.valueSize - 12, options);

        for (const auto& iaaddr : options)
        {
            if (iaaddr.type == DHCPV6_OPTION_IAADDR && iaaddr.valueSize >= 24)
            {
                if (utils::read<uint32_t>(iaaddr.value + 16) > utils::read<uint32_t>(iaaddr.value + 20))
                {
                    iata.addresses.clear();
                    iata.status = { Dhcpv6StatusCode::UnspecFail };
                    return iata;
                }

                Dhcpv6StatusMessage status = { Dhcpv6StatusCode::None };

                if (iaaddr.valueSize > 24)
                {
                    std::vector<packet::TLV16Option> iaaddrOpts;
                    parseDhcpv6Options(iaaddr.value + 24, iaaddr.valueSize - 24, iaaddrOpts);
                    for (const auto& iaaddrOpt : iaaddrOpts)
                    {
                        if (iaaddrOpt.type == DHCPV6_OPTION_STATUS_CODE && iaaddrOpt.valueSize >= 2)
                        {
                            status = {
                                static_cast<Dhcpv6StatusCode>(utils::read<uint16_t>(iaaddrOpt.value)),
                                std::string(reinterpret_cast<const char*>(iaaddrOpt.value + 2), iaaddrOpt.valueSize - 2)
                            };
                            break;
                        }
                    }
                }

                iata.addresses.push_back({ utils::read<__uint128_t>(iaaddr.value), status });
            }
            else if (iaaddr.type == DHCPV6_OPTION_STATUS_CODE && iaaddr.valueSize >= 2 && iata.status.code == Dhcpv6StatusCode::None)
            {
                iata.status = {
                    static_cast<Dhcpv6StatusCode>(utils::read<uint16_t>(iaaddr.value)),
                    std::string(reinterpret_cast<const char*>(iaaddr.value + 2), iaaddr.valueSize - 2)
                };
            }
        } 

        return iata;
    }
    return std::nullopt;
}

std::optional<IAPDBlock> Dhcpv6Server::extractIA_PD(const packet::TLV16Option& option, DhcpNetwork* network, Duid& clientID, bool isBinding)
{
    if (option.type == DHCPV6_OPTION_IA_PD && option.valueSize >= 12)
    {
        IAPDBlock iapd;
        iapd.iaid = option.value;

        if (!network)
        {
            iapd.status = { isBinding ? Dhcpv6StatusCode::NoBinding : Dhcpv6StatusCode::NoPrefixAvail };
            return iapd;
        }

        std::vector<packet::TLV16Option> options;
        parseDhcpv6Options(option.value + 12, option.valueSize - 12, options);

        for (const auto& iaprefix : options)
        {
            if (iaprefix.type == DHCPV6_OPTION_IA_PREFIX && iaprefix.valueSize >= 25)
            {
                if (utils::read<uint32_t>(iaprefix.value) > utils::read<uint32_t>(iaprefix.value + 4))
                {
                    iapd.prefixes.clear();
                    iapd.status = { Dhcpv6StatusCode::UnspecFail };
                    return iapd;
                }

                Dhcpv6StatusMessage status = { Dhcpv6StatusCode::None };
                uint32_t preferred = 0;
                uint32_t valid = 0;

                if (iaprefix.valueSize > 25)
                {
                    std::vector<packet::TLV16Option> iaprefixOpts;
                    parseDhcpv6Options(iaprefix.value + 25, iaprefix.valueSize - 25, iaprefixOpts);
                    for (const auto& iaprefixOpt : iaprefixOpts)
                    {
                        if (iaprefixOpt.type == DHCPV6_OPTION_STATUS_CODE && iaprefixOpt.valueSize >= 2)
                        {
                            status = {
                                static_cast<Dhcpv6StatusCode>(utils::read<uint16_t>(iaprefixOpt.value)),
                                std::string(reinterpret_cast<const char*>(iaprefixOpt.value + 2), iaprefixOpt.valueSize - 2)
                            };
                            break;
                        }
                    }
                }

                uint8_t prefixLen = iaprefix.value[8];
                types::IPv6Prefix prefix = { utils::read<__uint128_t>(iaprefix.value + 9), prefixLen };
                auto* pool = selectPrefixPool(network, &prefix);

                if (auto it = configs.staticIAPDConfigs.find({ clientID, utils::read<uint32_t>(iapd.iaid), prefix.addr, prefix.prefixLength }); it != configs.staticIAPDConfigs.end())
                {
                    preferred = it->second.preferred;
                    valid = it->second.valid;
                }

                iapd.prefixes.push_back({ prefix, status, pool, preferred, valid });
            }
            else if (iaprefix.type == DHCPV6_OPTION_STATUS_CODE && iaprefix.valueSize >= 2 && iapd.status.code == Dhcpv6StatusCode::None)
            {
                iapd.status = {
                    static_cast<Dhcpv6StatusCode>(utils::read<uint16_t>(iaprefix.value)),
                    std::string(reinterpret_cast<const char*>(iaprefix.value + 2), iaprefix.valueSize - 2)
                };
            }
        }

        return iapd;
    }   
    return std::nullopt;
}

bool Dhcpv6Server::addServerUnicast(packet::TLV16BufferManager& tlv, types::IPv6Address leasedIp, interface::Interface& iface)
{
    uint8_t ip[16];
    uint8_t* buf = nullptr;
    if (leasedIp.isGlobalUnicast())
    {
        buf = iface.configs.ipv6.getGlobalUnicast(ip);
    }
    else if (leasedIp.isLocalUnicast())
    {
        buf = iface.configs.ipv6.getLocalUnicast(ip);
    }
    if (buf)
    {
        tlv.append(DHCPV6_OPTION_UNICAST, 16, ip, 16);
        return true;
    }
    return false;
}

DhcpNetwork* Dhcpv6Server::matchAddressToPool(const types::IPv6Address& addr, uint32_t interfaceKey)
{
    for (const auto& [prefix, pool] : prefixToPool)
    {
        if (pool->configs.interfaceKey != interfaceKey || prefix.prefixLength > 128) continue; // Invalid

        __uint128_t mask = (prefix.prefixLength == 0)
            ? 0
            : ~(__uint128_t(0)) << (128 - prefix.prefixLength);

        if ((addr & mask) == (prefix.addr && mask))
        {
            return pool;
        }
    }

    return nullptr;
}

PrefixPoolConfig* Dhcpv6Server::selectPrefixPool(const DhcpNetwork* network, const types::IPv6Prefix* requestedPrefix)
{
    if (!network) return nullptr;

    for (PrefixPoolConfig* cfg : network->prefixPools)
    {
        if (!cfg || !cfg->pool) continue;

        PrefixPool& pool = *cfg->pool;

        if (pool.isFull()) continue;

        if (requestedPrefix && requestedPrefix->addr != 0)
        {
            if (!pool.withinRange({ requestedPrefix->addr, pool.delegationLength }))
                continue;
        }

        return cfg;
    }
    return nullptr;
}

bool Dhcpv6Server::addStaticLease(IANABlock& block, const IAKey& key)
{
    if (auto lit = configs.staticNAs.find(key); lit != configs.staticNAs.end() && lit->second.second)
    {
        auto& network = lit->second.second;
        if (auto it = network->leaseManager->staticNAs.find(key); it != network->leaseManager->staticNAs.end())
        {
            block.addresses.push_back({ it->second.address, { Dhcpv6StatusCode::Success }, it->second.preferred, it->second.valid });
            if (auto tit = network->leaseManager->leaseTimerIDs.find(it->second.address); tit != network->leaseManager->leaseTimerIDs.end()) timeManager.cancelTimer(tit->second);
            auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(it->second.valid);
            network->leaseManager->leaseTimerIDs[it->second.address] = timeManager.addTimer(expiry, [lmgr = network->leaseManager, addr = it->second.address, key](uint32_t) {
                lmgr->expireLease(addr, key);
            });
            network->leaseManager->leaseKeys[it->second.address] = key;
            return true;
        }
    }
    return false;
}

bool Dhcpv6Server::addStaticPrefix(IAPDBlock& block, const IAKey& key)
{
    if (auto lit = configs.staticPDs.find(key); lit != configs.staticPDs.end() && lit->second.second)
    {
        auto& prefix = lit->second.second;
        if (auto it = prefix->leaseManager->staticPDs.find(key); it != prefix->leaseManager->staticPDs.end())
        {
            block.prefixes.push_back({ it->second.prefix, { Dhcpv6StatusCode::Success }, prefix, it->second.preferred, it->second.valid });
            if (auto tit = prefix->leaseManager->prefixTimerIDs.find(it->second.prefix); tit != prefix->leaseManager->prefixTimerIDs.end()) timeManager.cancelTimer(tit->second);
            auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(it->second.valid);
            prefix->leaseManager->prefixTimerIDs[it->second.prefix] = timeManager.addTimer(expiry, [lmgr = prefix->leaseManager, p = it->second.prefix, key](uint32_t) {
                lmgr->expirePrefix(p, key);
            });
            prefix->leaseManager->prefixKeys[it->second.prefix] = key;
            return true;
        }
    }
    return false;
}

bool Dhcpv6Server::addStaticAdvertisedLease(IANABlock& block, const IAKey& key)
{
    if (auto lit = configs.staticNAs.find(key); lit != configs.staticNAs.end() && lit->second.second)
    {
        auto& network = lit->second.second;
        if (auto it = network->leaseManager->staticNAs.find(key); it != network->leaseManager->staticNAs.end())
        {
            block.addresses.push_back({ it->second.address, { Dhcpv6StatusCode::Success }, it->second.preferred, it->second.valid });
            const IALeaseKey leaseKey = {key.duid, key.iaid, it->second.address};
            network->pool->advertised[leaseKey] = {
                timeManager.addTimer(
                std::chrono::steady_clock::now() + std::chrono::seconds(it->second.valid),
                [pool = network->pool, leaseKey](uint32_t) {
                    std::lock_guard<std::mutex> lock(pool->mutex);
                    pool->advertised.erase(leaseKey);
                    pool->advertisedIPs.erase(leaseKey.address);
                })
            };
            network->pool->advertisedIPs.insert(leaseKey.address);
            return true;
        }
    }
    return false;
}

bool Dhcpv6Server::addStaticAdvertisedPrefix(IAPDBlock& block, const IAKey& key)
{
    if (auto lit = configs.staticPDs.find(key); lit != configs.staticPDs.end() && lit->second.second)
    {
        auto& prefix = lit->second.second;
        if (auto it = prefix->leaseManager->staticPDs.find(key); it != prefix->leaseManager->staticPDs.end())
        {
            block.prefixes.push_back({ it->second.prefix, { Dhcpv6StatusCode::Success }, prefix, it->second.preferred, it->second.valid });
            const IAPrefixKey leaseKey = {key.duid, key.iaid, it->second.prefix.addr, it->second.prefix.prefixLength };
            prefix->pool->advertised[leaseKey] = {
                timeManager.addTimer(
                std::chrono::steady_clock::now() + std::chrono::seconds(it->second.valid),
                [pool = prefix->pool, leaseKey](uint32_t) {
                    std::lock_guard<std::mutex> lock(pool->mutex);
                    pool->advertised.erase(leaseKey);
                    pool->advertisedPDs.erase({ leaseKey.address, leaseKey.prefixLength });
                })
            };
            prefix->pool->advertisedPDs.insert({ leaseKey.address, leaseKey.prefixLength });
        }
        return true;
    }
    return false;
}

} // namespace services::dhcp
