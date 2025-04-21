#include "TopologyGenerator.h"
#include <cmath>
#include <algorithm>
#include <unordered_set>

using namespace Topology;

static bool isVirtual(const std::string name)
{
    return name == "VR" || name.find("VR") == 0;
}

std::string SubnetAllocator::ipFromInt(uint32_t ip)
{
    return std::to_string((ip >> 24) & 0xFF) + "." +
           std::to_string((ip >> 16) & 0xFF) + "." +
           std::to_string((ip >> 8) & 0xFF) + "." +
           std::to_string(ip & 0xFF);
}

std::pair<std::string, std::string> SubnetAllocator::nextPair()
{
    uint32_t base = current;
    current += 4; // /30
    return { ipFromInt(base + 1), ipFromInt( base + 2 ) };
}

TopologyGenerator::TopologyGenerator(Gns3Harness& harness) : h(harness) {}

TopologyGenerator::~TopologyGenerator()
{
    if (activeProject)
        h.deleteProject(projectId);
}

nlohmann::json TopologyGenerator::addRouter(const std::string& name, int x, int y)
{
    return h.addNode(projectId, TEMPLATE_ID, name, x, y);
}

nlohmann::json TopologyGenerator::addVirtualRouter(const std::string& name, int x, int y)
{
    return h.addNode(projectId, CLOUD_ID, name, x, y);
}

TopologyResult TopologyGenerator::generate(const TopologyVarient& config)
{
    projectId = h.newProject(Functions::generateRandomString(10));
    activeProject = true;

    auto result = std::visit([&](const auto& c) -> TopologyResult {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, Settings::Mesh>) return generateMesh(c);
        else if constexpr (std::is_same_v<T, Settings::Ring>) return generateRing(c);
        else if constexpr (std::is_same_v<T, Settings::Star>) return generateStar(c);
        else if constexpr (std::is_same_v<T, Settings::Tree>) return generateTree(c);
        else if constexpr (std::is_same_v<T, Settings::PointToPoint>) return generatePointToPoint(c);
        else if constexpr (std::is_same_v<T, Settings::CustomLayoutSettings>) return generateCustom(c);
        else throw std::runtime_error("Unhandled topology config");
    }, config);

    return result;
}

std::string TopologyGenerator::getNextAvailableInterface(const TopologyNode* node)
{
    std::unordered_set<std::string> used;
    for (const auto& [iface, _] : node->interfaceIPs)
        used.insert(iface);

    for (const auto& port : node->config["ports"])
    {
        std::string iface = port["name"];
        if (!used.count(iface))
            return iface;
    }

    throw std::runtime_error("No available interfaces on node: " + node->name);
}

void TopologyGenerator::link(TopologyNode* a, TopologyNode* b)
{
    if (!a || !b) return;

    auto [ipA, ipB] = allocator.nextPair();

    std::string ifaceA = getNextAvailableInterface(a);
    std::string ifaceB = getNextAvailableInterface(b);

    a->interfaceIPs[ifaceA] = ipA;
    b->interfaceIPs[ifaceB] = ipB;

    a->links[ifaceB] = {ipB, b};
    b->links[ifaceA] = {ipA, a};

    h.link(projectId, a->config, ifaceA, b->config, ifaceB);
}

TopologyResult TopologyGenerator::generateMesh(const Settings::Mesh& cfg)
{
    TopologyResult result;
    result.projectId = projectId;
    result.node->name = "VR";

    for (int i = 0; i < cfg.nodeCount; ++i)
    {
        std::string name = cfg.baseName + std::to_string(i + 1);
        int x = cfg.layoutGrid ? cfg.spacing * (i % static_cast<int>(std::ceil(std::sqrt(cfg.nodeCount)))) : i * cfg.spacing;
        int y = cfg.layoutGrid ? cfg.spacing * (i / static_cast<int>(std::ceil(std::sqrt(cfg.nodeCount)))) : 0;
        if (result.nodeList.empty())
        {
            auto json = addVirtualRouter(name, x, y);
            auto* node = new TopologyNode{ name, json };
            result.node = node;
            result.nodeList.push_back(node);
        }
        else
        {
            auto json = addRouter(name, x, y);
            auto* node = new TopologyNode{ name, json };
            result.nodeList.push_back(node);
        }
    }

    for (size_t i = 0; i < result.nodeList.size(); ++i) 
    {
        for (size_t j = i; j < result.nodeList.size(); ++j)
        {
            link(result.nodeList[i], result.nodeList[j]);
        }
    }

    return result;
}

TopologyResult TopologyGenerator::generateRing(const Settings::Ring& cfg)
{
    TopologyResult result;
    result.projectId = projectId;

    auto jsonVR = addVirtualRouter("VR", cfg.centerX + cfg.radius, cfg.centerY);
    auto* vr = new TopologyNode{"VR", jsonVR};
    result.nodeList.push_back(vr);
    result.node = vr;

    for (int i = 1; i < cfg.nodeCOunt; ++i)
    {
        std::string name = cfg.baseName + std::to_string(i + 1);
        double angle = 2 * M_PI * i / cfg.nodeCOunt;
        int x = cfg.centerX + std::cos(angle) * cfg.radius;
        int y = cfg.centerY + std::sin(angle) * cfg.radius;
        auto json = addRouter(name, x, y);
        auto* node = new TopologyNode{ name, json };
        result.nodeList.push_back(node);
    }

    for (int i = 0; i < result.nodeList.size(); ++i)
        link(result.nodeList[i], result.nodeList[(i + 1) % result.nodeList.size()]);

    return result;
}

