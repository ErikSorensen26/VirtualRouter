/**
 * @file HardwareManager.h
 * @brief System-wide registry for physical network interfaces and carrier monitoring.
 */

#ifndef HARDWARE_MANAGER
#define HARDWARE_MANAGER

#include <vector>
#include <string>
#include <json.hpp>
#include <map>
#include <thread>
#include <atomic>
#include <Mac.hpp>
#include "interface/configs/InterfaceType.hpp"

namespace interface { class Interface; enum class InterfaceType : uint8_t; }
namespace cli { class FileSystem; }

namespace hardware
{

/**
 * @brief Low-level hardware metadata for a single physical or virtual NIC.
 *
 * Populated by @ref HardwareManager during interface discovery and stored
 * alongside each registered @ref interface::Interface. The fields reflect
 * what the kernel exposes via @c ioctl / @c SIOCGIFHWADDR.
 */
struct HwIfaceInfo
{
    uint32_t    index;     ///< Kernel interface index (from @c if_nametoindex).
    std::string ifname;    ///< Interface name (e.g. "eth0", "dummy0").
    types::Mac  mac;       ///< 48-bit MAC address packed into the low 6 bytes.
    uint64_t    bandwidth; ///< Link speed in bits per second (0 if unknown).
};

/**
 * @class HardwareManager
 * @brief System-wide registry for physical NICs: discovery, carrier monitoring, and interface lookup.
 * @ingroup HARDWARE
 *
 * `HardwareManager` reads a JSON hardware configuration file at startup to learn
 * which physical interfaces exist, then monitors carrier state changes using a
 * dedicated netlink socket thread. It maintains a mapping from kernel interface
 * indices to @ref HwIfaceInfo records and to the logical @ref interface::Interface
 * objects that have been registered against each NIC.
 *
 * ## Architectural Role
 * `HardwareManager` is the bridge between the OS network stack and the router's
 * logical interface layer. It does not own @ref interface::Interface objects —
 * those are owned by @ref core::Global. Instead it acts as a registry that lets
 * any subsystem look up hardware metadata by interface type and index.
 *
 * ## Lifecycle & Ownership
 * - Constructed by @ref core::Global as a default-initialised object; fully
 *   initialised by calling @ref addHardware() after the file system is ready.
 * - The netlink monitor thread is started inside @ref addHardware() and joined
 *   in the destructor.
 *
 * ## Concurrency Model
 * - @ref registeredInterfaces is accessed from both the netlink monitor thread
 *   (read during carrier events) and the control thread (writes during
 *   @ref registerInterface / @ref unregisterInterface); callers must synchronise
 *   externally or accept the implicit serialization provided by the control plane.
 * - @ref hwInfo and @ref physicalInterfaces are populated once during
 *   @ref addHardware() and treated as read-only thereafter.
 *
 * @see HwIfaceInfo, interface::Interface
 */
class HardwareManager
{
public:
    using StateCallback = std::function<void(bool carrier)>; ///< Carrier-state notification callback type.

    /**
     * @brief Constructs a HardwareManager in an empty, uninitialised state.
     *
     * Call @ref addHardware() before using any other methods.
     */
    HardwareManager() = default;

    /**
     * @brief Destroys the HardwareManager and stops the netlink monitor thread.
     *
     * Signals @c nlThread to exit and joins it. Any carrier events that arrive
     * after destruction are silently dropped.
     */
    ~HardwareManager();

    /**
     * @brief Loads hardware configuration and starts carrier monitoring.
     *
     * Parses @p hwConfigFile to build the initial interface inventory, optionally
     * creates dummy interfaces for testing, and launches the netlink monitor thread.
     *
     * @param hwConfigFile   Path to the JSON hardware configuration file.
     * @param fileSystem     File system abstraction used to read the config file.
     * @param enableDummies  If `true`, dummy interfaces listed in the config are created
     *                       via @c ip link add type dummy if they do not already exist.
     */
    void addHardware(const std::string& hwConfigFile, cli::FileSystem& fileSystem, bool enableDummies = false);

    /**
     * @brief Returns the kernel interface index for the given type and slot number.
     *
     * @param type   Logical interface type (Ethernet, Loopback, …).
     * @param index  Zero-based slot index within @p type's list.
     * @return Kernel @c ifindex, or `0` if not found.
     */
    uint32_t getInterface(interface::InterfaceType type, int index);

