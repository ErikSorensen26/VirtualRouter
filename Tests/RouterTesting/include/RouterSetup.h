#pragma once

#include <string>
#include <vector>
#include <TelnetManager.hpp>
#include <json.hpp>
/*
struct InterfaceConfig
{
    std::string name;
    std::string ip;
    std::string subnet;
};

struct EigrpConfig
{
    int processID;
    std::vector<std::pair<std::string, std::string>> networks;
};

struct RouterConfig
{
    std::string name;
    std::string telnetIp;
    int telnetPort;
    std::vector<InterfaceConfig> interfaces;
    EigrpConfig eigrp; 
    bool enableRestconf = false;
};

class RouterSetup
{
public:
    RouterSetup(const std::string& configFile);
    void processRouters();

private:
    void parseConfigFile(const std::string& configFile);
    void configureRouter(const RouterConfig& routerConfig);
    void configureEigrp(TelnetManager& telnet, const EigrpConfig& eigrp);
    void configureRestconf(TelnetManager& telnet);

    std::vector<RouterConfig> routerConfigs;
};
*/