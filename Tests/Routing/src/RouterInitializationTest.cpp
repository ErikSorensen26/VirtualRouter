#include <Gns3Harness.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <Functions.h>

int main()
{
    //try
    {
        Gns3Harness gns;

        std::string name = Functions::generateRandomString(10);
        std::string project = gns.newProject(name);

        nlohmann::json cloud = gns.addCloud(project, "cloud", {"test0", "test1"}, -200, 0);

        std::string template_id = "1e1e8090-0f15-4500-899c-bb115569e069";
        nlohmann::json r1 = gns.addNode(project, template_id, "R1", 0, -100);
        std::cout << "r1 " << r1["console"] << std::endl;
        nlohmann::json r2 = gns.addNode(project, template_id, "R2", 0, 100);
        std::cout << "r2 " << r2["console"] << std::endl;

        gns.link(project, r1, "GigabitEthernet0/0", r2, "GigabitEthernet0/0");
        gns.link(project, r1, "GigabitEthernet1/0", cloud, "test0");
        gns.link(project, r2, "GigabitEthernet1/0", cloud, "test1");


        gns.startAll(project);

        gns.waitForBoot(project, r1);

        //gns.stopAll(project);
        //gns.deleteProject(project);
        //std::cout << "[+] Project deleted.\n";
    }
    // catch (const std::exception& e)
    // {
    //     std::cerr << "[!] ERROR: " << e.what() << "\n";
    //     return 1;
    // }
};

