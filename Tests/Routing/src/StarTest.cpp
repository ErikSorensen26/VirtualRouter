#include <TopologyGenerator.h>

int main() {
    Gns3Harness g;
    Topology::TopologyGenerator top(g);

    Topology::Settings::Star star;
    star.baseName = "hi";
    star.centerIsVirtual = true;
    star.spokeCount = 4;
    auto item = top.generate(star);
    std::cout << "done"  << std::endl;
}
