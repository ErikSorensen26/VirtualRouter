# VirtualRouter — Architecture

---

## Table of Contents

- [Design Philosophy](#design-philosophy)
- [Subsystem Architecture](#subsystem-architecture)

---

## Design Philosophy

> **The data plane must never yield to the control plane.**
>
> Constraint context: Forwarding happens on nanosecond timescales. Route updates, SPF runs, and BGP best-path recalculations happen on millisecond timescales. Any synchronization point between the two domains introduces unbounded jitter — a routing protocol can't be allowed to stall a packet mid-flight.
>
> Mechanism: The FIB is protected by RCU. A forwarding thread takes one memory barrier and reads without acquiring anything. When a route changes, the control plane copies the new FibEntry to the heap, atomically swaps the pointer, and defers freeing the old one until all active readers drain. A full BGP convergence has zero impact on forwarding throughput — the hardware threads don't know the control plane exists. Trade-off: writers pay a copy-and-retire cost, which is fine because route convergence is rare and the read path is on every packet.
>
> Status: This is a foundational invariant and not a tunable optimization.

> **Abstractions must cost nothing at runtime.**
>
> Constraint context: Every unnecessary indirection compounds across millions of packets per second. A virtual dispatch or a hash-map lookup that could have been a compile-time decision is CPU that could have forwarded packets.
>
> Mechanism: Templates are used as a correctness and performance tool throughout. The CLI parser's match logic is generated at compile time via fold expressions — at runtime it's a linear chain of comparisons, no dispatch table. The config registry resolves field access by type tag at compile time, no string lookup, no runtime branching at read sites. BGP's address family system instantiates `AddressFamilyInstance<N>` separately per AFI/SAFI — the IPv4 and IPv6 paths are completely separate compiled objects with no dead branches in either. If a decision can be made at compile time, it is.
>
> Status: This is a foundational invariant and not a tunable optimization.

> **Type safety is a design tool, not a language feature.**
>
> Constraint context: The worst bugs in protocol software are the ones that compile, run, and produce wrong routing behavior silently — wrong config key returning a default value, wrong address family processing a prefix, an unhandled LSA type skipped without error. These are nearly impossible to catch in testing.
>
> Mechanism: The config registry makes it a compile error to read a field from the wrong registry scope. The CLI command system makes it a compile error to have an unhandled mode. BGP's address family variant makes it a compile error to handle a new AFI/SAFI at some `std::visit` sites but not others. OSPF LSA bodies are `std::variant`, so pattern-matching over LSA types is exhaustive by construction. All of these would be silent runtime failures in a less type-safe design. The type system enforces invariants the RFC states in English.
>
> Status: This is a foundational invariant and not a tunable optimization.

> **Protocol state is single-threaded; threading is structural.**
>
> Constraint context: Fine-grained locking on protocol data structures creates lock ordering requirements that are easy to violate, heisenbugs that only reproduce under specific scheduling, and forces every function to reason about what locks it holds. That complexity scales with protocol complexity and makes correctness unverifiable for anything as stateful as BGP or OSPF.
>
> Mechanism: Each protocol process serializes all mutations through a `ProcessQueue` — a lock-free MPSC ring managed by the `ControlScheduler`. Hardware threads, the TCP engine, and the timer subsystem are producers only. They enqueue closures and return immediately. A ThreadPool worker drains the queue one item at a time. Protocol code never runs concurrently with itself for a given process, so it needs no internal locks. The only mutexes in the system protect the narrow external boundaries: the VRF interface list, the ARP/NDP neighbor tables. Trade-off: cross-thread communication requires an enqueue rather than a direct call, adding one scheduler quantum of latency per event.
>
> Status: This is a foundational invariant and not a tunable optimization.

> **RFC compliance is the specification.**
>
> Constraint context: The system must interoperate with real Cisco, Juniper, and FRR instances without special-casing either side. Protocol approximations — simplified state machines, skipped edge cases, wrong timer semantics — produce interoperability failures that only surface under specific topologies or load conditions and are nearly impossible to debug after the fact.
>
> Mechanism: Each protocol implements the actual standard. OSPFv2 implements all 8 neighbor states, the exact DR/BDR two-pass election from RFC 2328 §9.4, sequence number rollover, and MaxAge flooding with correct purge semantics. BGP implements all 31 FSM events, all 11 best-path selection steps, AS4 path reconstruction per RFC 4893 §4.2.3, and capability negotiation per the OPEN message spec. EIGRP implements actual DUAL with the feasibility condition as written — not a simplified approximation.
>
> Status: This is a foundational invariant and not a tunable optimization.

> **VRF isolation is a first-class constraint, not a retrofit.**
>
> Constraint context: VRF support bolted onto a single-instance design requires wrapping every piece of global state with a VRF tag, auditing every code path for cross-VRF leakage, and accepting that teardown will have subtle ordering bugs. That audit never completes and the bugs never stop appearing.
>
> Mechanism: `VirtualRouter` is the root of all protocol state from day one. Each VRF has its own RIB, FIB, TCP stack, and protocol instances. There is no global protocol state anywhere. Adding a second VRF is instantiating a second `VirtualRouter`. Hardware interfaces are globally owned but VRF-attached — a NIC exists regardless of which VRF has claimed it, and ownership transfers via `Interface::setVRF()` in one well-defined operation. Trade-off: all protocol state is heap-allocated per VRF rather than statically allocated once.
>
> Status: This is a foundational invariant and not a tunable optimization.

---

## Subsystem Architecture

### Repository Layout

The directory structure encodes layering rules, not just organization. Code in
`hardware/` depends on kernel AF_PACKET and AF_XDP APIs and must not know
protocols exist. Code in `routing/` depends on VRF state and must not know
which I/O backend is running. A cross-include between these trees is an
immediately visible design violation — the directory boundary makes it auditable.

```
VirtualRouter/src/
│
├── core/                       VirtualRouter, RoutingTable
│   └── routing/                RIB, FIB, RouteWatcher, RibBucket
│
├── routing/
│   ├── bgp/                    RFC 4271 BGP
│   │   ├── af/                 AddressFamily template + per-AF instance
│   │   ├── decision/           BestPath comparator + DecisionEngine
│   │   ├── neighbor/           Neighbor, NeighborAf, NeighborTable
│   │   ├── rib/                LocRib, AttributeManager, RibTypes
│   │   ├── session/            Session, Fsm, SessionTimers, Capabilities
│   │   └── transport/          BgpRx, BgpTx
│   │
│   ├── ospf/                   OSPFv2 + OSPFv3
│   │   ├── area/               Area management
│   │   ├── interface/          OSPF interface state machine
│   │   ├── database/           LSDB with pmr optimization
│   │   ├── rib/                OSPF RIB + route types
│   │   ├── topology/           SPF computation
│   │   └── transmission/       Wire format encode/decode
│   │
│   └── eigrp/                  EIGRP classic + named mode
│       ├── rtp/                Reliable Transport + Neighbor FSM
│       ├── topology/           Topology table and metric types
│       └── interface/          Interface manager
│
├── cli/
│   ├── parser/                 Command<>, CliModeParser<>, FixedString
│   ├── modes/                  Mode enum, path table, contexts
│   ├── execution/              Executor<> — mode switching
│   └── runtime/                CliSession, I/O loop
│
├── configs/
│   ├── RegistryTypes.hpp       Field types and concepts
│   ├── SubRegistry.hpp         Hierarchical registry with masking
│   ├── FieldAccessor.hpp       Typed accessors with live notification
│   └── registry/
│       ├── global/             GlobalRegistry, VrfRegistry
│       ├── interface/          InterfaceRegistry, ArpRegistry, NdpRegistry
│       ├── policy/             RouteMapRegistry, AccessListRegistry, PrefixListRegistry
│       └── router/             BgpRegistry, OspfRegistry, EigrpRegistry, etc.
│
├── policy/                     Policy runtime engine (JIT — in design)
├── transport/tcp/              Tcp, TcpEngine, Connection, TxBuffer, Listener
├── hardware/
│   ├── ingress/                IngressBase, IngressXdp, IngressPacket
│   └── egress/                 EgressBase, EgressPacket, EgressSend
├── processing/                 Packet header dispatch layer
├── infrastructure/
│   ├── arp/                    ARP table + request/reply
│   └── ndp/                    NDP (IPv6 neighbor discovery)
├── interface/                  Interface abstraction (physical/loopback/SVI)
├── qos/                        TxQueueManager, RxQueueManager, egress policies
├── utils/                      ThreadPool, TimeManager, Logger
└── types/                      IPAddress, IPPrefix, AddressFamily
```

---

### System Overview

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                         CLI / Configuration Layer                            │
│     CliModeParser<> + Command<> templates  ·  RegistryDatabase<>             │
└──────────────────────────────────┬───────────────────────────────────────────┘
                                   │ ApplyFn callbacks on field change
┌──────────────────────────────────▼───────────────────────────────────────────┐
│                        Global System Controller                               │
│  ControlScheduler  ·  ThreadPool  ·  TimeManager  ·  Interface registry      │
└──────────────────────────────────┬───────────────────────────────────────────┘
                                   │ creates / owns VRFs
┌──────────────────────────────────▼───────────────────────────────────────────┐
│                         VirtualRouter  (= VRF)                                │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌──────────────────┐    │
│  │ BgpProcess  │  │ OspfProcess │  │    Eigrp    │  │  RoutingTable    │    │
│  │  (per AS)   │  │ (per procId)│  │  (per AS)   │  │  (RIB + FIB)    │    │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └────────┬─────────┘    │
│         └────────────────┴─────────────────┴──────────────────┘             │
│                            route install / withdraw                           │
└──────────────────────────────────┬───────────────────────────────────────────┘
                                   │ FIB lookup (RCU, lock-free)
┌──────────────────────────────────▼───────────────────────────────────────────┐
│                       Interface Layer + ARP/NDP + TCP                         │
└──────────────────────────────────┬───────────────────────────────────────────┘
                                   │
┌──────────────────────────────────▼───────────────────────────────────────────┐
│              Packet Processing Pipeline  (header dispatch)                    │
│         Ethernet → IP → L4 classify → local / transit / control split        │
└──────────────────────────────────┬───────────────────────────────────────────┘
                                   │
┌──────────────────────────────────▼───────────────────────────────────────────┐
│                     Hardware I/O  (per Interface)                             │
│         IngressBase ←→ RxQueue  ·  EgressBase ←→ TxQueue                     │
│         IngressXdp (AF_XDP) or IngressPacket (TPACKET_V3)                     │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

### 1. Global System Controller

#### Purpose

`Global` is the process-level. It owns everything that exists
independently of how many VRFs are running: the hardware interface registry,
`ControlScheduler`, `ThreadPool`, `TimeManager`, the CLI engine, and TX/RX
queue managers. It has to be separate from `VirtualRouter` because these
resources must exist before the first VRF is created and survive after the last
one is destroyed — you can't tear down the thread pool while a VRF is still
draining its queues.

#### Design Decision

Hardware interfaces are globally owned but VRF-attached. The VRF holds a
reference; `Interface::setVRF()` handles all teardown and re-attach in one
place.

Rejected alternative: VRF owns its interfaces. Moving an interface between VRFs
would require coordinating teardown across two VRF objects at the same time with
no clean synchronization point. The interface reassignment would have to tell the
old VRF to tear down ARP, NDP, and protocol neighbors, and tell the new VRF to
set them back up, all while the interface itself might be in an inconsistent
state. `setVRF()` on the interface is the only place that knows the full sequence.

#### Invariants

- Hardware interfaces are registered in `Global` before any VRF may claim them.
  A VRF that tried to attach an unregistered interface would be operating on a
  resource the system has no record of — memory it doesn't own.
- `Global` is destroyed last, after all `VirtualRouter` instances. Reversing this
  order would tear down the ThreadPool while protocol queues are still draining,
  causing use-after-free in every in-flight callback.
- The `ThreadPool` and `TimeManager` are shared across all VRFs. No per-VRF
  thread management exists; adding it would multiply OS thread count by VRF count
  and complicate scheduler fairness with no benefit.

---

### 2. VirtualRouter — The VRF Boundary

#### Purpose

`VirtualRouter` is the root of all per-VRF state: RIB, FIB, TCP stack, and
protocol instances. Keeping it separate from `Global` isn't just organization —
it's the mechanism that makes VRF isolation correct. A route installed in VRF A
is stored inside that VRF's `RoutingTable`; there is no code path by which it
can appear in VRF B's FIB.

#### Design Decision

`VirtualRouter` fully owns all protocol instances. Destroying one destroys all
its protocols in a well-defined order with no external coordination needed.

Rejected alternative: Globally owned protocol instances referenced by VRFs.
This approach requires the caller to know which protocols exist in a VRF at
teardown time and call teardown on each individually. Adding a new protocol
would silently break VRF teardown for anyone who didn't update the teardown
code — and the bug would only show up under specific timing as a use-after-free.
Full VRF ownership means `~VirtualRouter()` gets it right automatically.

**Router ID calculation** (`calculateRID`): Scans all interfaces for the highest
IPv4 address on a loopback, then falls back to the highest Ethernet IPv4 if no
loopback exists. This matches Cisco IOS RID election and gives deterministic IDs
on stable topologies without requiring manual configuration.

#### Invariants

- No protocol state exists outside a `VirtualRouter`. Code that reaches for
  protocol state without going through the VRF boundary is either wrong or a
  design violation.
- Destroying a `VirtualRouter` is sufficient and complete teardown. Callers don't
  need to know which protocols were running or in what order to stop them.
- Adding a second VRF requires instantiating a second `VirtualRouter` — no other
  code changes. Any design where this statement is false has leaked global
  protocol state somewhere.

---

### 3. RIB / FIB / RouteWatcher

#### Purpose

This subsystem maintains the canonical view of which routes exist (RIB), selects
the best among competing protocol sources, and presents the result to the
forwarding path as a lock-free lookup structure (FIB). RouteWatcher gives
protocols a push notification when prefixes they care about change, without
polling.

These three pieces are distinct because they operate at different timescales and
under different concurrency requirements. The RIB mutates under protocol-queue
serialization. The FIB is read lock-free on every forwarded packet. RouteWatcher
callbacks fire on the RIB's scheduler thread and must not touch protocol state
directly. Merging any two of these would either break the forwarding path's
lock-free guarantee or force the RIB to understand each protocol's threading
model.

#### Design Decision

**RIB is template-parameterized over address type** — `Rib<uint32_t>` for IPv4,
`Rib<__uint128_t>` for IPv6. Each (prefix, length) pair owns a `RibBucket` with
all competing source routes. After every add/remove, `selectBest()` scans by
admin-distance then metric, copies the winner to a new heap `FibEntry`,
atomically swaps it into the RCU-protected FIB pointer, and retires the old one.

Rejected alternative: A single RIB with runtime AF flags. Branching on `if (af
== IPv6)` in every route operation makes it impossible for the compiler to catch
type mismatches across address families. Every new AF would require auditing
every conditional. The template approach generates completely separate, type-safe
code paths per AF with the compiler enforcing correctness at each call site.

**FIB is RCU-protected.**

> **Decision: RCU for the FIB**
>
> Constraint: The FIB is read on every forwarded packet at nanosecond timescales. Even an uncontended mutex costs hundreds of nanoseconds for the acquire/release pair and forces all forwarding threads to serialize behind the same lock.
>
> Mechanism: Reads take one memory barrier and proceed. Writes copy the new entry, atomically swap the pointer, and retire the old copy after all active readers drain. Writer cost scales with entry size, not with reader count.
>
> Trade-offs: Writers are more expensive than a mutex write. This is acceptable because FIB writes happen only on route convergence — rare relative to packet arrivals.

**RouteWatcher provides three watch modes:**

- `watchRoute(prefix, length)` — fires when the best route for a specific prefix
  changes. Used for redistribution.
- `watchAddress(addr)` — LPM watch that automatically re-pins to the next less-
  specific prefix when the covering route withdraws. Used for BGP NHT so that
  next-hop tracking follows supernet fallbacks transparently.
- `watchProtocol(source, id)` — fires when any best route from a specific
  `(RouteSource, processId)` pair changes. Used for cross-protocol redistribution.

Callbacks return `bool`; `true` removes the watch. They fire on the RIB's
scheduler thread, which means protocols on different schedulers must post back to
their own queue before touching their own state. Violating this is a data race —
for BGP specifically it would require adding locks to the full Adj-RIB-In,
Loc-RIB, and NHT data structures to guard against concurrent access from both
the BGP ProcessQueue consumer and the RIB scheduler thread.

Rejected alternative: Polling for next-hop reachability. Even at 1-second
intervals, a stale next-hop goes undetected for up to a full second. For BGP NHT,
that means advertising routes with unreachable next-hops to peers for a second
after withdrawal. RouteWatcher reacts within one scheduler quantum.

#### Invariants

- FIB reads never acquire any lock. Any code that acquires a lock on the FIB
  read path would serialize forwarding threads and defeat the RCU design.
- A `FibEntry` pointer obtained under an RCU guard is valid for the lifetime of
  that guard. Accessing a FibEntry after the guard exits is undefined behavior —
  the old entry may have been freed.
- `selectBest()` runs to completion before any new add/remove on the same bucket
  is processed. Interleaving them would produce a FIB entry that reflects neither
  the old nor new state correctly.
- RouteWatcher callbacks never touch protocol state directly. Any protocol that
  mutates its own state inside a RouteWatcher callback is introducing a second
  execution context alongside the ProcessQueue consumer, which requires locks on
  everything that callback touches.

---

### 4. BGP

#### Purpose

BGP implements RFC 4271 with RFC 4893 (4-byte AS), RFC 2918 (route refresh), and
MP-BGP (RFC 4760). It's a separate subsystem because it needs a full TCP-based
session FSM, per-AFI Adj-RIB-In/Out pipelines, an 11-step best-path decision
engine, and flyweight path attribute storage — none of which overlap with OSPF
or EIGRP.

#### Design Decision

**Session and address family are fully decoupled.** The session drives the FSM
and delivers parsed messages. Address family instances (`AddressFamilyInstance<N>`)
consume those messages independently through their own pipeline.

Rejected alternative: A session class with AF-specific branches. Every FSM
transition site would have to know which AFs the session is carrying, and every
new AFI would require auditing all those sites for missed cases. The current
design makes adding an AFI a pure addition — one new NLRI policy struct, one new
instantiation, zero changes to existing code.

**Per-AFI logic is compile-time policy.** `AddressFamilyInstance<N>` is
specialized separately for IPv4 unicast, IPv6 unicast, VPNv4, VPNv6. Each is a
fully separate compiled instance. `AddressFamilyVariant` is a `std::variant` of
all instantiations; `std::visit` enforces that every visit site handles every
variant member.

**FSM** implements RFC 4271 §8:
```
IDLE → CONNECT → ACTIVE → OPEN_SENT → OPEN_CONFIRMED → ESTABLISHED
         ↑___________________________|
```
Additional events: `ROUTE_REFRESH`, `BFD_DOWN/UP`, `MAX_PREFIX_REACHED`.
Collision detection: higher Router ID wins, lower gets `CEASE`.

**Inbound processing pipeline:** When an UPDATE arrives, ingress policy runs
first — AS-PATH loop detection, ALLOWAS_IN counting, and any configured
route-map. Routes that pass are inserted into Adj-RIB-In; withdrawals remove
them. The prefix limit is checked after insertion. Then the decision engine runs
for the affected prefix, selects the best path across all neighbors and locally
originated routes, installs the winner into the Loc-RIB and the global
RoutingTable, and recomputes Adj-RIB-Out to update what gets advertised to
peers.

**AttributeManager** deduplicates path attributes by content hash. Routes store
a `uint32_t pathId`. `RouteBase` RAII handles retain/release automatically.

> **Decision: Flyweight path attributes**
>
> Constraint: In a full BGP table, many routes share identical AS-PATHs. Storing a full attribute copy per route multiplies table size by the average sharing factor. Egress mutations (AS prepend) would then need to update every affected route individually.
>
> Mechanism: AttributeManager stores each unique attribute set once, keyed by content hash. Routes reference sets by `uint32_t pathId`. Egress mutations produce new entries rather than modifying existing ones. `RouteBase` RAII maintains refcounts through copy/move/destroy without requiring explicit management at call sites.
>
> Trade-offs: Hash-map lookup on attribute insertion and a refcount delta on route copy/destroy. Both are off the forwarding critical path.

#### Invariants

- The FSM is the only thing that transitions session state. External code that
  directly sets a session to ESTABLISHED or IDLE bypasses the RFC-mandated
  validation in OPEN_RECEIVED and will produce sessions with incorrect negotiated
  state.
- All BGP state mutations happen on the BGP ProcessQueue. Any external thread
  that touches BGP data structures directly is introducing a race with the
  ProcessQueue consumer and needs locks on everything it touches — which breaks
  the design invariant that protocol code is lock-free.
- No `RouteBase` outlives its referenced attribute set in AttributeManager. If
  a route is destroyed without releasing its pathId, the attribute set leaks. If
  a route holds a stale pathId after the attribute set is freed, it's a
  use-after-free.
- Every `std::visit` over `AddressFamilyVariant` handles all variant members.
  This is enforced at compile time — a missing case for a new AFI is a build
  error, not a runtime crash.

---

### 5. OSPF

#### Purpose

OSPF implements RFC 2328 (OSPFv2) and RFC 5340 (OSPFv3) as a dual-stack
implementation sharing one SPF engine and one neighbor state machine. It's
separate from BGP and EIGRP because link-state routing requires a flood-based
Link-State Database, DR/BDR election, and a Dijkstra computation — none of which
have any counterpart in distance-vector or path-vector protocols.

#### Design Decision

**OSPFv2 and OSPFv3 share one algorithmic core**, parameterized by `PolicyV2`/
`PolicyV3` for wire format differences.

The two protocol versions are similar enough — same neighbor state machine, same
SPF algorithm, same flooding rules — that a separate implementation for each
would mean rewriting most of the protocol logic twice. The template parameter
isolates exactly what differs between them: LSA encoding, address formats, option
bits. Everything algorithmic lives in one place.

**LSA bodies are `std::variant`**, not virtual classes.

> **Decision: `std::variant` for LSA bodies**
>
> Constraint: The LSDB holds thousands of LSAs. Virtual dispatch adds a vtable pointer per LSA (8 bytes), a heap allocation per LSA body, and produces non-exhaustive dispatch — a new LSA type that's missing from a `switch` compiles and runs incorrectly.
>
> Mechanism: LSA bodies are `std::variant<RouterLsa, NetworkLsa, SummaryLsa, ...>`. `std::visit` is exhaustive — a missing case for a new LSA type is a compile error. Storage is inline in the variant, no extra heap allocation.
>
> Trade-offs: Adding a new LSA type requires updating every `std::visit` site. That's the point — it prevents forgetting one.

**SPF** (Dijkstra) runs on the LSDB and feeds `OspfRib`, which installs routes
into the VRF's `RoutingTable`. `OSPF_LSDB_USE_PMR=1` enables `std::pmr` arena
allocation for LSA storage during SPF — the arena is bulk-freed after SPF
completes, which is the right trade for a "compute then discard" pattern.

**Neighbor FSM** follows RFC 2328 §10:
```
DOWN → ATTEMPT → INIT → 2WAY → EXSTART → EXCHANGE → LOADING → FULL
```
DR/BDR election uses the full RFC 2328 §9.4 two-pass algorithm. On neighbor
DOWN, all LSAs from that router are MaxAge-flooded via `area.flushNeighborLsas`.

**OSPFv3** wraps two `OspfProcess` instances (one per IPv4/IPv6 AF) under a
single `OspfV3Instance`, following the RFC 5340 model where OSPFv3 is
address-family-agnostic at the protocol level.

#### Invariants

- Every LSA type that appears in the LSDB is handled at every processing site.
  An unhandled LSA type means SPF silently ignores a router or network link,
  producing incorrect topology and wrong routes.
- SPF runs on a consistent LSDB snapshot. Any LSDB mutation during SPF would
  produce a shortest-path tree over an inconsistent graph — the resulting routes
  are undefined.
- Neighbor FSM transitions follow RFC 2328 §10 exactly. Skipping states (e.g.,
  going straight from 2WAY to FULL) bypasses the DBD exchange and LSR process,
  guaranteeing a desynchronized LSDB.
- All OSPF state mutations happen on the OSPF ProcessQueue. External threads
  touching OSPF state directly create races with SPF computation and neighbor
  FSM transitions.

---

### 6. EIGRP

#### Purpose

EIGRP implements the Diffusing Update Algorithm (DUAL) for loop-free convergence
using only neighbor-local information. It's separate from BGP and OSPF because
DUAL requires per-neighbor query tracking, a feasibility condition enforced on
every topology update, SIA timer escalation, and its own reliable transport
protocol over UDP multicast — none of which share code with link-state or path-
vector protocols.

#### Design Decision

**DUAL** enforces the feasibility condition on every topology update. A route
is only a Feasible Successor if the neighbor's Reported Distance is strictly
less than this router's current Feasible Distance. This guarantees the backup
is loop-free without running SPF.

**Active/Passive state machine:**
```
PASSIVE (successors exist)
    │  successor lost, no feasible successor
    ▼
ACTIVE (queries sent to all neighbors; SIA timer running)
    │  all replies received
    ▼
PASSIVE (new successor, updated FD)
```
SIA escalation: after `MAX_SIA_RETRIES` without reply, a SIA-QUERY goes out.
If that also times out, the non-responding neighbor is torn down. This prevents
the topology from staying stuck indefinitely if a neighbor becomes unreachable
mid-query.

**RTP (Reliable Transport Protocol) over UDP multicast.**

EIGRP sends control traffic to the EIGRP multicast group. RTP runs on top of
that, adding per-neighbor reliability for packets that need it. Hellos don't —
they're unreliable and never retransmitted. Everything else (Updates, Queries,
Replies, ACKs) uses per-neighbor sequence tracking, exponential backoff, and
Jacobson/Karels RTT estimation.

**Composite metric** is computed from bandwidth and delay as the primary inputs,
with load, reliability, and MTU available through the K-value weighting system.
128-bit intermediate precision prevents overflow in the calculation. The default
K-value configuration weights only bandwidth and delay, which is the standard
operating mode.

Classic mode and named mode share the same `Eigrp` implementation. The only
difference is config structure at the CLI level, resolved before reaching any
protocol code.

#### Invariants

- The feasibility condition is checked on every topology update before installing
  a route as a Feasible Successor. Skipping this check allows routing loops —
  DUAL's loop-freedom proof depends entirely on the FC holding universally.
- A neighbor is never declared UP before K-value validation. K-value mismatches
  that slip through create neighbors with incompatible metric calculations,
  producing routes that both sides compute differently.
- `sendFullTopology()` is called exactly once per neighbor transition to UP. A
  second call sends duplicate updates; skipping it leaves the new neighbor with
  an incomplete topology table.
- All EIGRP state mutations run on the EIGRP ProcessQueue. External threads
  touching topology state directly race with DUAL's active/passive transitions.

---

### 7. ControlScheduler & ProcessQueue

#### Purpose

`ControlScheduler` is the mechanism that makes all protocol state machines
lock-free. It transforms asynchronous multi-producer task posting into a single
sequential stream of work per protocol process. Without it, every protocol would
need to protect its own state with mutexes, and the complexity of getting lock
ordering right across FSM transitions, timer callbacks, and TCP events would be
unmanageable. It's global — owned by `Global` and alive for the process lifetime
— because the serialization guarantee has to be enforced externally. A process
can't enforce the boundary it depends on.

#### Design Decision

**Serialization via a per-queue drain state machine, not a dedicated thread.**

`ControlScheduler` has no thread of its own. Each queue carries one atomic
`runState` word with four states — `IDLE`, `SCHEDULED`, `RUNNING`,
`RESCHEDULED`. A poster publishes its task into the queue's ring, then reads
`runState` **with an atomic RMW** and acts on what it sees: `IDLE` → claim
`SCHEDULED` and submit one drain task to the ThreadPool; `RUNNING` → flag
`RESCHEDULED` so the running drain loops again; `SCHEDULED`/`RESCHEDULED` →
nothing, a pass that is obligated to see the item is already due. The drain
claims `RUNNING` on entry, consumes until empty, and exits by CAS-ing back to
`IDLE`; if a poster flagged `RESCHEDULED` in the meantime the CAS fails and the
drain rescans instead of exiting.

Every transition on `runState` is a read-modify-write, never a plain load or
store. This is load-bearing, not style: an RMW is required to read the latest
value in the variable's modification order and to synchronize with the poster
that published the item. Two lost-wakeup bugs were reproduced on x86 where a
plain load (poster side) or a plain resume-store (drain side) let a fully
published task strand in the ring with the queue idle — at frequencies around
one in 10⁵–10⁶ posts. If you touch this protocol, keep every transition an RMW.

At most one drain runs per queue at any moment. Tasks execute strictly one at a
time in FIFO order. The thread that runs the drain can change between cycles —
the single-threaded invariant holds within a cycle, not across them.

Rejected alternative: A dedicated thread per queue or a single global scheduler
thread. A dedicated thread per queue sits idle most of the time and costs OS
resources proportional to how many protocol processes exist. A global scheduler
thread becomes a serialization bottleneck for all queues simultaneously.
Multiplexing over the shared `ThreadPool` gets single-threaded-per-queue
semantics with zero thread overhead for idle queues. Switching to mutex-based
queues would reintroduce lock contention on every enqueue from hardware RX
threads and the TCP engine.

Rejected alternative: Labeled sub-queues (up to 8 per queue, drained
round-robin) existed for priority separation but never gained a production
consumer. They were removed: each queue is exactly one task ring, which keeps
every concurrency argument one-dimensional.

**One handle class, two roles:** `ProcessQueue` is a single move-only handle
covering both ownership and borrowing. The handle returned by
`ControlScheduler::create()` owns the queue — its `reset()`/destructor closes
it, discards pending tasks, and recycles the slot. `ref()` returns a borrowing
`ProcessQueue` that can post but never destroys, and may safely outlive the
queue (posts through a stale handle are dropped). There is no separate ref
class; owner and borrower share one type and one lifetime protocol.

**Handle lifetime safety:** Every handle carries an `alive` flag and a
`pending` counter. `release()` (run by the destructor too) sets
`alive = false`, cancels the handle's outstanding timers, and blocks until
every task posted through it has executed or been discarded. After it returns,
no lambda from this handle touches protocol code — every protocol destructor
depends on this. Releasing from *inside* one of the handle's own tasks is
supported: the current task is exempted from the wait and remaining queued work
is pumped inline on the drain thread, so teardown-from-task cannot deadlock.

**Destruction safety:** Every thread touching a queue's ring memory (post,
drain, waitIdle) holds the slot's `accessors` guard. Destruction claims the
slot's generation with a single CAS — claim and invalidation are one atomic
step — then waits for `accessors == 0` before freeing the ring. Ring memory is
never reclaimed under a reader, and a stale handle can never post into a
recycled slot.

