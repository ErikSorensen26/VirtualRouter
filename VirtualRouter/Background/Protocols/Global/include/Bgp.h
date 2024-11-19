#pragma once

#include <map>
#include <memory>

using namespace std;
namespace Protocol {
class Bgp {
public:
    Bgp();
private:
};
}

extern Protocol::Bgp* CurrentBgp;
extern map<int, std::shared_ptr<Protocol::Bgp>> BgpList;