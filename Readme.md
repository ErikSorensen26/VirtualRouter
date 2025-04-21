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

















client
receive reconfig key
token bucket for rate limiting - 20 packets in 20 seconds
if t1 and t2 are 0, they are up to the client

begins with sending message to server, terminates with either 1(success) or 2(failed)
client must update elapsed time

client retransmission
RT
IRT
MRC
MRT
MRD
RAND

first message
RT = IRT + RAND*IRT
second message
RT = 2*RTprev + RAND*RTprev

if (RT > MRT)
  rt = MRT + RAND*MRT

fails when messaage is transmitted mrc terminates

message fails MRD seconds after sent

if both MRC and MRD are non-zero, it fails if either 2 are met

if both MRC and MRD are 0, the client continues

client discard reconfig if no unicast
reconfig needs authetication (RKAP)

treat the reconfig message as if the t1 time ran out

use of unicast may avoid delays due to relaying of messages

must include elapsed time and clientid must include option request in solicit

IRT SOL_TIMEOUT
MRT SOL_MAX_TR
MRC 0
MRD 0

after sending rapid commit, it will reject response without it

renew needs to have elapsed time and oro for sol_max_rt

client needs to pick lowest t1/t2 option in all received ips and renew all ips at the same time

rebind has no server id

information request needs elapsed time and oro request for INF_MAX_RT

client can't use any address its releasing as source

first thing client does is looks for top level status code

client will retransmit unspecfail status

needs to preform duplicate address protection

ignores transaction id on a reconfig reply

   When the client detects that it may have moved to a new link and it
   has obtained addresses and no delegated prefixes from a server, the
   client SHOULD initiate a Confirm/Reply message exchange.  The client
   includes any IAs assigned to the interface that may have moved to a
   new link, along with the addresses associated with those IAs, in its
   Confirm message.  Any responding servers will indicate whether those
   addresses are appropriate for the link to which the client is
   attached with the status in the Reply message it returns to the
   client.




relay





server
send reconfig key
client uses local-link
client sends most addresses to multicast
client can use info keygg
if receives invalid ip, it sends reply with unspecfail
track what clients you have sent your unicast address, only accept unicast from them

client discard reconfig of no unicast
if the server responds with an advertise message, the client initiates a configuration exchange

when to expect renew vs info-request during reconfig

client responds to reconfig with a renew, rebind, or info-request message with the reconfig option
be suspicious of the info-request

only enable unicast when relay not used

solicit requires option request to get SOL_MAX_RT option and any other options it needs

renew needs to support elapsed time

The client MAY include an IA option for each binding it desires but
   has been unable to obtain.  In this case, if the client includes the
   IA_PD option to request prefix delegation, the client MAY include the
   IA Prefix option encapsulated within the IA_PD option, with the
   "IPv6-prefix" field set to 0 and the "prefix-length" field set to the
   desired length of the prefix to be delegated.  The server MAY use
   this value as a hint for the prefix length.  The client SHOULD NOT
   include an IA Prefix option with the "IPv6-prefix" field set to 0
   unless it is supplying a hint for the prefix length.

information request needs elapsed time and oro request for INF_MAX_RT

releasee and decline needs elapse time
decline can't have iapd
clients should only decline conflicting bindings (already in use)

client can't use any address its releasing as source

support "usemulticast" status if the message should have multicast with server/client duid

can return additional options through oro

only request to reconfig accepts

requests with existing bindings will renew lease

unicast confirms are not allowed

binding returns no binding status if cant rebind message

preference in advertise message

The server includes a Reconfigure Accept option (see Section 21.20)
   if the server wants to indicate that it supports the Reconfigure
   mechanism.

reconfig -> unicast -> relay

unicast not allowed for solicit, confirm and rebind


replay detextion with rdm field of auth

iait does not have t1 or t2

leasetime of 0xffffffff means 'infinite'

   +---------------+------+--------------------------------------------+
   | Name          | Code | Description                                |
   +---------------+------+--------------------------------------------+
   | Success       |    0 | Success.                                   |
   |               |      |                                            |
   | UnspecFail    |    1 | Failure, reason unspecified; this status   |
   |               |      | code is sent by either a client or a       |
   |               |      | server to indicate a failure not           |
   |               |      | explicitly specified in this document.     |
   |               |      |                                            |
   | NoAddrsAvail  |    2 | The server has no addresses available to   |
   |               |      | assign to the IA(s).                       |
   |               |      |                                            |
   | NoBinding     |    3 | Client record (binding) unavailable.       |
   |               |      |                                            |
   | NotOnLink     |    4 | The prefix for the address is not          |
   |               |      | appropriate for the link to which the      |
   |               |      | client is attached.                        |
   |               |      |                                            |
   | UseMulticast  |    5 | Sent by a server to a client to force the  |
   |               |      | client to send messages to the server      |
   |               |      | using the                                  |
   |               |      | All_DHCP_Relay_Agents_and_Servers          |
   |               |      | multicast address.                         |
   |               |      |                                            |
   | NoPrefixAvail |    6 | The server has no prefixes available to    |
   |               |      | assign to the IA_PD(s).                    |
   +---------------+------+--------------------------------------------+

            Client Server IA_NA/                  Elap. Relay       Server
           ID     ID   IA_TA IA_PD  ORO   Pref Time   Msg. Auth. Unicast
 Solicit   *             *     *     *           *
 Advert.   *      *      *     *           *
 Request   *      *      *     *     *           *
 Confirm   *             *                       *
 Renew     *      *      *     *     *           *
 Rebind    *             *     *     *           *
 Decline   *      *      *     *                 *
 Release   *      *      *     *                 *
 Reply     *      *      *     *                             *     *
 Reconf.   *      *                                          *
 Inform.   * (see note)              *           *
 R-forw.                                               *
 R-repl.                                               *

   NOTE: The Server Identifier option (see Section 21.3) is only
   included in Information-request messages that are sent in response to
   a Reconfigure (see Section 18.2.6).






Mrugalski, et al.            Standards Track                  [Page 149]

RFC 8415                      DHCP for IPv6                November 2018


                                                                  Info
           Status  Rap. User  Vendor Vendor Inter. Recon. Recon. Refresh
            Code  Comm. Class Class  Spec.    ID    Msg.  Accept  Time
   Solicit          *     *     *      *                    *
   Advert.   *            *     *      *                    *
   Request                *     *      *                    *
   Confirm                *     *      *
   Renew                  *     *      *                    *
   Rebind                 *     *      *                    *
   Decline                *     *      *
   Release                *     *      *
   Reply     *      *     *     *      *                    *        *
   Reconf.                                           *
   Inform.                *     *      *                    *
   R-forw.                             *      *
   R-repl.                             *      *

           SOL_MAX_RT  INF_MAX_RT
   Solicit
   Advert.    *
   Request
   Confirm
   Renew
   Rebind
   Decline
   Release
   Reply      *           *
   Reconf.
   Inform.
   R-forw.
   R-repl.