**Timer integration:** `ProcessQueue::postAfter()` registers a `DelayedSlot`
with the `TimeManager`. When the timer fires, `onTimerFired` claims the slot
and posts the delayed task through the normal `post()` path, so timer callbacks
arrive at the protocol process serialized with everything else. Fire and cancel
arbitrate through one atomic `genClaim` word — `(generation << 1) | claimed` —
so claiming a slot for a specific generation is a single CAS and a stale
`cancel()` can never hijack a slot that was freed and recycled in between. If a
fired task's trampoline cannot be posted because the ring is momentarily full,
the timer is re-armed a millisecond out rather than silently dropped. The
delayed-slot free list is a tagged (ABA-proof) Treiber stack.

#### Invariants

- At most one drain task runs per `ProcessQueue` at any moment, enforced by the
  `runState` state machine. Two concurrent drains would produce two execution
  contexts for the same protocol process, requiring locks on all protocol state.
- All tasks in a queue execute one at a time in FIFO order. Protocol code that
  assumes this and has no internal locks is correct by design — break this
  invariant and it needs locks everywhere.
- Every `runState` transition is an atomic RMW. A plain load or store on this
  variable reintroduces a lost-wakeup race that strands published tasks.
- Timer callbacks arrive through the same `post()` path as all other events and
  are serialized with them. Any timer callback that bypasses the queue and calls
  protocol code directly is a race with the ProcessQueue consumer.
