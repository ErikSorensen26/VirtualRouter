// Finish auth hash stuff
// add 0003 and 0002 tlv for classic
#include <Eigrp.h>
#include <InterfaceConfigs.h>
#include <Encapsulation.h>
#include <VirtualRouter.h>
#include <Interface.h>
#include <algorithm>
#include <Global.h>
#include <IPPacket.h>
#include <Functions.h>
#include <PacketBuilder.hpp>
#include <InterfaceConfigs.h>
#include <Encryption.hpp>

#pragma region Eigrp

namespace Protocol
{





#pragma endregion

#pragma region EigrpInterface

        
#pragma endregion

#pragma region ClassicEigrp

    

    void ClassicEigrp::initializeEigrp()
    {
        // Call base class initialziation
        Eigrp::initializeEigrp();

        // Classic-specific configuration
        configs.autoSummarizationEnabled.store(true, std::memory_order_release);
        Logger::getInstance().info() << "Classic EIGRP-specific initialization complete" << std::endl;
    }

    void ClassicEigrp::shutdown()
    {
        Logger::getInstance().info() << "Shutting down Classic EIGRP." << std::endl;

        // Call base class shutdown
        Eigrp::shutdown();

        // Clear network configurations
        configs.networks.clear();
        Logger::getInstance().info() << "Cleared all Classic-configured networks." << std::endl;
    }

#pragma endregion

#pragma region NamedEigrp

    NamedEigrp::NamedEigrp(uint32_t& as, AddressFamily af, const std::string& name, VirtualRouter* vrf, bool multicast)
        : Eigrp(as, af, vrf), processName(name) {}

    void NamedEigrp::initializeEigrp() {
        // Call base class initialization
        Eigrp::initializeEigrp();

        // Named-soecific configuration
        configs.autoSummarizationEnabled.store(false, std::memory_order_release);
        Logger::getInstance().info() << "Initialized Named EIGRP (" << processName << ")." << std::endl;
    }

    void NamedEigrp::shutdown() {
        Logger::getInstance().info() << "Shutting down Named EIGRP (" << processName << ")." << std::endl;

        // Call base class shutdown
        Eigrp::shutdown();

        // Clear interface configurations
        Logger::getInstance().info() << "Cleared interface-specific configurations." << std::endl;
    }

#pragma endregion

#pragma region Topology

}

#pragma endregion
