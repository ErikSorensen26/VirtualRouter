// MultiSession.h

#ifndef BGP_MULTI_SESSION_H
#define BGP_MULTI_SESSION_H

#include <unordered_map>
#include <variant>
#include <cstdint>

#include "bgp/BgpTypes.hpp"
#include "tcp/Connection.h"

namespace BGP
{
class Session;
class BgpProcess;

class MultiSession
{
public:
    std::unordered_map<AfiSafi, Session> sessions;
    std::unordered_map<uint64_t, std::variant<Session*, TCP::Connection>> connections;

    Session* findMultiSession(uint64_t id);
    Session* activateSession(TCP::ConnId id, const AfiSafi& afiSafi);
    void close(TCP::ConnId id);
};
}

#endif // BGP_MULTI_SESSION_H