- After `ProcessQueue::release()` returns, no task posted through that handle
  executes any protocol code. Every protocol object that uses a borrowed handle
  to receive events relies on this to safely destroy itself.
- Queue ring memory is only freed after the generation is claimed and the
  slot's accessor count is observed at zero.

---

### 8. TimeManager

#### Purpose

`TimeManager` provides accurate one-shot and recurring timers. It needs its own
dedicated thread because timer accuracy requires continuous spinning on a priority
queue — sharing a thread with protocol work or queue draining would introduce
jitter proportional to protocol computation time, which is exactly the wrong
trade for a timer implementation.

#### Design Decision

Timer callbacks post to a `ProcessQueue` rather than executing protocol code
directly on the timer thread.

Rejected alternative: Calling protocol code from the timer thread. This makes
the timer thread a second execution context for every protocol state machine
alongside the ProcessQueue consumer. Every protocol data structure the timer
callback touches now needs a lock, reintroducing exactly what the ProcessQueue
design was built to eliminate.

> **Decision: Timer callbacks post to ProcessQueue**
>
> Constraint: Protocol state is single-threaded by design. The timer thread executing protocol code breaks that by creating a second thread for each process.
>
> Mechanism: The timer thread posts a closure to the target `ProcessQueue` handle and returns immediately. If a timer fires after cancellation is requested but before the cancel takes effect, the generation mismatch check in the callback catches it and exits without touching any protocol state.
>
> Trade-offs: Timer-driven reactions are delayed by one scheduler quantum instead of being immediate. Bounded timer thread latency is worth that — the alternative requires locks everywhere.

