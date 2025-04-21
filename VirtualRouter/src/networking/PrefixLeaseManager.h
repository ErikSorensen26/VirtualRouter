// PrefixLeaseManager.h

#ifndef PREFIX_LEASE_MANAGER_H
#define PREFIX_LEASE_MANAGER_H

#include "PrefixPool.h"
#include <map>
#include <mutex>

class Dhcpv6ServerTest;

/**
 * @brief Tracks and manages prefix leases with time-based expiration.
 */
class PrefixLeaseManager
{
public:
    friend class ::Dhcpv6ServerTest;

    /**
     * @struct PrefixLease
     *
     * Holds information on a prefix lease.
     */
    struct PrefixLease
    {
        ByteString duid;
        double leaseStart;
        double leaseDuration;
        uint8_t length;
        uint32_t T1; ///< Renewal time (e.g. 50% of leaseDuration in seconds)
        uint32_t T2; ///< Rebind time (e.g. 80% of leaseDuration in seconds)
    };

    /**
     * @brief Construct a new Prefix Lease Manager.
     * @param pool The prefix pool to manage
     */
    explicit PrefixLeaseManager(PrefixPool* pool);

    /**
     * @brief Allocate a prefix with lease tracking.
     *
     * @param duid The client identifier
     * @param requestedLength The desired prefix length (e.g., /64)
     * @param leaseTime Time in seconds before expiration
     * @param t1Percent The percent of the leaseTime for t1.
     * @param t2Percent The percent of the leaseTime for t2.
     * @param usePrefix Indicates if the prefix should be used in the client ID.
     * @return The allocated prefix and length
     */
    std::pair<ByteString, uint8_t> allocatePrefix(const ByteString& duid, uint8_t requestedLength, double leaseTime, double t1Percent, double t2Percent, bool usePrefix = false);

    /**
     * @brief Allocate a requested prefix with lease tracking.
     *
     * @param duid The clients DUID that is requesting the prefix.
     * @param prefix The requested Prefix address from the client.
     * @param requestedLength The requested Prefix length.
     * @param leaseTime the lease time for this prefix.
     * @param t1Percent The percent of the leaseTime for t1.
     * @param t2Percent The percent of the leaseTime for t2.
     * @return True if the prefix was available and allocated, otherwise False.
     */
    bool allocateRequestedPrefix(const ByteString& duid, const ByteString& prefix, uint8_t requestedLength, double leaseTime, double t1Percent, double t2Percent);
    
    /**
     * @brief Allocates a prefix from a already offered lease.
     *
     * @param duid The clients DUID that is requesting the prefix.
     * @param leaseTime The amount of time the prefix lease will last.
     * @param t1Percent The percent of the leaseTime for t1.
     * @param t2Percent The percent of the leaseTime for t2.
     * @return The newly leased prefix.
     */
    std::pair<ByteString, uint8_t> activateLeaseFromTemp(const ByteString& duid, double leaseTime, double t1Percent, double t2Percent);

    /**
     * @brief Release a prefix manually.
     * @param prefix The prefix to release
     */
    void releasePrefix(const ByteString& prefix);

    /**
     * @brief Renew a prefix lease.
     * @param prefix The prefix to renew
     * @return true if successful
     */
    bool renewPrefix(const ByteString& prefix);

    /**
     * @brief Checks whether a prefix is allocated.
     * @param prefix The prefix to release.
     * @return True if the prefix is already allocated, otherwise false.
     */
    bool isAllocated(const ByteString& prefix);

    /**
     * @brief Cleanup expired leases.
     */
    void cleanupExpiredLeases();

    /**
     * @brief Returns a map of currently active leases.
     */
    std::map<ByteString, PrefixLease> getActiveLeases() const;

    /**
     * @return The requested lease as a pointer
     */
    PrefixLease* getLease(const ByteString& duid) {std::lock_guard<std::mutex> lock(leaseMutex); return &leases[duid];}

private:

    PrefixPool* pool;
    mutable std::mutex leaseMutex;
    std::map<ByteString, PrefixLease> leases;
};

#endif // PREFIX_LEASE_MANAGER_H
