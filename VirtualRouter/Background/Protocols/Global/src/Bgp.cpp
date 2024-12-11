#include <Bgp.h>

namespace Protocol {
Bgp::Bgp() {}
}

Protocol::Bgp* currentBgp;
std::map<int, std::shared_ptr<Protocol::Bgp>> bgpList;
