#include <Ospf.h>

namespace Protocol {
Ospf::Ospf() {}
}

Protocol::Ospf* CurrentOspf;
map<int, std::shared_ptr<Protocol::Ospf>> OspfList;