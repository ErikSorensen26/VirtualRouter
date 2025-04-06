// PrefixPool.h

#ifndef PREFIX_POOL_H
#define PREFIX_POOL_H

#include <ByteString.hpp>
#include <mutex>
#include <map>
#include <set>
#include <vector>

class PrefixLeaseManager;
class Dhcpv6ServerTest;

/**
 * @brief Manages a pool of IPv6 prefixes for delegation.
 */
class PrefixPool
{
    ByteString basePrefix; ///< Base prefix on the pool.
                           ///
    uint8_t baseLength; ///< Base prefix length on the pool.
    mutable std::mutex mutex; ///< Mutex for synchronization.
    std::map<ByteString, std::pair<ByteString, uint8_t>> allocated; ///< Maps the ID to the Prefix of the client.
    std::map<ByteString, std::pair<ByteString, uint8_t>> temporaryOffers; ///< Maps ID to temporary offers.
    std::set<ByteString> excluded;
    std::map<uint8_t, __uint128_t> lastIndexUsed;
    PrefixLeaseManager* leaseManager;

    ByteString generatePrefix(__uint128_t index, uint8_t length) const;
    bool prefixMatches(const ByteString& a, const ByteString& b, uint8_t length) const;
public:
    friend class ::Dhcpv6ServerTest;
    /**
     * @brief Construct a new Prefix Pool object.
     * @param basePrefix The base prefix of the pool (e.g. /48 block)
     * @param baseLength The prefix length of the base pool
     */
    PrefixPool(const ByteString& basePrefix, uint8_t baseLength);

    /**
     * @brief Adds the LeaseManager
     */
    void addLeaseManager(PrefixLeaseManager* lease);

    /**
     * @brief Allocate a prefix for a given DUID.
     * @param duid The client's DUID
     * @param requestedLength The requested prefix length (e.g., /64)
     * @param usePrefix Indicates if the prefix should be included in the client ID.
     * @return A pair of (allocated prefix, length). Empty if unavailable.
     */
    std::pair<ByteString, uint8_t> allocatePrefix(const ByteString& duid, uint8_t requestedLength, bool usePrefix = false);

    /**
     * @brief Allocate a specific prefix.
     * @param prefix The exact prefix to assign
     * @param length The length of the prefix
     * @param duid The client’s DUID
     * @return true if successful
     */
    bool allocateSpecificPrefix(const ByteString& prefix, uint8_t length, const ByteString& duid);

    /**
     * @brief Allocate a temporary prefix for a given DUID.
     *
     * @param prefix the specified prefix for the lease.
     * @param requestedLength The requested prefix length
     * @param duid The client's DUID
     * @param usePrefix Indicates if the prefix should be included in the client ID.
     * @return The temporary prefix.
     */
    std::pair<ByteString, uint8_t> allocateTempPrefix(const ByteString& duid, uint8_t requestedLength, bool usePrefix = false);

    /**
     * @brief Exclude a prefix from being assigned.
     * @param duid The prefix to exclude
     * @return True if 
     */
    bool excludePrefix(const ByteString& prefix);

    /**
     * @brief Release a prefix back to the pool.
     * @param prefix The prefix to release
     */
    void releasePrefix(const ByteString& prefix);

    /**
     * @brief Removes an existing excluded Prefix.
     *
     * @param ip The Prefix to unbind.
     * @return True if the reservation was removed, otherwise false.
     */
    bool removeExclusion(const ByteString& prefix);

    /**
     * @brief Checks if an Prefix is currently allocated.
     *
     * @param ip The Prefix address to check.
     * @return True if the Prefix is allocated, otherwise false.
     */
    bool isAllocated(const ByteString& prefix) const;

    /**
     * @brief Checks if an Prefix is currently reserved.
     *
     * @param ip The Prefix address to check.
     * @return True if the Prefix is reserved, otherwise false.
     */
    bool isExcluded(const ByteString& prefix) const;

    /**
     * @brief Check if a prefix is allocated or excluded.
     * @param prefix The prefix to check
     * @return true if already allocated or excluded
     */
    bool isAllocatedOrExcluded(const ByteString& prefix) const;

    /**
     * @brief Get all current prefix allocations.
     */
    std::map<ByteString, std::pair<ByteString, uint8_t>> getAllocations() const;

    /**
     * @brief Checks if temporary IP is offered.
     *
     * @param prefix Temporary prefix.
     * @param prefixLen Length of the temporary prefix.
     * @return True of the temporary IP is already offered.
     */
    bool isTemporarilyOffered(const ByteString& prefix);

    /**
     * @brief Activates an already offered prefix.
     *
     * @param duid The client's DUID
     * @return the newly leased prefix.
     */
    std::pair<ByteString, uint8_t> acivateOfferedPrefix(const ByteString& duid);

    /**
     * @brief Returns the prefix length for a givven allocated prefix.
     */
    uint8_t getPrefixLength(const ByteString& prefix) const;

    /**
     * @brief Returns the list of available prefixes.
     */
    std::vector<std::pair<ByteString, uint8_t>> getAvailablePrefixes(uint8_t requestedLength) const;

    /**
     * @brief Returns the temporary prefix associated with the id given.
     *
     * @param id The clients DUID.
     */
    std::pair<ByteString, uint8_t> getTempPrefix(const ByteString& id);

    /**
     * @brief clears out a specific temporary prefix.
     *
     * @param id The clients DUID.
     */
    void clearTempPrefix(const ByteString& id);

    bool allExcluded = false; ///< Boolean excluding all addresses, used for testing.
};

#endif //PREFIX_POOL_H
