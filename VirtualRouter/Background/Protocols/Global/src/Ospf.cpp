#include <Ospf.h>

namespace Protocol {
Ospf::Ospf() {}
}

Protocol::Ospf* currentOspf;
std::map<int, std::shared_ptr<Protocol::Ospf>> ospfList;
