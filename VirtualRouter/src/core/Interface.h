// Interface.h

#ifndef INTERFACE_H
#define INTERFACE_H

// Standard includes
#include <mutex>
#include <map>
#include <atomic>

#include <Ingress.h>
#include <Egress.hpp>
#include <InterfaceConfigs.h>
#include <PacketStructure.h>
#include <FIFOQueue.hpp>

#include <Ingress.h>

class EigrpTest; ///< Forward declaration of EigrpTest.
class VirtualRouter; ///< Forward declaration of VirtualRouter.
class MockInterface; ///< Forward declaration of MockInterface.
class PacketBuilder; ///< Forward declaration of PacketBuilder.
class TxDistributor; ///< Forward declaration of TxDistributor.
struct HwIfaceInfo; ///< Forward declaration of HwIfaceInfo.

enum class InterfaceType : uint8_t; ///< Forward declaration of InterfaceType.

namespace Eigrp
{
struct EigrpInterfaceInstance;  ///< Forward declaration of EigrpInterfaceInstance struct.
}

namespace Protocol 
{
class DhcpClient; ///< Forward declaration of DHCPv4 Client.
class Dhcpv6Client; ///< Forward declaration of DHCPv6 Client.
class Arp; ///< Forward declaration of ARP.
class Ndp; ///< Forward declaration of NDP.
}

namespace EigrpConfigs 
{
struct InterfaceConfigs; ///< Forward declaration of InterfaceConfigs.
}

/**
 * @enum StateChange
 * @brief Represents interface state changes.
 */
enum class StateChange
{
    SHUTDOWN, ///< Bring-up sequence (ARP/NDP/DHCP start)
    INITIATE, ///< Tear-down sequence (ARP/NDP/DHCP stop)
    IPCHANGE, ///< React to change in IPv4/IPv6 address
    IPREMOVAL ///< React to address deletion
};

/**
 * @struct InterfaceCreation
 * @brief Construction parameters for Interface objects.
 *
 * Contains initialization data that determines:
 * - Interface type (Ethernet, loopback, virtual, etc.)
 * - Interface ID and VRF assignment
 * - Low-level hardware metadata
 * - Whether debugging output is enabled
 *
 * This is a pure data structure; ownership remains with the caller.
 */
struct InterfaceCreation
{
    InterfaceType interfaceType;    ///< Type of interface.
    float interfaceId;              ///< ID of interface (user input).
    VirtualRouter& vrf;             ///< VRF that the interface will be initialized in.
    const HwIfaceInfo& info;        ///< Hardware information of the NIC.
    bool debug;                     ///< Debug mode for testing.
};

/**
 * @class Interface
 * @brief Represents a fully functional L2/L3 interface within a VirtualRouter (VRF).
 *
 * The Interface class integrates:
 * - Hardware bring-up/bring-down (via TxQueueManager, RxQueueManager, and HwManager)
 * - L2 neighbor discovery (ARP for IPv4, NDP for IPv6)
 * - IP configuration management for IPv4 and IPv6
 * - Packet ingress and egress pipelines
 * - Per-interface state machines (shutdown, carrier, IP change events)
 * - DHCP client behavior
 * - EIGRP per-interface configuration and refresh triggers
 *
 * ## Architectural Role
 * An Interface is the primary binding between:
 * - **Hardware NICs** (via TxDistributor / Rx queues)
 * - **VRF control plane** (routing protocols, timers, forwarding logic)
 * - **Neighbor discovery processes** (ARP/NDP)
 * - **Address assignment subsystems** (DHCPv4/DHCPv6)
 *
 * Each interface belongs to exactly one VRF at a time, but may be moved to
 * another VRF using `setVRF()`, which performs a full operational reset.
 *
 * ## Concurrency Model
 * The Interface is highly concurrent:
 * - ARP, NDP, DHCP clients may run their own threads or timers.
 * - Packet ingress and egress run on separate hardware queues.
 * - IPv6 address structures are protected by `configs.ipMutex`.
 * - Interface state transitions occur under atomic flags.
 *
 * **No global locks are taken inside the interface** except when notifying
 * routing protocols through the VRF (EIGRP refresh).
 *
 * ## Packet Processing Pipeline
 * Ingress path:
 * ```
 * NIC → RxQueue → processIngress() → inspect() → decapsulate() → processPacket()
 * ```
 *
 * Egress path:
 * ```
 * enqueuePacket() → encapsulate() → TxDistributor → NIC driver
 * ```
 *
 * ## State Machine
 * StateChange:
 * - INITIATE: Bring-up sequence (ARP/NDP/DHCP start)
 * - SHUTDOWN: Tear-down sequence (ARP/NDP/DHCP stop)
 * - IPCHANGE: React to change in IPv4/IPv6 address
 * - IPREMOVAL: React to address deletion
 *
 * These state transitions propagate into the control plane, especially EIGRP.
 *
 * ## Ownership
 * Interface owns:
 * - ARP/NDP protocol handler instances
 * - DHCP client instance
 * - EIGRP interface configs for each AF/AS
 *
 * Interface does **not** own:
 * - The VRF it belongs to
 * - PacketBuilder buffers (owned by the caller)
 * - Hardware queues (owned by TxQueueManager/RxQueueManager)
 */
