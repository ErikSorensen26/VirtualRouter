/**
 * @file Global.h
 * @brief Central system controller owning all global router subsystems.
 */

/**
 * @defgroup CORE Core System
 * @brief Core router infrastructure: lifecycle management, scheduling, RIB/FIB, and global state.
 */

#ifndef GLOBAL_H
#define GLOBAL_H

#include <string>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <ThreadPool.hpp>
#include <TimeManager.h>
#include <Mac.hpp>

#include "interface/configs/InterfaceType.hpp"
#include "qos/egress/TxQueueManager.h"
#include "qos/ingress/RxQueueManager.h"
#include "cli/session/CliEngine.h"
#include "security/keys/KeyChainManager.h"
#include "ControlScheduler.h"
#include "AddressFamily.hpp"

#define DEFAULT_HOSTNAME "router"
#define DEFAULT_VRF ""

namespace config { struct GlobalRegistry; }

namespace interface { class Interface; }
namespace hardware { struct HwIfaceInfo; }
namespace cli { class CliEngine; }
namespace services::dhcp { class DhcpServer; class Dhcpv6Server; }

/**
 * @namespace core
 * @brief Top-level namespace for all VirtualRouter core infrastructure.
 *
 * Contains the primary ownership and lifecycle objects: @ref Global (system
 * controller), @ref VirtualRouter (per-VRF routing instance), @ref RoutingTable
 * (RIB/FIB), @ref ControlScheduler (task scheduling), @ref ThreadPool, and
 * @ref TimeManager.  Nothing in this namespace handles packets directly; it
 * provides the scaffolding on which protocol subsystems are built.
 */
namespace core
{

class VirtualRouter;

/**
 * @class Global
 * @brief Central control-plane and process-wide manager for all router subsystems.
 * @ingroup CORE
 *
 * The Global class acts as the top-level orchestration object for the entire virtual
 * router system. It owns and manages all global services that must be accessible from
 * any subsystem, including:
 *
 * - Global configuration state (AAA, IPv6 routing, NSF parameters, ARP/NDP settings)
 * - interface::Interface lifecycle management (creation, deletion, lookup)
 * - Virtual routing instances (VRFs)
 * - DHCPv4 / DHCPv6 server instances
 * - Global thread pool and timing subsystem
 * - CLI engine entry point
 * - Hardware TX/RX queue managers
 * - Authentication key-chain infrastructure
 *
 * ## Design Role
 * Global serves as the “system controller” of the router, analogous to the process-level
 * management layer in a real network OS (e.g., Cisco IOS-XE system controller, JunOS MGMTD).
 * It is never copied, never moved, and is intended to remain alive for the entire lifetime
 * of the application.
 *
 * All subsystems that require access to global state or cross-component coordination
 * interact with this class. This includes:
 *
 * - DHCP servers needing interface access
 * - Routing protocols resolving interfaces or VRFs
 * - CLI configuration commands altering system-wide behavior
 * - Hardware managers interacting with global thread pool resources
 * - ARP/NDP resolution layers referencing global neighbor limits and policy
 *
 * ## Concurrency Model
 * Global is heavily multi-threaded by design. Key aspects include:
 *
 * - Hostname protection via std::shared_mutex
 * - interface::Interface list protected by a dedicated std::mutex
 * - Routing instance map protected by a dedicated std::mutex
 * - Atomic values for fast-path configuration flags (IPv6, AAA, NSF)
 * - utils::ThreadPool and core::TimeManager used for protocol timers and async tasks
 *
 * Global does **not** attempt to serialize all operations.  
 * Instead, it exposes fine-grained locking for high-throughput data-plane and control-plane behavior.
 *
 * ## Responsibilities vs Non-Responsibilities
 * Global **does** own lifecycle and configuration control.
 * Global **does not** handle:
 * - Packet forwarding
 * - Protocol-specific logic (EIGRP, OSPF, BGP, DHCP, NDP)
 * - Per-interface packet IO
 *
 * Those live in VirtualRouter, interface::Interface, and protocol-specific subsystems.
 *
 * ## Lifetime
 * Global is constructed once at program start and destroyed only at shutdown.  
 * When destroyed, it is responsible for releasing all dynamically allocated:
 * - VirtualRouter instances
 * - interface::Interface objects
 * - DHCPv4 / DHCPv6 server instances
 *
 * Configuration and runtime state **must not outlive Global**.
 */
class Global 
{
public:
    /**
     * @brief Construct a Global system controller using standard OS-based startup files.
     *
     * This constructor initializes every global subsystem required for virtual router
     * operation:
     *
     * - Allocates and configures the global thread pool.
     * - Creates the core::TimeManager used by all periodic or deadline-based operations.
     * - Initializes the CLI engine, which may load startup configuration.
     * - Configures TX/RX queue managers according to CPU policy.
     * - Optionally enables routing subsystems before any interface is created.
     *
     * @param fs         File system object for file system access.
     * @param stfs       Structured set of startup files (.json startup, configs, etc.)
     * @param enableRouting If true, routing is enabled at boot (used for testing purposes)
     * @param test       Enables deterministic testing mode (no hardware initialization).
     *
     * In normal operation, this constructor triggers engine.initEngine(), which loads and
     * parses startup configuration files. In testing mode, initialization is deferred so
     * hardware and driver layers are not touched.
     *
     * @note The CLI engine *requires* access to Global during construction, therefore
     * this object passes a reference to itself into `CliEngine`.
     */
    Global(cli::FileSystem& fs, const cli::StartupFiles& stfs = {}, bool enableRouting = false, bool test = false);

