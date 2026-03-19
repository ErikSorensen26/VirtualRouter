# VirtualRouter

A software router written in C++23 targeting Linux x86-64. It implements a
multi-protocol control plane with an interactive Cisco-style CLI, a type-safe
configuration registry, VRF isolation, and a layered packet pipeline spanning
L2 through L4. The project is under active development.

---

## Feature Overview

| Component | Feature | Status |
|---|---|---|
| Routing | OSPFv2 / OSPFv3 | Implemented (not fully tested) |
| Routing | EIGRP classic & named mode | Implemented |
| Routing | BGP-4 | Partial (FSM + session layer active) |
| L3 services | ARP / Proxy ARP | Implemented |
| L3 services | NDP / SLAAC / DAD | Implemented |
| L3 services | DHCPv4 server, client, relay | Implemented (not fully tested) |
| L3 services | DHCPv6 server (stateful & stateless), client, relay | Implemented (not fully tested) |
| CLI | JSON-driven, multi-session | Implemented |
| VRF | Multiple independent routing instances | Implemented |
| Transport | TCP stack (used by BGP) | Implemented |
| Packet I/O | libpcap ingress | Implemented |

---

## Architecture

### Component hierarchy

```
Global
├── ControlScheduler      — control-plane timer/event dispatch
├── CliEngine             — command tree, session management
├── HardwareManager       — interface bring-up, libpcap ingress
├── QoS managers          — ingress / egress queues
├── DHCPv4 server         — global DHCPv4 process
├── DHCPv6 server         — global DHCPv6 process
├── KeyChainManager       — authentication key chains
└── VirtualRouter[]       — one per VRF
     ├── RIB / FIB        — independent per-VRF routing tables (IPv4 + IPv6)
     ├── Interface list   — per-VRF interface namespace
     └── Protocol instances
          ├── OspfProcess (OSPFv2, by process-ID)
          ├── OspfProcess (OSPFv3, by process-ID)
          ├── Eigrp       (classic, by AS number)
          ├── Eigrp       (named mode, by name)
          └── BgpProcess  (in development)
```

Source: `src/global/Global.h`, `src/global/VirtualRouter.h`

### VRF model

Each `VirtualRouter` instance holds a completely independent routing table,
interface namespace, and set of protocol processes. VRFs cannot share interfaces
or routing state. The default VRF cannot be deleted. Multiple EIGRP AS instances
and multiple OSPF process IDs are supported per VRF.

Source: `src/global/VirtualRouter.h`

### Interface model

Each `Interface` owns its own ARP and NDP handler, DHCP/DHCPv6 client, and
per-protocol configuration. IP address changes drive a small state machine
(INITIATE → IPCHANGE / IPREMOVAL → SHUTDOWN). The interface does not own
hardware queues—those are managed globally.

Source: `src/interface/Interface.h`

---

## Routing Protocols

### OSPF (OSPFv2 + OSPFv3)

`src/protocols/ospf/`

The OSPF implementation supports both OSPFv2 (IPv4) and OSPFv3 (dual-stack) as
separate process instances within a VRF.

**Process (`OspfProcess.h/.cpp`)**
- Manages multiple areas via an unordered map keyed by area ID
- Tracks router type flags: ABR and ASBR designation
- Maintains an external LSA database and summary address suppression table
- Handles default route insertion and process reset
- Reads router ID from config registry with fallback to interface addresses

**Areas (`area/`)**
- Per-area LSDB with LSA installation, aging, and flooding decisions
- Area types: backbone, stub, NSSA, standard
- Area ranges and inter-area prefix summarization with metric computation
- Flood manager with dedicated flood queue and type routing
- LSA originator for router-LSAs, network-LSAs, and summary-LSAs
- PMR allocator support for controlled memory use

**SPF engine (`spf/`)**
- Full Dijkstra SPF calculation
- Incremental SPF (iSPF) repair mode for delta updates
- Topology graph built separately from calculation (`SpfTopology`)
- Edge-change tracking for selective recalculation
- Cost overflow protection

