#include <Bgp.h>

namespace Protocol {
Bgp::Bgp() {}
}

Protocol::Bgp* currentBgp;
std::map<uint32_t, std::shared_ptr<Protocol::Bgp>> bgpList;
