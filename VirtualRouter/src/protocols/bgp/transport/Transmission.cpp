// Transmission.cpp

#include "Transmission.h"
#include "bgp/BgpProcess.h"

namespace BGP
{
Transmission::Transmission(BgpProcess& proc) 
    : process(proc)
{}
}
