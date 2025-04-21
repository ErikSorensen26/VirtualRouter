#include <TopologyGenerator.h>

int main() {
    Gns3Harness g;
    Topology::TopologyGenerator top(g);

    Topology::Settings::Tree tree;
    tree.baseName = "hi";
    tree.branchingFactor = 3;
    tree.depth = 3;
    tree.virtualNodeIndex = 2;
    auto item = top.generate(tree);
    std::cout << "done"  << std::endl;
}
