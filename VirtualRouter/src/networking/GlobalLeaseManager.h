// GlobalLeaseManager.h

#ifndef GLOBAL_LEASE_MANAGER_H
#define GLOBAL_LEASE_MANAGER_H

#include <ByteString.hpp>
#include <LeaseManager.h>
#include <optional>

/**
 * @brief Structure representing a global lease record.
 *
 * Contains the network identifier and the lease details.
 */
struct GlobalLeaseRecord
{
    ByteString networkID;         ///< The network this lease belongs to.
    LeaseManager::Lease lease;  ///< The lease information.
};

/**
 * @brief GlobalLeaseManager aggregates multiple per-network LeaseManager instances.
 *
 * This class maintains a global lookup table for leases keyed by client MAC addresses.
 * It provides function to add/remove per-network LeaseManager instances and to update or query the global lease records.
 */
class GlobalLeaseManager
{
public:
    /**
     * @brief Registers a per-network LeaseManager.
     *
     * @param networkID The network identifier (<network>/<mask>).
     * @param lm Pointer to the LeaseManager instande for that network.
     */
    void addLeaseManager(const ByteString& networkID, LeaseManager* lm);

    /**
     * @brief Removes the LeaseManager for a given network.
     *
     * @param networkID The network Identifier (<network>/<mask>).
     */
    void removeLeaseManager(const ByteString& network);

    /**
     * @brief Updates the global lease record for a given client MAC.
     *
     * Should be called when a per-network LeaseManager allocates or updates a lease.
     *
     * @param mac The client MAC address.
     * @param networkID The network identifier for the lease.
     * @param lease The lease information.
     */
    void updateLeaseRecord(const ByteString& mac, const ByteString& networkID, const LeaseManager::Lease& lease);

    /**
     * @brief Removes the global lease record for a given client MAC.
     * 
     * Should be called when a lease is released.
     *
     * @param mac The client MAC address.
     */
    void removeLeaseRecord(const ByteString& mac);

    /**
     * @breif Looks up a global lease recors by client MAC.
     *
     * @param mac The client MAC address.
     * @return An optionsl GlobalLeaseRecord if found, or std::nullopt if not.
     */
    std::optional<GlobalLeaseRecord> findLease(const ByteString& mac) const;

    /**
     * @brief Moves the key on a LeaseManager.
     *
     * @param oldKey Old networkID.
     * @param newKey New networkID.
     * @return True if success, otherwise false.
     */
    bool moveManager(const ByteString& oldKey, const ByteString& newKey);

private:
    mutable std::mutex mtx; ///< Mutex for protecting interal data.

    /// Map from networkID to the coorsponding per-network LeaseManager pointer.
    std::unordered_map<ByteString, LeaseManager*> leaseManagers;

    /// Global lookup table: maps client MAC addresses to their aggregated lease record.
    std::unordered_map<ByteString, GlobalLeaseRecord> globalLookup;
};

#endif // GLOBAL_LEASE_MANAGER_H
