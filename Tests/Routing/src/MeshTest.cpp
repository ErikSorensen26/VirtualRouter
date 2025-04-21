#include <TopologyGenerator.h>

int main() {
    Gns3Harness g;
    Topology::TopologyGenerator top(g);

    Topology::Settings::Mesh mesh;
    mesh.baseName = "mesh";
    mesh.layoutGrid = true;
    mesh.nodeCount = 4;
    auto item = top.generate(mesh);
    std::cout << "done"  << std::endl;
}