#### Invariants

- The timer thread never executes protocol code. If it did, every protocol
  would need locks to guard against concurrent access from both the ProcessQueue
  consumer and the timer thread.
- Timer thread latency is independent of protocol computation time. A long-running
  SPF or best-path computation has no effect on when the next timer fires.
- A cancelled timer never executes its callback, even if the timer fires between
  the cancel request and the actual TimeManager cancellation. The generation
  mismatch check in the callback handles this race deterministically.

---

### 9. Configuration Registry

#### Purpose

The configuration registry provides type-safe hierarchical storage for all
protocol and system configuration. It's separate from protocol implementations
because the same config fields need to be readable by the CLI layer, the protocol
engines, and live-notification callbacks, all with different lifetimes and
threading models. If each protocol managed its own config storage, the CLI would
need to know about every protocol's internal data structures to write anything.

#### Design Decision

**Type-tag keys over everything else.**

The design started with massive per-protocol structs with every field hardcoded.
That worked until inheritance entered the picture — you can't mask a struct field
against a parent struct field without writing bespoke traversal code for every
single field. The type-tag system replaced it entirely.

Each config field is 1–3 lines to define. The inheritance chain is handled
automatically by the masking system. Because everything is resolved at compile
time the CLI parser can be generated from the type structure directly rather than
from runtime inspection. Compile-time dispatch is also faster than any runtime
lookup. The tradeoff is debug info size — the template instantiations add to the
debug symbol table — but with how the registry is structured it has minimal
impact on actual binary size, and it's a one-time cost per field definition
rather than per access.

