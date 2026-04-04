// Configs.cpp

// TODO add hex range volatile value

#include <iostream>

#include <IPAddress.h>
#include "Configs.h"
#include "CliUtils.h"

namespace cli
{
void Configs::printConfig() 
{
    //std::cout << root.dump(4) << std::endl;
}

Configs::Configs(FileSystem& fs)
    : fileSystem(fs)
{}

void Configs::initConfigs(const StartupFiles& stfs, bool enableDummies)
{
    // Reset all variables before
    root.clear();

    hwManager.addHardware(stfs.hwConfigFile, fileSystem, enableDummies);

    /*if (configSchema.is_null() || !configSchema.is_object())
    {
        configSchema = nlohmann::ordered_json::object();
    }*/

    routerConfigFilename = stfs.routerConfigFile;

    // Load JSON configuration file into doc
    if (fileSystem.fileExists(stfs.startupFile))
    {
        std::string content;
        if (fileSystem.readFile(stfs.startupFile, content))
        {
            try
            {
                root = nlohmann::ordered_json::parse(content);
            }
            catch (json::parse_error& e)
            {
                root = nlohmann::ordered_json::object();
            }
        }
    }
    else 
    {
        root = nlohmann::ordered_json::object();
    }
}


std::vector<std::string> Configs::recoverConfigs(nlohmann::ordered_json* json)
{
    // TODO
}

}
