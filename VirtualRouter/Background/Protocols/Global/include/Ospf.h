// Ospf.h

#ifndef OSPF_H
#define OSPF_H

#include <map>
#include <memory>

namespace Protocol {
class Ospf {
public:
    Ospf();
private:
};
}

extern Protocol::Ospf* currentOspf;
extern std::map<uint16_t, std::shared_ptr<Protocol::Ospf>> ospfList;

#endif // OSPF_H