TopologyResult TopologyGenerator::generateStar(const Settings::Star& cfg)
{
    TopologyResult result;
    result.projectId = projectId;

    auto center = cfg.centerIsVirtual
        ? addVirtualRouter("VR", cfg.centerX, cfg.centerY)
        : addRouter("R1", cfg.centerX, cfg.centerY);

    TopologyNode* centerNode = nullptr;

    bool vNodePlaced = false;

    vNodePlaced = cfg.centerIsVirtual;
    centerNode = new TopologyNode{ cfg.centerIsVirtual ? "VR" : "R1", center };
    result.nodeList.push_back(centerNode);
    if (cfg.centerIsVirtual)
    {
        result.node = centerNode;
    }

    for (int i = 0; i < cfg.spokeCount; ++i)
    {
        std::string name = cfg.baseName + std::to_string(i + 1);
        double angle = 2 * M_PI * i / cfg.spokeCount;
        int x = cfg.centerX + std::cos(angle) * cfg.radius;
        int y = cfg.centerY + std::sin(angle) * cfg.radius;
        if (!cfg.centerIsVirtual && !vNodePlaced)
        {
            auto json = addVirtualRouter("VR", x, y);
            auto* node = new TopologyNode{ name, json };
            result.nodeList.push_back(node);
            link(centerNode, node);
            result.node = node;
            vNodePlaced = true;
        }
        else
        {
            auto json = addRouter(name, x, y);
            auto* node = new TopologyNode{ name, json };
            result.nodeList.push_back(node);
            link(centerNode, node);
        }
    }

    return result;
}

TopologyResult TopologyGenerator::generateTree(const Settings::Tree& cfg) 
{
    TopologyResult result;
    result.projectId = projectId;

    int id = 1;
    int y = cfg.startY;

    std::vector<TopologyNode*> current;
    std::vector<TopologyNode*> next;

    auto makeNode = [&](int id, int x, int y) -> TopologyNode*
    {
        std::string name = cfg.baseName + std::to_string(id);
        bool isVR = (id == cfg.virtualNodeIndex);
        nlohmann::json json = isVR
            ? addVirtualRouter("VR", x, y)
            : addRouter(name, x, y);

        auto* node = new TopologyNode{ isVR ? "VR" : name, json };
        result.nodeList.push_back(node);
        if (isVR) result.node = node;
        return node;
    };

    auto* root = makeNode(id++, cfg.startX, y);
    current.push_back(root);

    for (int level = 1; level < cfg.depth; ++level) 
    {
        y += cfg.spacingY;
        int x = cfg.startX - (cfg.branchingFactor * cfg.spacingX / 2);

        TopologyNode* lastChild = nullptr;
        for (auto* parent : current) 
        {
            for (int i = 0; i < cfg.branchingFactor; ++i) 
            {
                auto* child = makeNode(id++, x, y);
                link(parent, child);
                x += cfg.spacingX;
                next.push_back(child);
                if (lastChild && cfg.chainChildren)
                {
                    link(lastChild, child);
                }
                lastChild = child;
            }
        }

        current = next;
        next.clear();
    }

    return result;
}

TopologyResult TopologyGenerator::generatePointToPoint(const Settings::PointToPoint& cfg) 
{
    TopologyResult result;
    result.projectId = projectId;

    int x = cfg.startX;
    int y = cfg.StartY;

    for (size_t i = 0; i < cfg.nodeSequence.size(); ++i) 
    {
        std::string name = "N" + std::to_string(i + 1);
        auto json = cfg.nodeSequence[i] == NodeType::Virtual
            ? addVirtualRouter(name, x, y)
            : addRouter(name, x, y);
        auto* node = new TopologyNode{ name, json };
        result.nodeList.push_back(node);
        x += cfg.spacing;
    }

    for (size_t i = 0; i < result.nodeList.size() - 1; ++i) {
        link(result.nodeList[i], result.nodeList[i + 1]);
    }

    return result;
}

TopologyResult TopologyGenerator::generateCustom(const Settings::CustomLayoutSettings& cfg) {
    TopologyResult result;
    result.projectId = projectId;

    std::unordered_map<std::string, TopologyNode*> lookup;

    for (const auto& n : cfg.nodes) {
        auto json = isVirtual(n.name)
            ? addVirtualRouter(n.name, n.x, n.y)
            : addRouter(n.name, n.x, n.y);
        auto* node = new TopologyNode{ n.name, json };
        result.nodeList.push_back(node);
        lookup[n.name] = node;
    }

    for (const auto& linkSpec : cfg.links) {
        auto* from = lookup.at(linkSpec.fromNode);
        auto* to = lookup.at(linkSpec.toNode);
        link(from, to);
    }

    return result;
}
