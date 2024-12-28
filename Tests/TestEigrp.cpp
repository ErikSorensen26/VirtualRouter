#include <RouterSetup.h>
#include <iostream>

int main()
{
    std::string configFile = "../Tests/Configs/Topology1.json";
    RouterSetup setup(configFile);
    setup.processRouters();
    std::cout << "All routers have been configured successfully!" << std::endl;
    return 0;
}