    /**
     * @brief Destructor for the Global system controller.
     *
     * Ensures clean teardown of all global subsystems:
     * - DHCPv4 / DHCPv6 servers
     * - All routing instances (VRFs)
     * - All interfaces
     *
     * The destructor acquires all appropriate locks before destroying shared data structures.
     * Subsystems must not attempt to access Global during teardown.
     */
    ~Global();

    // Hostname Management

    /**
     * @brief Set the system hostname.
     *
     * This operation is thread-safe and affects all subsystems displaying or using the
     * global hostname (CLI prompt, syslog, routing protocol identity in certain modes).
     *
     * @param name New hostname. Must be a valid CLI identifier.
     */
    void setHostname(const std::string& name);

    /**
     * @brief Retrieve the current system hostname.
     *
     * Lock-free for readers via shared lock. Used by CLI engine, syslog-like output,
     * DHCP servers, and various debugging messages.
     *
     * @return std::string Copy of current hostname.
     */
    std::string getHostname();

    // GLOBAL FEATURE FLAGS

    /**
     * @brief Enable or disable IPv6 unicast routing globally.
     *
     * Equivalent to IOS command `ipv6 unicast-routing`.  
     * Routing protocols check this flag before installing IPv6 routes.
     *
     * @param enable True to enable, false to disable.
     */
    void setIPv6UnicastRouting(bool enable) { ipv6RoutingUnicast.store(enable, std::memory_order_relaxed); }

    /**
     * @brief Query whether IPv6 unicast routing is enabled.
     */
    bool isIPv6UnicastRouting() { return ipv6RoutingUnicast.load(std::memory_order_relaxed); }

    /**
     * @brief Enable or disable AAA processing globally.
     *
     * AAA controls how the CLI engine authenticates users and may affect management
     * protocols or system access control.
     */
    void setAAA(bool enable) { aaaEnabled.store(enable, std::memory_order_relaxed); }

    /**
     * @brief Determine if AAA is enabled.
     */
    bool isAAA() {return aaaEnabled.load(std::memory_order_relaxed); }

    // INTERFACE MANAGEMENT
    
    /**
     * @brief Reconciles the live interface list with the current configuration registry.
     *
     * Called after any change to the interface configuration table. Creates
     * interfaces that appear in the registry but not in the live list, and
     * removes interfaces present in the live list but absent from the registry.
     * Each new interface is initialized with hardware metadata and assigned to
     * the default VRF.
     */
    void interfaceRefresh();

    /**
     * @brief Create a new logical or physical interface and assign it to the default VRF.
     *
     * Interfaces represent IO endpoints (AF_PACKET, dummy, tunnel, VLAN interfaces, etc.)
     * and contain protocol stacks, ARP/NDP tables, hardware state, and configuration.
     *
     * @param interfaceKey   Type of interface (Ethernet, Loopback, Tunnel, etc.)
     * @param hwInfo         Low-level hardware metadata (ifindex, MAC, driver type)
     * @param debug          Enables verbose hardware-layer logging for this interface.
     *
     * @return Pointer to created interface::Interface on success, or nullptr if key already exists.
     *
     * @thread_safety Protected internally by interfaceMutex.
     */
    interface::Interface* addInterface(interface::InterfaceKey interfaceKey, const hardware::HwIfaceInfo& hwInfo, bool debug = false);

    /**
     * @brief Retrieve an interface by its computed key.
     *
     * @param key Internal interface lookup key.
     * @return Pointer to interface::Interface or nullptr if not found.
     *
     * @note This returns a raw pointer; ownership stays with Global.
     */
    interface::Interface* getInterface(interface::InterfaceKey key);