    /**
     * @brief Associates a logical interface with its hardware info record.
     *
     * Called by @ref core::Global when a new @ref interface::Interface is created.
     * Enables carrier-event callbacks to reach the interface when the link changes.
     *
     * @param info   Hardware metadata record (must remain valid).
     * @param iface  Logical interface to notify.
     */
    void registerInterface(const HwIfaceInfo* info, interface::Interface* iface);

    /**
     * @brief Removes the association between a logical interface and its hardware record.
     *
     * Called when an interface is destroyed. After this call no carrier events for
     * @p info will reach @p iface.
     *
     * @param info   Hardware metadata record used as the lookup key.
     * @param iface  Logical interface to deregister.
     */
    void unregisterInterface(const HwIfaceInfo* info, interface::Interface* iface);

    /**
     * @brief Returns the @ref HwIfaceInfo for the given kernel interface index.
     *
     * @param index  Kernel @c ifindex.
     * @return Pointer to the record, or @c nullptr if unknown.
     */
    const HwIfaceInfo* getHwInfo(uint32_t index) const;

    /** @brief Returns the full map of all physical interfaces grouped by type. */
    const std::unordered_map<interface::InterfaceType, std::vector<uint32_t>>& getPhysicalInterfaces() { return physicalInterfaces; }

    /**
     * @brief Returns all kernel interface indices for a given interface type.
     * @param type  Interface type to query.
     */
    const std::vector<uint32_t>& getPhysicalInterfaces(interface::InterfaceType type) { return physicalInterfaces[type]; }

    /**
     * @brief Extracts hardware metadata from an open socket and a populated @c ifreq.
     *
     * Used during initial interface discovery to query the kernel for MAC address
     * and link speed via @c ioctl.
     *
     * @param sock  Open AF_INET socket for @c ioctl calls.
     * @param ifr   @c ifreq pre-populated with the interface name.
     * @return Populated @ref HwIfaceInfo on success; @c std::nullopt on failure.
     */
    std::optional<HwIfaceInfo> extractHwInfo(int sock, struct ifreq& ifr);

    /**
     * @brief Brings a network interface up (sets @c IFF_UP).
     *
     * @param ifname  Interface name.
     * @return `true` on success, `false` on @c ioctl failure.
     */
    bool bringUp(const std::string& ifname);

    /**
     * @brief Takes a network interface down (clears @c IFF_UP).
     *
     * @param ifname  Interface name.
     * @return `true` on success, `false` on @c ioctl failure.
     */
    bool bringDown(const std::string& ifname);

private:
    /**
     * @brief Worker loop that reads netlink carrier-state events and invokes @ref onLinkEvent().
     *
     * Runs on @c nlThread until @c nlThreadRunning is set to @c false.
     */
    void netlinkMonitorThread();

    /**
     * @brief Dispatches a carrier-state change to all interfaces registered on @p index.
     *
     * @param index      Kernel @c ifindex that changed state.
     * @param carrierUp  @c true if the link is now up; @c false if it went down.
     */
    void onLinkEvent(uint32_t index, bool carrierUp);

    /**
     * @brief Verifies that @p ifname exists in the kernel; creates it as a dummy if allowed.
     *
     * @param ifname  Interface name to check or create.
     * @return `true` if the interface exists or was successfully created.
     */
    bool ensureInterface(const char* ifname);

    /**
     * @brief Creates a kernel dummy interface with the given name.
     *
     * @param ifname  Name for the new dummy interface.
     * @return `true` on success.
     */
    bool createDummy(const char* ifname);

    std::unordered_map<uint32_t, std::vector<interface::Interface*>> registeredInterfaces;  ///< ifindex → registered logical interfaces.
    std::unordered_map<interface::InterfaceType, std::vector<uint32_t>> physicalInterfaces; ///< Type → list of kernel ifindices.
    std::unordered_map<uint32_t, HwIfaceInfo> hwInfo;                                       ///< ifindex → hardware metadata.

    nlohmann::ordered_json configJson; ///< Parsed hardware configuration JSON.
    bool allowDummies;                 ///< Whether dummy interface creation is permitted.

    int nlSock = -1;                          ///< Netlink socket file descriptor for RTMGRP_LINK.
    std::thread nlThread;                     ///< Thread running @ref netlinkMonitorThread().
    std::atomic<bool> nlThreadRunning{false}; ///< Controls the netlink monitor loop.
};

} // namespace hardware

#endif // HARDWARE_MANAGER

