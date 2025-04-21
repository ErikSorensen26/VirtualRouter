#include <TopologyGenerator.h>

int main() {
    Gns3Harness g;
    Topology::TopologyGenerator top(g);

    Topology::Settings::PointToPoint p2p;
    p2p.nodeSequence = {Topology::NodeType::Router, Topology::NodeType::Router, Topology::NodeType::Virtual, Topology::NodeType::Router };
    auto item = top.generate(p2p);
    std::cout << "done"  << std::endl;
}
