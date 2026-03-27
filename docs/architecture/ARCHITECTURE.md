# VirtualRouter — Architecture

This document explains *why* the system is built the way it is. It does not
describe what files exist or list class members — the code does that. Each
section answers the question: "why was this done this way, and what would
break if it were done differently?"

---

## Table of Contents

- [Design Philosophy](#design-philosophy)
1. [Repository Layout](#1-repository-layout)
2. [High-Level Architecture](#2-high-level-architecture)
3. [Global Routing & VRF](#3-global-routing--vrf)
4. [RIB / FIB / RouteWatcher System](#4-rib--fib--routewatcher-system)
5. [BGP](#5-bgp)
6. [OSPF](#6-ospf)
7. [EIGRP](#7-eigrp)
8. [Async Control Plane — ProcessQueue & ControlScheduler](#8-async-control-plane--processqueue--controlscheduler)
9. [TimeManager](#9-timemanager)
10. [Configuration Registry](#10-configuration-registry)
11. [CLI Engine](#11-cli-engine)
12. [TCP Transport Layer](#12-tcp-transport-layer)
13. [Hardware — Ingress & Egress Pipelines](#13-hardware--ingress--egress-pipelines)
14. [Infrastructure — ARP & NDP](#14-infrastructure--arp--ndp)
15. [Interface Layer](#15-interface-layer)
16. [QoS](#16-qos)
17. [Cross-Cutting Design Patterns](#17-cross-cutting-design-patterns)
18. [Concurrency Model](#18-concurrency-model)

---

## Design Philosophy

A few core convictions shaped every decision in this codebase. They're worth stating
explicitly because they explain choices that might otherwise look like over-engineering.

### The data plane must never yield to the control plane

Packet forwarding happens on nanosecond timescales. Route updates, SPF runs, and BGP
best-path recalculations happen on millisecond timescales. These two worlds should
never block each other.

The FIB is protected by RCU — a forwarding thread takes one memory barrier and
proceeds without lock acquisition, ever. The control plane publishes a new FIB entry
atomically and retires the old one after all readers have drained. A BGP convergence
event, an OSPF SPF run, or an EIGRP diffusing computation has zero impact on
forwarding throughput. The hardware RX threads don't even know the control plane
exists; they just do a FIB lookup and hand the packet to egress.

This isn't a performance optimization applied after the fact — it's the foundational
constraint the entire threading model is built around.

### Abstractions should cost nothing at runtime

C++ templates are used not as a convenience but as a correctness and performance
tool. The CLI parser generates its match logic at compile time via fold expressions
— at runtime it is a straight sequence of string comparisons with no hash table and
no dynamic dispatch. The config registry resolves field access by type tag at compile
time — no string lookup, no map, no runtime branching at config read sites. The BGP
address family system specializes `AddressFamilyInstance<N>` at compile time for each
AFI/SAFI — the IPv4 and IPv6 code paths are fully separate, with no `if (af == IPv6)`
branches in the hot path.

The rule of thumb: if something can be decided at compile time, it is. If it can't,
the runtime cost should be explicit and measurable.

### Type safety is a design tool, not just a feature

The config registry makes it impossible to read a field from the wrong registry scope
— the compiler rejects it. The CLI command system makes it impossible to have an
unhandled command at runtime — if a mode has no registered parser, it's a compile
error. The BGP address family variant ensures that every visit site handles every
enabled AFI/SAFI — an incomplete `std::visit` lambda is a compile error. OSPF LSA
bodies are `std::variant`, so pattern-matching over LSA types is exhaustive by
construction.

These aren't conveniences. They prevent entire categories of bugs that would
otherwise be silent runtime failures — wrong config key, wrong address family,
unhandled LSA type. The type system enforces protocol invariants that the RFC states
in English.

### Protocol state is single-threaded; threading is structural

Instead of protecting protocol data structures with mutexes, each protocol process
serializes all mutations through a `ProcessQueue` — a lock-free MPSC ring. Hardware
threads, the TCP engine, and the timer subsystem all produce events onto this queue;
a ThreadPool worker drains it one event at a time. Protocol code never needs to worry
about concurrent access to its own state because by construction it only ever runs on
one thread at a time.

Locks exist only at the narrow boundaries where the outside world touches shared
mutable state: the VRF interface list, the ARP/NDP neighbor tables. Everywhere else,
the threading model makes locks unnecessary. This is not "avoid locks because they're
slow" — it's "structure the system so that locks are the wrong tool for most of the
problem."

### RFC compliance is the specification

Each protocol implements the actual standard, not an approximation. OSPFv2 implements
all 8 neighbor states, the exact DR/BDR two-pass election from RFC 2328 §9.4,
sequence number rollover, and MaxAge flooding with proper purge semantics. BGP
implements all 31 FSM events, all 11 best-path selection steps, AS4 path
reconstruction per RFC 4893 §4.2.3, and capability negotiation per the OPEN message
spec. EIGRP implements actual DUAL with the feasibility condition as specified — not a
simplified variant.

The goal is a router that can form adjacencies with real Cisco, Juniper, and FRR
instances and behave correctly without special casing. That requires reading the RFC
carefully enough to get the corner cases right, not just the happy path.

### VRF isolation is a first-class constraint, not a retrofit

`VirtualRouter` is the root of all protocol state from day one. Each VRF has its own
RIB, its own FIB, its own TCP stack, its own OSPF and BGP processes. There is no
global protocol state. This is not a multi-tenancy feature added later — it's the
base assumption the ownership model is built on. Adding a second VRF is adding a
second `VirtualRouter`. Nothing else changes.

---

## 1. Repository Layout

The directory structure is not arbitrary. It encodes coupling constraints: code in
`hardware/` depends on kernel AF_PACKET and AF_XDP APIs and must not know that
protocols exist. Code in `routing/` depends on VRF state and must not know which
kernel I/O backend is running. Neither layer can accidentally reach the other because
they live in separate trees with no cross-includes in those directions.

The separation also makes the layering auditable. If `routing/bgp/` ever included
anything from `hardware/`, that would immediately signal a design violation. The
directory layout is the first line of enforcement for the layered architecture
described in §2.

```
VirtualRouter/src/
│
├── core/                       Per-VRF system objects: VirtualRouter, RoutingTable
│   └── routing/                RIB, FIB, RouteWatcher, RibBucket
│
├── routing/
│   ├── bgp/                    Full RFC 4271 BGP implementation
│   │   ├── af/                 AddressFamily template system + per-AF instance
│   │   ├── decision/           BestPath comparator + DecisionEngine
│   │   ├── neighbor/           Neighbor, NeighborAf, NeighborTable
│   │   ├── rib/                LocRib, AttributeManager, RibTypes
│   │   ├── session/            Session, Fsm, SessionTimers, ProcessQueue, Capabilities
│   │   └── transport/          Transmission (BgpRx, BgpTx)
│   │
│   ├── ospf/                   OSPFv2 + OSPFv3 implementation
│   │   ├── area/               Area management and inter-area logic
│   │   ├── interface/          OSPF interface state machine
│   │   ├── lsa/                Per-LSA-type headers + bodies (v2 and v3 variants)
│   │   ├── lsdb/               Link-State Database with pmr optimization
│   │   ├── rib/                OSPF RIB + route types
│   │   ├── topology/           SPF computation and route path types
│   │   └── transmission/       OSPF wire format encode/decode
│   │
│   └── eigrp/                  EIGRP classic + named mode
│       ├── rtp/                Reliable Transport Protocol + Neighbor state machine
│       ├── topology/           Topology table and metric types
│       └── interface/          Interface manager
│
├── cli/
│   ├── parser/                 Command<>, CliModeParser<>, FixedString
│   ├── modes/                  Mode enum, CliMode path table
│   │   └── contexts/           Per-mode Context objects (GlobalContext, OspfContext, etc.)
│   ├── execution/              Executor<> — mode switching and dispatch
│   └── runtime/                CliSession, I/O loop
│
├── configs/
│   └── registry/               RegistryDatabase<> and per-protocol registry definitions
│       └── router/             BgpRegistry, OspfRegistry, EigrpRegistry, etc.
│
├── transport/
│   └── tcp/                    Tcp, TcpEngine, Connection, TxBuffer, Listener
│
├── hardware/
│   ├── ingress/                IngressBase, IngressXdp, IngressPacket
│   └── egress/                 EgressBase, EgressPacket, EgressSend
│
├── infrastructure/
│   ├── arp/                    ARP table + request/reply handling
│   └── ndp/                    NDP (IPv6 neighbor discovery)
│
├── interface/                  Interface abstraction (physical/loopback/SVI)
├── services/                   DHCP, DNS
├── qos/                        TxQueueManager, RxQueueManager, egress policies
├── security/                   Key management, authentication
├── utils/                      ThreadPool, TimeManager, Logger
├── processing/                 Packet processing pipeline (scaffold)
├── packet/                     Header definitions (BgpHeader, etc.)
├── types/                      IPAddress, IPPrefix, AddressFamily enum
└── web/                        REST API (minimal scaffold)
```

---

## 2. High-Level Architecture

The system is layered so that information flows in exactly one direction: hardware
reads packets, the FIB decides where they go, the RIB decides what the FIB contains,
and protocols decide what the RIB contains. No layer reaches back into the layer
below it. This one-directional flow is what makes the forwarding path lock-free: the
forwarding thread never needs to ask the control plane anything — it just reads the
FIB and acts on it.

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                         CLI / Configuration Layer                            │
│     CliModeParser<> + Command<> templates  ·  RegistryDatabase<>             │
└──────────────────────────────────┬───────────────────────────────────────────┘
                                   │ config reads/writes
┌──────────────────────────────────▼───────────────────────────────────────────┐
│                        Global System Controller                               │
│  ThreadPool  ·  TimeManager  ·  ControlScheduler  ·  ARP/NDP global config   │
└──────────────────────────────────┬───────────────────────────────────────────┘
                                   │ creates/owns
┌──────────────────────────────────▼───────────────────────────────────────────┐
│                         VirtualRouter  (= VRF)                                │
│  ┌───────────────┐  ┌───────────────┐  ┌──────────────┐  ┌────────────────┐  │
│  │  BgpProcess   │  │  OspfProcess  │  │    Eigrp     │  │  RoutingTable  │  │
│  │  (per AS)     │  │  (per procId) │  │  (per AS/VRF)│  │  (RIB + FIB)  │  │
│  └───────┬───────┘  └──────┬────────┘  └──────┬───────┘  └───────┬────────┘  │
│          │                 │                   │                  │           │
│          └─────────────────┴───────────────────┴──────────────────┘          │
│                            route install / withdraw                           │
└──────────────────────────────────┬───────────────────────────────────────────┘
                                   │ FIB lookup
┌──────────────────────────────────▼───────────────────────────────────────────┐
│                       Interface Layer                                         │
│         Interface  ·  per-VRF TCP::Tcp  ·  ARP/NDP tables                    │
└──────────────────────────────────┬───────────────────────────────────────────┘
                                   │
┌──────────────────────────────────▼───────────────────────────────────────────┐
│                     Hardware I/O  (per Interface)                             │
│         IngressBase ←→ RxQueue  ·  EgressBase ←→ TxQueue                     │
│         IngressXdp (XDP fast path) or IngressPacket (kernel socket)           │
└──────────────────────────────────────────────────────────────────────────────┘
```

All protocol control-plane work (FSM transitions, route recalculation, UPDATE
generation) is serialized through per-process `ProcessQueue` instances so that
protocol threads never block hardware I/O threads.

### Packet Processing Flow

```mermaid
flowchart TB
    %% ── Ingress ──────────────────────────────────────────────────────────────
    A([PHYSICAL IN-INTERFACE\nIngressXdp · IngressPacket])

    A --> Q1{IPv4 or IPv6?}
    Q1 -- NO --> DROP([Drop])
    Q1 -- YES --> Q2{Destination\nLocal IP?}

    %% ── Local delivery path ──────────────────────────────────────────────────
    Q2 -- YES --> LI

    subgraph LI [" LOCAL IN "]
        direction LR
        ARP_H[ARP / NDP\nHandler]
        TCP_H[TCP Stack\nTcpEngine]
        RAW_H[Raw Socket\nOSPF · EIGRP]
    end

    LI --> RP

    subgraph RP [" ROUTER PROCESSES "]
        direction LR
        BGP_P[BGP\nFSM · Best-Path\nAdj-RIB-In/Out]
        OSPF_P[OSPF v2/v3\nSPF · Flooding\nLSDB]
        EIGRP_P[EIGRP\nDUAL · RTP\nTopology Table]
    end

    RP --> RIBS

    subgraph RIBS [" RIB / FIB (RoutingTable) "]
        direction LR
        RIB_N[RIB\nper-source candidates\nadminDistance · metric]
        SEL_N[selectBest]
        FIB_N[FIB\nLPC Trie\nRCU-protected]
        RIB_N --> SEL_N --> FIB_N
    end

    %% ── Transit / forwarding path ────────────────────────────────────────────
    Q2 -- NO --> FWD

    subgraph FWD [" ROUTING "]
        direction TB
        FIBL[FIB Lookup\nLPC Trie · lock-free RCU read]
        Q3{Route\nFound?}
        NHR[Next-Hop Resolution\nARP / NDP Lookup]
        REWR[TTL Decrement\nMAC Rewrite]
        FIBL --> Q3
        Q3 -- NO --> ICMP([ICMP Unreachable])
        Q3 -- YES --> NHR --> REWR
    end

    FIB_N -. "FIB fast-path\n(RCU read)" .-> FIBL

    REWR --> QOS

    subgraph QOS [" QoS "]
        TXQ[TxQueueManager\nEgress Classification]
    end

    %% ── Locally generated packets ────────────────────────────────────────────
    RP --> LO

    subgraph LO [" LOCAL OUT "]
        direction LR
        TCP_O[TCP Stack\nBGP TX]
        RAW_O[Raw Socket\nOSPF · EIGRP TX]
    end

    QOS --> OUT
    LO --> OUT

    OUT([PHYSICAL OUT-INTERFACE\nEgressXdp · EgressPacket])
```

**Path legend**

| Path | Description |
|------|-------------|
| Physical In → Routing → Physical Out | Transit packet forwarding. FIB lookup is a lock-free RCU read on the LPC trie. ARP/NDP resolves the next-hop MAC. TTL is decremented and the Ethernet header is rewritten before egress. |
| Physical In → Local In → Router Processes | Control traffic destined for this router (BGP TCP/179, OSPF multicast 224.0.0.5/6, EIGRP multicast 224.0.0.10). Delivered to the TCP stack or raw socket handler, then dispatched to the owning protocol process via its `ProcessQueue`. |
| Router Processes → RIB/FIB | Protocol processes install and withdraw routes through `RoutingTable`. `selectBest` picks the winner by admin-distance then metric and atomically swaps the new `FibEntry` into the LPC trie via RCU. |
| Router Processes → Local Out → Physical Out | Control packets generated by protocol processes (BGP UPDATEs, OSPF LSAs, EIGRP Hellos). BGP sends via the TCP stack; OSPF and EIGRP write directly to raw sockets. |

---

## 3. Global Routing & VRF

`VirtualRouter` is the VRF boundary from day one — not a feature bolted on later.
The consequence is that there is no global protocol state anywhere in the system.
Each VRF has its own RIB, FIB, TCP stack, and protocol instances. Adding a second
VRF is instantiating a second `VirtualRouter`. Removing a VRF is destroying one.
Nothing else needs to change.

Interfaces are globally owned but VRF-attached, which is an intentional ownership
split. Interfaces are hardware resources — a physical NIC exists whether or not a
VRF has claimed it. Ownership by the VRF would complicate interface reassignment
(moving an interface between VRFs would require protocol teardown coordination across
two VRF objects). Instead, the VRF holds a reference, and `setVRF()` on the
Interface handles all the teardown and re-attach logic in one place.

Protocol instances (EIGRP, OSPF) are fully owned by the VRF. This means protocol
teardown is automatic on VRF destruction — no external coordination needed.

### VirtualRouter — The VRF Container

```
VirtualRouter
│
├── interface::InterfaceManager                     ifaceMgr
│   └── (see §15. Interface Layer)
│
├── unordered_map<uint32_t, EigrpAutonomousSystem>  eigrpList
│   └── (mutex eigrpAutonomousSystemMutex)
│
├── unordered_map<string, EigrpNamed>               namedEigrpList
│   └── (mutex eigrpNamedMutex)
│
├── unordered_map<uint32_t, OspfProcess>            ospfList
├── unordered_map<uint32_t, OspfV3Instance>         ospfv3List
│
├── RoutingTable                                    routingTable
│   ├── Rib<uint32_t>    (IPv4 RIB)
│   └── Rib<__uint128_t> (IPv6 RIB)
│
├── TCP::Tcp                                        tcpManager
│   └── (isolated per-VRF TCP socket namespace)
│
├── string                                          instanceName
├── set<AddressFamily>                              enabledAddressFamilies
└── uint32_t                                        rid  (calculateRID())
```

**Router ID calculation** (`calculateRID`): Scans interface list for the highest
IPv4 address on a loopback; if none found, falls back to the highest IPv4 on any
Ethernet interface. This mirrors Cisco IOS RID election behavior.

---

## 4. RIB / FIB / RouteWatcher System

Three design decisions define this subsystem.

**Template parameterization over address family.** `Rib<AddrType>` is instantiated
separately for IPv4 (`uint32_t`) and IPv6 (`__uint128_t`). The compiler generates
two completely separate, type-safe tables with no runtime branching on address
family. The alternative — a single table with `if (af == IPv6)` everywhere — would
scatter address-family conditionals through every route operation and make it
impossible for the compiler to reason about type correctness across address families.

**RCU for the FIB.** The FIB is read on every forwarded packet. Lock acquisition on
that path is not acceptable — even an uncontended mutex costs hundreds of nanoseconds.
RCU makes the read completely lock-free: a forwarding thread takes one memory barrier,
does its lookup, and exits the guard. The writer pays the cost: it copies the new
entry to the heap, atomically swaps it in, and defers freeing the old one until all
current readers have exited. For a read-overwhelmingly-dominant data structure like
the FIB, this is exactly the right trade.

**RouteWatcher instead of polling.** Protocols need to react to route changes — BGP
NHT tracks whether a next-hop is reachable; redistribution tracks whether a source
protocol's best route changed. Polling would waste CPU and introduce reaction latency
proportional to the poll interval. `RouteWatcher` fires callbacks immediately when
the relevant prefix changes, from the RIB's scheduler thread, keeping reaction
latency at one scheduler quantum.

### Rib<AddrType>

```
Rib<AddrType>
│
├── unordered_map<PrefixKey, RibBucket<AddrType>*>  table
│   └── RIB entries grouped by exact prefix; each bucket holds all sources
│
├── Fib<AddrType>                                   fib
│   └── LPCTrie<AddrType>  (Longest Prefix Compression Trie)
│       └── Lock-free lookups via RCU read guards
│
├── ProcessQueue                                    scheduler
│   └── All RIB mutations serialized through this queue
│
└── RouteWatcher<AddrType>                          routeWatcher
    └── Callback-based change notification system
```

**RibEntry<AddrType>**: The unit stored per prefix per source.

```
RibEntry<AddrType>
├── prefix: AddrType
├── length: uint8_t            (prefix length / mask)
├── source: RouteSource        (BGP, OSPF, EIGRP, STATIC, CONNECTED, ...)
├── processId: uint64_t        (which process instance installed this)
├── adminDistance: uint8_t     (preference between sources; lower wins)
├── metric: uint64_t
├── tag: uint32_t
├── topInfo: void*             (opaque protocol-specific metadata)
├── nextHops: NextHopPath[8]   (up to MAX_NEXTHOP=8 equal-cost next hops)
│   └── NextHopPath: { optional<AddrType> nextHop, uint32_t iface, uint32_t weight }
└── nextHopCount: uint8_t
```

**FibEntry<AddrType>**: A stripped-down heap copy of the winning `RibEntry`,
containing only forwarding fields (prefix, length, nextHops). Stored atomically
in `RibBucket::fibEntry` and retired via RCU when replaced.

### RibBucket<AddrType>

Each unique (prefix, length) pair owns one `RibBucket`. It holds all competing
source routes and maintains the current best selection.

```
RibBucket<AddrType>
│
├── vector<RibEntry<AddrType>>             routes      ← all installed sources
├── RibEntry<AddrType>*                    bestEntry   ← current winner (ptr into routes)
├── RibEntry<AddrType>*                    prevBest    ← winner before last selectBest()
└── atomic<RibEntry<AddrType>*>            fibEntry    ← RCU-protected heap copy for FIB
```

`selectBest()` runs after every add/remove. It saves `prevBest = bestEntry`, then
scans `routes` comparing `adminDistance` then `metric`. The winner is deep-copied
to a new heap `FibEntry`, atomically swapped into `fibEntry`, and the old copy is
`RCU::retire()`'d. This ensures FIB readers are never exposed to a pointer into the
potentially reallocating `routes` vector.

### FIB — LPC Trie

The forwarding table uses a **Longest Prefix Compression (LPC) Trie** — a compact
binary trie with path compression. Lookups are O(prefix bits) worst-case and
typically much faster due to compression skipping constant bit ranges.

FIB lookups are protected by **RCU (Read-Copy-Update)**:
- Readers take an `RCU::Guard` — zero lock acquisition, just a memory barrier
- Writers copy the new entry to the heap, swap atomically, then `RCU::retire()` the
  old pointer for deferred free after all readers have exited their guards
- `RCU::synchronize()` used at teardown to drain all pending retires

The packet forwarding fast path (`fib.lookup(addr)`) is completely lock-free.

### RouteWatcher<Addr>

`RouteWatcher` is the RIB's callback notification system. Protocols and BGP's NHT
register interest and receive typed callbacks when the best route changes. All
watches are evaluated on the RIB's `ProcessQueue` scheduler thread, so callbacks
run in a controlled, single-threaded context.

```
RouteWatcher<Addr>
│
├── unordered_map<PrefixKey, vector<WatchNode>>      prefixWatchTable
│   └── Exact (prefix, length) → list of watchers
│
├── unordered_map<WatchId, PrefixKey>                prefixIdMap
│   └── Reverse map for O(1) removal
│
├── unordered_map<SrcPidKey, vector<WatchNode>>      protocolWatchTable
│   └── (RouteSource, processId) → list of watchers
│       Used for redistribution: "tell me whenever BGP pid=42 best changes"
│
├── unordered_map<WatchId, AddrWatchState>           addrWatches
│   └── Per-address watch bookkeeping (re-pins on prefix withdraw)
│
├── Fib<Addr>&                                       fib
├── ProcessQueueRef                                  scheduler
└── AtomicStack<uint32_t>                            availableIds   ← ID recycling pool
```

**Return value convention**: Callbacks return `bool`. Returning `true` means
"I'm done — unsubscribe me." Returning `false` keeps the watch alive.

#### Three Watch Modes

**1. `watchRoute(prefix, length, ctx, fn, filter)` — Exact prefix watch**

Fires whenever the best route for the specific (prefix, length) changes according
to `filter`.

**2. `watchAddress(addr, ctx, fn, filter)` — LPM address watch (auto-repinning)**

Watches the reachability of a specific host address via longest-prefix match. This
is the watch mode used by BGP NHT. When the covering prefix withdraws, the watch
automatically re-pins to the next less-specific covering prefix — an NHT caller
transparently follows route changes through supernet fallbacks.

**3. `watchProtocol(src, pid, ctx, fn)` — Per-source protocol watch**

Fires whenever the best route from a specific `(RouteSource, processId)` pair
changes on any prefix. Designed for redistribution.

### RoutingTable — Dual-AF Facade

`RoutingTable` wraps `Rib<uint32_t>` and `Rib<__uint128_t>` under a single object
owned by `VirtualRouter`. All methods dispatch based on `AddrType` via `if constexpr`.

### BGP Next-Hop Tracking (NHT)

BGP NHT is implemented inside `AddressFamilyInstance<N>` using `watchAddress`.
It tracks whether each installed route's next-hop is currently reachable in the
global RIB, and triggers best-path recomputation when reachability changes.

The NHT callback fires on the RIB's scheduler thread. Because BGP state must only
be mutated from the BGP scheduler thread, the callback immediately posts back to
the BGP process queue via `NhtCtx::bgpSched` before touching any BGP state. A
configurable trigger delay (`BGP_NEXT_HOP_TRIGGER_DELAY`) batches multiple NHT
changes behind a timer, preventing thrashing when a link flap affects many prefixes
simultaneously.

---

## 5. BGP

BGP implements RFC 4271 with RFC 4893 (4-byte AS), RFC 2918 (route refresh),
RFC 4724 (graceful restart framework), and MP-BGP (RFC 4760). The design is
structured around two ideas: the FSM is the ground truth for session state, and
per-AFI logic is expressed as compile-time policy rather than runtime branching.

A BGP session carries no awareness of address families — it only drives the FSM
and delivers parsed messages. Address family instances consume those messages
independently and install into the global RIB through a shared `RoutingTable`
reference. This separation means adding a new AFI/SAFI requires no changes to
the session or FSM code — only a new policy type and a new instantiation.

### BgpProcess — The Process Root

```
BgpProcess
│
├── uint32_t                                  asNumber         (const)
├── TCP::Listener                             listener         (passive TCP)
│
├── unordered_map<IPAddress, Session>         sessions
│   └── keyed by peer IP address
│
├── unordered_map<AfiSafi, AddressFamilyVariant>  addressFamilies
│   └── each entry is one of:
│       AddressFamilyInstance<IPv4UnicastNlri>
│       AddressFamilyInstance<IPv6UnicastNlri>
│       AddressFamilyInstance<VpnV4Nlri>
│       AddressFamilyInstance<VpnV6Nlri>
│
├── NeighborTable                             ntable
├── AttributeManager                          attrMgr
├── ProcessQueue                              scheduler        (all FSM serialized here)
└── Config::Reference<BgpRegistry>           configs
```

Static TCP callbacks (`onConnectCallback`, `onAcceptCallback`, `onReceiveCallback`)
are registered with the TCP engine. They receive a `ConnCallbackCtx` with a `void*
user` field pointing back to the `BgpProcess`, then enqueue FSM events onto the
scheduler. The TCP thread never directly modifies FSM state — it only enqueues events.

### Session — Per-Peer State

```
Session
│
├── TCP::Connection         primaryConn    (active/outbound)
├── TCP::Connection         secondaryConn  (passive/inbound — during collision resolution)
│
├── Fsm                     fsm
├── SessionTimers           timers
│   ├── holdTimer
│   ├── keepaliveTimer
│   ├── connectRetryTimer
│   └── delayOpenTimer
│
├── Capabilities            localCaps
├── Capabilities            peerCaps
├── NegotiatedCapabilities  negotiated
│
├── Neighbor&               neighbor       (config + RIB state reference)
└── BgpProcess&             process
```

### FSM (Fsm.cpp)

The FSM implements RFC 4271 §8 exactly. States:

```
IDLE → CONNECT → ACTIVE → OPEN_SENT → OPEN_CONFIRMED → ESTABLISHED
         ↑___________________________|
         (TCP failure at any active state → back to IDLE or ACTIVE)
```

Additional events beyond RFC 4271:
- `ROUTE_REFRESH` (RFC 2918)
- `BFD_DOWN / BFD_UP` (framework hooks)
- `MAX_PREFIX_REACHED` (prefix limit enforcement)

On `OPEN_RECEIVED`, the FSM validates:
- BGP version (must be 4)
- Hold time (must meet `MINIMUM_HOLDTIME` config)
- AS number matching (eBGP: must match configured peer ASN)
- Capability negotiation (OPEN parameters → `NegotiatedCapabilities`)

Collision detection is handled by comparing Router IDs; the higher-RID session wins
and the lower is sent `CEASE` notification.

### Address Family Template System

All per-AFI logic (NLRI encoding, route table types, wire format) is a compile-time
policy. The alternative — a single class with `if (afi == IPv6)` scattered throughout
— would mean every code path carries dead branches for every AFI that isn't active,
and adding a new AFI requires auditing every one of those branches. The template
approach means each AFI is a fully separate compiled instance. Adding a new AFI
(e.g., L2VPN EVPN) means defining an `EvpnNlriPolicy` struct with the required type
aliases and adding it to the variant — no existing code changes.

```
AddressFamily<AfiSafi::IPv4Unicast>
    resolves to → AddressFamilyInstance<IPv4UnicastNlriPolicy>

AddressFamily<AfiSafi::IPv6Unicast>
    resolves to → AddressFamilyInstance<IPv6UnicastNlriPolicy>

AddressFamily<AfiSafi::VpnV4>
    resolves to → AddressFamilyInstance<VpnV4NlriPolicy>
```

`AddressFamilyVariant` is a `std::variant` of all possible instantiations, stored
in the `addressFamilies` map. `std::visit` is used when you need to operate on all
enabled AFs uniformly at runtime.

### AddressFamilyInstance<N>

```
AddressFamilyInstance<N>
│
├── AdjRibInTable<N>    adjRibIn    ← post-policy, per-neighbor routes
├── AdjRibOutTable<N>   adjRibOut   ← computed egress routes per neighbor
├── N::LocRib           locRib      ← best route per prefix; type chosen by NlriPolicy
│
├── BgpProcess&         process
├── VirtualRouter&      policy      (for route installation)
├── AttributeManager&   attrMgr     (shared with process)
└── Config::Reference<BgpAddressFamilyRegistry>  configs
```

**Route processing pipeline** (inbound UPDATE):

```
onParsedUpdateFromPeer(Neighbor&, ParsedUpdate<Nlri>)
    │
    ├─ For each announced prefix:
    │   ├─ applyIngressPolicy(route) → bool (true = DROP)
    │   │   ├─ AS_PATH loop detection
    │   │   ├─ ALLOWAS_IN / ALLOWAS_IN_OCCURANCES counting
    │   │   └─ Route policy filter (future: route maps)
    │   │
    │   ├─ Insert into adjRibIn[neighborId][prefix]
    │   ├─ MAXIMUM_PREFIX check → post MAX_PREFIX_REACHED if exceeded
    │   └─ Queue recomputeNlri(prefix)
    │
    └─ For each withdrawn prefix:
        ├─ Remove from adjRibIn
        └─ Queue recomputeNlri(prefix)

recomputeNlri(prefix)
    │
    ├─ Collect all candidates: adjRibIn[*][prefix]
    ├─ DecisionEngine::selectBest(candidates) → optional<LocalRoute<N>>
    │
    ├─ If winner changed:
    │   ├─ locRib.installOrReplace(prefix, winner)
    │   ├─ VirtualRouter::installRoute(prefix, winner)  ← into global RIB
    │   └─ recomputeAdjRibOut(prefix, winner)
    │
    └─ If no winner: withdraw from locRib + global RIB
```

### Decision Engine

`DecisionEngine` wraps `BestPathComparator` and implements the 11-step RFC 4271
best-path algorithm plus extensions:

```
Step  0: WEIGHT (higher wins) — Cisco extension
Step  1: LOCAL_PREF (higher wins)
Step  2: Locally originated (prefer over received)
Step  3: AS_PATH length (shorter wins)
Step  4: Origin code (IGP=0 < EGP=1 < Incomplete=2)
Step  5: MED (lower wins; medMissingAsWorst config option)
Step  6: eBGP > iBGP
Step  7: IGP metric to next-hop (lower wins; ignoreIgpMetric config option)
Step  8: Oldest eBGP route (prefer established session) OR compare Router ID
         (compareRouterId config option controls this)
Step  9: Router ID (lower wins; used as deterministic tiebreaker)
Step 10: Cluster List length (for route reflectors; shorter wins)
Step 11: Peer IP address (lowest wins; final deterministic tiebreaker)
```

### AttributeManager — Flyweight Path Attributes

BGP path attributes are expensive to copy. In a full table with 1M routes, many
routes share identical AS-PATHs and community sets. Storing a full copy per route
would be both wasteful and incorrect — a change to a shared attribute would need to
be applied everywhere. `AttributeManager` deduplicates attributes using refcounted
flyweights keyed by content hash. Routes store only a `uint32_t pathId`. `RouteBase`
RAII wraps retain/release so that reference counts are automatically maintained
through copy/move/destroy of any route object.

---

## 6. OSPF

OSPF implements RFC 2328 (OSPFv2) and RFC 5340 (OSPFv3) as a single dual-stack
implementation. The central decision is that OSPFv2 and OSPFv3 share one SPF engine
and one neighbor state machine, parameterized by a `PolicyV2` / `PolicyV3` template
argument that supplies the wire format differences. This avoids duplicating the
algorithmic core while keeping the protocol-specific encoding details entirely
separate.

LSA bodies are stored as `std::variant`, not virtual base classes. The LSDB can
contain thousands of LSAs, so vtable pointer overhead per LSA is measurable.
`std::variant` stores all types in a discriminated union with no heap allocation per
entry and no pointer indirection. More importantly, `std::visit` over a `std::variant`
is exhaustive — the compiler enforces that every SPF code path handles every LSA type.
A missing case is a compile error, not a runtime crash.

### OspfProcess — Process Root

OSPFv2 runs a single `OspfProcess` per process ID. OSPFv3 wraps two instances (IPv4
and IPv6 AFs) under an `OspfV3Instance`:

```
OSPFv2:
  OspfProcess (procId, af=IPv4)

OSPFv3:
  OspfV3Instance (procId)
  ├── OspfProcess* ipv4Instance
  └── OspfProcess* ipv6Instance

OspfProcess
│
├── uint16_t                         procId
├── AddressFamily                    af            (IPv4 or IPv6)
├── uint32_t                         rid           (or calculated from interfaces)
├── bool                             isABR, isASBR
│
├── map<uint32_t, Area>              areas         (created lazily by insureArea())
├── InterfaceManager                 ifaceMgr
├── OspfRib                          rib
│
├── ProcessQueue                     scheduler     (serializes SPF + LSA work)
└── Config::Reference<OspfRegistry> configs
```

Area 0 (backbone) is treated specially for ABR logic. `initiateReset()` posts resets
to all areas, tearing down neighbors and MaxAge-flooding all LSAs.

### LSDB Design

The LSDB uses `std::pmr` (polymorphic memory resource) containers when
`OSPF_LSDB_USE_PMR=1` is set, allowing arena-style allocation for LSA storage
during SPF computation. This reduces allocator overhead for large LSDBs.

LSA types are stored in a `std::variant`:

```
OSPFv2 LsaBody variant:
  std::variant<RouterLsaV2, NetworkLsaV2, SummaryNetworkLsa, SummaryRouterLsa,
               ExternalLsaV2, OpaqueLsaV2>

OSPFv3 LsaBody variant:
  std::variant<RouterLsaV3, NetworkLsaV3, InterAreaPrefixLsa, InterAreaRouterLsa,
               ExternalLsaV3, LinkLsa, IntraAreaPrefixLsa>
```

### SPF Computation

SPF is Dijkstra's algorithm run on the LSDB. The result feeds into `OspfRib` which
then installs routes into the VRF's global `RoutingTable`.

`TopologyTypes.hpp` defines the SPF output types:

```
OspfNextHop   = { uint32_t interfaceId, IPAddress nextHop }
OspfRouter    = { uint32_t rid, uint64_t cost, vector<OspfNextHop> nextHops }
OspfPath      = { type, area, cost, adminDistance, nextHops, ... }
OspfRoute     = { prefix: IPPrefix, paths: vector<OspfPath> }
```

### LSA Origination Templates

OSPF uses templated origination methods to handle both v2 and v3 wire formats
through a common policy interface. `Policy` provides compile-time hooks for how to
encode/decode LSA bodies and how to compute keys. This pattern avoids a large v2/v3
runtime switch in every origination path.

### Neighbor State Machine

The neighbor FSM mirrors RFC 2328 §10 exactly. Each `Neighbor` transitions through:

```
DOWN → ATTEMPT → INIT → 2WAY → EXSTART → EXCHANGE → LOADING → FULL
```

Key transitions:
- **INIT**: Hello received; router ID and options validated; DR/BDR election triggered
- **2WAY**: Bidirectional communication confirmed; decision whether to form adjacency
- **EXSTART**: Master/slave negotiation; DBD sequence initialized
- **EXCHANGE**: DBD packets exchanged; LSR list built from `compareLSASummary`
- **LOADING**: LSU/LSAck retransmit in progress; LSR retransmit timer active
- **FULL**: Adjacency complete; Router/Network LSA (re)originated

On neighbor **DOWN**: LSU/LSR retransmit lists cleared; all LSAs from that router are
MaxAge-flooded via `area.flushNeighborLsas(rid)`.

DR/BDR election uses the full RFC 2328 §9.4 two-pass algorithm. After election,
EXSTART is triggered for affected neighbors and the originator updates the Router LSA.

---

## 7. EIGRP

EIGRP's most interesting design problem is DUAL — the Diffusing Update Algorithm.
Unlike distance-vector protocols that are prone to count-to-infinity, and unlike
link-state protocols that require full topology knowledge, DUAL achieves loop-free
convergence with only neighbor-local information by enforcing the feasibility
condition. Getting DUAL right requires careful implementation of the active/passive
state machine, the SIA timer escalation, and per-neighbor query tracking. That's
where the implementation complexity lives.

The RTP layer (Reliable Transport Protocol) is EIGRP's answer to TCP — it provides
reliable, ordered delivery of Updates, Queries, and Replies over UDP multicast without
requiring a full TCP stack. It's implemented with per-neighbor sequence tracking,
exponential backoff, and Jacobson/Karels RTT estimation — the same algorithm TCP uses.

Classic mode and named mode share the same `Eigrp` implementation object. The only
difference is how configuration is structured at the CLI level, which is resolved
before it reaches the protocol code.

### Ownership Model

```
VirtualRouter
├── unordered_map<uint32_t, EigrpAutonomousSystem>   eigrpList     (classic mode)
│   └── EigrpAutonomousSystem
│       ├── Eigrp* ipv4
│       └── Eigrp* ipv6
│
└── unordered_map<string, EigrpNamed>                namedEigrpList
    └── EigrpNamed
        ├── Eigrp* ipv4
        └── Eigrp* ipv6
```

### Eigrp Class

```
Eigrp
│
├── uint16_t                  asNumber     (const)
├── AddressFamily             addressFamily (const)
├── bool                      namedMode
├── VirtualRouter*            routingInstance
│
├── RouterID                  rid          (static or calculated)
├── EigrpTopology             topology     (wraps DuelEngine + TopologyTable)
├── InterfaceManager          ifaceMgr     (interface tracking)
├── EigrpConfig               configMgr    (config validation)
├── GlobalAggregator          aggregator   (auto + manual summary route generation)
├── RouteManager              routeManager (installs routes into VRF RIB)
└── NeighborRegistry          allNeighbors (global neighbor tracking)
```

### DUAL Algorithm — DuelEngine

`DuelEngine` is the full EIGRP DUAL implementation. The feasibility condition is
fully implemented:

```cpp
route.isFeasibleSuccessor = (route.routeInfo.reportedDistance < bestFD);
```

A neighbor's route is a Feasible Successor if and only if its Reported Distance is
strictly less than this router's current Feasible Distance. This guarantees the backup
route is loop-free without requiring a full SPF run — it's the core insight of DUAL.

#### Successor Selection and Variance

```
recalculateSuccessors():
  1. Find minimum FD across all valid neighbor routes
  2. Update stored bestFD for the prefix
  3. For each candidate:
     - Mark as Successor if metric == bestFD
     - Mark as FeasibleSuccessor if RD < bestFD  (feasibility condition)
     - Mark as in-variance if metric <= bestFD * variance  (unequal-cost ECMP)
  4. Populate entry->successors[], entry->feasibleSuccessors[]
```

#### Active/Passive State Machine

```
PASSIVE (normal state, successors exist)
    │
    │  successor lost, no feasible successor available
    ▼
ACTIVE (diffusing computation underway)
    │  ├─ Send QUERY to all neighbors (except route origin)
    │  ├─ Start SIA timer
    │  └─ Track replies via OutgoingQuery / pending queries map
    │
    │  all replies received (concludeActive)
    ▼
PASSIVE (new successor installed, FD updated)

POISENED (route being withdrawn)
    │  holdtime expires
    ▼
removed from topology table
```

#### SIA (Stuck-In-Active) Handling

After `MAX_SIA_RETRIES` retransmissions without a reply, a SIA-QUERY is sent. If
that also times out, `handleSIATimeout()` tears down the non-responding neighbor,
preventing the topology from being permanently stuck if a neighbor becomes
unreachable mid-query.

### RTP — Reliable Transport Protocol

RTP gives EIGRP reliable multicast without requiring TCP for every control packet.
Not all EIGRP packets need reliability (Hellos are best-effort), but Updates, Queries,
and Replies must be acknowledged. Building a lightweight reliability layer over UDP
multicast is cheaper than running a full TCP session per neighbor.

**Sequence tracking**: Per-neighbor `lastSeqRecv`, `sentInitSeq`, `recvInitSeq`.
Sequence number 0 is reserved for unreliable packets. Wrap-around at `uint32_t::max`
wraps to 1, not 0.

**ACK modes**: explicit unicast ACK, piggybacked ACK on the next outgoing packet,
or implicit ACK from a newer sequence number.

**Retransmission**: exponential backoff with `rto = min(rto * 2.0, 60s)`.
After `MAX_RETRANSMISSIONS = 16` exhausted → neighbor declared DOWN.

**RTO computation** (TCP-style Jacobson/Karels):
```
srtt   = (1-1/8)*srtt   + (1/8)*sample
rttvar = (1-1/4)*rttvar + (1/4)*|sample - srtt|
rto    = clamp(srtt + 4*rttvar, 1s, 60s)
```

### Neighbor State Machine

```
DOWN ──hello received──► PENDING ──init exchange complete──► UP
  ▲                          │                                │
  └──hold timer expires───────┘       hold timer expires ─────┘
  └──retransmit exhausted─────────────────────────────────────┘
```

**K-value validation** happens at PENDING — mismatched K-values reject the neighbor
before any state is allocated.

**Initialization exchange**: both sides exchange NULL updates with the init bit set,
acknowledging each other's init updates. Only after `checkInit()` confirms both sides
have acked does `sendFullTopology()` transmit the complete topology to the new neighbor.

### Composite Metric Engine

Full K-value composite metric formula with 128-bit intermediate precision:

```
scaledBW   = (10,000,000 × 65,536) / interfaceBandwidth
scaledDelay = (delay_picoseconds / 1,000,000) × 65,536

base = K1×scaledBW + K3×scaledDelay
if K2 != 0: base += K2×scaledBW / (256 - load)
if K5 != 0: base = base × K5 / (K4 + reliability)
```

Default: K1=1, K2=0, K3=1, K4=0, K5=0 → classic bandwidth+delay formula.

### Topology Controller and Split Horizon

Split horizon is the default and prevents routing loops by not advertising a route
back on the interface it was learned from. It can be disabled per-interface when
hub-and-spoke topologies require full routing knowledge at spokes.

Route aggregation suppresses more-specific routes on interfaces where a summary is
configured. `TopologyController::filterAdvertisableRoutes()` checks suppression
before advertising any route.

---

## 8. Async Control Plane — ProcessQueue & ControlScheduler

Protocol state machine work is serialized through per-process lock-free queues. The
alternative — protecting protocol data structures with fine-grained mutexes — was
considered and rejected. Fine-grained locking produces lock ordering requirements
that are easy to violate, creates heisenbugs that only reproduce under thread
scheduling variations, and requires every protocol function to reason about which
locks it holds and which it needs to acquire. Serializing through a queue eliminates
that entire problem class: protocol code never needs a lock because by construction
it only ever executes on one thread at a time.

The queue is lock-free (MPSC sequence CAS) so that hardware RX threads and the timer
thread can post events to protocol queues without contending with each other or with
the consumer.

### ProcessQueue

Every protocol process (BGP, OSPF, EIGRP) owns one `ProcessQueue`. This is the
single point of serialization for all control-plane mutations.

```
ProcessQueue
│
├── sub[kMaxSubQueues]: SubQueue[]    (up to 8 labeled lanes)
│   └── SubQueue: lock-free MPSC ring
│       ├── head, tail: atomic<uint64_t>
│       └── slots[]: Slot
│           ├── seq: atomic<uint64_t>
│           └── task: ThreadPool::Task
│
├── labels[]: { name, subIndex }[]   (sorted; binary search to find lane)
│
├── atomic<bool> closed, scheduled, draining
└── atomic<bool> deferDestroy
```

**Sub-queues (lanes)** allow different event types to have independent FIFO ordering
without head-of-line blocking. BGP uses separate lanes for TIMERS, RX, and
NOTIFICATIONS. Within each lane, events are strictly ordered. Events across lanes are
interleaved by the scheduler based on availability.

**Enqueue path** (lock-free CAS loop):
```
producer:
  head = queue.head.fetch_add(1)     ← atomic claim of slot
  slot = &slots[head % capacity]
  while slot.seq != head: _mm_pause() ← wait for previous producer to finish
  slot.task = fn
  slot.seq.store(head + 1)           ← publish to consumer
```

**ProcessQueueRef** — a shared reference to a `ProcessQueue` with atomic refcount.
`release()` marks the queue closed and spins until all in-flight callbacks have
completed. This is the safe teardown mechanism: after `release()` returns, no more
callbacks will execute from this ref, and the owning object is safe to destroy.

### ThreadPool

Global worker thread pool. Workers continuously drain `ProcessQueue` sub-queues.
`ThreadPool::Task` uses **Small Object Optimization** — 128 bytes of inline storage
covers any lambda that captures up to ~12 pointers. No heap allocation per task.

---

## 9. TimeManager

Protocol timers (hold timer, keepalive, connect retry, SPF delay, SIA timer) must
fire accurately, but they must not execute protocol logic directly on the timer
thread. If the timer thread called into protocol state machines, it would create a
second execution context for those machines alongside the ProcessQueue consumer,
requiring locks on all protocol state. Instead, the timer thread posts tasks onto
the target `ProcessQueue` and returns immediately. Protocol code executes only on
the ThreadPool, regardless of what triggered it.

`TimeManager` provides:
- **One-shot timers**: fire once at absolute expiration time
- **Recurring timers**: automatically reschedule after each fire
- **Cancellation**: O(1) cancel by timer ID

Timer callbacks are delivered on a dedicated timer thread, which posts tasks onto
the target `ProcessQueue` rather than executing protocol logic directly. If a timer
fires after cancellation is requested but before the cancel takes effect, the callback
detects the generation mismatch and exits without executing.

---

## 10. Configuration Registry

The configuration system uses C++ type tags as config keys. There are no runtime
string lookups, no `map<string, variant>`, and no possibility of a typo producing a
silently wrong default. A config read site that uses the wrong key is a compile error.
A config read site that accesses a key from the wrong registry scope is a compile
error.

The downside is ceremony: adding a config field requires adding a type definition
in the registry header. For a router, this is the right trade — config key typos in
production protocol code are silent, persistent bugs.

### RegistryDatabase<...>

```cpp
using Registry = RegistryDatabase<
    OspfRegistry,
    OspfAreaRegistry,
    OspfInterfaceRegistry,
    BgpRegistry,
    BgpBaseRegistry,
    BgpAfBaseRegistry,
    BgpNeighborSessionRegistry,
    BgpNeighborRegistry
>;
```

Each `*Registry` is a struct defining a set of typed config fields. A field is
accessed by its type tag:

```cpp
// Read optional field:
auto rid = proc.getConfigs().get<Config::Bgp::BGP_ROUTER_ID>();
if (rid.hasValue()) return rid.load();

// Read required field:
bool gr = procCfg.get<Config::Bgp::BGP_GRACEFUL_RESTART>().load();
```

`get<Tag>()` returns a `ConfigField<T>` wrapper with `.hasValue()` and `.load()`.
The compiler resolves the correct registry and field at compile time — no hash lookup,
no string comparison.

### Registry Hierarchy

```
Global Registry
├── BgpRegistry                     (process-level BGP settings)
│   ├── BGP_ROUTER_ID
│   ├── BGP_AS_NUMBER
│   ├── BGP_GRACEFUL_RESTART
│   └── ...
│
├── BgpNeighborSessionRegistry      (per-neighbor session settings)
│   ├── BGP_NEIGHBOR_REMOTE_AS
│   ├── KEEPALIVE_INTERVAL
│   ├── MINIMUM_HOLDTIME
│   └── ...
│
├── BgpAfBaseRegistry               (per-AF, per-neighbor settings)
│   ├── ACTIVATE
│   ├── SEND_COMMUNITY
│   ├── MAXIMUM_PREFIX / WARNING_ONLY
│   └── ...
│
├── OspfRegistry                    (process-level OSPF)
├── OspfAreaRegistry                (per-area settings)
├── OspfInterfaceRegistry           (per-interface OSPF settings)
└── ...
```

`Config::Reference<RegistryType>` is a lightweight non-owning reference to a
registry scope. Each protocol process holds one and uses it to read its own
configuration without needing to know about other registries.

---

## 11. CLI Engine

The CLI is a fully compile-time command parser. There are no runtime command
registration tables, no function pointer maps, and no `strcmp` on command tokens.
Commands are types.

The reason is consistency between the definition and the dispatch. In a runtime
registration table, a command can be defined but never registered, or registered
under the wrong name, or unregistered without updating related code. None of these
are detectable at compile time. In the template system, the command IS its own
parser — there is no separate registration step. Adding a command means adding one
type and including it in the parser list. Removing it means deleting it. The compiler
enforces completeness.

### Command<Context, Handler, Parts...>

A `Command` captures the full pattern of a CLI command at compile time:

```cpp
using RouteCmd = Command<
    GlobalContext,
    &GlobalContext::handleIpRoute,
    "ip"_tok, "route"_tok, ARG, ARG, ARG
>;
```

- Fixed tokens (`"ip"_tok`, `"route"_tok`) must match exactly
- `ARG` matches any single token and captures it
- `ARG_REST` matches the remainder of the line

The `match()` and `tryExecute()` static methods are generated at compile time — no
runtime dispatch table.

### CliModeParser<Mode, Context, Commands...>

Groups commands under one CLI mode. `execute(ctx, tokens)` folds over the `Commands...`
pack, trying each until one matches. At runtime this is a linear chain of function
calls with no virtual dispatch.

`FindParser<M>` is a compile-time lookup that produces a hard compile error if no
parser is registered for a requested mode — it's impossible to enter a CLI mode that
has no handler.

### Executor<Parsers...>

Manages mode switching at the session level using **double-buffering**: `changeMode<M>`
builds the new context in the inactive slot, then atomically swaps `head`. If a
command fails after entering a mode, `revert()` swaps back with no cleanup needed.

### Context Hierarchy

```
ContextBase
├── terminal: CliSession&
└── negate: bool               (set when "no" keyword precedes command)

GlobalContext : ContextBase
OspfContext : ContextBase
EigrpContext : ContextBase
InterfaceContext : ContextBase
```

The `negate` flag on `ContextBase` maps to the `no` keyword: the same command handler
checks `ctx.negate` to decide whether to apply or remove a configuration.

---

## 12. TCP Transport Layer

Each `VirtualRouter` owns an isolated `TCP::Tcp` instance. The alternative — a shared
TCP stack with per-VRF socket namespaces — would require kernel namespace management,
leak TCP connection state between VRFs on failure paths, and make VRF teardown complex
(which VRF connections need to be closed when the VRF is destroyed?). With isolated
stacks, VRF destruction cleanly tears down all its TCP connections automatically.
BGP processes in different VRFs cannot accidentally share sockets. Multi-tenant
scenarios are naturally correct.

### Stack Structure

```
TCP::Tcp
└── TcpEngine*    (implementation object; full TCP state machine)
    ├── Listeners: map<ListenId, ListenState>
    ├── Connections: map<ConnId, ConnectionState>
    │   └── ConnectionState: TCP FSM per socket
    │       ├── State (SYN_SENT, ESTABLISHED, FIN_WAIT_1, ...)
    │       ├── TxBuffer (ring of linked blocks; commit/peek/consume)
    │       └── RxBuffer
    └── Timers (retransmit, TIME_WAIT, etc.)
```

### Connection API

```
Connection
├── reserveSpan(minBytes) → span<uint8_t>   ← zero-copy write
├── write(span<const uint8_t>) → size_t     ← copy bytes into TxBuffer
├── flush() → size_t                         ← transmit pending bytes
└── disconnect()
```

**Zero-copy write path**: `reserveSpan()` returns a writable view into the next
available region of `TxBuffer`. The caller fills the span in-place, then calls
`flush()`. No intermediate copy. `BgpTx` serializes BGP messages this way — it
reserves space, writes the message directly, then commits.

### TxBuffer

A linked ring of blocks supporting `reserveSpan`, `commit`, `peek`, `consume`, and
`spliceFrom`. The linked-block design avoids a single large circular buffer — blocks
can be varied in size, and `spliceFrom` enables O(1) composition of multiple protocol
messages for zero-copy scatter-gather sends.

### Callback Registration

BGP registers three `noexcept` static callbacks with the TCP engine. They receive a
`ConnCallbackCtx` with a `void* user` field pointing to the `BgpProcess`, then
enqueue FSM events onto the BGP scheduler. The TCP thread never calls protocol logic
directly — it only enqueues work.

---

## 13. Hardware — Ingress & Egress Pipelines

The hardware layer's design principle is that the routing code above it must never
know which backend is running. Two ingress backends (`IngressXdp` via AF_XDP, and
`IngressPacket` via TPACKET_V3) and two egress backends (`EgressPacket` via TPACKET_V2,
and `EgressSend` via plain `sendto`) all present the same interface. The factory tries
the high-performance backend first and falls back silently. The result is that the
same routing code runs correctly on a machine with full AF_XDP support or on a basic
VM with minimal kernel capabilities.

| Layer   | Fast backend              | Fallback backend              |
|---------|---------------------------|-------------------------------|
| Ingress | `IngressXdp` (AF_XDP)     | `IngressPacket` (TPACKET_V3)  |
| Egress  | `EgressPacket` (TPACKET_V2 mmap ring) | `EgressSend` (AF_PACKET sendto) |

### Ingress

#### IngressBase

`IngressBase` owns one RX thread per NIC queue. The thread polls frames, delivers
each to `Interface::processIngress`, and batches frame returns to minimize ring update
overhead. A batch of return calls is flushed to the kernel in a single store rather
than one per frame.

```
IngressBase
├── Interface&            iface
├── thread                ingressThread   (pinned to opts.cpuId if ≥ 0)
├── pollFrame(out FrameView) → bool       [pure virtual]
├── returnToDevice(index)                 [pure virtual]
└── onReturnFlush()                       [virtual — batch commit hook]
```

#### IngressXdp

AF_XDP (XSK) zero-copy ingress. The kernel delivers frames directly into a
user-allocated UMEM region — no copy ever occurs between NIC and user space.

Key design points:
- **Zero-copy**: binds with `XDP_ZEROCOPY` first; falls back to copy mode.
- **`XDP_RING_NEED_WAKEUP`**: kicks the kernel only when the flag is set, avoiding
  unnecessary syscalls.
- **Deferred fill ring commit**: frame returns are staged locally and committed to
  the kernel in a single atomic store per batch via `onReturnFlush()`.

#### IngressPacket

TPACKET_V3 block-based RX ring — the standard kernel AF_PACKET fast path.
`PACKET_FANOUT` is configured when `opts.fanoutGroup > 0`, enabling multiple RX
queues on the same interface to load-balance across cores.

### Egress

#### EgressBase

Owns the per-queue MPMC free ring tracking available frame slots (Vyukov
sequence-slot algorithm). All egress subclasses share this free ring management.

Frame layout per slot:
```
[ payload area: packetSize + MTU_PADDING(128) ][ alignment pad ][ PacketSlot ]
```

`PacketSlot` (32 bytes) carries frame index, DSCP/ECN/CoS markings, payload length,
flow hash, and class ID — everything the TX pipeline needs without touching the packet
payload.

#### EgressPacket

TPACKET_V2 memory-mapped TX ring. Frames are written directly into the mmap'd region;
a single `sendto(MSG_DONTWAIT, nullptr, 0)` kicks the kernel to transmit all queued
frames at once. `PACKET_QDISC_BYPASS` bypasses the kernel qdisc to reduce latency.

Reclaim uses a scan-cursor strategy to avoid O(N) cost on every frame allocation —
`onAllocNudge()` scans at most 64 slots, `waitWritable()` does a full scan.

#### EgressSend

Fallback: allocates a plain `posix_memalign`'d frame area and sends each frame via
`sendto(AF_PACKET)`. Incurs a syscall per frame and a kernel copy. Used when
`EgressPacket` construction fails.

---

## 14. Infrastructure — ARP & NDP

ARP and NDP serve the same role: given a next-hop IP address, find its MAC address
for the Ethernet header rewrite. They are kept separate from the FIB because
reachability (FIB) and L2 resolution (ARP) are logically independent. A route can be
present in the FIB but the ARP entry can be stale or missing — these are different
failure modes requiring different handling.

The ARP/NDP tables use `shared_mutex` rather than RCU. ARP entries are written
frequently — they age, get evicted, and are refreshed on every reply. RCU's deferred
free overhead is well-suited to very rare writes; for ARP churn, a shared_mutex is
simpler and the extra read-path cost is acceptable. ARP lookups happen in the egress
path after the FIB lookup, not in the innermost forwarding hot path.

When a next-hop MAC is not found, the packet is queued and an ARP/NS request is sent.
On reply, the queued packets are flushed. This prevents dropping packets solely due
to ARP cache miss on first-use.

### NDP (IPv6 Neighbor Discovery)

NDP mirrors ARP for IPv6, additionally handling Router Solicitation/Advertisement
for SLAAC and Duplicate Address Detection (DAD) for link-local address assignment.

---

## 15. Interface Layer

The interface layer solves a dependency inversion problem. Protocols need to know
about interface events (address added, link up/down) to start neighbors and originate
LSAs. But the `Interface` class should not know that BGP, OSPF, or EIGRP exist — that
would create a circular dependency between the interface layer and the protocol layer.

The solution is an event bus. `Interface` fires typed events (`IPv4_READY`,
`IF_DOWN`, etc.) into `InterfaceManager`, and protocols subscribe to the events they
care about at startup. `Interface` has no knowledge of its subscribers.

Per-protocol config (EIGRP hello interval, OSPF cost, passive mode) lives on
`Interface` itself rather than in the protocol's interface manager. This keeps the
config co-located with the thing it describes. Protocols access it via
`getEigrpConfig(as)` and `getOspfConfig()`, which lazily allocate `config::Reference`
blocks on first access.

### Interface

```
Interface
├── InterfaceConfigs            configs          (IPv4/IPv6 address lists, per-protocol state)
├── infrastructure::Arp         arp
├── infrastructure::Ndp         ndp
├── qos::egress::TxDistributor* tx               (egress queue handle)
├── atomic<bool>                shutdownFlag
├── atomic<bool>                carrierFlag
└── atomic<VirtualRouter*>      routingInstance  (lock-free VRF pointer)
```

**VRF reassignment** (`setVRF()`): tears down ARP/NDP/DHCP, removes the interface
from the old VRF's `InterfaceManager`, then attaches to the new one and restarts.

### InterfaceManager

One `InterfaceManager` per `VirtualRouter`. It owns the event bus for all three event
dimensions (state, IPv4, IPv6) and exposes `subscribe`/`unsubscribe` to protocols.

`notify()` is `private` and called only by `Interface` (declared `friend`). It
copies the matching callback list before invoking callbacks, so no lock is held
during protocol code — callbacks can safely call back into `InterfaceManager`.

### utils::EventManager

Generic event bus used by `InterfaceManager`. Enforces at compile time that:
- The event type is `enum class` with `uint8_t` underlying type
- The enum defines a `COUNT` sentinel as its last enumerator

`run()` snapshots the callback list under lock, releases the lock, then invokes
each callback — making it safe for a callback to register or unregister without
deadlock.

---

## 16. QoS

The QoS subsystem exists to solve two problems: how to assign NIC queues to CPU cores
efficiently, and how to enqueue packets from any thread without stalling the sender.

For the first problem, a dedicated consumer thread per TX queue is pinned to a
specific core. The TX ring buffer (mmap'd DMA region) stays hot in that core's L1/L2
cache. If producers called `send()` directly, the TX buffer would thrash between
cores on every enqueue, and `send()` would block when the NIC ring is full, stalling
the producer. The dedicated consumer absorbs backpressure — producers enqueue up to
ring capacity and move on.

For the second problem, the `FIFOQueue` uses the Vyukov sequence-number MPMC ring.
Multiple producer threads claim slots atomically without contending for a mutex.

### TxQueueManager

`TxQueueManager` manages the egress pipeline for each registered interface. It
queries the NIC's hardware TX queue count, assigns queues to CPU cores based on
policy, and creates/destroys `FIFOQueue` + `EgressBase` pairs as the CPU pool changes.

**CPU policies:**
- `CpuPolicy::EqualShare` — each interface gets `floor(cores / interfaces)` queues.
- `CpuPolicy::Weighted` — queue count proportional to `TxIfacePolicy::weight`.
- `txCoreBias` (0–1) — fraction of cores reserved for TX in a shared pool.

### RxQueueManager

Mirrors `TxQueueManager` for the ingress side. Uses the same CPU policies and
`reoptimize()` pattern. When `IngressPacket` (TPACKET_V3) is used, multiple RX
queues on the same interface share a `PACKET_FANOUT` group so the kernel
load-balances packets across them.

### BaseQueue

Abstract base for TX queues. Owns one `EgressBase` and one consumer thread.

The consumer uses a **double-drain pattern** to eliminate the missed-wake race:
1. Drain the queue fully.
2. Store `wakeSignal = 0` (signal intent to sleep).
3. Drain again (catch items enqueued between steps 1 and 2).
4. `futex_wait` only if `wakeSignal` is still 0.

A producer that enqueues between steps 2 and 4 stores `wakeSignal = 1`, causing
`futex_wait` to return immediately. This ensures no wake signal is ever lost without
requiring a lock.

### FIFOQueue

Concrete `BaseQueue` subclass. Implements a bounded MPMC ring using the Vyukov
sequence-number algorithm. Capacity must be a power of 2. Each slot is cache-line
aligned to prevent false sharing between producers writing into adjacent slots.

### TxDistributor

Routes frames across a `QueueState*[]` of per-CPU TX queues. Distribution policies:

| Policy | Behaviour |
|---|---|
| `BEST_EFFORT` | Always queue 0 |
| `FLOW_HASH` | `pkt->flowHash % N` — consistent per-flow ordering |
| `ROUND_ROBIN` | Atomic counter `% N` |
| `WEIGHTED_RR` | Rejection-sampling over `weights[]` |

---

## 17. Cross-Cutting Design Patterns

### Policy-Based Compile-Time Dispatch

**Why:** Runtime branching on address family (`if (afi == IPv6)`) would scatter
conditionals through every route processing path. Any new AFI requires auditing
every branch. The policy template approach generates a fully separate compiled
instance per AFI — each is complete, has no dead branches, and adding a new AFI
touches no existing code.

Used by: BGP address families, OSPF v2/v3 origination templates.

### std::variant for Discriminated Unions

**Why:** Virtual base classes require vtable pointers (8 bytes per object overhead),
heap allocation per entry, and pointer indirection on access. For collections with
thousands of entries (LSDB, AFI variant map), that overhead is measurable.
`std::variant` stores all types in a discriminated union with zero overhead, trivial
destructibility (enabling arena allocation), and exhaustive `std::visit` — a missing
case is a compile error.

Used by: OSPF `LsaBody`, BGP `AddressFamilyVariant`.

### Flyweight + RAII for BGP Attributes

**Why:** In a full BGP table, many routes share identical AS-PATHs and community
sets. Storing a full copy per route would make the table several times larger than
necessary. `AttributeManager` deduplicates by content hash; `RouteBase` RAII
maintains refcounts automatically through copy/move/destroy. No route can outlive
its attributes; no attribute set is freed while a route still references it.

### Lock-Free Queues with Sequence Counters

**Why:** A mutex-based queue serializes all producers behind one lock. The Vyukov
sequence-number MPMC ring lets N producers write into N different claimed slots
simultaneously — the only contention is on the atomic `head` increment. This scales
to many hardware RX threads posting events to protocol ProcessQueues without
measurable lock contention.

Used by: `SubQueue` (ProcessQueue), `FIFOQueue`, `EgressBase` free ring.

### RCU for Read-Heavy FIB

**Why:** The FIB is read on every forwarded packet but written rarely (only when
routes change). RCU makes the read completely lock-free at the cost of more complex
write semantics. For this access pattern — read on every packet, write on route
convergence — RCU is exactly the right tool.

### RouteWatcher Cross-Thread Safety

**Why:** `RouteWatcher` callbacks fire on the RIB's scheduler thread. Any protocol
running on a different scheduler must not touch its own state from that callback.
The pattern is: store a `ProcessQueueRef` in the callback context and immediately
`post` back to the protocol's own thread. BGP NHT uses this — `NhtCtx::bgpSched.post()`
— ensuring BGP AF state is only ever mutated from the BGP scheduler thread.

### Compile-Time CLI Command Matching

**Why:** A runtime dispatch table requires a separate registration step that can
drift out of sync with the command definitions. In the template system, the command
type IS its own parser — no registration, no sync problem, no possibility of a
command existing but never being reachable.

### Per-VRF TCP Isolation

**Why:** Shared TCP with kernel namespace management is complex and leaks state
across VRFs on failure paths. Isolated stacks mean VRF teardown automatically
closes all its connections; BGP processes in different VRFs cannot share sockets
regardless of misconfiguration.

### X-Macro for Mode Table

**Why:** Without an X-macro, the mode enum and the path/prompt arrays would be
defined in separate places and could drift out of sync. The X-macro defines the
mode table once and derives both the enum and the string tables from the same source.
Adding a mode is one line; the compiler catches any inconsistency.

---

## 18. Concurrency Model

Three rules govern all concurrency in this system:

1. **Hardware threads never touch protocol state.** RX threads do FIB lookups (lock-free
   RCU read) and deliver packets to the TCP engine or raw socket handler. They never
   call into BGP, OSPF, or EIGRP.

2. **Protocol state changes only on ThreadPool workers draining a ProcessQueue.**
   The TCP engine, timer thread, and hardware threads are producers only — they enqueue
   events and return. Protocol code never runs concurrently with itself for a given
   process.

3. **The timer thread only enqueues work.** It never executes protocol logic directly.
   Its latency is bounded; it is never blocked by protocol computation.

Breaking any of these rules introduces a parallel execution path to protocol state
machines and immediately requires locks on all protocol state — the exact problem the
`ProcessQueue` design was built to avoid.

```
Thread Roles:
┌───────────────────────────────────────────────────────────────┐
│ Hardware RX Thread(s)   (one per Interface, IngressBase::runLoop)
│   → poll NIC, parse Ethernet
│   → FIB lookup (lock-free RCU read)
│   → for control traffic: deliver to TCP::Tcp
│   → for forwarded traffic: pass to EgressBase
│   → NEVER acquires protocol state locks
└───────────────────────────────────────────────────────────────┘
┌───────────────────────────────────────────────────────────────┐
│ TCP Engine Thread       (TcpEngine internal)
│   → manages TCP state machines
│   → when data arrives for BGP: calls onReceiveCallback (noexcept)
│   → callback enqueues FSM event → ProcessQueue  (lock-free)
│   → NEVER calls BGP Session directly
└───────────────────────────────────────────────────────────────┘
┌───────────────────────────────────────────────────────────────┐
│ ThreadPool Workers      (N threads, global pool)
│   → drain ProcessQueue SubQueues
│   → execute protocol callbacks (FSM transitions, SPF, best-path)
│   → one queue drained at a time per protocol process
│   → serialized per-process; concurrent across processes
└───────────────────────────────────────────────────────────────┘
┌───────────────────────────────────────────────────────────────┐
│ Timer Thread            (TimeManager internal)
│   → fires timer callbacks
│   → calls ProcessQueueRef::post (never protocol code directly)
│   → bounded latency; never blocked by protocol work
└───────────────────────────────────────────────────────────────┘
```

**Per-component mutexes** (not global):
- `VirtualRouter::interfaceMutex` — protects interface list
- `VirtualRouter::eigrpMutex` — protects EIGRP map
- `ARP/NDP::shared_mutex` — protects neighbor tables

**Lock-free**:
- FIB lookup (RCU guard)
- ProcessQueue enqueue (sequence CAS)
- `RibBucket::fibEntry` swap (atomic exchange + RCU retire)
- `RouteWatcher::availableIds` (AtomicStack CAS)

**Deadlock prevention**: No component takes two locks simultaneously. Protocol layers
serialize through ProcessQueue — no mutex needed for FSM state. Hardware threads
never contend with protocol threads for the same lock.

---

*End of Architecture Reference*
