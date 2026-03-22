// PeerTemplate.h

#ifndef BGP_PEER_TEMPLATE_H
#define BGP_PEER_TEMPLATE_H

#include <string>
#include <unordered_map>
#include <cstring>

#include "configs/registry/router/BgpRegistry.h"
#include "bgp/BgpTypes.hpp"

namespace BGP
{
class BgpProcess;

class PeerGroup
{
public:
    PeerGroup(const std::string& groupName, BgpProcess& proc);
    ~PeerGroup() = default;

    PeerGroup(const PeerGroup&) = delete;
    PeerGroup& operator=(const PeerGroup&) = delete;
    PeerGroup(PeerGroup&&) = delete;
    PeerGroup& operator=(PeerGroup&&) = delete;

    const std::string name;

    Config::BgpNeighborSessionRegistry& getSessionConfigs()
    {
        return sessionConfigs.get();
    }

    const Config::BgpNeighborSessionRegistry& getSessionConfigs() const
    {
        return sessionConfigs.get();
    }

    // Get or lazily create per-AF config for this peer group.
    Config::BgpNeighborRegistry* getAfConfigs(const AfiSafi& afi);
    const Config::BgpNeighborRegistry* getAfConfigs(const AfiSafi& afi) const;

private:
    BgpProcess& process;
    Config::Reference<Config::BgpNeighborSessionRegistry> sessionConfigs;
    mutable std::unordered_map<AfiSafi, Config::Reference<Config::BgpNeighborRegistry>> afConfigs;
};

class PeerSessionTemplate
{
public:
    PeerSessionTemplate(const std::string& groupName, BgpProcess& proc);
    ~PeerSessionTemplate() = default;

    PeerSessionTemplate(const PeerSessionTemplate&) = delete;
    PeerSessionTemplate& operator=(const PeerSessionTemplate&) = delete;
    PeerSessionTemplate(PeerSessionTemplate&&) = delete;
    PeerSessionTemplate& operator=(PeerSessionTemplate&&) = delete;

    const std::string name;

    Config::BgpNeighborSessionRegistry& getConfigs()
    {
        return configs.get();
    }

    const Config::BgpNeighborSessionRegistry& getConfigs() const
    {
        return configs.get();
    }

private:
    Config::Reference<Config::BgpNeighborSessionRegistry> configs;
};

class PeerPolicyTemplate
{
public:
    PeerPolicyTemplate(const std::string& groupName, BgpProcess& proc);
    ~PeerPolicyTemplate() = default;

    PeerPolicyTemplate(const PeerPolicyTemplate&) = delete;
    PeerPolicyTemplate& operator=(const PeerPolicyTemplate&) = delete;
    PeerPolicyTemplate(PeerPolicyTemplate&&) = delete;
    PeerPolicyTemplate& operator=(PeerPolicyTemplate&&) = delete;

    const std::string name;

    Config::BgpNeighborRegistry& getConfigs()
    {
        return configs.get();
    }

    const Config::BgpNeighborRegistry& getConfigs() const
    {
        return configs.get();
    }

private:
    Config::Reference<Config::BgpNeighborRegistry> configs;
};

class PeerTemplateTable
{
public:
    explicit PeerTemplateTable(BgpProcess& proc);

    // Re-wire all neighbor pointers to match the current template objects.
    void sync();

    PeerGroup& createPeerGroup(const std::string& name);
    void removePeerGroup(const std::string& name);
    PeerSessionTemplate& createPeerSessionTemplate(const std::string& name);
    void removePeerSessionTemplate(const std::string& name);
    PeerPolicyTemplate& createPeerPolicyTemplate(const std::string& name);
    void removePeerPolicyTemplate(const std::string& name);

    PeerGroup* lookupPeerGroup(const std::string& name);
    const PeerGroup* lookupPeerGroup(const std::string& name) const;
    PeerSessionTemplate* lookupPeerSessionTemplate(const std::string& name);
    const PeerSessionTemplate* lookupPeerSessionTemplate(const std::string& name) const;
    PeerPolicyTemplate* lookupPeerPolicyTemplate(const std::string& name);
    const PeerPolicyTemplate* lookupPeerPolicyTemplate(const std::string& name) const;

private:
    void syncPeerGroups();
    void syncPeerSessionTemplates();
    void syncPeerPolicyTemplates();

    BgpProcess& process;
    std::unordered_map<std::string, PeerGroup> peerGroups;
    std::unordered_map<std::string, PeerSessionTemplate> peerSessionTemplates;
    std::unordered_map<std::string, PeerPolicyTemplate> peerPolicyTemplates;
};

} // namespace BGP

#endif // BGP_PEER_TEMPLATE_H
