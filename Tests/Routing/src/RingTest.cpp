#include <TopologyGenerator.h>

int main() {
    Gns3Harness g;
    Topology::TopologyGenerator top(g);

    Topology::Settings::Ring ring;
    ring.baseName = "hi";
    ring.nodeCOunt = 5;
    auto item = top.generate(ring);
    std::cout << "done"  << std::endl;
}
