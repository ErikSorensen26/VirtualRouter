#include <RouterSetup.h>
#include <fstream>
#include <exception>
#include <iostream>
#include <thread>
#include <chrono>
/*
using json = nlohmann::json;

RouterSetup::RouterSetup(const std::string& configFile)
{
    parseConfigFile(configFile);
}

void RouterSetup::parseConfigFile(const std::string& configFile)
{
    std::ifstream file(configFile);
    if (!file.is_open())
    {
        throw std::runtime_error("Failed to open configuration file.");
    }

    json configJson;
    file >> configJson;

    for (const auto& router : configJson["routers"])
    {
        RouterConfig routerConfig;

        // Check if essential keys exist
        if (router.contains("name")) routerConfig.name = router["name"];
        else throw std::runtime_error("Missing 'name' key in router configuration.");

        if (router.contains("telnetIp")) routerConfig.telnetIp = router["telnetIp"];
        else throw std::runtime_error("Missing 'telnetIp' key in router configuration.");

        if (router.contains("telnetPort")) routerConfig.telnetPort = router["telnetPort"];
        else throw std::runtime_error("Missing 'telnetPort' key in router configuration.");

        if (router.contains("interfaces"))
        {
            for (const auto& iface : router["interfaces"])
            {
                InterfaceConfig interfaceConfig;
                if (iface.contains("name")) interfaceConfig.name = iface["name"];
                else throw std::runtime_error("Missing 'name' key in interface configuration.");

                if (iface.contains("ip")) interfaceConfig.ip = iface["ip"];
                else throw std::runtime_error("Missing 'ip' key in interface configuration.");

                if (iface.contains("subnet")) interfaceConfig.subnet = iface["subnet"];
                else throw std::runtime_error("Missing 'subnet' key in interface configuration.");

                routerConfig.interfaces.push_back(interfaceConfig);
            }
        }
        else throw std::runtime_error("Missing 'interfaces' key in router configuration.");

        if (router.contains("eigrp"))
        {
            auto ospfConfig = router["eigrp"];
            if (ospfConfig.contains("processID")) routerConfig.eigrp.processID = ospfConfig["processID"];
            else throw std::runtime_error("Missing 'processID' key in EIGRP configuration.");

            if (ospfConfig.contains("networks"))
            {
                for (const auto& network : ospfConfig["networks"])
                {
                    if (network.contains("ip") && network.contains("wildcard"))
                    {
                        routerConfig.eigrp.networks.push_back(
                            std::make_pair(network["ip"], network["wildcard"])
                        );
                    }
                    else
                    {
                        throw std::runtime_error("Missing keys in EIGRP network configuration.");
                    }
                }
            }
            else throw std::runtime_error("Missing 'networks' key in EIGRP configuration.");
        }
        routerConfig.enableRestconf = true;
        routerConfigs.push_back(routerConfig);
    }
}

void RouterSetup::processRouters()
{
    for (const auto& routerConfig : routerConfigs)
    {
        try 
        {
            configureRouter(routerConfig);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error configuring router " << routerConfig.name << ": " << e.what() << std::endl;
        }
    }
}

void RouterSetup::configureRouter(const RouterConfig& routerConfig)
{
    TelnetManager telnet;
    std::cout << "Configuring router: " << routerConfig.name << std::endl;

    telnet.Connect(routerConfig.telnetIp, routerConfig.telnetPort);
    telnet.SendCommand("\r\n");
    telnet.SendCommand("end");
    telnet.SendCommand("enable");
    telnet.SendCommand("configure terminal");

    // Configure interfaces
    for (const auto& iface : routerConfig.interfaces)
    {
        telnet.SendCommand("interface " + iface.name);
        telnet.SendCommand("ip address " + iface.ip + " " + iface.subnet);
        telnet.SendCommand("no shutdown");
        telnet.SendCommand("exit");
    }

    // Configure OSPF
    if (routerConfig.eigrp.processID != 0)
    {
        configureEigrp(telnet, routerConfig.eigrp);
    }

    // Enable RESTCONF
    if (routerConfig.enableRestconf)
    {
        configureRestconf(telnet);
    }

    telnet.SendCommand("end");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    telnet.Disconnect();

    std::cout << "Finishing configuring router: " << routerConfig.name << std::endl;
}

void RouterSetup::configureEigrp(TelnetManager& telnet, const EigrpConfig& eigrp)
{
    std::cout << "Configure EIGRP Process ID: " << eigrp.processID << std::endl;

    telnet.SendCommand("router eigrp " + std::to_string(eigrp.processID));

    for (const auto& network : eigrp.networks)
    {
        telnet.SendCommand("network " + std::get<0>(network) + " " + std::get<1>(network));
    }

    telnet.SendCommand("exit");
}

void RouterSetup::configureRestconf(TelnetManager& telnet)
{
    std::cout << "Enabling RESTCONF on the server..." << std::endl;

    telnet.SendCommand("ip http server");
    telnet.SendCommand("ip http secure-server");
    telnet.SendCommand("restconf");
    telnet.SendCommand("exit");

    std::cout << "RESTCONF enabled successfully" << std::endl;
}
*/