#include <Terminal.h>
#include <Logger.h>

int main() 
{
    Logger::getInstance().initialize(true, /*isolateMode*/true);

    Terminal* terminal = new Terminal(false);
    while (true) {
        terminal->handleInput();
    }
    delete terminal;
    std::string bin;
    std::cin >> bin;
    return 0;
}


// #include <Decapsulation.h>
// #include <Encapsulation.h>
// #include <Profiler.hpp>
// #include <gperftools/profiler.h>
// 
// int main() {
//     int threadNum = std::thread::hardware_concurrency() / 2;
//     std::cout << threadNum << std::endl;
//     std::atomic<uint64_t> globalCount{0};
//     bool stop_flag = false;
// 
//     std::vector<std::thread> threads;
//     for (int i = 0; i < threadNum; ++i)
//     {
//         threads.emplace_back([&]() {
//             uint64_t localCount = 0;
//             while(!stop_flag)
//             {
// 
//                 PacketInfo packetInfo;
//                 EthernetHeader eth;
//                 eth.destinationMac = std::string("\xFF\xFF\xFF\xFF\xFF\xFF", 6);
//                 eth.sourceMac = std::string("\xAA\xBB\xCC\xDD\xEE\xFF", 6);
//                 eth.type = std::string("\x08\x00", 2); // MPLS Unicast
//                 packetInfo.Layer2.push_back(eth);
// 
//                 IPv4Header ipv4;
//                 ipv4.version = "4";
//                 ipv4.headerLength = "5";
//                 ipv4.serviceField = std::string("\x00", 1);
//                 ipv4.totalLength = std::string("\x00\x00", 2); // Total Length=20
//                 ipv4.identification = std::string("\x00\x01", 2);
//                 ipv4.fragmentFlag.reserved = "0";
//                 ipv4.fragmentFlag.fragment = "1";
//                 ipv4.fragmentFlag.moreFragment = "0";
//                 ipv4.fragmentFlag.fragmentOffset = "0000000000000";
//                 ipv4.TTL = std::string("\x40", 1);
//                 ipv4.protocol = std::string("\x00", 1);
//                 ipv4.checksum = std::string("\x00\x00", 2);
//                 ipv4.sourceAddress = std::string("\xC0\xA8\x01\x01", 4); // 192.168.1.1
//                 ipv4.destinationAddress = std::string("\xC0\xA8\x01\x02", 4); // 192.168.1.2
//                 packetInfo.Layer3.push_back(std::move(ipv4));
// 
//                 ByteString encapsulated = ByteString(1000, 0xFF);
// 
//                 auto result = encapsulate(packetInfo, encapsulated);
//                 Packet pak(result.value());
//                 pak.inspection(result.value());
// 
//                 ++localCount;
//             }
//             globalCount.fetch_add(localCount, std::memory_order_relaxed);
//         });
//     }
// 
//     std::this_thread::sleep_for(std::chrono::seconds(10));
//     stop_flag = true;
//     for (auto& thread : threads)
//     {
//         if (thread.joinable())
//         {
//             thread.join();
//         }
//     }
// 
//     std::cout << globalCount.load() << std::endl;
//        Profiler::getInstance().notify("End");
//        Profiler::getInstance().print();
//     return 0;
// }
