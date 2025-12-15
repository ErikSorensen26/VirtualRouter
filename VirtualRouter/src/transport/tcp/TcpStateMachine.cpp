// TcpStateMachine.cpp

#include "TcpStateMachine.h"

#include "TcpConnection.h"
#include <TcpErrors.hpp>
#include "TcpSegmentBuilder.h"
#include "TcpOutput.h"

#include <algorithm>
#include <vector>

namespace TCP {

static TcpOptions buildSynOptions(const TcpConnection& c)
{
    TcpOptions o{};
    o.mss = c.peerMssOrDefault(); // will be replaced by local MSS policy in a full stack
    (void)c;
    // NOTE: For a full implementation you would include:
    // - local MSS (based on interface MTU)
    // - window scale offer
    // - SACK permitted
    return o;
}

void TcpStateMachine::onSegment(TcpConnection& c, const TcpSegment& seg, TcpOutput& out)
{
    // RST always aborts the connection.
    if (seg.hdr.getFlagRST()) {
        c.state = TcpState::CLOSED;
        c.timers.reset();
        c.rtxq.clear();
        return;
    }

    // Update peer window if possible (ACK segments carry it).
    if (seg.hdr.getFlagACK())
    {
        c.snd.updatePeerWindow(seg.hdr.getWindowSize(), seg.hdr.getSequenceNumber(), seg.hdr.getAckNumber());
    }

    // State-specific handling.
    switch (c.state) {
        case TcpState::CLOSED:
        {
            // If a stray segment arrives for a closed connection, reply with RST (stack-level policy).
            // Out-of-scope: some stacks suppress RST for certain cases.
            TcpSegment rst = TcpErrors::makeRstForIncoming(seg);
            out.send(rst);
            break;
        }

        case TcpState::LISTEN:
        {
            if (!seg.hdr.getFlagSYN())
                // Ignore non-SYN to LISTEN (or RST depending on policy).
                break;

            // Initialize receive.
            c.rcv.irs = seg.hdr.getSequenceNumber();
            c.rcv.rcvNxt = seg.hdr.getSequenceNumber() + 1;
            c.recvBuf.setRcvNxt(c.rcv.rcvNxt);

            // Initialize send.
            c.snd.iss = c.snd.iss ? c.snd.iss : TcpSeq(std::chrono::steady_clock::now().time_since_epoch().count());
            c.snd.sndUna = c.snd.iss;
            c.snd.sndNxt = c.snd.iss + 1;

            // Negotiate basic peer options.
            if (seg.options.mss.has_value()) c.peerMss = *seg.options.mss;
            if (seg.options.windowScale.has_value()) c.peerWndShift = *seg.options.windowScale;

            c.snd.peerWndShift = c.peerWndShift;

            // Send SYN-ACK.
            TcpOptions opts{};
            opts.mss = c.cfg.defaultMss;
            opts.windowScale = c.cfg.defaultWindowShift;
            opts.sackPermitted = true;

            const std::uint16_t advWin = c.flow.windowFieldFromFreeSpace(c.recvBuf.freeSpace());
            TcpSegment synAck = TcpSegmentBuilder::build({
                *c.getIface(),
                c.key.local,
                c.key.remote,
                c.snd.iss,
                c.rcv.rcvNxt,
                advWin,
                0,
                opts
            });
            synAck.hdr.setFlagSYN(true);
            synAck.hdr.setFlagACK(true);

            out.send(synAck);
            c.rtxq.onSegmentSent(synAck);
            c.timers.startRto(std::chrono::steady_clock::now() + c.cfg.initialRto);
            c.state = TcpState::SYN_RECEIVED;
            break;
        }
        case TcpState::SYN_SENT:
        {
            // Expect SYN-ACK (or SYN for simultaneous open).
            if (seg.hdr.getFlagSYN() && seg.hdr.getFlagACK())
            {
                // Validate ACK for our SYN.
                if (seg.hdr.getAckNumber() != c.snd.iss + 1)
                {
                    TcpSegment rst = TcpErrors::makeRstForIncoming(seg);
                    out.send(rst);
                    c.state = TcpState::CLOSED;
                    break;
                }

                // Accept peer ISS.
                c.rcv.irs = seg.hdr.getSequenceNumber();
                c.rcv.rcvNxt = seg.hdr.getSequenceNumber() + 1;
                c.recvBuf.setRcvNxt(c.rcv.rcvNxt);

                if (seg.options.mss.has_value()) c.peerMss = *seg.options.mss;
                if (seg.options.windowScale.has_value()) c.peerWndShift = *seg.options.windowScale;
                c.snd.peerWndShift = c.peerWndShift;

                // ACK peer SYN.
                const std::uint16_t advWin = c.flow.windowFieldFromFreeSpace(c.recvBuf.freeSpace());
                TcpSegment ack = TcpSegmentBuilder::buildPureAck(
                    *c.getIface(), c.key.local, c.key.remote, c.snd.sndNxt, c.rcv.rcvNxt, advWin
                );
                out.send(ack);

                // Mark our SYN acked.
                c.rtxq.onAckReceived(seg.hdr.getAckNumber());
                c.snd.sndUna = seg.hdr.getAckNumber();

                c.state = TcpState::ESTABLISHED;
                c.timers.cancelRto();
            }
            else if (seg.hdr.getFlagSYN() && !seg.hdr.getFlagACK())
            {
                // Simultaneous open: respond with SYN-ACK.
                c.rcv.irs = seg.hdr.getSequenceNumber();
                c.rcv.rcvNxt = seg.hdr.getSequenceNumber() + 1;
                c.recvBuf.setRcvNxt(c.rcv.rcvNxt);

                const std::uint16_t advWin = c.flow.windowFieldFromFreeSpace(c.recvBuf.freeSpace());
                TcpSegment synAck = TcpSegmentBuilder::build({
                    *c.getIface(),
                    c.key.local,
                    c.key.remote,
                    c.snd.iss,
                    c.rcv.rcvNxt,
                    advWin,
                    0,
                    buildSynOptions(c)
                });
                synAck.hdr.setFlagSYN(true);
                synAck.hdr.setFlagACK(true);
                out.send(synAck);
                c.rtxq.onSegmentSent(synAck);
                c.state = TcpState::SYN_RECEIVED;
            }
            break;
        }
        case TcpState::SYN_RECEIVED:
        {
            // Await ACK of our SYN.
            if (seg.hdr.getFlagACK() && seg.hdr.getFlagACK() == c.snd.iss + 1)
            {
                c.rtxq.onAckReceived(seg.hdr.getAckNumber());
                c.snd.sndUna = seg.hdr.getAckNumber();
                c.state = TcpState::ESTABLISHED;
                c.timers.cancelRto();
            }
            break;
        }
        default:
            // Established / closing states handled in TcpConnection.cpp common logic.
            break;
    }

    // Common in-band processing for non-handshake states:
    // (Established/closing) process data/ACK/FIN and schedule ACKs.
    if (c.state == TcpState::ESTABLISHED ||
        c.state == TcpState::FIN_WAIT_1 ||
        c.state == TcpState::FIN_WAIT_2 ||
        c.state == TcpState::CLOSE_WAIT ||
        c.state == TcpState::CLOSING ||
        c.state == TcpState::LAST_ACK ||
        c.state == TcpState::TIME_WAIT) {

        // ACK processing
        if (seg.hdr.getFlagACK()) {
            const TcpAck ackNo = seg.hdr.getAckNumber();
            const bool advanced = ackNo > c.snd.sndUna;
            c.loss.onAckProcessed(ackNo, advanced);

            if (advanced) {
                const std::size_t newlyAckedSeq = c.rtxq.onAckReceived(ackNo);
                c.snd.sndUna = ackNo;

                // Consume only as many bytes as exist in the send buffer.
                const std::size_t consume = std::min<std::size_t>(newlyAckedSeq, c.sendBuf.size());
                if (consume) {
                    c.sendBuf.consume(consume);
                    c.cc.onAcked(consume);
                }

                // FIN ack tracking (best-effort)
                if (c.finSent && !c.finAcked && c.sendBuf.size() == 0) {
                    if (ackNo == c.snd.sndNxt) c.finAcked = true;
                }
            } else {
                // Duplicate ACK: maybe fast retransmit.
                if (c.loss.shouldFastRetransmit()) {
                    if (const auto* e = c.rtxq.oldest()) {
                        auto r = c.rtxq.buildRetransmission(*e, c.peerMssOrDefault());
                        if (r.has_value()) {
                            r->setSrc(c.key.local);
                            r->setDst(c.key.remote);
                            r->setAck(c.rcv.rcvNxt);
                            r->hdr.setFlagACK(true);
                            r->setWindowSize(c.flow.windowFieldFromFreeSpace(c.recvBuf.freeSpace()));
                            out.send(*r);
                            c.cc.onFastLoss();
                            c.loss.onFastRetransmitSent();
                        }
                    }
                }
            }
        }

        // Data/FIN processing
        const bool inOrder = (seg.hdr.getSequenceNumber() == c.rcv.rcvNxt);
        c.recvBuf.setRcvNxt(c.rcv.rcvNxt);
        const std::size_t adv = c.recvBuf.insert(seg.hdr.getSequenceNumber(), seg.payload);
        if (adv)
        {
            c.rcv.rcvNxt += static_cast<TcpSeq>(adv);
            c.recvBuf.setRcvNxt(c.rcv.rcvNxt);
        }

        // FIN consumes 1 sequence number if in-order after payload.
        if (seg.hdr.getFlagFIN())
        {
            const TcpSeq finSeq = seg.hdr.getSequenceNumber() + static_cast<TcpSeq>(seg.payload.size());
            if (finSeq == (c.rcv.rcvNxt))
            {
                c.finReceived = true;
                c.rcv.rcvNxt += 1;
                c.recvBuf.setRcvNxt(c.rcv.rcvNxt);
                c.ackm.requestImmediate();
            }
            else
            {
                // Out-of-order FIN: out-of-scope to queue FIN separately here.
                c.ackm.requestImmediate();
            }
        }

        c.ackm.onSegmentReceived(seg, inOrder);

        // State transitions for close handshake.
        if (c.state == TcpState::ESTABLISHED && c.finReceived)
        {
            c.state = TcpState::CLOSE_WAIT;
        }

        if (c.state == TcpState::FIN_WAIT_1 && c.finAcked)
        {
            c.state = c.finReceived ? TcpState::TIME_WAIT : TcpState::FIN_WAIT_2;
            if (c.state == TcpState::TIME_WAIT)
            {
                c.timers.startTimeWait(std::chrono::steady_clock::now() + c.cfg.timeWaitDuration);
            }
        }

        if (c.state == TcpState::FIN_WAIT_2 && c.finReceived)
        {
            c.state = TcpState::TIME_WAIT;
            c.timers.startTimeWait(std::chrono::steady_clock::now() + c.cfg.timeWaitDuration);
        }

        if (c.state == TcpState::LAST_ACK && c.finAcked)
        {
            c.state = TcpState::CLOSED;
        }

        if (c.state == TcpState::CLOSING && c.finAcked)
        {
            c.state = TcpState::TIME_WAIT;
            c.timers.startTimeWait(std::chrono::steady_clock::now() + c.cfg.timeWaitDuration);
        }
    }

    // ACK sending (immediate or delayed).
    if (c.ackm.ackPending())
    {
        if (c.ackm.immediateAckPending())
        {
            c.sendAckOnly(out);
        }
        else
        {
            // Arm delayed ACK timer if not armed.
            if (!c.ackm.isDelayedAck())
            {
                auto now = std::chrono::steady_clock::now();
                c.ackm.startDelayedAck(now + c.cfg.delayedAckDelay);
                c.timers.startDelayedAck(now + c.cfg.delayedAckDelay);
            }
        }
    }

    // Attempt to flush pending outbound data/control for states that allow it.
    c.flush(out);
}
} // namespace tcp
