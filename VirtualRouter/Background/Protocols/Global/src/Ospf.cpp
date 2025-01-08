#include <Ospf.h>

namespace Protocol {
Ospf::Ospf() {}
}

Protocol::Ospf* currentOspf;
std::map<uint16_t, std::shared_ptr<Protocol::Ospf>> ospfList;
