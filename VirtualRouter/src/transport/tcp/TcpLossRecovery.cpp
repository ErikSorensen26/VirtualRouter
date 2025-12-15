// TcpLossRecovery.cpp

#include "TcpLossRecovery.h"

namespace TCP
{
void TcpLossRecovery::reset() noexcept
{
    lastAck = 0;
    dupAcks = 0;
    fastRetxFired = false;
}

void TcpLossRecovery::onAckProcessed(uint32_t ackNo, bool advancedAck) noexcept
{
    if (advancedAck)
    {
        lastAck = ackNo;
        dupAcks = 0;
        fastRetxFired = false;
    }
    else
    {
        if (ackNo == lastAck)
        {
            ++dupAcks;
        }
        else
        {
            lastAck = ackNo;
            dupAcks = 0;
            fastRetxFired = false;
        }
    }
}

void TcpLossRecovery::onDuplicateAck() noexcept
{
    ++dupAcks;
}

bool TcpLossRecovery::shouldFastRetransmit() const noexcept
{
    return (dupAcks >= 3) && !fastRetxFired;
}

void TcpLossRecovery::onFastRetransmitSent() noexcept
{
    fastRetxFired = false;
}

uint32_t TcpLossRecovery::dupAckCount() const noexcept
{
    return dupAcks;
}
}