class Interface
{
public:
    friend class ::MockInterface; ///< Test harness access for controlled interface testing.
    friend class ::EigrpTest; ///< Test harness access for controlled EIGRP testing.

    /**
     * @brief Construct a new Interface object.
     *
     * Performs initial hardware registration:
     * - Registers the interface with TxQueueManager and RxQueueManager
     * - Registers with the HwManager (allows link up/down control)
     * - Initializes configuration structures
     *
     * @param cfgs Construction parameters defining VRF, type, hardware metadata.
     *
     * The interface is created in a shutdowned state; caller must invoke
     * `startThreads()` to fully activate it.
     */
    Interface(const InterfaceCreation& cfgs);

    /**
     * @brief Destructor.
     *
     * Performs full cleanup of:
     * - All dynamic protocol handlers (ARP, NDP, DHCP)
     * - Hardware TX/RX queue registration
     * - VRF membership (interface removed from VRF)
     *
     * The interface is always shutdown before deletion.
     */
    virtual ~Interface();

    /**
     * @brief Cleanup helper invoked by destructor and VRF teardown.
     *
     * - Shuts down the interface (`shutdown(true)`)
     * - Removes interface from DHCPv6 (future work)
     * - Deletes DHCP client instance
     * - Removes interface from its VRF’s interface list
     *
     * Safe to call multiple times.
     */
    void cleanupInterface();

    /**
     * @brief Assign an IPv4 address to the interface.
     *
     * Performs:
     * - Address/mask assignment
     * - Gratuitous ARP broadcasts (two, per RFC behavior)
     * - EIGRP interface refresh events
     *
     * @param ip     IPv4 address in host byte order.
     * @param subnet Prefix length (0–32).
     */
    virtual void setIPv4(uint32_t ip, uint8_t subnet);

    /**
     * @brief Assign an IPv6 address to the interface.
     *
     * Performs:
     * - Address insertion into IPv6 address lists
     * - Duplicate Address Detection (NDP)
     * - VRF-level EIGRP IPv6 refresh
     *
     * @param ip        Pointer to 16-byte IPv6 address.
     * @param linkLocal True if creating a link-local address.
     * @param prefix    Prefix length (default 64).
     * @param eui64     Whether EUI-64 formatting should apply.
     */
    virtual void setIPv6(const uint8_t* ip, bool linkLocal = false, uint8_t subnet = 64, bool eui64 = false);

    // IP MANAGEMENT

    /**
     * @brief Remove the interface's IPv4 configuration.
     */
    void removeIPv4();

    /**
     * @brief Remove a specific IPv6 address or the link-local address.
     *
     * @param ip Optional IPv6 address; if null, removes the link-local.
     */
    void removeIPv6(const uint8_t* ip = nullptr);

    /**
     * @brief Remove all IPv6 addresses from this interface.
     */
    void removeAllIPv6();

    /**
     * @brief Retrieve all tentative IPv6 addresses currently undergoing DAD.
     *
     * Used by ND to determine which addresses require resolution.
     *
     * @return Vector of IPv6 addresses in tentative state.
     */
    std::vector<std::array<uint8_t, 16>> getTentativeAddress();

    /**
     * @brief Mark an IPv6 address as duplicate and remove it.
     *
     * Called after ND reports a conflict.
     *
     * @param address The duplicate IPv6 address.
     * @param linkLocal True if matching against link-local address.
     */
    void markAddressDuplicate(const uint8_t* address, bool linkLocal = false);

    // INTERFACE STATE

    /**
     * @brief Executes full administrative shutdown or bring-up of the interface.
     *
     * On shutdown:
     * - Stops ARP/NDP/DHCP
     * - Stops TX/RX hardware threads
     * - Triggers EIGRP interface removal events
     *
     * On bring-up:
     * - Restarts threads
     * - Re-initializes ARP/NDP
     * - Re-enables DHCP
     *
     * @param shut True = shutdown, False = enable.
     */
    virtual void shutdown(bool shut);