**Three FieldState values per field:**

- `INHERIT` — no local value; reads traverse the mask chain to the parent and
  return its effective value. Default for all maskable fields.
- `CANNED` — a local value is set, shadowing the parent. Reads return it without
  traversal.
- `UNSET` — no local value and no parent. Used by optional fields with no
  meaningful default.

The state is stored atomically on the field itself. Inheritance traversal needs
no lock and no allocation.

**General parent-child masking** (`SubRegistry::setMask(parent)`): Links one
registry to another of the same type. Every field in the child points to the
corresponding parent field for traversal. This works the same way for every use:

- BGP peer-groups: a neighbor registry masks a peer-group registry. Group fields
  flow to all members; member fields shadow the group.
- Interface config templates: a physical interface masks a named template. Shared
  settings live in the template; per-interface values shadow them.
- Any future hierarchical scope uses the same mechanism — the registry engine
  doesn't change.

Rejected alternative: Per-protocol inheritance logic. BGP would implement peer-
group lookup, OSPF would implement its own template lookup, and each
implementation would have different bugs and different semantics with no shared
enforcement.

**Live notification** (`ContextProvider` + `ApplyFn`): Protocol processes bind
themselves to a `ContextProvider` at startup. When a field's effective value
changes via `set()` or `unset()`, the registered `ApplyFn` fires immediately.
Structural changes to `OwnedListField<T, K, H>` fire applier `H`. The CLI layer
writes to the registry; it never calls protocol code directly.

