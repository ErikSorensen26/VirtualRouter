// EigrpConfig.h

#ifndef EIGRP_CONFIG_H
#define EIGRP_CONFIG_H

#include <cstdint>
#include <unordered_set>
#include <IPAddress.h>

#include "eigrp/EigrpTypes.hpp"
#include "configs/registry/router/EigrpRegistry.h"
#include "configs/RegistryReference.hpp"

namespace routing::eigrp
{
class Eigrp;

class EigrpConfig
{
public:

    EigrpConfig(Eigrp& base);
    void addNetworkRange(const types::IPv4Prefix& newNetwork);
    void delNetworkRange(const types::IPv4Prefix& delNetwork);
    bool isInNetworkRange(types::IPv4Address testIp) const;
    void clearNetworks();
    void enableStub(bool isStub, bool advertiseConnected = true, bool advertiseStatic = true, bool advertiseSummary = true, bool advertiseRedistributed = true);
    void setPassiveInterface(uint32_t key, bool add = true);
    void enableUnicastPeer(const types::IPAddress& neighborIp, uint32_t key);
    void disableUnicastPeer(const types::IPAddress& neighborIp, uint32_t key);

    config::EigrpRegistry& getConfigs() { return configs.get(); }

    // Process-level config accessors
    bool stubEnabled() const { return configs->get<config::Eigrp::STUB>().load(); }
    StubConfig getStubConfig() const;
    KValue getKValues() const;

    bool isPassive(uint32_t key) const;
    std::unordered_set<types::IPAddress> getUnicastNeighbors(uint32_t key) const;

    bool getDampening() const          { return configs->get<config::Eigrp::DAMPENING>().load(); }
    bool getDampeningWarning() const   { return configs->get<config::Eigrp::DAMPENING_WARNINGS>().load(); }
    uint8_t getDampeningInterval() const  { return configs->get<config::Eigrp::DAMPENING_INTERVAL>().load(); }
    uint16_t getDampeningResetTime() const { return configs->get<config::Eigrp::DAMPENING_RESET_TIME>().load(); }
    uint16_t getDampeningRestart() const   { return configs->get<config::Eigrp::DAMPENING_RESTART>().load(); }
    uint16_t getDampeningRestartCount() const { return configs->get<config::Eigrp::DAMPENING_RESTART_COUNT>().load(); }

    uint32_t getMaximumPrefixes() const { return configs->get<config::Eigrp::MAXIMUM_PREFIX>().load(); }
    uint8_t getRibScale() const         { return configs->get<config::Eigrp::RIB_SCALE>().load(); }
    uint8_t getAD() const               { return configs->get<config::Eigrp::INTERNAL_ADMIN_DISTANCE>().load(); }
    uint8_t getExternalAD() const       { return configs->get<config::Eigrp::EXTERNAL_ADMIN_DISTANCE>().load(); }
    uint8_t getMaxPaths() const         { return configs->get<config::Eigrp::MAX_PATHS>().load(); }
    uint8_t getMaxHops() const          { return configs->get<config::Eigrp::MAX_HOPS>().load(); }
    uint8_t getVariance() const         { return configs->get<config::Eigrp::VARIANCE>().load(); }
    config::eigrp::TrafficShareMode getTrafficMode() const { return configs->get<config::Eigrp::TRAFFIC_SHARE>().load(); }

    bool isNonStopForwarding() const { return configs->get<config::Eigrp::NON_STOP_FORWARDING>().load(); }
    uint16_t getPurgeTime() const    { return configs->get<config::Eigrp::GRACEFUL_PURGE_TIME>().load(); }
    bool isAutoSummarized() const    { return configs->get<config::Eigrp::AUTO_SUMMARIZATION>().load(); }
    void setAutoSummary(bool enable) { configs->get<config::Eigrp::AUTO_SUMMARIZATION>().set(enable); }

    uint16_t getSIATime() const
    {
        auto& field = configs->get<config::Eigrp::ACTIVE_TIME>();
        return field.hasValue() ? field.load() : 90;
    }

    uint16_t getDelTimer() const { return 120; }

private:

    Eigrp& base;
    config::Reference<config::EigrpRegistry> configs;
};
} // namespace routing

#endif // EIGRP_CONFIG_H

