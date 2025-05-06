#include "TopologyGenerator.h"
#include <cmath>
#include <unordered_set>
#include <CliSession.h>
#include <CliEngine.h>
#include <future>

using namespace Topology;

void TopologyGenerator::configVirtualSession(CliSession* session)
{
    if (session->engine.physicalInterfaces.size() < topology->node->interfaceIPs.size())
    {
        throw std::runtime_error("Not enough interfaces to preform test.");
    }
    
    session->handleInput("enable\n");
    session->handleInput("configure terminal\n");

    for (const auto& [interface, ip] : topology->node->interfaceIPs)
    {
        for (int i = 0; i < session->engine.physicalInterfaces.size(); i++)
        {
            if (session->engine.physicalInterfaces[i] == interface)
            {
                session->handleInput("interface GigabitEthernet " + std::to_string(i) + "\n");
                session->handleInput("ip address " + ip + " 255.255.255.252\n");
                session->handleInput("no shutdown\n");
            }
        }
    }
    for (const auto& [interface, ip] : topology->node->interfaceIPv6s)
    {
        for (int i = 0; i < session->engine.physicalInterfaces.size(); i++)
        {
            if (session->engine.physicalInterfaces[i] == interface)
            {
                session->handleInput("ipv6 unicast-routing\n");
                session->handleInput("interface GigabitEthernet " + std::to_string(i) + "\n");
                session->handleInput("ipv6 address " + ip + "/64\n");
                session->handleInput("no shutdown\n");
            }
        }
    }
}

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

std::string SubnetAllocator::ipFrom128(__uint128_t ip)
{
    uint16_t parts[8];
    for (int i = 0; i < 8; ++i)
    {
        parts[i] = (ip >> ((7 - i) * 16)) & 0xFFFF;
    }

    // Find longest sequence of zero blocks for "::" compression
    int bestStart = -1, bestLen = 0;
    for (int i = 0; i < 8;)
    {
        if (parts[i] == 0)
        {
            int j = i;
            while (j < 8 && parts[j] == 0) ++j;
            int len = j - i;
            if (len > bestLen)
            {
                bestStart = i;
                bestLen = len;
            }
            i = j;
        }
        else
        {
            ++i;
        }
    }

    std::ostringstream oss;
    for (int i = 0; i < 8;)
    {
        if (i == bestStart)
        {
            oss << "::";
            i += bestLen;
            if (i >= 8) break;
        }
        else
        {
            if (i > 0 && i != bestStart + bestLen) oss << ":";
            oss << std::hex << parts[i];
            ++i;
        }
    }

    std::string out = oss.str();
    if (out == "") out = "::";
    return out;
}

std::pair<std::string, std::string> SubnetAllocator::nextPair(bool ipv6)
{
    if (!ipv6)
    {
        uint32_t base = current;
        current += 4; // /30
        return { ipFromInt(base + 1), ipFromInt( base + 2 ) };
    }
    else
    {
        __uint128_t base = currentv6;
        currentv6 += (__uint128_t(1) << 64);
        return { ipFrom128(base + 1), ipFrom128(base + 2) };
    }
}

TopologyGenerator::TopologyGenerator(Gns3Harness& harness) : h(harness) {}

TopologyGenerator::~TopologyGenerator()
{
    for (auto& [_, client] : telnetClients)
    {
        client->Disconnect();
        delete client;
    }
    telnetClients.clear();
    if (activeProject)
        h.deleteProject(projectId);
    if (topology)
        delete topology;
}

nlohmann::json TopologyGenerator::addRouter(const std::string& name, int x, int y)
{
    return h.addNode(projectId, TEMPLATE_ID, name, x, y);
}

nlohmann::json TopologyGenerator::addVirtualRouter(const std::string& name, int x, int y)
{
    return h.addNode(projectId, CLOUD_ID, name, x, y);
}

void TopologyGenerator::generate(const TopologyVarient& config)
{
    projectId = h.newProject(Functions::generateRandomString(10));
    activeProject = true;

    std::visit([&](const auto& c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, Settings::Mesh>) {generateMesh(c); return;}
        else if constexpr (std::is_same_v<T, Settings::Ring>) {generateRing(c); return;}
        else if constexpr (std::is_same_v<T, Settings::Star>) {generateStar(c); return;}
        else if constexpr (std::is_same_v<T, Settings::Tree>) {generateTree(c); return;}
        else if constexpr (std::is_same_v<T, Settings::PointToPoint>) {generatePointToPoint(c); return;}
        else if constexpr (std::is_same_v<T, Settings::CustomLayoutSettings>) {generateCustom(c); return;}
        else throw std::runtime_error("Unhandled topology config");
    }, config);

    h.startAll(projectId);
    applyIPConfiguration();
}