**Field access:**
```cpp
// Optional field:
auto rid = proc.getConfigs().get<Config::Bgp::BGP_ROUTER_ID>();
if (rid.hasValue()) return rid.load();

// Required field:
bool gr = procCfg.get<Config::Bgp::BGP_GRACEFUL_RESTART>().load();
```

`get<Tag>()` dispatches through `FieldAccessor` at compile time. No hash lookup,
no runtime branching — the correct accessor type with the inheritance chain and
applier already wired in.

#### Invariants

- A field can't be accessed as a different type. Wrong-type access is a compile
  error, not an assertion failure at runtime when you're already debugging
  something else.
- A field from scope A can't be accessed through scope B. BGP code that
  accidentally reads an OSPF field is a compile error, not a subtly wrong value.
- An `AtomicField` always produces a value from `load()` — either local or
  inherited. Protocol code that calls `load()` on an `AtomicField` never reads
  uninitialized memory regardless of whether the operator configured that field.
- Calling `load()` on an `OptionalAtomicField` without checking `hasValue()` first
  is a debug-mode assertion failure. Protocol code that skips the presence check
  can't silently interpret UNSET as a zero or false.
- The ApplyFn fires exactly when the effective value changes — neither more nor
  less. Protocol code that depends on notifications to react to config changes
  can't miss one or receive a spurious one.

---

### 10. Policy Engine

#### Purpose

The policy engine evaluates route-maps, access-lists, and prefix-lists — the
mechanisms through which operators control which routes are accepted, rejected,
and how they're modified. It's separate from individual protocol implementations
because the same route-map can be attached to multiple protocols simultaneously,
and evaluation must produce identical results regardless of which protocol
triggers it.

#### Design Decision

**JIT compilation per attachment point**, not an interpreter loop over the
registry schema at evaluation time.

When a route-map is attached to a protocol endpoint (e.g., `neighbor X route-map
POLICY in`), the engine compiles that specific (route-map, attachment-point) pair
into a specialized instruction set. The compiled set contains only the match
conditions and set actions that appear in the configured sequences. Branches for
unconfigured conditions are never generated.

Rejected alternative: Interpreter loop over the registry schema on every route
evaluation. An interpreter has to test every possible match condition on every
route regardless of what the policy actually uses. For a BGP router processing a
full-table convergence, that's evaluation overhead proportional to the total
schema size rather than to what's configured. A minimal policy gets the same
overhead as a maximal one.

> **Decision: JIT policy compilation**
>
> Constraint: Route-map evaluation is on the protocol hot path during convergence. An interpreter's overhead is proportional to the number of conditions the schema supports, not the number the operator configured.
>
> Mechanism: When a route-map is attached to an endpoint, the engine compiles the (route-map, attachment-point) pair into a specialized instruction set. Only the conditions in the configured sequences are included. The compiled set is cached and invalidated on config change.
>
> Trade-offs: Compilation takes time at attachment and on config change. Evaluation is faster for every route processed afterward. For a stable network, policy attaches once and evaluates thousands of times — the trade is clearly in favor of compiling.

**Current status:** The registry schemas (`RouteMapRegistry`, `AccessListRegistry`,
`PrefixListRegistry`) fully represent the configuration surface. The runtime
evaluator and instruction management architecture — storage, invalidation, and
how compiled sets are referenced from the protocol path — are in active design.

#### Invariants

- A compiled instruction set is invalidated and recompiled when the referenced
  route-map config changes. A stale instruction set that doesn't reflect current
  config would silently apply the wrong policy.
- Evaluation of a compiled instruction set produces the same result as sequential
  interpretation of the source sequences for the same input route. Any divergence
  is a correctness bug in the compiler.
- A route-map has no visible effect on route processing until compilation
  completes. Partially-compiled policy would apply some conditions but not others,
  producing inconsistent results mid-convergence.

---

### 11. CLI Engine

#### Purpose

The CLI parses operator commands and writes configuration to the registry. It's
separate from the registry because the two have different concerns: the registry
stores values and fires callbacks; the CLI handles mode navigation, tokenization,
command matching, and prefix semantics (`no`, `default`). The registry has no
knowledge of how values are set — only that they are.

#### Design Decision

**Commands are compile-time types, not runtime-registered callbacks.**

The CLI started as a full runtime system. It was a disaster — every new command
required writing a significant amount of boilerplate, matching logic had to be
duplicated or abstracted behind layers that made it harder to follow, and there
was no clean structure for handling argument capture and validation consistently.
The template approach replaced all of that. Adding a command is one type and one
line in the parser list. The match logic, argument capture, and dispatch are all
handled by the template machinery with no per-command boilerplate.

**`Command<Handler, Parts...>`** captures a command pattern at compile time.
Fixed tokens match by precomputed hash; `ARG` tokens capture and forward values.
`CliModeParser<Mode, Context, Commands...>` folds over the pack at runtime as a
linear chain with no virtual dispatch.

**Executor ping-pong buffer:** Two `Context<>` slots. `changeMode<M>` placement-
news the new context into the inactive slot and swaps the active index. Mode
switching is O(1), no heap allocation. `revert()` swaps back; the previous
context is still valid in its slot. `NavEntry` stores the mode, dispatch thunk,
and construction thunk — the back-stack reconstructs the exact context type
without RTTI.

**`negate` / `defaulted` flags** on `ContextBase`: The tokenizer sets flags when
it sees `no` or `default` rather than routing to separate command types. Each
handler inspects the flags. This halves the number of command definitions and
guarantees the positive and negative forms are always in sync — they're the same
type.

**X-macro for mode table:** The mode enum and path/prompt arrays are generated
from a single X-macro. A mode that exists in the enum but not in the array is a
compile error.

`FindParser<M>` is a compile-time lookup. Requesting a mode with no registered
parser is a build error — entering an unhandled mode is impossible.