    /**
     * @brief Simulate physical carrier up/down events.
     *
     * Mirrors NIC link status and triggers shutdown() accordingly.
     *
     * @param carrier True if carrier is present.
     */
    void physicalShutdown(bool carrier);

    /**
     * @brief Enqueue a packet for transmission.
     *
     * @param packetInfo PacketBuilder containing L3/L4/L2 details.
     * @param mac Optional destination MAC to overwrite into Ethernet header.
     *
     * Steps:
     * - Encapsulate into full Ethernet frame
     * - Apply MAC overwrite if provided
     * - Push to TX queue
     *
     * No transmission occurs if thread subsystem is not running.
     */
    virtual void enqueuePacket(PacketBuilder& packetInfo, const uint8_t* mac = nullptr);

    // VRF MANAGEMENT

    /**
     * @brief Get the VRF the interface currently belongs to.
     *
     * @return VirtualRouter* Pointer to VRF (atomic load, lock-free).
     */
    VirtualRouter* getVRF();

    /**
     * @brief Reassign this interface to a new VRF.
     *
     * Performs:
     * - Shutdown of ARP/NDP/DHCP
     * - Removal of IP configuration
     * - Removal from existing VRF
     * - Assignment to new VRF
     * - Startup sequence for new VRF
     *
     * Used to simulate VRF-lite or multi-tenant environments.
     *
     * @param vrf Target VRF.
     * @return True if reassigned, false if VRF was unchanged.
     */
    bool setVRF(VirtualRouter* vrf);

    std::atomic<bool> shutdownFlag = true; ///< Administrative shutdown flag.
    std::atomic<bool> carrierFlag = true; ///< Physical carrier status flag.

    InterfaceConfigs configs; ///< IP addressing and protocol configuration.

    Protocol::Arp* arp = nullptr; ///< ARP module instance (ipv4).
    Protocol::Ndp* ndp = nullptr; ///< NDP module instance (ipv6).

    // EIGRP INTERFACES

    std::map<uint32_t, Eigrp::EigrpInterfaceInstance> eigrpInterfaceList; ///< EIGRP interface-level state.

    /**
     * @brief Retrieve or allocate EIGRP per-interface config block.
     *
     * @param as Autonomous System number.
     * @param af Address Family (IPv4 or IPv6).
     * @param negate If true, returns nullptr instead of allocating.
     * @return Pointer to EIGRP interface config block.
     */
    EigrpConfigs::InterfaceConfigs* getEigrpConfig(uint32_t as, AddressFamily af, bool negate);

    // DHCP CLIENT STATE

    Protocol::DhcpClient* dhcp = nullptr; ///< DHCPv4 client instance.
    //Protocol::Dhcpv6Client* dhcpv6 = nullptr; ///< DHCPv6 client instance.

    // RUNNING MANAGEMENT

    /**
     * @brief Brings donw ARP/NDP, DHCP, Tx/Rx queues, and unregisters hardware.
     *
     * Called during interface SHUTDOWN state or VRF reassignment.
     */
    void stopThreads();

    /**
     * @brief Bring up ARP/NDP, DHCP, Tx/Rx queues, and initialize hardware.
     *
     * Called during interface INITIATE state or VRF reassignment.
     */
    virtual void startThreads();

    TxDistributor* tx;      ///< Egress object for packet sending.

    // INGRESS

    /**
     * @brief Main ingress handler for packets arriving from NIC.
     *
     * Decodes packet into internal PacketInfo representation and forwards to L3/L4 stack.
     *
     * @param packet Raw packet bytes.
     * @param size   Total packet length.
     */
    void processIngress(uint8_t* packet, size_t size);

private:

    std::atomic<VirtualRouter*> routingInstance = nullptr; ///< VRF pointer (atomic for lock-free reads).

    /**
     * @brief Internal state machine transition for IPv4.
     */
    void stateChange(StateChange state);

    /**
     * @brief Internal state machine transition for IPv6.
     */
    void stateChangeV6(StateChange state);

    std::mutex ipInfoMutex; ///< Protects IPv4/IPv6 settings where atomics aren't used.

    bool debug; ///< Debug flag for verbose logging.

    std::atomic<bool> threadsRunning; ///< True when Rx/Tx threads and protocol modules are active.
};

#endif // INTERFACE_H
