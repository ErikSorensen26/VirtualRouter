// EigrpConfig.h

#ifndef EIGRP_CONFIG_H
#define EIGRP_CONFIG_H

#include <cstdint>
#include <cstddef>
#include <EigrpTypes.hpp>

struct EigrpHeader;
class EigrpInterface;

namespace Protocol
{
class Eigrp;

class EigrpConfig
{
public:

    EigrpConfig(Eigrp& base)
        : base(base) {}
    /**
     * @brief Adds a network to the EIGRP configuration.
     *
     * Registers a new network with EIGRP, allowing the protocol to advertise and
     * route traffic through the specified network.
     *
     * @param newNetwork Network configuration to add.
     */
    void addNetworkRange(const EigrpConfigs::Network& newNetwork);

    /**
     * @brief Adds common TLVs to an EIGRP header
     *
     * Adds common TLVs to an EIGRP header such as stub, version, sequence,
     * auth, parameter.
     *
     * @param hdr Header that TLVs are heing added to.
     * @param cfg EigrpInterface object with needed interface configs.
     * @param isUpdate Indicates if this is for a UPDATE header.
     * @param isAck Indicates if this is for a ACK header
     * @param neighborIp IP of the neighbor that this packet is being sent to.
     * @param sequence Sequence number of the packet being sent.
     */
    size_t addCommonTlvs(EigrpHeader& hdr, EigrpInterface& cfg, bool isUpdate, bool isAck, const uint8_t* neighborIp, uint32_t sequence);

    /**
     * @brief Redistributes a route from another protocol into EIGRP.
     *
     * Injects routes learned from external routing protocols (e.g., OSPF, BGP) into
     * the EIGRP routing table, allowing for route redistribution and integration of
     * diverse routing information.
     *
     * @param destination Destination network.
     * @param mask Subnet mask of the destination.
     * @param protocol Protocol identifier.
     */
    void redistributeRoute(const uint8_t* destination, uint8_t mask, const uint16_t protocol);

    /**
     * @brief Enables or disables auto-summarization.
     *
     * Toggles the auto-summarization feature, allowing EIGRP to automatically summarize
     * routes at major network boundaries, simplifying routing tables and reducing
     * routing protocol overhead.
     *
     * @param enable True to enable, false to disable.
     */
    void enableAutoSummary(bool enable);

    /**
     * @brief Recomputes auto summaries when a new routes is learned
     *
     * Takes all existing auto summarized routes and recalculates the summarized routes
     */
    void recomputeAutoSummaries();

    /**
     * @brief Sets the EIGRP process as a stub.
     *
     * Configures the EIGRP process to operate in stub mode, limiting the types of routes
     * advertised to reduce routing protocol complexity and overhead, especially in hub-and-spoke
     * network topologies.
     * 
     * @param isStub True to set as stub, false otherwise.
     * @param advertiseConnected Advertise connected routes.
     * @param advertiseLeakMap Advertise leak-map routes.
     * @param advertiseStatic Advertise static routes.
     * @param advertiseSummary Advertise summary routes.
     * @param advertiseRedistributed Advertise redistributed routes.
     */
    void enableStub(bool isStub, bool advertiseConnected = true, bool advertiseLeakMap = true, bool advertiseStatic = true, bool advertiseSummary = true, bool advertiseRedistributed = true);

    /**
     * @brief Checks if the EIGRP process is configured as a stub.
     *
     * Indicates whether the EIGRP process is operating in stub mode, affecting route
     * advertisements and protocol behavior accordingly.
     *
     * @return True if stub is enabled, false otherwise.
     */
    bool stubEnabled() const { return configs.stubConfig.isStub; }

    /**
     * @brief Adds or removes a passive interface.
     *
     * Will store the interface in a list as passive and if the interface
     * is active, it will set the interface as passive.
     *
     * @param type Type of interface (e.g. GigabitEthernet, Ethernet)
     * @param id ID of the passive interface.
     * @param add optional boolean that tells whether to add or remove the passive interface.
     */
    void setPassiveInterface(uint32_t key, bool add = true);

public:
    uint8_t getVariance() { return configs.variance.load(std::memory_order_relaxed); }
    uint8_t getAD() { return configs.adminDistance.load(std::memory_order_relaxed); }
    uint8_t getExternalAD() { return configs.externalAdminDistance.load(std::memory_order_relaxed); }
    uint32_t getLowestBandwidth() { return configs.lowestBandwidth.load(std::memory_order_relaxed); }

private:

    EigrpConfigs::EigrpConfigs configs; ///< Configuration settings for EIGRP.
    std::shared_mutex eigrpDataMutex; ///< Mutex for synchronizing access to EIGRP data structures.
    Eigrp& base;
};
}

#endif // EIGRP_CONFIG_H
