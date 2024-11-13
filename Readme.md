chapter  1

ipv4, ipv6

dhcpv4
dhcpv6 stateless & statefull
slaac

dhcp relay
dhcp snooping

arp
proxy arp
NDP

cef
rib, fib ctr..

chapter 2








                    ┌─────────────< Logincal-Int <──────────────┐
        ┌────────> BridgePort ──> MPLS ────────> IPv4/ipv6 ───> Decapsulate
  In-Interface      ├──> Input ───┘├──> Input ───┘├──> Input ───┘└─> LocalIn
                   ↓├──> Forward  ↓├──> Forward  ↓├──> Forward        └─> RouterProcess
  Out-Interface     ├──< Output <─┐├──< Output <─┐├──< Output <─┐          └─> LocalOut
        └────> Encalsulation <─── BridgePort <── MPLS <──────── IPv4/ipv6 <─────┘  ↑│w
                    └─────────────> Logincal-Int ───────────────────────────────────┘


                    ┌───────────────────────────────────────────< Logincal-Int <──────────────────────────────────────────────┐
        ┌────────> BridgePort ────────────────────> MPLS ─────────────────────────────────> IPv4/ipv6 ─────────────────────> Decapsulate
  In-Interface      │   ┌─> Pre-Routing ──┐  Input ──┘└─> Decision            ┌─ Decapsulate │└─> Pre-Routing <─ Decrypt      │└─> LocalIn
                    └─> DTS-NAT ───> Decision ──┤          │└─> Pop-Label ──> Tunnel ────────┘     └──> Decision  └─> Policy ─┘     └─> RouterProcess
                              ┌── Firewall <── Forward     └─> Switch-Label                     Forward <──┘└─> Input ───┘               └─> LocalOut
                    ┌─ Ip <─ SRC-NAT <─ Output                  │                                └─> Post-Routing <── Output                   │  │
                    │  └─> Post-Routing └─> Decision ┐┌─────────┘                                  ┌──┘   Encryption  │                        │  │
  Out-Interface     ├───────┘                        │├──Push-Label <──────── Decision ──────┐┌─> Policy <─┘└─> Decision ─────┐                │ ↑│
        └────────> Encalsulation <───────────────── BridgePort <─────────────────────────── MPLS <────────────────────────── IPv4/ipv6 <───────┘  │
                    └───────────────────────────────────────────> Logincal-Int ───────────────────────────────────────────────────────────────────┘

PreRouting ──> Hotspot-In ──> Raw-Routing ──> Connection-Tracking ──> Mangle-Pre-Routing ──> DTS-NAT
Input ──> Mangle-Input ──> Filter-Input ──> HTP-Global ──> Simple-Queues
Forward ──> Decision ──> TTL-1 ──> Mangle Forward ──> Filter-Forward ──> Accounting
Output ──> Decision ──> Raw-Output ──> Connection-Tracking ──> Mangle-Output ──> Filter-Output ──> Router-Adjustment
Post-Routing ──> Mangle-Post-Routing ──> SRC-NAT ──> Hotspot-Out ──> HTB-Global ──> Simple-Queues




Here's an overview of how these networking tables are filled, which ones are filled at the same time, and when they are filled:

Routing Table

How Filled:
Static Routes: Manually configured by network administrators.
Dynamic Routes: Populated and updated automatically by routing protocols (e.g., OSPF, BGP, EIGRP, RIP).
When Filled:
Startup: Initial static routes are loaded.
Runtime: Dynamic routes are continuously updated as routing information is exchanged.
Filled At The Same Time: The Forwarding Information Base (FIB) is updated in sync with the routing table.

Forwarding Information Base (FIB)

How Filled: Derived from the routing table, optimized for quick lookup.
When Filled:
Startup: Initial static routes are loaded.
Runtime: Updated in real-time as the routing table changes.
Filled At The Same Time: Routing table updates trigger FIB updates.

Address Resolution Protocol (ARP) Table

How Filled:
On-Demand: Devices send ARP requests when they need to resolve an IP address to a MAC address.
Static Entries: Manually configured by network administrators.
When Filled:
On-Demand: As needed when devices communicate on the local network.
Startup: Pre-configured static ARP entries are loaded.

Neighbor Discovery Protocol (NDP) Table (for IPv6)

How Filled:
On-Demand: Devices send Neighbor Solicitation messages to resolve IPv6 addresses to MAC addresses.
When Filled:
On-Demand: As needed when devices communicate on the local network.
Startup: Pre-configured static NDP entries are loaded.

MAC Address Table

How Filled:
Dynamic Learning: Switches learn MAC addresses from incoming frames and associate them with the receiving port.
Static Entries: Manually configured by network administrators.
When Filled:
Runtime: Continuously updated as frames are received.
Startup: Pre-configured static MAC entries are loaded.

Routing Information Base (RIB)

How Filled:
Dynamic Routes: Populated by routing protocols (e.g., OSPF, BGP).
When Filled:
Runtime: Continuously updated as routing information is exchanged.
Filled At The Same Time: Routing table updates based on RIB information.

Policy-Based Routing (PBR) Table

How Filled:
Manual Configuration: Network administrators define routing policies.
When Filled:
Startup: Policies are loaded.
Runtime: Policies can be dynamically applied or updated.

Multicast Routing Table

How Filled:
Dynamic Learning: Populated by multicast routing protocols (e.g., PIM) and group membership reports (e.g., IGMP, MLD).
When Filled:
Runtime: Continuously updated as multicast group memberships change and routing information is exchanged.

Access Control Lists (ACLs)

How Filled:
Manual Configuration: Network administrators define ACL rules.
When Filled:
Startup: ACL rules are loaded.
Runtime: Rules can be dynamically applied or updated.