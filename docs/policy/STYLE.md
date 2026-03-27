# VirtualRouter — Comment Style Guide

This document defines the comment schema used across the codebase.
Comments describe *how* something works and *why* it was designed that way.
They do not simply restate what the code obviously does.

---

## Header Files (`.h` / `.hpp`)

### File Banner

Every header gets a `@file` block. `@brief` is one sentence.

```cpp
/**
 * @file EgressBase.h
 * @brief Abstract base for all hardware egress (TX) backends.
 */
```

Source files (`.cpp`) do not get `@file` blocks — Doxygen picks up all
documentation from the headers.

---

### Subsystem Groups

#### What qualifies as a group

Every directory under `src/` that contains at least one header becomes a
group. The group hierarchy mirrors the directory hierarchy exactly — a
directory's group is always a subgroup of its parent directory's group.

A namespace does **not** qualify as a group if it is:
- A single file used only for namespace encapsulation
- A collection of static free functions or utilities with no shared state
- A pure type or header collection (no behaviour, no ownership)

#### Definition and nesting

Define each group exactly once, in the primary header for that subsystem.
All other files in the subsystem reference it with `@ingroup`. Groups are
nested by placing `@ingroup <Parent>` inside a child `@defgroup`:

```cpp
// Top-level group — defined in the primary header for the subsystem.
/**
 * @defgroup Routing Routing Protocols
 * @brief All routing protocol implementations.
 */

// Subgroup — nested inside Routing in Doxygen output.
/**
 * @defgroup EIGRP EIGRP
 * @ingroup Routing
 * @brief EIGRP implementation: DUAL, RTP, topology table, interface manager.
 */
```

Every file, class, and struct in a subsystem references the **leaf** group,
not the parent:

```cpp
/**
 * @file Eigrp.h
 * @brief Core EIGRP process object.
 * @ingroup EIGRP          ← leaf group, not Routing
 */
```

---

### Namespace Documentation

Document a namespace **once**, in the primary header where its main class or
entry point lives. Every other file that reopens the namespace does not repeat
the block — Doxygen merges all reopenings under the single definition.

```cpp
/**
 * @namespace hardware::egress
 * @brief Egress (TX) backend implementations and the shared EgressBase.
 *
 * EgressBase owns the MPMC free ring and frame layout logic. Concrete
 * subclasses (EgressPacket, EgressSend) implement the hardware send path.
 * Constructed exclusively by the hardware::egress::create() factory.
 */
namespace hardware::egress { ... }
```

---

### Forward Declarations

Inline trailing `///< ` with a brief one-liner.

```cpp
namespace routing::eigrp {
    struct EigrpAutonomousSystem; ///< Classic-mode EIGRP AS container.
    struct EigrpNamed;            ///< Named-mode EIGRP configuration group.
}
```

---

### Class / Struct Documentation

Full Doxygen block. `@brief` is one sentence. The body uses `##` markdown
sections to explain the design — what the class contains, how it fits into
the system, why it owns what it owns. Add `@warning` for any invariant that,
if violated, corrupts state or causes undefined behavior.

```cpp
/**
 * @brief One sentence describing what this represents.
 * @ingroup SubsystemGroup
 *
 * A short paragraph expanding on the concept and how it maps to something
 * real (e.g. a VRF, a protocol process, a queue). Explain what it mirrors
 * or models if that isn't obvious from the name.
 *
 * Each instance contains:
 * - First major owned resource
 * - Second major owned resource
 *
 * ## Architectural Role
 * Where does this sit in the ownership hierarchy? What is it the boundary
 * between? What does it NOT do, and why is that responsibility elsewhere?
 *
 * ## Lifecycle & Ownership
 * Who creates and destroys this? What does construction initialize?
 * What teardown must happen and in what order?
 *
 * ## Concurrency Model          (only if non-trivial)
 * Which members are guarded by which locks? What granularity and why?
 * What is safe to call concurrently without coordination?
 *
 * ## Fast Path vs. Slow Path    (only if relevant)
 * Which operations are on the forwarding critical path? Which are not?
 *
 * @warning Any invariant that, if violated, causes state corruption.
 *
 * @see RelatedClass
 */
class ClassName { ... };
```

Sections to include (only those that apply):

| Section | Use when |
|---|---|
| `## Architectural Role` | The class's position in the system needs explanation |
| `## Lifecycle & Ownership` | Construction/destruction has non-trivial invariants |
| `## Concurrency Model` | Multiple locks or non-obvious thread-safety guarantees |
| `## [Feature] Integration` | The class interacts with another subsystem in a constrained way |
| `## Fast Path vs. Slow Path` | Some methods are on the forwarding critical path |

---

### Constructor & Destructor

Always documented. Constructor describes what state is established and why
defaults are what they are. Destructor describes the teardown sequence and
any ordering constraints.

```cpp
/**
 * @brief Constructs a VirtualRouter instance.
 *
 * Initializes an empty VRF with:
 * - No attached interfaces
 * - Empty routing table
 * - No protocol instances
 *
 * IPv4 is enabled by default because it is the most common base AF.
 *
 * @param global Reference to the Global controller that owns this VRF.
 * @param name   Unique VRF name (e.g., "default").
 */
VirtualRouter(Global& global, const std::string& name);

/**
 * @brief Destructs the VirtualRouter.
 *
 * Performs a full teardown:
 * - Detaches all interfaces and removes them from the global registry
 * - Deletes all EIGRP and OSPF instances
 *
 * Interfaces are removed via global.removeInterface() to ensure hardware
 * resources, protocol references, and NDP/ARP caches are also cleaned up.
 *
 * All teardown is performed under the appropriate locks to prevent races
 * with active protocol threads or timer callbacks.
 */
~VirtualRouter();
```

