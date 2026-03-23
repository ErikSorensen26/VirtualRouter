// MultiSession.h

#ifndef BGP_MULTI_SESSION_H
#define BGP_MULTI_SESSION_H

#include <unordered_map>
#include <variant>
#include <cstdint>

#include "bgp/BgpTypes.hpp"
#include "tcp/Connection.h"

namespace routing::bgp
{
class Session;
class BgpProcess;

class MultiSession
{
public:
    std::unordered_map<AfiSafi, Session> sessions;
    std::unordered_map<uint64_t, std::variant<Session*, transport::tcp::Connection>> connections;

    Session* findMultiSession(uint64_t id);
    Session* activateSession(transport::tcp::ConnId id, const AfiSafi& afiSafi);
    void close(transport::tcp::ConnId id);
};
} // namespace routing::bgp

#endif // BGP_MULTI_SESSION_H

