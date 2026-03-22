// MultiSession.cpp

#include "MultiSession.h"
#include "Session.h"

namespace BGP
{
Session* MultiSession::findMultiSession(uint64_t id)
{
    auto it = connections.find(id);
    if (it == connections.end() || !std::holds_alternative<Session*>(it->second))
        return nullptr;
    return std::get<Session*>(it->second);
}

Session* MultiSession::activateSession(TCP::ConnId id, const AfiSafi& afiSafi)
{
    auto cit = connections.find(id);
    if (cit == connections.end() || !std::holds_alternative<TCP::Connection>(cit->second))
        return nullptr;

    auto sit = sessions.find(afiSafi);
    if (sit == sessions.end() || sit->second.established())
        return nullptr;

    sit->second.acceptConnection(std::move(std::get<TCP::Connection>(cit->second)));
    cit->second = &sit->second;

    return &sit->second;
}

void MultiSession::close(TCP::ConnId id)
{
    auto it = connections.find(id);
    if (it == connections.end() || !std::holds_alternative<TCP::Connection>(it->second))
        return;
    std::get<TCP::Connection>(it->second).disconnect();
    connections.erase(it);
}
}
