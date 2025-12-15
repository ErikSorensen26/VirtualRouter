// TcpStack.cpp

#include "Tcp.h"
#include <TcpErrors.hpp>
#include <VirtualRouter.h>
#include <Global.h>

namespace TCP {

TcpStack::TcpStack(VirtualRouter& vrf, Config cfg)
    : cfg(std::move(cfg)),
      vrf(vrf),
      output(vrf),
      timeWait(vrf.global.timeManager)
{
    nextEphemeral = cfg.ephemeralMin;
}

TcpStack::ConnId TcpStack::allocateConnId() noexcept {
    return nextConnId++;
}

TcpStack::ListenId TcpStack::allocateListenId() noexcept {
    return nextListenId++;
}

TcpPort TcpStack::allocateEphemeralPort(const IPAddress& localAddr)
{
    const TcpPort minP = cfg.ephemeralMin;
    const TcpPort maxP = cfg.ephemeralMax;

    if (minP == 0 || maxP == 0 || minP > maxP)
        return 0;

    const TcpPort range = maxP - minP + 1;
    if (nextEphemeral < minP || nextEphemeral > maxP)
        nextEphemeral = minP;

    auto portUsed = [&](TcpPort p) -> bool
    {
        for (const auto& kv : listeners)
        {
            const auto& ep = kv.second.listener.localEndpoint();
            if (ep.port != p)
                continue;

            if (ep.isWildcardAddress() || ep.address == localAddr)
                return true;
        }

        for (const auto& kv : conns)
        {
            const auto& ep = kv.second.conn.socketKey().local;

            if (ep.port != p)
                continue;

            if (ep.isWildcardAddress() || ep.address == localAddr)
                return true;
        }

        return false;
    };

    for (TcpPort i = 0; i < range; ++i)
    {
        TcpPort p = nextEphemeral;
        nextEphemeral = (p == maxP) ? minP : TcpPort(p + 1);

        if (!portUsed(p))
            return p;
    }

    return 0;
}

/*TcpPort TcpStack::allocateEphemeralPort(const IPAddress& localAddr)
{
    const TcpPort minP = cfg.ephemeralMin;
    const TcpPort maxP = cfg.ephemeralMax;

    if (minP == 0 || maxP == 0 || minP > maxP)
        return 0;

    for (std::size_t tries = 0; tries <= (maxP - minP); ++tries) {
        TcpPort p = nextEphemeral;
        nextEphemeral = (p == maxP) ? minP : TcpPort(p + 1);

        bool used = false;
        for (const auto& kv : listeners) {
            if (kv.second.listener.localEndpoint().port == p) { used = true; break; }
        }
        if (!used) {
            for (const auto& kv : conns) {
                if (kv.second.conn.socketKey().local.port == p) { used = true; break; }
            }
        }
        if (!used) return p;
    }

    return 0;
}*/

TcpStack::ListenerEntry* TcpStack::findListenerById(ListenId id) noexcept
{
    auto it = listeners.find(id);
    return it == listeners.end() ? nullptr : &it->second;
}

const TcpStack::ListenerEntry* TcpStack::findListenerById(ListenId id) const noexcept
{
    auto it = listeners.find(id);
    return it == listeners.end() ? nullptr : &it->second;
}

TcpStack::ConnEntry* TcpStack::findConnById(ConnId id) noexcept
{
    auto it = conns.find(id);
    return it == conns.end() ? nullptr : &it->second;
}

const TcpStack::ConnEntry* TcpStack::findConnById(ConnId id) const noexcept
{
    auto it = conns.find(id);
    return it == conns.end() ? nullptr : &it->second;
}

TcpStack::ConnEntry* TcpStack::findConnByKey(const TcpSocketKey& key) noexcept
{
    auto it = keyToConn.find(key);
    if (it == keyToConn.end()) return nullptr;
    return findConnById(it->second);
}

const TcpStack::ConnEntry* TcpStack::findConnByKey(const TcpSocketKey& key) const noexcept
{
    auto it = keyToConn.find(key);
    if (it == keyToConn.end()) return nullptr;
    return findConnById(it->second);
}

TcpStack::ListenerEntry* TcpStack::findListenerForLocal(const TcpEndpoint& local) noexcept
{
    for (auto& kv : listeners)
    {
        const auto& lstLocal = kv.second.listener.localEndpoint();
        TcpSocketKey tmp{ local, {} };
        if (tmp.matchListener(lstLocal))
            return &kv.second;
    }
    return nullptr;
}

const TcpStack::ListenerEntry* TcpStack::findListenerForLocal(const TcpEndpoint& local) const noexcept
{
    for (const auto& kv : listeners)
    {
        const auto& lstLocal = kv.second.listener.localEndpoint();
        TcpSocketKey tmp{ local, {} };
        if (tmp.matchListener(lstLocal))
            return &kv.second;
    }
    return nullptr;
}

TcpStack::ListenId TcpStack::listen(const TcpEndpoint& local, std::size_t backlog)
{
    const ListenId id = allocateListenId();
    listeners.emplace(id, ListenerEntry{ id, TcpListener(local, backlog) });
    return id;
}

void TcpStack::unlisten(ListenId id)
{
    listeners.erase(id);
}

std::optional<TcpStack::ConnId> TcpStack::accept(ListenId id)
{
    auto* le = findListenerById(id);
    if (!le) return std::nullopt;

    auto k = le->listener.accept();
    if (!k.has_value()) return std::nullopt;

    auto* ce = findConnByKey(*k);
    if (!ce) return std::nullopt;
    return ce->id;
}

TcpStack::ConnId TcpStack::connect(const TcpEndpoint& localIn, const TcpEndpoint& remote)
{
    TcpEndpoint local = localIn;
    if (local.port == 0) {
        local.port = allocateEphemeralPort(local.address);
    }

    TcpSocketKey key{ local, remote };
    const ConnId id = allocateConnId();

    auto [it, ok] = conns.emplace(id, ConnEntry{ id, TcpConnection(key, cfg.connectionDefaults, ) });
    (void)ok;

    keyToConn[key] = id;

    it->second.conn.startConnection();
    it->second.conn.onTick(output); // drive initial SYN send

    return id;
}

std::size_t TcpStack::send(ConnId id, std::span<const std::uint8_t> data)
{
    auto* ce = findConnById(id);
    if (!ce) return 0;
    return ce->conn.send(data);
}

std::size_t TcpStack::recv(ConnId id, std::span<std::uint8_t> out)
{
    auto* ce = findConnById(id);
    if (!ce) return 0;
    return ce->conn.recv(out);
}

void TcpStack::shutdown(ConnId id, TcpShutdown how)
{
    auto* ce = findConnById(id);
    if (!ce) return;
    ce->conn.shutdown(how, output);
}

void TcpStack::close(ConnId id)
{
    auto* ce = findConnById(id);
    if (!ce) return;
    ce->conn.close(output);
}

TcpState TcpStack::state(ConnId id) const
{
    auto* ce = findConnById(id);
    if (!ce) return TcpState::CLOSED;
    return ce->conn.getState();
}

std::optional<TcpSocketKey> TcpStack::socketKey(ConnId id) const
{
    auto* ce = findConnById(id);
    if (!ce) return std::nullopt;
    return ce->conn.socketKey();
}

TcpStack::ConnId TcpStack::createChildForSyn(ListenerEntry& lst, const TcpSegment& syn)
{
    // Child connection key uses the actual destination address (not wildcard listener).
    TcpSocketKey childKey{ syn.dst, syn.src };

    if (!lst.listener.admitHalfOpen(childKey)) {
        // Out-of-scope: send SYN-ACK? ignore? policy.
        return 0;
    }

    const ConnId id = allocateConnId();
    auto [it, ok] = conns.emplace(id, ConnEntry{ id, TcpConnection(childKey, iface, cfg.connectionDefaults) });
    (void)ok;
    keyToConn[childKey] = id;

    // Initialize and drive handshake.
    it->second.conn.startFromSyn(syn);
    it->second.conn.onSegment(syn, output); // LISTEN->SYN-RECEIVED path will send SYN-ACK

    return id;
}

void TcpStack::input(const TcpSegment& seg)
{
    // Demux key: local=dst, remote=src
    const TcpSocketKey key{ seg.dst, seg.src };

    if (auto* ce = findConnByKey(key)) {
        ce->conn.onSegment(seg, output);
        return;
    }

    // TIME-WAIT table support (optional; currently not populated by this skeleton).
    if (auto ack = timeWait.maybeAckForIncoming(seg, 0); ack.has_value()) {
        output.send(*ack);
        return;
    }

    if (auto* le = findListenerForLocal(seg.dst)) {
        if (seg.hdr.getFlagSYN() && !seg.hdr.getFlagACK()) {
            (void)createChildForSyn(*le, seg);
            return;
        }
        // Non-SYN to LISTEN: ignore or RST depending on policy.
        return;
    }

    // No match: respond with RST if appropriate.
    // Out-of-scope: exact RFC behavior for all corner cases.
    TcpSegment rst = TcpErrors::makeRstForIncoming(seg);
    output.send(rst);
}

void TcpStack::reapClosed()
{
    for (auto it = conns.begin(); it != conns.end(); ) {
        if (it->second.conn.isClosed()) {
            keyToConn.erase(it->second.conn.socketKey());
            it = conns.erase(it);
        } else {
            ++it;
        }
    }
}

void TcpStack::tick()
{
    // TODO REMOVE
    // Expire TIME-WAIT table.
    timeWait.expire(now);

    // Drive timers for all connections.
    for (auto& kv : conns_) {
        kv.second.conn.onTick(now, output_);
    }

    reapClosed(now);
}

} // namespace tcp

