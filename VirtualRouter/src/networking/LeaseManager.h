// DhcpLease.h

#ifndef DHCP_LEASE_H
#define DHCP_LEASE_H

#include <ByteString.hpp>
#include <IPPool.h>
#include <unordered_map>
#include <mutex>

class GlobalLeaseManager;
class DhcpServerTest;
class Dhcpv6ServerTest;

class LeaseManager
{
public:
    friend class DhcpServerTest;
    friend class Dhcpv6ServerTest;

    /**
     * @brief Represents an ip lease.
     */
    struct Lease
    {
        ByteString ipAddress;  ///< The IP address assigned to the client.
        ByteString clientID; ///< The Identifier address of the client requesting the lease.
        double leaseStart;      ///< The start time of the lease in seconds since the Unix epoch.
        double leaseDuration;   ///< The duration of the lease in seconds.
        uint32_t T1; ///< Renewal time (e.g. 50% of leaseDuration in seconds)
        uint32_t T2; ///< Rebind time (e.g. 80% of leaseDuration in seconds)
    };

    /*
     * @brief Constructs a LeaseManager with an associated IPPool.
     *
     * @param pool Pointer to the IPPool managing available addresses.
     */
    LeaseManager(IPPool* pool);

    /*
     * @brief Adds a Global lease manager to manage the leased subnet.
     *
     * @param networkID networkID of the subnet lease pool.
     * @param globalLm GlobalLeaseManager to add lease manager to.
     */
     void addGlobalManager(ByteString& networkID, GlobalLeaseManager* globalLm);

    /**
     * @brief Allocates an IP address from the specified network for a client based on its MAC address.
     *
     * Ensure that the allocated UP is not already in use.
     *
     * @param leaseTime The time the lease will last.
     * @param t1Percent The percent of the leaseTime for t1.
     * @param t2Percent The percent of the leaseTime for t2.
     * @param macAddress The MAC address of the client.
     * @param useIP Indicates whether the IP should be used in the Client ID.
     * @return The allocated IP address, or an empty string if no address is available.
     */
    ByteString allocateIP(double leaseTime, double t1Percent, double t2Percent, const ByteString* macAddress, bool useIP = false);

    /**
     * @brief Allocates a requested IP address from the specified network for a client based on its mac address
     *
     * @param leaseTime The time the lease will last.
     * @param t1Percent The percent of the leaseTime for t1.
     * @param t2Percent The percent of the leaseTime for t2.
     * @param macAddress The MAC address of the client.
     * @return True if the address was allocated, otherwise false.
     */
    bool allocateRequestedIP(const ByteString& requestedIP, double leaseTime, double t1Percent, double t2Percent, const ByteString* macAddress);

    /**
     * @brief Releases an IP address back to the pool.
     *
     * @param ipAddress The IP address to release.
     */
    void releaseIP(const ByteString& ipAddress);

    /**
     * @brief Checks if an IP is currently allocated.
     *
     * @param ipAddress The IP address to check.
     * @return True if allocated, false otherwise.
     */
    bool isAllocated(const ByteString& ipAddress);

    /**
     * @brief Extends the lease value of an IP
     *
     * @param IP address to renew.
     */
    bool renewLease(const ByteString& ipAddress);

    /**
     * @brief Activates a lease for a temporary address in the pool.
     *
     * @param id The ID that is holding the temporary address.
     * @param leaseTime The amount of time the lease will last.
     * @param t1Percent The percent of the leaseTime for t1.
     * @param t2Percent The percent of the leaseTime for t2.
     * @return ByteString The leased IP.
     */
    ByteString activateLeaseFromTemp(const ByteString& ip, double leaseTime, double t1Percent, double t2Percent, const ByteString* id);

    /**
     * @brief Returns a reference to the active list of active leases
     */
    const std::unordered_map<ByteString, Lease>& getActiveLeases() const;

    /**
     * @brief Cleans up expired leases based on their duration.
     */
    void cleanupExpiredLeases();

private:
    IPPool* ipPool; ///< Pointer to the associated IPPool.
    //LeasePool* leasePool; ///< Pointer to the associated LeasePool
    GlobalLeaseManager* globalLeaseManager = nullptr;

    std::unordered_map<ByteString, Lease> leases; ///< Mapping of IPs to leases.

    std::mutex leaseMutex; ///< Mutex for synchronizing lease management.
    ByteString networkID; ///< LeaseManager identifier.
};

#endif // DHCP_LEASE_H
