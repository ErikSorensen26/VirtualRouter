// TcpConnection.cpp
#include "TcpConnection.h"
#include <TimeManager.h>
#include <Interface.h>

#include "TcpErrors.hpp"
#include "TcpSegmentBuilder.h"
#include "TcpStateMachine.h"
#include "TcpOutput.h"

#include <algorithm>
#include <vector>

namespace TCP {

static TcpSeq nextIssSeed() {
    static TcpSeq s = 0x10203040u;
    s += 64000u;
    return s;
}

TcpConnection::TcpConnection(TcpSocketKey key, const Config& cfg, TimeManager& tmgr, Interface* iface)
    : key(std::move(key)),
      cfg(cfg),
      sendBuf(cfg.sendBufferBytes),
      recvBuf(cfg.recvBufferBytes),
      rtxq(*this),
      ackm(tmgr),
      af(key.local.address.isV6 ? AddressFamily::IPv6 : AddressFamily::IPv4),
      iface(iface),
      tmgr(tmgr)
{
    flow.reset();
    ackm.reset();
    timers.reset();
    snd.reset();
    rcv.reset();
    cc.reset();
    loss.reset();

    peerMss = 0;
    peerWndShift = 0;
    localWndShift = cfg.defaultWindowShift;
    flow.setLocalWindowShift(localWndShift);
}

const TcpSocketKey& TcpConnection::socketKey() const noexcept { return key; }
TcpState TcpConnection::getState() const noexcept { return state; }

void TcpConnection::startConnection() noexcept
{
    if (!iface) return;
    // NOTE: Selecting a correct local address for the route is out-of-scope here.
    // Caller should pass a valid local address/port in the key or the stack should select it.

    Interface* localIface = iface.load(std::memory_order_relaxed);

    snd.iss = nextIssSeed() ^ TcpSeq(std::chrono::steady_clock::now().time_since_epoch().count());
    snd.sndUna = snd.iss;
    snd.sndNxt = snd.iss + 1;

    // Calculate mss
    if (af == AddressFamily::IPv4)
        cfg.mss = localIface->configs.ipv4.mtu.load(std::memory_order_relaxed) - IPv4Header::fixedSize - TcpHeader::fixedSize;
    else
        cfg.mss = localIface->configs.ipv6.mtu.load(std::memory_order_relaxed) - IPv6Header::fixedSize - TcpHeader::fixedSize;

    // Offer options.
    TcpOptions opts{};
    opts.mss = cfg.mss;
    opts.windowScale = cfg.defaultWindowShift;
    opts.sackPermitted = true;

    const std::uint16_t advWin = flow.windowFieldFromFreeSpace(recvBuf.freeSpace());

    TcpSegment syn = TcpSegmentBuilder::build({
        *localIface,
        key.local,
        key.remote,
        snd.iss,
        0,
        advWin,
        0,
        opts
    });

    syn.hdr.setFlagSYN(true);
    syn.buildHeader(cfg.mss);

    // Out-of-scope: actual wire send is via TcpOutput callback.
    // The stack will call onTick / flush soon; we can keep the SYN in retransmission queue now:
    rtxq.onSegmentSent(syn);
    timers.startRto(std::chrono::steady_clock::now() + cfg.initialRto);
    state = TcpState::SYN_SENT;

    // NOTE: Actual transmission occurs in flush() which is invoked by TcpStateMachine::onTick/onSegment.
}

void TcpConnection::startFromSyn(const TcpSegment& syn)
{
    // Child connection created by TcpStack for a LISTEN socket.
    state = TcpState::LISTEN;

    // Initialize send ISS.
    snd.iss = nextIssSeed() ^ TcpSeq(std::chrono::steady_clock::now().time_since_epoch().count());
    snd.sndUna = snd.iss;
    snd.sndNxt = snd.iss + 1;

    // Initialize receive from peer SYN.
    rcv.irs = syn.hdr.getSequenceNumber();
    rcv.rcvNxt = syn.hdr.getSequenceNumber() + 1;
    recvBuf.setRcvNxt(rcv.rcvNxt);

    if (syn.options.mss.has_value()) peerMss = *syn.options.mss;
    if (syn.options.windowScale.has_value()) peerWndShift = *syn.options.windowScale;
    snd.peerWndShift = peerWndShift;

    // Build and send SYN-ACK immediately.
    TcpOptions opts{};
    opts.mss = cfg.defaultMss;
    opts.windowScale = cfg.defaultWindowShift;
    opts.sackPermitted = true;

    const std::uint16_t advWin = flow.windowFieldFromFreeSpace(recvBuf.freeSpace());
    TcpSegment synAck = TcpSegmentBuilder::build({
        *iface,
        key.local,
        key.remote,
        snd.iss,
        rcv.rcvNxt,
        advWin,
        0,
        opts,
    });
    synAck.hdr.setFlagSYN(true);
    synAck.hdr.setFlagACK(true);

    // This send occurs in TcpStateMachine::onSegment() path when state is Listen;
    // here we directly queue and transition to SynReceived, but do not send.
    // Out-of-scope: you may choose to send directly here if your design allows.
    rtxq.onSegmentSent(synAck);
    timers.startRto(std::chrono::steady_clock::now() + cfg.initialRto);
    state = TcpState::SYN_RECEIVED;
}

void TcpConnection::onSegment(const TcpSegment& seg, TcpOutput& out)
{
    TcpStateMachine::onSegment(*this, seg, out);
}

std::size_t TcpConnection::send(std::span<const std::uint8_t> data)
{
    const size_t written = sendBuf.push(data);
    if (!iface) return 0;
    if (written == 0) return 0;

    flush();
    return written;
}

std::size_t TcpConnection::recv(std::span<std::uint8_t> out)
{
    return recvBuf.read(out);
}

size_t TcpConnection::readableBytes() const noexcept
{
    return recvBuf.buffered();
}

size_t TcpConnection::writableBytes() const noexcept
{
    return sendBuf.freeSpace();
}

void TcpConnection::shutdown(TcpShutdown how, TcpOutput& out)
{
    // Minimal: shutting down write maps to close().
    // Out-of-scope: half-close semantics for read/write at app layer.
    if (how == TcpShutdown::WRITE || how == TcpShutdown::READ_WRITE) {
        close(out);
    }
}

void TcpConnection::close(TcpOutput& out)
{
    if (!iface) return;
    if (state == TcpState::ESTABLISHED)
    {
        state = TcpState::FIN_WAIT_1;
        TcpSegment ctrl = createControl();
        ctrl.hdr.setFlagFIN(true);
        ctrl.hdr.setFlagACK(true);
        sendControl(ctrl, out);
    }
    else if (state == TcpState::CLOSE_WAIT)
    {
        state = TcpState::LAST_ACK;
        TcpSegment ctrl = createControl();
        ctrl.hdr.setFlagFIN(true);
        ctrl.hdr.setFlagACK(true);
        sendControl(ctrl, out);
    }
}

void TcpConnection::abort(TcpOutput& out) {
    // Abort by sending RST (best-effort) then closing.
    if (!iface) return;
    TcpSegment rst(*iface);
    rst.setSrc(key.local);
    rst.setDst(key.remote);
    rst.setSeq(snd.sndNxt);
    rst.setAck(rcv.rcvNxt);
    rst.hdr.setFlagRST(true);
    rst.hdr.setFlagACK(true);
    out.send(rst);

    state = TcpState::CLOSED;
    timers.reset();
    rtxq.clear();
}

bool TcpConnection::isClosed() const noexcept
{
    return state == TcpState::CLOSED;
}

bool TcpConnection::inTimeWait() const noexcept
{
    return state == TcpState::TIME_WAIT;
}

/*std::optional<TcpConnection::TimePoint> TcpConnection::nextDeadline() const noexcept
{
    return timers.nextDeadline();
}*/

std::uint16_t TcpConnection::peerMssOrDefault() const noexcept
{
    return peerMss ? peerMss : cfg.defaultMss;
}

std::uint8_t TcpConnection::peerWindowShift() const noexcept { return peerWndShift; }
std::uint8_t TcpConnection::localWindowShift() const noexcept { return localWndShift; }

void TcpConnection::sendAckOnly(TcpOutput& out)
{
    if (!iface) return;
    const std::uint16_t advWin = flow.windowFieldFromFreeSpace(recvBuf.freeSpace());
    TcpSegment ack = TcpSegmentBuilder::buildPureAck(
        *iface, key.local, key.remote, snd.sndNxt, rcv.rcvNxt, advWin
    );
    out.send(ack);
    ackm.onAckSent(rcv.rcvNxt);
    timers.cancelDelayedAck();
}

TcpSegment TcpConnection::createControl()
{
    TcpSegment seg(*iface);
    seg.setSrc(key.local);
    seg.setDst(key.remote);
    seg.setSeq(snd.sndNxt);
    seg.setAck(rcv.rcvNxt);
    seg.setWindowSize(flow.windowFieldFromFreeSpace(recvBuf.freeSpace()));
    return seg;
}

void TcpConnection::sendControl(TcpSegment& seg, TcpOutput& out)
{
    // Control options (SYN variants).
    if (seg.hdr.getFlagSYN()) {
        TcpOptions opts{};
        opts.mss = cfg.defaultMss;
        opts.windowScale = cfg.defaultWindowShift;
        opts.sackPermitted = true;
        seg.options = std::move(opts);
    }

    out.send(seg);

    // Track retransmission if consumes sequence space.
    if (seg.dataLen() > 0)
    {
        rtxq.onSegmentSent(seg);
        timers.startRto(std::chrono::steady_clock::now() + cfg.initialRto);

        if (seg.hdr.getFlagSYN()) snd.sndNxt += 1;
        if (seg.hdr.getFlagFIN()) {
            finSent = true;
            snd.sndNxt += 1;
        }
    }
}

void TcpConnection::sendRstResponse(const TcpSegment& in, TcpOutput& out)
{
    TcpSegment rst = TcpErrors::makeRstForIncoming(in);
    out.send(rst);
}

void TcpConnection::flush()
{
    if (!iface) return;
    // Only send data once the connection is established or in closing states.
    if (!(state == TcpState::ESTABLISHED ||
          state == TcpState::FIN_WAIT_1 ||
          state == TcpState::FIN_WAIT_2 ||
          state == TcpState::CLOSE_WAIT ||
          state == TcpState::CLOSING ||
          state == TcpState::LAST_ACK))
    {
        return;
    }

    // Compute available send window.
    const std::uint32_t effWnd = cc.sendWindowBytes(snd.sndWndBytes);
    const std::uint32_t inFlight = snd.bytesInFlight();
    if (effWnd <= inFlight) return;

    std::uint32_t avail = effWnd - inFlight;
    if (avail == 0) return;

    const std::uint16_t mss = peerMssOrDefault();
    const std::size_t offset = static_cast<std::size_t>(snd.sndNxt - snd.sndUna);

    // Send as many full segments as we can from the buffer.
    while (avail > 0)
    {
        const std::size_t maxPayload = std::min<std::size_t>(mss, avail);
        if (maxPayload == 0) break;

        std::vector<std::uint8_t> tmp(maxPayload);
        const std::size_t got = sendBuf.peek(offset + (snd.sndNxt - snd.sndUna - offset), tmp);
        if (got == 0) break;

        tmp.resize(got);

        TcpSegment data(*iface);
        data.setSrc(key.local);
        data.setDst(key.remote);
        data.setSeq(snd.sndNxt);
        data.setAck(rcv.rcvNxt);
        data.hdr.setFlagACK(true);
        data.setWindowSize(flow.windowFieldFromFreeSpace(recvBuf.freeSpace()));
        data.payload = std::move(tmp);

        out.send(data);

        rtxq.onSegmentSent(data);
        timers.startRto(std::chrono::steady_clock::now() + cfg.initialRto);

        snd.sndNxt += static_cast<TcpSeq>(got);
        cc.onPacketSent(got);

        if (got >= avail) break;
        avail -= static_cast<std::uint32_t>(got);
    }

    // If we're in a FIN-sending state and no payload remains, send FIN if not sent yet.
    if ((state == TcpState::FIN_WAIT_1 || state == TcpState::LAST_ACK) && !finSent && sendBuf.size() == 0)
    {
        TcpSegment ctrl = createControl();
        ctrl.hdr.setFlagFIN(true);
        ctrl.hdr.setFlagACK(true);
        sendControl(ctrl, out);
    }
}
} // namespace tcp
