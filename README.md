# VirtualRouter

A software router written in C++17 targeting Linux x86-64. Implements BGP, OSPFv2/v3, and EIGRP with VRF isolation, a lock-free forwarding plane, a virtual TCP stack, and a compile-time CLI parser.

---

## Status

| Component | Status |
|-----------|--------|
| OSPFv2 / OSPFv3 | Complete |
| EIGRP classic & named mode | Complete (redistribution not yet wired) |
| BGP-4 | Complete (route maps / redistribution not yet wired) |
| ARP / Proxy ARP | Complete |
| NDP / SLAAC / DAD | Complete |
| DHCPv4 server, client, relay | Implemented (not fully tested) |
| DHCPv6 server, client, relay | Implemented (not fully tested) |
| CLI (multi-session, Cisco-style) | Complete |
| VRF | Complete |
| TCP stack (BGP transport) | Complete |
| QoS queue disciplines | Scaffold |
| Multicast | Not started |
| Route maps / prefix lists | Not started |

---

## Requirements

| | |
|---|---|
| OS | Linux x86-64 |
| CMake | ≥ 3.16 |
| C++ | C++17 |
| libpcap | via PkgConfig |
| OpenSSL | `OpenSSL::SSL`, `OpenSSL::Crypto` |
| libtelnet | `/usr/lib/x86_64-linux-gnu/libtelnet.so` |
| pthread | direct link |
| GTest / GMock | tests only |

---

## Build

**Debug (default)**

```bash
cmake -B build
cmake --build build -j$(nproc)
# Output: build/VirtualRouterExec
```

**Static binary** (requires pre-built static archives in `/opt/static-libs/`)

```bash
cmake -B build -DBUILD_STATIC_ROUTER=ON
cmake --build build -j$(nproc)
```

**Without debug symbols**

```bash
cmake -B build -DENABLE_DEBUG_SYMBOLS=OFF
cmake --build build -j$(nproc)
```

**Virtual test interfaces**

```bash
sudo bash scripts/add_interface.sh
```

---

## Run

```
Usage: VirtualRouterExec [options]

  -h, --help                Show this help and exit
  -u, --unix <PATH>         UNIX socket API, bind to PATH
  -D, --no-default          Daemon mode (no CLI session)
  -c, --config <FILE>       Running config file (writable)
  -s, --startup-config <F>  Startup config file (read-only bootstrap)
  -H, --hw-config <FILE>    Hardware config (interfaces, bindings)
  -d, --debug               Enable crash logging via backtrace
```

```bash
build/VirtualRouterExec -H hardware.json -s startup.json -c running.json
```

---

## Testing

```bash
cmake -B build && cmake --build build -j$(nproc)

# Run all tests
ctest --test-dir build

# Interactive TUI runner
build/TestSelection
```

---

## Further Reading

[ARCHITECTURE.md](docs/architecture/ARCHITECTURE.md) — internal design reference covering the RIB/FIB, protocol internals, concurrency model, CLI engine, and config registry.

[GRAMMAR_SYNTAX.md](docs/architecture/GRAMMAR_SYNTAX.md) — complete JSON syntax reference for the CLI command grammar.
