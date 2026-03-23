// MultiSession.cpp

#include "MultiSession.h"
#include "Session.h"

namespace routing::bgp
{
Session* MultiSession::findMultiSession(uint64_t id)
{
    auto it = connections.find(id);
    if (it == connections.end() || !std::holds_alternative<Session*>(it->second))
        return nullptr;
    return std::get<Session*>(it->second);
}

Session* MultiSession::activateSession(transport::tcp::ConnId id, const AfiSafi& afiSafi)
{
    auto cit = connections.find(id);
    if (cit == connections.end() || !std::holds_alternative<transport::tcp::Connection>(cit->second))
        return nullptr;

    auto sit = sessions.find(afiSafi);
    if (sit == sessions.end() || sit->second.established())
        return nullptr;

    sit->second.acceptConnection(std::move(std::get<transport::tcp::Connection>(cit->second)));
    cit->second = &sit->second;

    return &sit->second;
}

void MultiSession::close(transport::tcp::ConnId id)
{
    auto it = connections.find(id);
    if (it == connections.end() || !std::holds_alternative<transport::tcp::Connection>(it->second))
        return;
    std::get<transport::tcp::Connection>(it->second).disconnect();
    connections.erase(it);
}
} // namespace routing