---

### Methods

`@brief` is one sentence. Add a second paragraph for non-obvious contracts,
preconditions, or behavior. `@note` for caveats callers must know. `@warning`
for anything that can corrupt state or violate invariants if misused.
`@see` to point to a closely related function or class.

```cpp
/**
 * @brief Calculates a Router ID for a routing process.
 *
 * Performs a full router ID calculation covering all interfaces in the VRF:
 *  - First will look for the highest loopback IPv4 address.
 *  - Second will look for the highest ethernet IPv4 address.
 *  - If no IPv4 addresses exist then it will need to be manually defined.
 *
 * @param[out] rid Populated with the selected Router ID on success.
 * @return True if a Router ID was found, false if no IPv4 addresses exist.
 *
 * @note Duplicate RIDs are possible. To prevent them, manually define the
 * Router ID for the routing process.
 *
 * Router IDs DO NOT change as interfaces change after the process starts.
 */
bool calculateRID(uint32_t& rid);

/**
 * @brief Remove and delete an EIGRP Autonomous System.
 *
 * Any attached EIGRP IPv4 or IPv6 instances inside the AS are deleted
 * as part of the cleanup.
 *
 * @param id AS number to remove.
 * @return True if removed, false if missing.
 *
 * @warning stop() must be called on the AS before calling this. Deleting
 * a running AS will tear down its scheduler mid-flight and corrupt state.
 */
bool removeEigrpAutonomousSystem(uint32_t id);
```

Simple getters whose name and return type make them self-explanatory get
**no comment**.

```cpp
// No comment needed.
std::string getName()    { return instanceName; }
uint32_t getInstanceId() { return instanceId; }
bool isDefault()         { return defaulted; }
```

---

### Templates

Use `@tparam` for each template parameter, the same way you use `@param` for
function arguments. Document what the type must satisfy — concept, required
members, or expected behaviour — not just what it is named.

```cpp
/**
 * @brief Per-AFI route processing instance.
 * @ingroup BGP
 *
 * Owns Adj-RIB-In, Adj-RIB-Out, and Loc-RIB for one address family.
 * All route processing for this AFI is serialized through the owning
 * BgpProcess scheduler.
 *
 * @tparam N  NLRI policy type. Must provide:
 *              - N::NlriType     — the prefix type (e.g. IPv4Prefix)
 *              - N::LocRib       — the local RIB container type
 *              - N::afi, N::safi — AFI/SAFI constants
 */
template<typename N>
class AddressFamilyInstance { ... };
```

---

### Section Dividers

All-caps, plain comment. Groups related declarations. Doxygen ignores these;
they exist for human readers scanning the header.

```cpp
// EIGRP AUTONOMOUS SYSTEMS

// EIGRP NAMED SYSTEMS

// OSPF PROCESS

// GLOBAL HELPERS
```

---

### Private Members

Inline `///< ` for non-obvious members. Include ownership, constraints, or
which subsystem modifies the value when that is not clear from the name.
Nothing for members whose name and type together are self-explanatory.

```cpp
private:
    uint32_t instanceId{0};
    const bool defaulted{false};

    RoutingTable routingTable; ///< Per-VRF RIB + FIB generation logic.
    Global& global;            ///< Reference to the global system controller.

    std::unordered_map<uint32_t, EigrpAutonomousSystem> eigrpList; ///< Classic-mode EIGRP AS containers. Guarded by eigrpMutex.
    std::unordered_map<std::string, EigrpNamed> namedEigrpList;    ///< Named-mode EIGRP groups. Guarded by eigrpNamedMutex.
```

---

### Enums

Document the enum itself and any value whose effect on state or control flow
is not obvious from the name.

```cpp
/**
 * @brief Operational state of an OSPF interface.
 * @ingroup OSPF
 */
enum class InterfaceState : uint8_t
{
    Down     = 0, ///< Interface is administratively or operationally down.
    Loopback = 1, ///< Interface is a loopback; no neighbors formed.
    Waiting  = 2, ///< Waiting for DR/BDR election (wait timer running).
    P2P      = 3, ///< Point-to-point; no DR/BDR election needed.
    DROther  = 4, ///< Neither DR nor BDR on this segment.
    Backup   = 5, ///< This router is the BDR.
    DR       = 6, ///< This router is the DR; originates Network LSA.
};
```

---

### Cross-References

`@ref` links inline within prose. `@see` generates a standalone "See Also"
block at the end of the entry. Use `@ref` when the reference is part of a
sentence; use `@see` for related symbols that are worth knowing about but
aren't mentioned in the body text.

```cpp
/**
 * @brief Fires whenever the best route for a watched prefix changes.
 *
 * All callbacks run on the RIB's ProcessQueue thread. Consumers on a
 * different scheduler must post back to their own queue before touching
 * their own state — see @ref NhtCtx for the BGP NHT pattern.
 *
 * @see ProcessQueueRef::post
 */
```

---

## Source Files (`.cpp`)

No `@file` block. No Doxygen blocks. Implementation details that are not
obvious from the header belong here as inline comments.

Comment when:
- The implementation choice is non-obvious (explain why this approach)
- A multi-step algorithm has a sequence that matters (number the steps)
- A subtle invariant or ordering constraint must hold
- A spec reference clarifies the logic (cite the RFC section)

Do not comment:
- Anything the code already says clearly
- Method-level `@brief` (lives in the header)
- Commented-out code
