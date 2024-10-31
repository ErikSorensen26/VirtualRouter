#include <Bgp.h>

namespace Protocol {
Bgp::Bgp() {}
}

Protocol::Bgp* CurrentBgp;
map<int, std::shared_ptr<Protocol::Bgp>> BgpList;