    /**
     * @brief Remove and destroy an interface.
     *
     * All protocol sessions on the interface should already be shut down externally.
     *
     * @param key Lookup key for the interface.
     * @return True if interface was removed, false if not found.
     */
    bool removeInterface(interface::InterfaceKey key);

    // ROUTING INSTANCES (VRFs)

    /**
     * @brief Reconciles the live VRF list with the current configuration registry.
     *
     * Called after any change to the VRF configuration table. Creates VRF
     * instances that appear in the registry but not in the live routing instance
     * map, and removes VRFs that are no longer configured. Newly created VRFs
     * are initialized with their configured address families.
     */
    void routingInstanceRefresh();

    /**
     * @brief Create a new routing instance (VRF).
     *
     * A routing instance contains:
     * - Its own RIB/FIB
     * - Per-VRF routing protocols
     * - Independent address family enablement
     *
     * @param name Name of the VRF (must be unique).
     * @return Pointer to new VirtualRouter or nullptr if already exists.
     */
    VirtualRouter* addRoutingInstance(const std::string& name = DEFAULT_VRF);

    /**
     * @brief Retrieve a routing instance by name and optionally by address family.
     *
     * @param name VRF name.
     * @param ad Address family (IPv4/IPv6). If NONE, any AF is accepted.
     * @return Pointer to VirtualRouter or nullptr if not found or AF not enabled.
     */
    VirtualRouter* getRoutingInstance(const std::string& name = DEFAULT_VRF, types::AddressFamily = types::AddressFamily::NONE);

    /**
     * @brief Remove a routing instance.
     *
     * The default routing instance cannot be deleted.
     *
     * @param name VRF name to delete.
     * @return True on successful removal, false on failure.
     */
    bool removeRoutingInstance(const std::string& name);

    core::ThreadPool threadPool;       ///< Global thread pool for off-loading.
    core::TimeManager timeManager;     ///< Global time manager for time keeping.
    ControlScheduler scheduler;  ///< Global control plane execution engine.

    // DHCP SERVERS
    
    services::dhcp::DhcpServer* dhcpServer = nullptr;     ///< Global IPv4 DHCP Server.
    services::dhcp::Dhcpv6Server* dhcpv6Server = nullptr; ///< Global IPv6 DHCP Server.

    // AUTHENTICATION

    security::authentication::KeyChainManager keyChainManager; ///< Manages key chains for protocols.

    // GLOBAL RESET

    /**
     * @brief Reset the entire global state to factory defaults.
     *
     * - Clears all interfaces
     * - Clears all routing instances except "default"
     * - Disables IPv6 routing
     * - Disables AAA
     * - Resets hostname to default
     *
     * @note All protocols and timers referencing removed interfaces or VRFs must be
     * prepared to handle invalidation after a reset.
     */
    void reset();

private:
    // INTERNAL STATE

    Global& operator=(const Global&) = delete;

    std::unique_ptr<config::GlobalRegistry> pConfigs; ///< Heap-allocated global config registry (decouples Global.h from GlobalRegistry.h).

    std::string hostname = DEFAULT_HOSTNAME;    ///< System hostname.
    std::shared_mutex hostnameMutex;            ///< Mutex protecting the hostname.

    std::atomic<bool> ipv6RoutingUnicast = false; ///< Global IPv6 routing flag.
    std::atomic<bool> aaaEnabled = false;         ///< Global AAA enable flag.

    // interface::Interface table
    std::mutex interfaceMutex; ///< Guards interfaceList for all CRUD operations.
    std::unordered_map<interface::InterfaceKey, interface::Interface> interfaceList; ///< All physical/logical interfaces. Owned by Global.

    // Routing Instances
    std::mutex routingInstanceMutex; ///< Guards routingInstances for all CRUD operations.
    std::unordered_map<std::string, VirtualRouter> routingInstances; ///< All VRF instances. Owned by Global.
    
public:
    // PUBLIC SYSTEM COMPONENTS

    bool routingEnabled = false; ///< Initial routing enable flag.
    bool testingMode = false;    ///< Testing mode flag.

    config::GlobalRegistry& getConfigs(); ///< Returns the global configuration registry.
    config::GlobalRegistry& configs;     ///< Alias for getConfigs() — kept for call-site compatibility.

    cli::CliEngine engine;            ///< Global CLI engine for user interface.

    qos::egress::TxQueueManager txMgr;        ///< Hardware TX queue controller.
    qos::ingress::RxQueueManager rxMgr;        ///< Hardware RX queue controller.

};

} // namespace core

#endif // GLOBAL_H