std::string TopologyGenerator::getNextAvailableInterface(const TopologyNode* node)
{
    std::unordered_set<std::string> used;
    for (const auto& [iface, _] : node->interfaceIPs)
        used.insert(iface);
    for (const auto& [iface, _] : node->interfaceIPv6s)
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
    auto [ipv6A, ipv6B] = allocator.nextPair(true);

    std::string ifaceA = getNextAvailableInterface(a);
    std::string ifaceB = getNextAvailableInterface(b);

    a->interfaceIPs[ifaceA] = ipA;
    b->interfaceIPs[ifaceB] = ipB;

    a->links[ifaceB] = {ipB, b};
    b->links[ifaceA] = {ipA, a};

    a->interfaceIPv6s[ifaceA] = ipv6A;
    b->interfaceIPv6s[ifaceB] = ipv6B;

    a->v6links[ifaceB] = {ipv6B, b};
    b->v6links[ifaceA] = {ipv6A, a};

    h.link(projectId, a->config, ifaceA, b->config, ifaceB);
}

void TopologyGenerator::generateMesh(const Settings::Mesh& cfg)
{
    if (topology) delete topology;
    topology = new TopologyResult();
    topology->projectId = projectId;

    for (int i = 0; i < cfg.nodeCount; ++i)
    {
        std::string name = cfg.baseName + std::to_string(i + 1);
        int x = cfg.layoutGrid ? cfg.spacing * (i % static_cast<int>(std::ceil(std::sqrt(cfg.nodeCount)))) : i * cfg.spacing;
        int y = cfg.layoutGrid ? cfg.spacing * (i / static_cast<int>(std::ceil(std::sqrt(cfg.nodeCount)))) : 0;
        if (topology->nodeList.empty())
        {
            auto json = addVirtualRouter("VR", x, y);
            uint16_t port = 0;
            auto* node = new TopologyNode{ "VR", json, port };
            topology->node = node;
            topology->nodeList.push_back(node);
        }
        else
        {
            auto json = addRouter(name, x, y);
            uint16_t port = 0;
            if (json.is_object() && json.contains("console") && !json["console"].is_null())
            {
                port = json["console"];
            }
            auto* node = new TopologyNode{ name, json, port };
            topology->nodeList.push_back(node);
        }
    }

    for (size_t i = 0; i < topology->nodeList.size(); ++i) 
    {
        for (size_t j = i; j < topology->nodeList.size(); ++j)
        {
            link(topology->nodeList[i], topology->nodeList[j]);
        }
    }
}

void TopologyGenerator::generateRing(const Settings::Ring& cfg)
{
    if (topology) delete topology;
    topology = new TopologyResult();
    topology->projectId = projectId;

    auto jsonVR = addVirtualRouter("VR", cfg.centerX + cfg.radius, cfg.centerY);
    auto* vr = new TopologyNode{"VR", jsonVR};
    topology->nodeList.push_back(vr);
    topology->node = vr;

    for (int i = 1; i < cfg.nodeCount; ++i)
    {
        std::string name = cfg.baseName + std::to_string(i + 1);
        double angle = 2 * M_PI * i / cfg.nodeCount;
        int x = cfg.centerX + std::cos(angle) * cfg.radius;
        int y = cfg.centerY + std::sin(angle) * cfg.radius;
        auto json = addRouter(name, x, y);
        uint16_t port = 0;
        if (json.is_object() && json.contains("console") && !json["console"].is_null())
        {
            port = json["console"];
        }
        auto* node = new TopologyNode{ name, json, port };
        topology->nodeList.push_back(node);
    }

    for (int i = 0; i < topology->nodeList.size(); ++i)
        link(topology->nodeList[i], topology->nodeList[(i + 1) % topology->nodeList.size()]);
}

void TopologyGenerator::generateStar(const Settings::Star& cfg)
{
    if (topology) delete topology;
    topology = new TopologyResult();
    topology->projectId = projectId;

    auto center = cfg.centerIsVirtual
        ? addVirtualRouter("VR", cfg.centerX, cfg.centerY)
        : addRouter("R1", cfg.centerX, cfg.centerY);

    TopologyNode* centerNode = nullptr;

    bool vNodePlaced = false;

    vNodePlaced = cfg.centerIsVirtual;
    uint16_t port = 0;
    if (center.is_object() && center.contains("console") && !center["console"].is_null())
    {
        port = center["console"];
    }
    centerNode = new TopologyNode{ cfg.centerIsVirtual ? "VR" : "R1", center, port, };
    topology->nodeList.push_back(centerNode);
    if (cfg.centerIsVirtual)
    {
        topology->node = centerNode;
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
            uint16_t port = 0;
            if (json.is_object() && json.contains("console") && !json["console"].is_null())
            {
                port = json["console"];
            }
            auto* node = new TopologyNode{ name, json, port };
            topology->nodeList.push_back(node);
            link(centerNode, node);
            topology->node = node;
            vNodePlaced = true;
        }
        else
        {
            auto json = addRouter(name, x, y);
            uint16_t port = 0;
            if (json.is_object() && json.contains("console") && !json["console"].is_null())
            {
                port = json["console"];
            }
            auto* node = new TopologyNode{ name, json, port };
            topology->nodeList.push_back(node);
            link(centerNode, node);
        }
    }
}