#### Invariants

- Every CLI mode has exactly one registered parser, enforced at compile time.
  A mode without a parser could be entered by navigation but could never handle
  any input — it would silently accept or reject everything incorrectly.
- `changeMode<M>` never allocates heap memory. Mode switching inside the CLI I/O
  loop can't trigger allocation that might fail or stall.
- The positive and negative forms of every command share one type definition and
  can't diverge. A `no` form that silently no-ops when the positive form was
  broken is not an acceptable failure mode for network configuration.

---

### 12. TCP Transport Layer

#### Purpose

The TCP stack provides reliable ordered byte-stream delivery for BGP sessions
within a VRF. It's separate from BGP because connection lifecycle, retransmission,
and flow control are independent of BGP protocol logic, and the same stack is
available to other consumers within the VRF if needed.

#### Design Decision

**Per-VRF TCP stacks**, not a shared stack.

Rejected alternative: A shared stack with per-VRF socket namespaces. A shared
stack with VRF tags on every connection requires VRF teardown to enumerate all
connections belonging to that VRF and close them in order. Miss one and it leaks.
Tag one incorrectly and it closes the wrong connection. Per-VRF stacks mean
destroying the `VirtualRouter` closes every TCP connection automatically — there's
nothing to enumerate.

**Zero-copy write path via `TxBuffer`:**

> **Decision: Zero-copy BGP TX**
>
> Constraint: BGP generates large UPDATE batches during convergence. Copying each message to an intermediate buffer before transmitting multiplies serialization cost linearly with the number of prefixes announced.
>
> Mechanism: `reserveSpan()` returns a writable view into the next available region of `TxBuffer`. `BgpTx` writes the serialized message directly into the span and calls `commit()`. No intermediate copy. `spliceFrom` composes multiple messages in O(1) for scatter-gather sends.
>
> Trade-offs: The caller must know the message size before reserving. BGP message sizes are bounded by `kMaxMessageLen`, so this isn't a practical constraint.

**Callback integration:** BGP registers three `noexcept` static callbacks with
the TCP engine. Each callback receives a `ConnCallbackCtx` with a `void* user`
pointing to the `BgpProcess`, enqueues an FSM event onto the BGP ProcessQueue,
and returns. The TCP thread never calls into BGP directly.

#### Invariants

- All TCP connections in a VRF are closed when the VRF's `TCP::Tcp` is destroyed.
  Any connection that outlives its VRF holds a reference to destroyed state.
- BGP processes in different VRFs share no sockets. A misconfigured BGP process
  in VRF A can't accidentally send to a neighbor that belongs to VRF B.
- TCP callbacks never execute BGP state machine logic directly. A TCP callback
  that calls BGP code is running on the TCP thread and creates a race with the
  BGP ProcessQueue consumer.
- `TxBuffer` spans are committed or discarded before the next `reserveSpan`. A
  pending span that's abandoned leaves the buffer in an inconsistent state and
  corrupts all subsequent writes.

---

### 13. Packet Processing Pipeline

#### Purpose

The pipeline receives raw frames from hardware ingress and dispatches them to the
correct upper-layer handler. It exists as a distinct layer because neither the
hardware layer nor any individual protocol handler can own this classification —
hardware must not know protocols exist, and no single protocol handler knows all
traffic types.

#### Design Decision

Classification happens in a fixed sequence:

1. **Ethernet demux:** Identify EtherType (IPv4, IPv6, ARP, VLAN, etc.).
2. **IP classification:** Parse the IP header, identify the destination as local
   or transit.
3. **Protocol dispatch:**
   - Local control traffic → ARP/NDP handler, TCP stack (BGP), or raw socket
     (OSPF, EIGRP).
   - Transit traffic → FIB lookup, next-hop resolution, MAC rewrite.
4. **Interface association:** The receiving interface is tracked here for RPF
   checks and source-routing decisions.

The pipeline is stateless — it holds no per-flow state. Fragment reassembly and
stateful inspection are not performed here.

Rejected alternative: Classification inside the hardware ingress thread. This
couples kernel I/O details (AF_XDP ring layouts, TPACKET_V3 block offsets) to
dispatch logic. The pipeline provides a stable interface regardless of which
ingress backend delivered the frame.

#### Invariants

- Transit packets reach the FIB lookup without involving any protocol state
  machine. A pipeline that calls into OSPF or BGP for transit traffic breaks the
  separation between control and forwarding planes.
- Each control packet is delivered to exactly one handler based on EtherType, IP
  protocol, and destination. A packet delivered to two handlers or to the wrong
  one produces duplicate protocol messages or protocol state corruption.
- The pipeline holds no per-flow state. Any subsystem downstream that needs
  flow state must maintain it itself — the pipeline offers no session tracking.

---

### 14. Hardware I/O — Ingress & Egress

#### Purpose

The hardware layer moves raw frames between NICs and the processing pipeline. It
owns two independent backend pairs — AF_XDP or TPACKET_V3 for ingress, TPACKET_V2
mmap or plain `sendto` for egress — with the same interface on both sides. The
routing code above never knows which backend is running.

This can't be merged into the processing pipeline because the pipeline must be
backend-agnostic. AF_XDP and TPACKET_V3 have completely different ring layouts,
memory management, and kernel interaction patterns. Putting that complexity into
the pipeline would make it impossible to swap backends or run on kernels with
different capability sets.

#### Design Decision

**Factory with silent fallback.** The factory tries the high-performance backend
first; on failure it falls back silently. The routing code above never branches
on which backend is active.

**Ingress:**

`IngressXdp` (AF_XDP) delivers frames directly into user-allocated memory with
no copy between NIC and user space. Zero-copy mode is attempted first with a
fallback to copy mode on kernels that don't support it. Ring kicks are batched
to avoid unnecessary syscalls.

`IngressPacket` (TPACKET_V3) uses block-based ring delivery and supports
distributing load across multiple RX queues on the same interface for
multi-core scaling.

One RX thread per NIC queue. Frame returns are batched so that a group of
completions is flushed to the kernel in one operation rather than one per frame.

**Egress:**

`EgressBase` owns the per-queue MPMC free ring (Vyukov sequence-slot) shared
across all egress subclasses.

`EgressPacket` (TPACKET_V2) writes frames directly into the mmap'd ring and
kicks the kernel once to transmit everything queued, bypassing the kernel qdisc
for lower latency. Slot reclaim is bounded per allocation cycle to avoid O(N)
cost under load.

`EgressSend` is the fallback — it sends each frame individually via a kernel
socket call, incurring one syscall and one kernel copy per frame. Used when
`EgressPacket` fails to initialize.

#### Invariants

- No code above the hardware layer branches on which backend is active. Any such
  branch means the backend abstraction has leaked upward and the protocol code is
  now coupled to I/O implementation details it can't be coupled to.
- One RX thread exists per NIC queue; no two threads share the same queue state.
  Sharing a queue state without synchronization is a data race on every frame.
- Frame memory is never accessed after being returned to the ring. The ring slot
  is immediately available for kernel reuse on return — holding a reference to it
  is accessing memory the kernel now owns.

---

### 15. Infrastructure — ARP & NDP

#### Purpose

