// IPPool.h

#ifndef IP_POOL_H
#define IP_POOL_H

#include <ByteString.hpp>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <set>

// Forward declarations
class IPPoolTest;
class DhcpServerTest;
class LeaseManager;

/**
 * @brief Represents a dynamic sized IP pool for DHCP-managed networks.
 */
class IPPool
{
    ByteString baseAddress;         ///< First usable address (network + 1).
    ByteString lastAddress;         ///< Last usable address (network + pool size).
    __uint128_t poolSize;           ///< Number of usable IPs in the subnet.
    ByteString currentAddress;      ///< Pointer to the next IP for dynamic allocations.
    ByteString gateway;             ///< Current gateway to exclude.

    std::unordered_map<ByteString, ByteString> allocatedIPs; ///< Tracks dynamically allocated IPs to MAC addresses.
    std::unordered_set<ByteString> excludedAddresses; ///< Maps excluded IPs.
    std::unordered_map<ByteString, ByteString> temporaryOffers; ///< Maps ID to temporary offers.
    std::set<ByteString> releasedIPs; ///< Set for releasedIPs.
    mutable std::mutex poolMutex; ///< Mutex for thread synchronization.
public:
    friend class ::IPPoolTest;
    friend class ::DhcpServerTest;

    /**
     * @brief Default constructor with no parameters.
     */
    IPPool() {}

    /**
     * @brief Constructs an IPPool for a given network and subnet mask.
     *
     * @param network The network address as a ByteString.
     * @param subnetMask The subnet maske as a bytestring.
     */
    IPPool(const ByteString& network, const uint8_t& subnetPrefix, const ByteString& gateway);

    /**
     * @brief Adds the LeaseManager
     */
    void addLeaseManager(LeaseManager* lease);

    /**
     * @brief Allocates the next available IP from the pool.
     *
     * @param macAddress The MAC address of the client.
     * @return The allocated UP address as a ByteString, or an empty ByteString if none are available.
     */
    ByteString allocateIP(const ByteString& macAddress);

    /**
     * @brief Temporarily allocates an IP address.
     *
     * @param id Tracking id for temporary address.
     * @return temporarily allocated IP.
     */
    ByteString allocateTempIP(const ByteString& id);

    /**
     * @brief Excludes an IP for a specific MAC address.
     *
     * @param ip The IP address to reserve.
     * @return True if the reservation was successful, otherwise false.
     */
    bool excludeIP(const ByteString& ip);

    /**
     * @brief Reeleases an IP back to the pool.
     *
     * @param ip The IP address to release.
     * @return ip itorator.
     */
    void releaseIP(const ByteString& ip);

    /**
     * @brief Removes an existing excluded IP.
     *
     * @param ip The IP address to unbind.
     * @return True if the reservation was removed, otherwise false.
     */
    bool removeExclusion(const ByteString& ip);

    /**
     * @brief clears the mac address so it will not be allocated to any clients until the offer timeout expires
     * 
     * @param ip The IP address that is conflicted.
     * @return True if the ip exists in the pool and was set as conflicted, otherwise false.
     */
    bool setConflicted(const ByteString& ip);

    /**
     * @brief Checks if an IP is currently allocated.
     *
     * @param ip The IP address to check.
     * @return True if the IP is allocated, otherwise false.
     */
    bool isAllocated(const ByteString& ip) const;

    /**
     * @brief Checks if an IP is currently reserved.
     *
     * @param ip The IP address to check.
     * @return True if the IP is reserved, otherwise false.
     */
    bool isExcluded(const ByteString& ip) const;

    /**
     * @brief Checks if an IP is currently allocated or reserved.
     *
     * @param ip The IP address to check.
     * @return True if the IP is allocated or reserved, otherwise false.
     */
    bool isAllocatedOrExcluded(const ByteString& ip) const;

    /**
     * @brief Searches for a mac address against a IP address in the reserved addresses.
     * 
     * @param ip The IP address to search for
     * @param mac The MAC address looked for in the allocated ip
     */
    bool matchMacToIP(const ByteString ip, const ByteString& mac);

    /**
     * @brief Adjusts the pool to a new network or subnet mask configuration.
     *
     * @param network TThe new network address as a ByteString.
     * @param subnetPrefix The new subnet prefix as uint32_t.
     * @param gatway The default gateway of the dhcp pool.
     */
    void adjustPool(const ByteString& network, const uint8_t subnetPrefix, const ByteString& gateway);

    /**
     * @brief Checks if temporary IP is offered.
     *
     * @param ip Temporary IP address.
     * @return True of the temporary IP is already offered.
     */
    bool isTemporarilyOffered(const ByteString& ip);

    /**
     * @brief Gets the temp ip that was reserved.
     *
     * @param id Client ID that goes to the temp IP.
     * @return The requested IP address.
     */
    ByteString getTempIP(const ByteString& id);

    /**
     * @brief Activates a temporary ip being held.
     *
     * @param id The clients identification that is tied to the temporary address.
     * @return The newly leased IP address.
     */
    ByteString activateTempIP(const ByteString& id);

    /**
     * @brief Clears Temporary IPs
     *
     * @param ID Id attached to temporary IPs.
     */
    void clearTempOffer(const ByteString& id);

private:

    LeaseManager* leaseManager = nullptr; ///< Pointer to the lease manager if this pool is managed by one.
};

#endif // IP_POOL_H