void TopologyGenerator::generateTree(const Settings::Tree& cfg) 
{
    if (topology) delete topology;
    topology = new TopologyResult();
    topology->projectId = projectId;

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

        uint16_t port = 0;
        if (json.is_object() && json.contains("console") && !json["console"].is_null())
        {
            port = json["console"];
        }
        auto* node = new TopologyNode{ isVR ? "VR" : name, json, port };
        topology->nodeList.push_back(node);
        if (isVR) topology->node = node;
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
}

void TopologyGenerator::generatePointToPoint(const Settings::PointToPoint& cfg) 
{
    if (topology) delete topology;
    topology = new TopologyResult();
    topology->projectId = projectId;

    int x = cfg.startX;
    int y = cfg.StartY;

    for (size_t i = 0; i < cfg.nodeSequence.size(); ++i) 
    {
        std::string name = "N" + std::to_string(i + 1);
        auto json = cfg.nodeSequence[i] == NodeType::Virtual
            ? addVirtualRouter(name, x, y)
            : addRouter(name, x, y);
        uint16_t port = 0;
        if (json.is_object() && json.contains("console") && !json["console"].is_null())
        {
            port = json["console"];
        }
        auto* node = new TopologyNode{ name, json, port };
        topology->nodeList.push_back(node);
        x += cfg.spacing;
    }

    for (size_t i = 0; i < topology->nodeList.size() - 1; ++i)
    {
        link(topology->nodeList[i], topology->nodeList[i + 1]);
    }
}

void TopologyGenerator::generateCustom(const Settings::CustomLayoutSettings& cfg) 
{
    if (topology) delete topology;
    topology = new TopologyResult();
    topology->projectId = projectId;

    std::unordered_map<std::string, TopologyNode*> lookup;

    for (const auto& n : cfg.nodes) 
    {
        auto json = isVirtual(n.name)
            ? addVirtualRouter(n.name, n.x, n.y)
            : addRouter(n.name, n.x, n.y);
        uint16_t port = 0;
        if (json.is_object() && json.contains("console") && !json["console"].is_null())
        {
            port = json["console"];
        }
        auto* node = new TopologyNode{ n.name, json, port };
        topology->nodeList.push_back(node);
        lookup[n.name] = node;
    }

    for (const auto& linkSpec : cfg.links) {
        auto* from = lookup.at(linkSpec.fromNode);
        auto* to = lookup.at(linkSpec.toNode);
        link(from, to);
    }
}

void TopologyGenerator::applyIPConfiguration()
{
    std::vector<std::future<std::pair<Topology::TopologyNode*, bool>>> futures;

    // Launch all waites in parallel
    for (auto& node : topology->nodeList)
    {
        if (node->port == 0) continue;

        nlohmann::json nodeJson = node->config;

        futures.emplace_back(std::async(std::launch::async, [&, nodeConfig = nodeJson]() {
            bool success = h.waitForBoot(projectId, nodeConfig);
            return std::make_pair(node, success);
        }));
    }

    // Collect results
    for (auto& future : futures)
    {
        auto [node, ok] = future.get();
        if (!ok)
        {
            throw std::runtime_error("Timed out waiting for node to boot: " + node->name);
        }
    }

    // Configure each node
    for (auto* node : topology->nodeList)
    {
        if (!node || !node->config.contains("console") || node->config["console"].is_null()) continue;

        std::vector<std::string> configLines = {
            "enable",
            "configure terminal"
        };

        for (const auto& [iface, ip] : node->interfaceIPs)
        {
            configLines.push_back("interface " + iface);
            configLines.push_back("ip address " + ip + " 255.255.255.252");
            configLines.push_back("no shutdown");
            configLines.push_back("exit");
        }
        for (const auto& [iface, ip] : node->interfaceIPv6s)
        {
            configLines.push_back("ipv6 unicast-routing");
            configLines.push_back("interface " + iface);
            configLines.push_back("ipv6 address " + ip + "/64");
            configLines.push_back("no shutdown");
            configLines.push_back("exit");
        }

        const std::string& ip = h.getHost();
        uint16_t port = node->config["console"];

        h.addConfig(ip, port, configLines);
    }
}

bool TopologyGenerator::sendCommand(const std::string& command, uint16_t port)
{
    if (port == 0) return false;
    if (telnetClients.find(port) == telnetClients.end())
    {
        TelnetClient* telnet = new TelnetClient();
        telnet->Connect(h.getHost(), port);
        telnetClients[port] = telnet;
    }

    TelnetClient* client = telnetClients[port];
    client->SendCommand(command);
    return true;
}

void TopologyGenerator::sendCommandAll(const std::string& command)
{
    for (const auto& node : topology->nodeList)
    {
        if (node->port != 0)
        {
            sendCommand(command, node->port);
        }
    }
}

bool TopologyGenerator::setUpSniffer(CliSession* session)
{
    if (!topology) return false;

    for (const auto& sniffer : sniffers)
    {
        delete sniffer;
    }
    sniffers.clear();

    for (int i = 0; i < topology->node->interfaceIPs.size(); i++)
    {
        std::string name = session->engine.physicalInterfaces.at(i);
        PacketSniffer* sniffer = new PacketSniffer(name);
        sniffer->start();
        sniffers.push_back(sniffer);
    }
    return true;
}
