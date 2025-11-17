// EigrpConfig.h

#ifndef EIGRP_CONFIG_H
#define EIGRP_CONFIG_H

#include <cstdint>
#include <EigrpTypes.hpp>
#include <GlobalAggregator.h>

struct EigrpHeader;

namespace Eigrp
{
class Eigrp;

class EigrpConfig
{
public:

    EigrpConfig(Eigrp& base)
        : base(base), aggregator(base) {}
    void addNetworkRange(const EigrpConfigs::Network& newNetwork);
    bool isInNetworkRange(const uint8_t* testIp);
    void enableStub(bool isStub, bool advertiseConnected = true, bool advertiseLeakMap = true, bool advertiseStatic = true, bool advertiseSummary = true, bool advertiseRedistributed = true);
    bool stubEnabled() const { return configs.stubConfig.isStub; }
    void setPassiveInterface(uint32_t key, bool add = true);
    void enableUnicastPeer(const IPAddress& neighborIp, uint32_t key);
    void disableUnicastPeer(const IPAddress& neighborIp, uint32_t key);

    EigrpConfigs::EigrpConfigs& getConfigs() { return configs; }

public:
    inline uint8_t getVariance() { return configs.variance.load(std::memory_order_relaxed); }
    inline uint8_t getAD() { return configs.adminDistance.load(std::memory_order_relaxed); }
    inline uint8_t getExternalAD() { return configs.externalAdminDistance.load(std::memory_order_relaxed); }
    inline uint8_t getMaxHops() { return configs.maxHops.load(std::memory_order_relaxed); }
    inline uint8_t getRibScale() { return configs.ribScale.load(std::memory_order_relaxed); }
    inline uint16_t getDelTimer() { return configs.routeDelTimer.load(std::memory_order_relaxed); }
    inline uint16_t getSIATime() { return configs.stuckInActiveTime.load(std::memory_order_relaxed); }
    inline uint32_t getPurgeTime() { return configs.purgeTime.load(std::memory_order_relaxed); }
    inline uint32_t getWideMetrics() { return configs.wideMetric.load(std::memory_order_relaxed); }
    inline uint32_t getMaximumPrefixes() { return configs.maximumPrefix.load(std::memory_order_relaxed); }
    inline uint16_t getDampeningRestart() { return configs.dampeningRestart.load(std::memory_order_relaxed); }
    inline uint16_t getDampeningInterval() { return configs.dampeningInterval.load(std::memory_order_relaxed); }
    inline uint16_t getDampeningResetTime() { return configs.dampeningResetTime.load(std::memory_order_relaxed); }
    inline uint16_t getDampeningRestartCount() { return configs.dampeningRestartCount.load(std::memory_order_relaxed); }
    inline EigrpConfigs::StubConfig getStubConfig() { std::shared_lock<std::shared_mutex> lock(configs.configsMutex); return configs.stubConfig; }
    inline EigrpConfigs::KValue getKValues() { std::shared_lock<std::shared_mutex> lock(configs.configsMutex); return configs.kvalue; }
    inline bool getDampeningWarning() { return configs.dampeningWarnings.load(std::memory_order_relaxed); }
    inline bool isNonStopForwarding() { return configs.nonStopForwarding.load(std::memory_order_relaxed); }
    inline bool getDampening() { return configs.dampening.load(std::memory_order_relaxed); }
    inline bool isPassive(uint32_t key) { std::shared_lock<std::shared_mutex> lock(configs.configsMutex); return configs.passiveInterfaces.contains(key); }
    inline bool isAutoSummarized() { return configs.autoSummarizationEnabled.load(std::memory_order_relaxed); }
    inline EigrpConfigs::TrafficShareMode getTrafficMode() { return configs.trafficShareMode.load(std::memory_order_relaxed); }
    inline std::unordered_set<IPAddress> getUnicastNeighbors(uint32_t key) { std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
        if (auto it = configs.unicastNeighbors.find(key); it != configs.unicastNeighbors.end()) return it->second; else return {}; }

    inline void setAutoSummary(bool autoSummary) { configs.autoSummarizationEnabled.store(autoSummary, std::memory_order_release); }
    inline void setVariance(uint8_t variance) { configs.variance.store(variance, std::memory_order_release); }

private:

    Eigrp& base;
    GlobalAggregator aggregator;
    EigrpConfigs::EigrpConfigs configs; ///< Configuration settings for EIGRP.
};
}

#endif // EIGRP_CONFIG_H