**Constants** (`OspfTypes.hpp`): MAX_AGE 3600 s, REFRESH_AGE 1800 s, default
hello interval 10 s / 30 s.

> Testing note: the OSPF implementation has not been exhaustively tested and may
> contain edge-case bugs, particularly around area transitions and LSA flooding.

---

### EIGRP (classic & named mode)

`src/protocols/eigrp/`

EIGRP supports both the classic (AS-numbered) and named-mode (hierarchical)
configuration styles.

**Metric system** (`EigrpTypes.hpp`)
- Six K-values: K1 (bandwidth), K2 (load), K3 (delay), K4 (reliability),
  K5 (MTU), K6 (power). Defaults: K1=1, K3=1, others=0.

**Authentication**
- MD5 and SHA-256 per-interface or global

**Stub routing**
- Connected, static, summary, redistributed route advertisement control
- Receive-only mode, leak-map support

**Load balancing**
- Equal-cost multipath (`Balanced`)
- Unequal-cost load balancing via variance (`Minimum`, `MinimumAcrossInterfaces`)

**Topology & convergence** (`core/`)
- Topology table tracking (`Topology.h`)
- Route decision engine (`RouteManager.h`)
- Global route summarization (`GlobalAggregator.h`)
- Per-interface EIGRP state (`InterfaceManager.h`)
- Graceful restart / NSF support
- Stuck-in-active timer (default 90 s), route deletion timer (default 120 s)

**Reliable Transport Protocol** (`rtp/`)
- Reliable multicast and unicast packet delivery

---

### BGP-4

`src/protocols/bgp/`

BGP is under active development on the `Revamp` branch.

**What is implemented**
- Full RFC 4271 FSM (Idle → Connect → Active → OpenSent → OpenConfirm →
  Established) with all 31 events
- Session layer: active and passive TCP connections, collision detection,
  session timers (ConnectRetry, Hold, Keepalive, DelayOpen)
- TCP transport wrapper (`src/transport/tcp/`) used as the BGP transport
- Transmission layer: message parser (`BgpRx.cpp`) and builder (`BgpTx.cpp`)
  covering OPEN, UPDATE, NOTIFICATION, KEEPALIVE, ROUTE-REFRESH
- Address family framework (`af/AddressFamily.hpp`)
- RIB types: `LocRib`, extended communities, large communities, MP-reach with
  link-local next-hop, path attribute base including AIGP and weight
- Best-path decision engine (`decision/BestPath.cpp`): WEIGHT → LOCAL_PREF →
  AS_PATH length → ORIGIN → MED → neighbor type → IGP metric
- Config registries: `BgpRegistry.h`, per-neighbor and per-AF registries
- Neighbor table and per-AF neighbor configuration (`neighbor/`)

**What is not yet complete**
- Full RIB-to-FIB installation and interaction with the global routing table
- Route advertisement to peers (Adj-RIB-Out population)

---

## L3 Services

### ARP

`src/infrastructure/Arp.h/.cpp`

- Dynamic ARP cache with entry states: INCOMPLETE, COMPLETE, STALE
- Static ARP entries (never expire)
- Proxy ARP for foreign-network resolution
- Packet queuing per destination IP during resolution
- Configurable entry expiration (default 4 hours), probe interval (5 s),
  probe count (3)
- Incomplete queue depth limit (default 1024 entries)
- Optional acceptance of gratuitous ARP
- Thread-safe cache access via shared mutex

### NDP (IPv6 Neighbor Discovery)

`src/infrastructure/Ndp.h/.cpp`

- Neighbor cache with full NUD state machine: ACTIVE, REACHABLE, STALE,
  DELAY, PROBE, UNREACHABLE
- Static IPv6 neighbor entries
- Proxy NDP binding support
- Neighbor Solicitation / Advertisement (NS/NA) handling
- Router Advertisement / Solicitation (RA/RS) for prefix distribution
- Duplicate Address Detection (DAD)
- SLAAC (Stateless Address Auto-configuration)
- RA-Guard with MAC-based whitelist filtering
- ICMPv6 redirect handling
- Per-entry NUD timers and retry logic
- NSF integration: DAD suppression and convergence timers during restart
- Configurable reachable time (default 30 s), DAD timeout (1000 ms)

