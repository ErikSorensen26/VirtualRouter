#pragma once

#include <map>
#include <memory>

using namespace std;

namespace Protocol {
class Ospf {
public:
    Ospf();
private:
};
}

extern Protocol::Ospf* CurrentOspf;
extern map<int, std::shared_ptr<Protocol::Ospf>> OspfList;