#pragma once

#include <map>
#include <memory>

namespace Protocol {
class Bgp {
public:
    Bgp();
private:
};
}

extern Protocol::Bgp* currentBgp;
extern std::map<int, std::shared_ptr<Protocol::Bgp>> bgpList;
