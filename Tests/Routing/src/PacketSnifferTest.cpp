#include <PacketSniffer.hpp>
#include <gtest/gtest.h>

TEST(Misc_PacketSniffer, sniffs_shit)
{
    Logger::getInstance().initialize(true);
    PacketSniffer sniffer("test0");

    sniffer.start();

    std::this_thread::sleep_for(std::chrono::seconds(50));
}