### DHCPv4

`src/services/dhcp/dhcpv4/`

- Server with address pool management (`IPv4Pool`) and lease tracking
  (`IPv4LeaseManager`)
- Full option/TLV handling via `DhcpTlvManager`
- Relay agent
- Client for interface auto-configuration

> Testing note: the DHCPv4 implementation has not been fully tested and may
> contain bugs in edge cases such as relay handling and lease renewal.

### DHCPv6

`src/services/dhcp/dhcpv6/`

- Stateful server with IA_NA and prefix delegation (IA_PD) support
- Stateless (information-request) mode
- IPv6 address pool (`IPv6Pool`) and prefix delegation pool (`PrefixPool`)
- Lease manager (`IPv6LeaseManager`)
- DHCP authentication manager (`Dhcpv6AuthManager`)
- Relay agent
- Client for IPv6 interface auto-configuration

> Testing note: DHCPv6 follows the same testing caveat as DHCPv4 above.

---

## CLI

`src/cli/`

The CLI is driven entirely by a JSON command tree loaded at startup from
`configs/Commands.json`. Commands are validated against `configs/ConfigSchema.json`.

**Modes supported by the engine**

| Mode tag | Protocol |
|---|---|
| `EIGRP_CLASSIC` | Classic EIGRP |
| `EIGRP_NAMED` | Named EIGRP |
| `OSPF` | OSPFv2/v3 |
| `BGP` | BGP-4 |
| `RIP` | RIP |

**Session model**
- `CliEngine` owns the command tree (immutable after `initEngine()`)
- Each `CliSession` has independent mode context and I/O abstraction
- Multi-session: multiple sessions can be active simultaneously
- Console abstraction supports terminal, UNIX socket, and file I/O

**Access modes**

| Flag | Behaviour |
|---|---|
| (default) | Interactive CLI session on stdin/stdout |
| `-u <path>` | `WebSessionManager` multiplexes Telnet-like sessions over a UNIX socket |
| `-D` | No CLI session; router runs as a silent daemon |

**Configuration persistence**
- `initEngine()` loads config, command tree, and schema from JSON
- `recoverState()` restores configuration from persistent history
- Supports CBOR and text JSON formats for config serialization

---

## Repository Layout

```
VirtualRouter/
├── CMakeLists.txt              ← root build (C++23, CMake ≥ 3.16)
├── Doxyfile                    ← Doxygen configuration
├── scripts/                    ← helpers: add_interface.sh, packet_flood.sh, etc.
├── docs/                       ← architecture diagrams (PNG), pcap captures
├── .devcontainer/              ← VS Code Dev Container (NET_ADMIN, host networking)
│
├── VirtualRouter/
│   ├── CMakeLists.txt          ← builds libVirtualRouter + VirtualRouterExec
│   ├── configs/                ← Commands.json, ConfigSchema.json, Configs.json
│   ├── common/                 ← shared internal utilities
│   ├── external/               ← vendored: json.hpp, pugixml
│   ├── types/                  ← system-level type shims
│   └── src/
│       ├── main.cpp
│       ├── global/             ← Global, VirtualRouter (VRF), ControlScheduler
│       ├── interface/          ← Interface, per-interface config
│       ├── protocols/
│       │   ├── bgp/            ← FSM, session, RIB, transport, neighbor, AF
│       │   ├── ospf/           ← process, areas, SPF, database, v2/v3
│       │   ├── eigrp/          ← core, topology, RTP, interface
│       │   └── rip/            ← stub only
│       ├── services/
│       │   └── dhcp/           ← DHCPv4/v6 server, client, relay
│       ├── infrastructure/     ← ARP, NDP, Ethernet
│       ├── transport/
│       │   ├── tcp/            ← TCP stack (BGP transport)
│       │   └── udp/
│       ├── cli/                ← CliEngine, CliSession, parser, modes
│       ├── configs/registry/   ← type-safe config registries
│       ├── hardware/           ← HardwareManager, ingress/egress pipelines
│       ├── qos/                ← QoS ingress/egress
│       ├── security/keys/      ← key-chain management
│       ├── packet/headers/     ← packet header definitions
│       ├── types/              ← IPAddress, IPPrefix, AddressFamily, RCU
│       └── web/                ← WebSessionManager (UNIX-socket session mux)
│
├── Tests/
│   ├── CMakeLists.txt
│   ├── TestSelection.cpp       ← interactive TUI test runner
│   └── Internal/src/          ← unit tests (DHCP, configs, IP pool, …)
│
├── Utils/                      ← auxiliary build target
└── Debugger/                   ← crash/backtrace tool
```

