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
    recover.clear();

    hwManager.addHardware(stfs.hwConfigFile, fileSystem, enableDummies);

    routerConfigFilename = stfs.routerConfigFile;

    // TODO reload the startup configuration once utils::json supports serialization
}

}
