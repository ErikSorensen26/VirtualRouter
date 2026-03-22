// Collision.cpp

#include "Collision.h"
#include "packet/headers/BgpHeader.hpp"

namespace BGP
{
bool CollisionDetector::shouldKeep(bool isOutgoing, uint32_t localRid, uint32_t peerRid) noexcept
{
    if (localRid == peerRid)
        return false;

    if (localRid > peerRid)
        return isOutgoing;
    else
        return !isOutgoing;
}

uint16_t CollisionDetector::collisionNotificationCode() noexcept
{
    return BGP_NOTIFICATION_CEASE_COLLISION_RESOLUTION;
}
}