ARP and NDP resolve next-hop IP addresses to MAC addresses for Ethernet header
rewriting. They're separate from the FIB because reachability (FIB) and L2
resolution (ARP) are independent failure modes — a route can be in the FIB while
the ARP entry is stale or absent. Collapsing them would mean the same structure
needs to handle both nanosecond-scale FIB lookups and the much slower
request/reply cycle of ARP resolution.

#### Design Decision

**Split between a lock-free data-plane table and a scheduler-serialized control-plane cache.**

Both ARP and NDP maintain two structures. The data-plane table (`arpTable`,
`ndpTable`) is an `AtomicHashMap` containing only the IP→MAC mappings the
forwarding path needs. Reads are lock-free from any thread. The control-plane
cache (`arpCache`) holds the full entry state — timers, pending queues,
retry counts, NUD state. All mutations to the control-plane cache run on the
interface control scheduler, so no mutex is needed there either.

This means the forwarding path gets a lock-free ARP/NDP lookup comparable to
the FIB, while the control-plane complexity (aging, retransmit timers, queuing)
stays fully serialized without any locking.

**Packet queuing on miss:** When a next-hop MAC isn't known, the packet is queued
and an ARP request or Neighbor Solicitation is sent. On reply, queued packets
flush. ARP typically resolves in a few milliseconds. Dropping the packet and
waiting on the sender's retransmit timer — which for TCP is on the order of
seconds — is an unnecessary wait when the resolution delay is that short.

NDP additionally handles Router Solicitation/Advertisement for SLAAC and
Duplicate Address Detection for link-local address assignment.

#### Invariants

- A packet is never dropped solely because the ARP/NDP entry for its next-hop
  doesn't exist yet. Dropping first-packet to new destinations would break TCP
  connection establishment to any newly reachable host.
- Data-plane table reads (`arpTable`, `ndpTable`) are always lock-free. Any
  code that acquires a lock on the forwarding-path ARP/NDP lookup breaks the
  same separation between control and forwarding planes that the FIB RCU design
  enforces.
- All mutations to the control-plane cache run on the interface control
  scheduler. Any external thread that writes to `arpCache` directly creates a
  race with timer callbacks and resolution handlers.
- DAD completes before an address is used for protocol communication. Using a
  link-local address that fails DAD creates duplicate addresses on the segment,
  breaking neighbor discovery for that address.

---

### 16. Interface Layer

#### Purpose

The interface layer abstracts physical, loopback, and SVI interfaces and provides
the event bus through which protocols learn about interface state changes. It
can't be merged with the hardware layer because it manages per-protocol config,
ARP/NDP state, and event subscription — none of which belong in raw I/O. And it
can't hold direct protocol pointers because `Interface` would then depend on BGP,
OSPF, and EIGRP — a circular dependency that would make every protocol change
require touching the interface layer.

#### Design Decision

**Event bus, not direct protocol callbacks.**

Rejected alternative: `Interface` calls protocol methods directly on state
changes. This requires `Interface` to hold pointers to every protocol that might
care about its state. Adding a new protocol means adding a call site in
`Interface`. The list of pointers grows indefinitely and `Interface` becomes
coupled to every protocol's internal API. The event bus inverts this — protocols
subscribe to the events they care about, and `Interface` has zero knowledge of
its subscribers.

`InterfaceManager::notify()` copies the callback list before invoking, so no
lock is held during protocol callbacks. Callbacks can safely call back into
`InterfaceManager` without deadlocking.

**Per-protocol config lives on `Interface`**, not in the protocol's interface
manager.

Rejected alternative: Interface config in the protocol layer. Answering "what is
the OSPF cost of interface X?" would require two lookups: find the interface,
then find its OSPF config inside the OSPF process. Collocating config with the
interface it describes means one lookup via `getOspfConfig()` or
`getEigrpConfig(as)`.

**VRF reassignment** via `setVRF()` is the single point that handles interface
reassignment: tears down ARP/NDP/DHCP, removes from the old VRF's
`InterfaceManager`, attaches to the new one, and restarts. `IF_DOWN` fires
before detach, triggering neighbor teardown across all protocols through the
event bus — `setVRF` doesn't need to know which protocols are active.

#### Invariants

- `Interface` has no direct dependency on any protocol implementation. Any
  include of a protocol header from the interface layer is a coupling violation
  that makes the protocol undeletable without touching the interface layer.
- `IF_DOWN` always fires before an interface detaches from a VRF. Any protocol
  that relies on `IF_DOWN` to clean up neighbor state would leak that state if
  detach happened first.
- Protocol callbacks don't hold any `InterfaceManager` lock when they execute.
  A callback that tried to re-enter `InterfaceManager` while holding its lock
  would deadlock.

---

### 17. QoS

#### Purpose

QoS solves two problems: assigning NIC queues to CPU cores efficiently, and
giving forwarding threads a non-blocking path to egress. Without it, a forwarding
thread that sends directly to the NIC ring would either spin-wait when the ring
is full or drop packets — both unacceptable on a forwarding path. The queue
assignment policy (equal-share, weighted, core-bias) is a system-level decision
that needs to be configurable independently of which egress backend is running,
which is why it can't live inside the hardware layer.

#### Design Decision

**Dedicated consumer thread per TX queue**, not producers writing to the NIC
ring directly.

Rejected alternative: Producers call `send()` on the TX ring buffer directly.
The mmap'd ring thrashes between cores on every enqueue. When the ring is full,
`send()` blocks the forwarding thread. A dedicated consumer per TX queue pinned
to a specific core keeps the TX buffer in that core's L1/L2 cache. Producers
enqueue to the MPMC ring and return — backpressure is absorbed by the ring,
not by the forwarding thread.

> **Decision: Double-drain wake pattern for consumer threads**
>
> Constraint: A condvar requires a mutex on every enqueue/dequeue, acquired on the forwarding critical path. A naive "drain then sleep" pattern loses wake signals if items arrive between the drain completing and the sleep beginning.
>
> Mechanism: 1) Drain fully. 2) Store `wakeSignal = 0`. 3) Drain again to catch items enqueued in the window. 4) `futex_wait` only if `wakeSignal` is still 0. A producer that enqueues between steps 2 and 4 stores `wakeSignal = 1`, causing `futex_wait` to return immediately.
>
> Trade-offs: One extra drain pass per sleep cycle. Cheap compared to the mutex the alternative requires.

**CPU assignment policies** (`TxQueueManager`):
- `EqualShare` — `floor(cores / interfaces)` queues per interface.
- `Weighted` — queue count proportional to `TxIfacePolicy::weight`.
- `txCoreBias` (0–1) — fraction of cores reserved for TX in a shared pool.

`TxDistributor` routes frames across per-CPU TX queues by flow hash, round-robin,
or weighted round-robin.

#### Invariants

- A forwarding thread is never blocked by NIC ring fullness. A blocking forwarding
  thread stalls all packets behind it and introduces unbounded latency spikes.
  Backpressure must be absorbed by the MPMC ring, not by the forwarding path.
- Exactly one consumer thread drains each TX queue. Two consumers on the same
  queue produce out-of-order frame transmission and corrupt the ring state.
- Per-flow packet ordering is preserved within a single TX queue. Reordering
  within a flow triggers TCP retransmits and degrades throughput for any flow
  unlucky enough to have its packets reordered.

