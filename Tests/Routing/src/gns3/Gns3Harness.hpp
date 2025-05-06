// Gns3Harness.hpp

#ifndef GNS3_HARNESS_HPP
#define GNS3_HARNESS_HPP

#include <RestClient.hpp>
#include <TelnetManager.hpp>
#include <json.hpp>
#include <Functions.h>

class Gns3Harness 
{
public:
    explicit Gns3Harness(uint16_t port = 3080, std::string host = "127.0.0.1")
        : api(port, std::move(host)) {}

    std::string newProject(std::string_view name)
    {
        auto rsp = api.post("/v2/projects",
                            nlohmann::json{{"name", Functions::generateRandomString(10)}}.dump());
        return nlohmann::json::parse(rsp)["project_id"];
    }

    void deleteProject(const std::string& id) { api.del("/v2/projects/" + id); }

    std::string getHost() { return api.getHost(); }

    nlohmann::json addNode(const std::string& proj,
                        const std::string& tmpl,
                        std::string_view name,
                        int x, int y) {
        auto rsp = api.post("/v2/projects/" + proj + "/templates/" + tmpl,
            nlohmann::json{
                {"name", name},
                {"compute_id", "local"},
                {"x", x},
                {"y", y}
            }.dump());
        nlohmann::json parsed = nlohmann::json::parse(rsp);
        return parsed;
    }

    nlohmann::json addCloud(const std::string& project,
                         const std::string& name,
                         const std::vector<std::string>& interfaceNames,
                         int x = 0, int y = 0)
    {
        nlohmann::json ifaceList = nlohmann::json::array();
        for (const auto& iface : interfaceNames)
            ifaceList.push_back({ {"name", iface}, {"type", "ethernet"}, {"special", true} });

        nlohmann::json body = {
            {"name", name},
            {"node_type", "cloud"},
            {"compute_id", "local"},
            {"properties", {
                {"interfaces", ifaceList}
            }},
            {"x", x},
            {"y", y}
        };

        std::string rsp = api.post("/v2/projects/" + project + "/nodes", body.dump());
        nlohmann::json parsed = nlohmann::json::parse(rsp);
        return parsed;
    }
    void link(const std::string& proj,
              const nlohmann::json node1,
              const std::string& iface1,
              const nlohmann::json node2,
              const std::string& iface2) 
    {
        auto findAdapterPort = [](const nlohmann::json node, const std::string& name)
            -> std::pair<int, int>
        {
            if (!node.contains("ports") || !node["ports"].is_array())
                throw std::runtime_error("Missing or invalid ports");
            for (const auto& port : node["ports"])
            {
                if (port["name"] == name || port["short_name"] == name)
                {
                    return { port["adapter_number"], port["port_number"] };
                }
            }
            throw std::runtime_error("Interface name not found: " + name);
        };

        auto [a1, p1] = findAdapterPort(node1, iface1);
        auto [a2, p2] = findAdapterPort(node2, iface2);

        nlohmann::json body = {
            {"nodes", {
                {
                    {"node_id", node1["node_id"]},
                    {"adapter_number", a1},
                    {"port_number", p1}
                },
                {
                    {"node_id", node2["node_id"]},
                    {"adapter_number", a2},
                    {"port_number", p2}
                }
            }}
        };

        std::string rsp = api.post("/v2/projects/" + proj + "/links", body.dump());
    }
    void startAll(const std::string& proj) { api.post("/v2/projects/" + proj + "/nodes/start"); }
    void stopAll(const std::string& proj) { api.post("/v2/projects/" + proj + "/nodes/stop"); }

    void addConfig(const std::string& ip, uint16_t port,
                   const std::vector<std::string>& lines)
    {
        TelnetClient t;
        t.Connect(ip, port);
        for (auto& l: lines) t.SendCommand(l);
    }

    bool waitForBoot(const std::string& projectId, const nlohmann::json node, int timeoutSec = 60)
    {
        if (!node.is_object() || !node.contains("console") || node["console"].is_null()) return true;

        TelnetClient telnet;
        {
            uint16_t port = node["console"];
            telnet.Connect(api.getHost(), port);
        }

        const auto start = std::chrono::steady_clock::now();
        std::string buffer;

        std::vector<std::string> bootMarkers = {
            "Press RETURN to get started!"
        };

        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(timeoutSec))
        {
            std::string read = telnet.ReadSome();
            buffer += read;

            for (const auto& marker : bootMarkers)
            {
                if (buffer.find(marker) != std::string::npos)
                {
                    telnet.SendCommand("");
                    return true;
                }
            }
        }

        return false;
    }
private:
    RestClient api;
};

#endif // GNS3_HARNESS_HPP
