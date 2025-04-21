// TopologyGenerator.h

#ifndef TOPOLOGY_GENERATOR_H
#define TOPOLOGY_GENERATOR_H

#define TEMPLATE_ID "1e1e8090-0f15-4500-899c-bb115569e069"
#define CLOUD_ID "39e257dc-8412-3174-b6b3-0ee3ed6a43e9"

#include <Gns3Harness.hpp>
#include <vector>
#include <variant>

namespace Topology
{
    enum class NodeType
    {
        Router,
        Virtual
    };

    namespace Settings
    {
        struct Mesh
        {
            int nodeCount;
            std::string baseName = "R";
            int spacing = 200;
            bool layoutGrid = true;
        };

        struct Ring
        {
            int nodeCOunt;
            std::string baseName = "R";
            int radius = 300;
            int centerX = 300;
            int centerY = 300;
        };

        struct Star
        {
            int spokeCount;
            std::string baseName = "R";
            bool centerIsVirtual = true;
            int radius = 300;
            int centerX = 300;
            int centerY = 300;
        };

        struct Tree
        {
            int depth = 3;
            int virtualNodeIndex = 1;
            int branchingFactor = 3;
            std::string baseName = "R";
            bool chainChildren = false;
            int startX = 300;
            int startY = 300;
            int spacingX = 200;
            int spacingY = 200;
        };

        struct PointToPoint
        {
            std::vector<NodeType> nodeSequence;
            int startX = 300;
            int StartY = 300;
            int spacing = 300;
        };

        struct CustomLayoutSettings
        {
            struct LinkStyle
            {
                bool broadcast = false;
                int bandwidth = 1000000;
                int delayMs = 0;
            };

            struct NodeSpec
            {
                std::string name;
                NodeType type = NodeType::Router;
                int x = 0;
                int y = 0;
            };

            struct LinkSpec
            {
                std::string fromNode;
                std::string toNode;
                std::string fromIface;
                std::string toIface;
                LinkStyle style;
            };

            std::vector<NodeSpec> nodes;
            std::vector<LinkSpec> links;
        };
    }

    using TopologyVarient = std::variant<
        Settings::Mesh,
        Settings::Ring,
        Settings::Star,
        Settings::Tree,
        Settings::PointToPoint,
        Settings::CustomLayoutSettings
    >;

    struct TopologyNode
    {
        std::string name;
        nlohmann::json config;
        std::unordered_map<std::string, std::pair<std::string, TopologyNode*>> links;
        std::unordered_map<std::string, std::string> interfaceIPs;
    };

    struct TopologyResult
    {
        std::vector<TopologyNode*> nodeList;
        std::string projectId;
        TopologyNode* node; // Virtual node

        TopologyResult() = default;
        ~TopologyResult() 
        {
            for (const auto&  node : nodeList)
            {
                delete node;
            }
            nodeList.clear();
        }
    };

    class SubnetAllocator
    {
    public:
        SubnetAllocator(uint32_t base = 0xC0A80000)
            : current(base) {}

        std::pair<std::string, std::string> nextPair();
    private:
        uint32_t current;
        static std::string ipFromInt(uint32_t ip);
    };

    class TopologyGenerator
    {
    public:
        explicit TopologyGenerator(Gns3Harness& h);
        
        Topology::TopologyResult generate(const Topology::TopologyVarient& config);

        ~TopologyGenerator();

    private:

        Gns3Harness& h;
        std::string projectId;
        bool activeProject = false;
        SubnetAllocator allocator;

        // Intervals
        nlohmann::json addRouter(const std::string& name, int x, int y);
        nlohmann::json addVirtualRouter(const std::string& name, int x, int y);

        std::string getNextAvailableInterface(const TopologyNode* node);
        void link(TopologyNode* a, TopologyNode* b);

        Topology::TopologyResult generateMesh(const Topology::Settings::Mesh&);
        Topology::TopologyResult generateRing(const Topology::Settings::Ring&);
        Topology::TopologyResult generateStar(const Topology::Settings::Star&);
        Topology::TopologyResult generateTree(const Topology::Settings::Tree&);
        Topology::TopologyResult generatePointToPoint(const Topology::Settings::PointToPoint&);
        Topology::TopologyResult generateCustom(const Topology::Settings::CustomLayoutSettings&);
    };

}

#endif
