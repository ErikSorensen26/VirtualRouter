// IngressBase.cpp

#include "IngressBase.h"
#include <Interface.h>
#include <Global.h>
#include <VirtualRouter.h>
#include <Process.h>
#include <Decapsulation.h>
#include <poll.h>
#include <stdexcept>

void IngressBase::start()
{
    running.store(true, std::memory_order_release);
    ingressThread = std::thread([this]{ this->runLoop(); });
}

void IngressBase::stop()
{
    stopRx();
    // let backend wait
    waitUntilAllFramesReleased();
    running.store(false, std::memory_order_release);
    if (ingressThread.joinable()) ingressThread.join();
}

void IngressBase::runLoop()
{
    FrameView frame;
    while (true)
    {
        if (!running.load(std::memory_order_acquire)) break;

        bool any = false;
        while (pollFrame(frame))
        {
            any = true;
            iface.routingInstance->global.threadPool.enqueue([this, frm = frame]() {
                PacketInfo packetInfo;
                inspect(packetInfo, frm.payload, frm.length);
                decapsulate(packetInfo, frm.payload, frm.length);
                processPacket(frm.payload, frm.length, packetInfo, iface.routingInstance, &iface);
                releaseFrame(frm.index);
            });
        }
        if (!any) waitForData();
    }
}

