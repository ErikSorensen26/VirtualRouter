// Bgp.h

#ifndef BGP_H
#define BGP_H

#include <map>
#include <memory>

namespace Protocol 
{
    class Bgp 
    {
    public:
        Bgp();
    private:
    };
}

extern Protocol::Bgp* currentBgp;
extern std::map<uint32_t, std::shared_ptr<Protocol::Bgp>> bgpList;

#endif // BGP_H