---

## Requirements

| Requirement | Detail |
|---|---|
| OS | Linux x86-64 |
| CMake | ≥ 3.16 |
| C++ standard | C++23 (required) |
| libpcap | via PkgConfig |
| OpenSSL | `OpenSSL::SSL`, `OpenSSL::Crypto` |
| libcurl | via CMake `find_package` |
| zlib | via CMake `find_package` |
| libtelnet | `/usr/lib/x86_64-linux-gnu/libtelnet.so` |
| pthread | direct link |
| libatomic | `-latomic` |
| GTest / GMock | tests only |

---

## Build

### Standard build (debug)

Compiles with `-O0 -g3 -gdwarf-4 -fno-omit-frame-pointer` and full warnings.
Also builds `Utils/` and `Tests/`.

```bash
cmake -B build
cmake --build build -j$(nproc)
# Output: build/VirtualRouterExec
```

`configs/` and `dir/` are copied to the build directory automatically.

### Static build

Produces a single fully-static binary. Requires pre-built static archives in
`/opt/static-libs/` for libpcap, OpenSSL, libcurl, and zlib.

```bash
cmake -B build -DBUILD_STATIC_ROUTER=ON
cmake --build build -j$(nproc)
```

### Strip debug symbols

`ENABLE_DEBUG_SYMBOLS` is `ON` by default (adds `-g -rdynamic` for crash
backtraces). Disable for a stripped build:

```bash
cmake -B build -DENABLE_DEBUG_SYMBOLS=OFF
```

### Virtual test interfaces

```bash
sudo bash scripts/add_interface.sh
```

Creates dummy kernel network devices for testing without physical hardware.

---

## Run

```
Usage: VirtualRouterExec [options]

  -h, --help                Show this help message and exit
  -u, --unix <PATH>         Enable UNIX socket API, bind to PATH
  -D, --no-default          Disable startup CLI session (daemon mode)
  -c, --config <FILE>       Active writable config file (running config)
  -s, --startup-config <F>  Read-only startup config file (bootstrap only)
  -H, --hw-config <FILE>    Hardware config file (interfaces, bindings)
  -d, --debug               Enable debug mode (crash logging via backtrace)
```

**Interactive session with config files**

```bash
build/VirtualRouterExec \
    -H hardware.json \
    -s startup-config.json \
    -c running-config.json
```

**UNIX socket daemon**

```bash
build/VirtualRouterExec -D -u /run/virtualrouter.sock
```

---

## Testing

**Framework:** Google Test + Google Mock

```bash
# Build (included in normal build)
cmake -B build && cmake --build build -j$(nproc)

# Run all tests via ctest
ctest --test-dir build

# Interactive TUI runner
build/TestSelection
```

`TestSelection` provides a terminal menu with collapsible test groups and a
persistent default-suite preference.

### Unit tests

Located in `Tests/Internal/src/`:

| Test file | Coverage |
|---|---|
| `DhcpServerTest.cpp` | DHCPv4 server |
| `DhcpClientTest.cpp` | DHCPv4 client |
| `DhcpRelayTest.cpp` | DHCP relay agent |
| `Dhcpv6ServerTest.cpp` | DHCPv6 server |
| `ConfigsTest.cpp` | Configuration registry |
| `IPPoolTest.cpp` | IP address pool |

Reference pcap captures for manual verification are in `docs/` (EIGRP, OSPF,
BGP, DHCP sessions).
