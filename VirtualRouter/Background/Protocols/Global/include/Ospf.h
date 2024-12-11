#pragma once

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
extern std::map<int, std::shared_ptr<Protocol::Ospf>> ospfList;
