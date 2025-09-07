// Problem where enqueue is dequeueing and then eventually it stopes anqueueing and then the queue gets full

#include <CliEngine.h>
#include <CliSession.h>
#include <Logger.h>
#include <Global.h>

#include <VirtualRouter.h>
#include <Interface.h>
#include <RxQueueManager.h>
#include <TxQueueManager.h>
#include <TxDistributor.h>
#include <PacketSlot.hpp>
#include <Profiler.h>

int main()
{
    Global router = {};
    VirtualRouter* vr = router.getRoutingInstance("default");
    uint32_t key = calculateInterfaceKey(InterfaceType::ETHERNET, 1);
    router.addInterface(InterfaceType::ETHERNET, "lo", 1024, 1024, "010203040506", 1.0, false);
    Interface* iface = router.getInterface(key);
    vr->addInterface(iface, key);

    TxQueueManager* qmgr = new TxQueueManager();
    qmgr->setCorePool({4});
    qmgr->addInterface(*iface, "lo", {.maxQueues = 1});

    /*RxQueueManager* qmgr = new RxQueueManager();
    qmgr->setCorePool({4});

    qmgr->addInterface(*iface, "wlo1", {.maxQueues = 1});

    std::this_thread::sleep_for(std::chrono::seconds(5));

    uint32_t total = 0;
    for (const auto& [_, opts] : qmgr->ifs)
    {
        for (const auto& t : opts.queues)
        {
            total += t.ingress->totalSeen;
        }
    }*/

    uint32_t total = 0;
    //while (true)
    for (int i = 0; i < 10; ++i)
    {
        Profiler::getInstance().notify("buh " + std::to_string(i));
        FrameHandle f;
        if (iface->tx->getFrame(f))
        {
            f.slot->len = 1000;
            iface->tx->pushTo(f.qid, f.slot);
            ++total;
            if (total % 1000 == 0) std::cout << total << "\n";
        }
    }

    std::cout << total << std::endl;
    Profiler::getInstance().print();
    Profiler::getInstance().write("../profiler.txt");

    delete qmgr;
}

/*int main() 
{
    Logger::getInstance().initialize(true, *//*isolateMode*//*false);
    Global* global = new Global(true
    CliEngine& engine = global->engine;
    auto session = engine.createSession(false);
    while (true) {
        session->handleInput();
    }
    std::string bin;
    std::cin >> bin;
    return 0;
}*/
