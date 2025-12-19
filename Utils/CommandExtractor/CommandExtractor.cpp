#include <json.hpp>

#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <fstream>
#include <unordered_set>
#include <chrono>

#define TAG "ospf-topology"

#ifndef USE_SERIAL
#define USE_SERIAL 0
#endif

#if USE_SERIAL
#define CLIENT_TYPE SerialManager
#define CONNECTION1 "/dev/ttyUSB0"
#define CONNECTION2 9600
#include <SerialManager.hpp>
#else
#define CLIENT_TYPE TelnetClient
#define CONNECTION1 "127.0.0.1"
#define CONNECTION2 5004
#include <TelnetManager.hpp>
#endif

auto lastBackupTime = std::chrono::steady_clock::now();
const std::chrono::minutes backup(1);

using json = nlohmann::json;

auto trim ([](const std::string& str) -> std::string
{
    std::string filteredString;
    for (char c : str)
    {
        if (c >= 32 && c <= 126)
        {
            filteredString.push_back(c);
        }
    }
    size_t first = filteredString.find_first_not_of(" ");
    if (first == std::string::npos)
    {
        return "";
    }
    size_t last = filteredString.find_last_not_of(" \t\n\r");
    return filteredString.substr(first, last - first + 1);
});

std::vector<std::pair<std::string, std::vector<std::string>>> presets{
    {"<interface>", {"gigabitethernet", "loopback", "fastethernet"}},
    {"<ospf-top-nssa>", {"default-information-originate", "no-redistribution", "no-summary", "translate"}},
    {"<ospf-nssa>", {"default-information-originate", "no-ext-capability", "no-redistribution", "no-summary", "translate"}},
    {"<ospf-virtual-link>", {"authentication", "authentication-key", "dead-interval", "hello-interval", "message-digest-key", "retransmit-interval", "topology", "transmit-delay", "ttl-security"}},
    {"<ospf-virtual-linkv6>", {"authentication", "dead-interval", "encryption", "hello-interval", "retransmit-interval", "transmit-delay", "ttl-security"}},
    {"<cef>", {"ipv4-to-mpls", "ipv6-to-mpls", "mpls-end-of-stack", "mpls-non-end-of-stack"}},
    {"<dscp>", {"af11", "cs2", "af12", "cs3", "cs5"}},
    {"<ntp>", {"burst", "iburst", "key", "prefer", "minpoll", "version"}},
    {"<traps>", {"bfd", "eigrp", "aaa_server", "config", "channel", "pfr", "ip", "isis", "pim"}},
    {"<file-protocols>", {"ftp", "rcp", "scp", "sftp", "tftp"}},
    {"<snmp-groups>", {"access", "context", "notify", "read", "write"}},
    {"<monitor-pwoam>", {"detail", "error", "event", "exact-match"}},
    {"<monitor-l2vpn>", {"bfd", "detail", "error", "event"}},
};

bool isDecimal(const std::string& s)
{
    if (s.empty())
        return false;
    for (char c : s)
        if (c < '0' || c > '9')
            return false;
    return true;
}

void updatePrefix(const std::string& prefix, std::string& next, bool& isLine)
{
    if (next == "A.B.C.D")
    {
        next = "1.1.1.1";
    }
    else if (next == "X:X:X:X::X")
    {
        next = "fe80::1";
    }
    else if (next == "X:X:X:X::X/<0-128>")
    {
        next = "fe80::1/64";
    }
    else if ( next == "H.H.H")
    {
        next = "11:22:33:44:55:66";
    }
    else if (next[0] == '<' && isDecimal(std::string(1, next[1])))
    {
        size_t start = next.find('<');
        size_t dash = next.find('-');

        if (start != std::string::npos && dash != std::string::npos)
        {
            next = next.substr(start + 1, dash - start - 1);
        }
    }
    else if (next[0] == '<' && next[1] == '-' && isDecimal(std::string(1, next[2])))
    {
        size_t start = next.find('<');
        size_t dash = next.substr(2).find('-') + 2;

        if (start != std::string::npos && dash != std::string::npos)
        {
            next = next.substr(start + 1, dash - start - 1);
        }
    }
    else if (next == "N-N.H")
    {
        next = "10-10.0";
    }
    else if (next == "N.H")
    {
        next = "10.1";
    }
    else if (next == "Start-End")
    {
        next = "10-1";
    }
    else if (next == "LINE")
    {
        isLine = true;
    }
    else if (next == ":Host or :A.B.C.D")
    {
        next = ":1.1.1.1";
    }
    else if (next == "hh:mm")
    {
        next = "14:30";
    }
    else if (next == "hh:mm:ss")
    {
        next = "14:30:10";
    }
    else if (next == "DAY")
    {
        next = "monday";
    }
    else if (next == "MONTH")
    {
        next = "jan";
    }
    else if (next == "N")
    {
        next = "10";
    }
    else if (next == "Hex-string")
    {
        next = "ff";
    }
    else if (next == "X.121 Addr")
    {
        next = "2";
    }
    else if (next == "OUI:VPN-Index")
    {
        next = "111111:111111";
    }
    else if (next == "H.H...")
    {
        next = "1111.1111.1111";
    }
    else if (next == "XX.XXXX. ... .XXX.XX")
    {
        next = "1.1";
    }
    else if (next == "<cr>")
    {
        isLine = true;
    }
    else if (next.find("A.B.C.D") != std::string::npos)
    {
        next = "1.1.1.1";
    }
    else if (next.find("X:X:X:X") != std::string::npos)
    {
        next = "1::1";
    }

    if (prefix == "snmp" || prefix == "redistribute" || prefix == "redistributed" || prefix == "dspu" || prefix == "dlsw" || prefix == "privilege" || prefix == "logging" || prefix == "no" || prefix == "translate" || prefix == "username" || prefix == "access-list" || prefix == "alias" || prefix == "crypto" || prefix == "default" || next == "access-list" || next == "community-list" || next == "extcommunity-list" || prefix == "ip host" || prefix == "ip name-server" || prefix == "ip sla" || prefix == "do-exec")
    {
        isLine = true;
    }

    if (prefix == "clear" || prefix == "debug" || prefix == "show")
    {
        isLine = true;
    }

    if (prefix == "access-expression" || prefix == "apollo" || prefix == "appletalk" || prefix == "backup" || prefix == "bridge-group" || prefix == "clns" || prefix == "cmns" || prefix == "decnet" || prefix == "dspu" || prefix == "fras" || prefix == "iso-igrp" || prefix == "lat" || prefix == "llc2" || prefix == "mop" || prefix == "netbios" || prefix == "sap-priority" || prefix == "smrp" || prefix == "sna" || prefix == "tarp" || prefix == "vines" || prefix == "vpdn" || prefix == "ctunnel" || prefix == "media" || prefix == "media-type" || prefix == "location" || prefix == "snapshot" || prefix == "source" || prefix == "transmit-interface" || prefix == "tx-ring-limit" || prefix == "vnet" || prefix == "xconnect" || prefix == "history" || prefix == "ip dhcp" || prefix == "ip security" || prefix == "rate-limit")
    {
        isLine = true;
    }
}

// Utility: Split a string into lines.
std::vector<std::string> splitLines(const std::string& input) {
    std::vector<std::string> lines;
    std::istringstream stream(input);
    std::string line;
    while (std::getline(stream, line, '\n')) {
        if (line.substr(0, 2) == "  " && line.substr(0, 3) != "   ")
        {
            lines.push_back(trim(line));
        }
        std::cout << line << std::endl;
    }
    return lines;
}

// Utility: Parse a single command line into a JSON object.
// This assumes the output line is of the form:
//   <command> <spaces> <description>
// For example:
//   address-family       Enter Address Family command mode
json parseCommandLine(const std::string& line) {
    std::string name{};
    std::string description{};
    {
        bool endname = false;
        bool descStart = false;
        bool lastSpace = false;
        for (char ch : line) {
            if (!endname)
            {
                if (ch != ' ')
                {
                    if (lastSpace)
                    {
                        lastSpace = false;
                        name.push_back(' ');
                    }
                    name.push_back(ch);
                }
                else if (lastSpace)
                {
                    endname = true;
                }
                else
                {
                    lastSpace = true;
                }
            }
            else if (!descStart)
            {
                if (ch != ' ')
                {
                    descStart = true;
                    description.push_back(ch);
                }
            }
            else
            {
                description.push_back(ch);
            }
        }
        description = trim(description);
        name = trim(name);
    }
    // Remove any leading spaces from the description.
    
    json j;
    j["name"] = name;
    j["description"] = description;
    return j;
}

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), 
        [](char c) {
            return static_cast<char>(std::tolower(c));
        });
    return s;
}

bool isValidPreset(std::vector<std::string> base, std::vector<std::string> canidates)
{
    for (const auto& canidate : canidates)
    {
        bool found = false;
        for (const auto& str : base)
        {
            if (toLower(str).find(canidate) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

// Recursive function to build the command tree by querying the device.
// At each level, it sends a query (i.e., <prefix> ?), parses the output,
// then for each returned command, appends it to the prefix and recurses.
json getCommandTree(CLIENT_TYPE& telnet, const std::string& prefix, int depth = 0, int maxDepth = 30) {
    // Stop if we reach the maximum recursion depth.
    if (depth >= maxDepth)
        return json::array();
    
    // Build the query string.
    // If prefix is empty, simply query "?"
    // Otherwise, append a space and then "?"
    telnet.sendCommand("\x15");
    std::string query = prefix.empty() ? "?" : prefix + " ?";
    std::string response = telnet.sendCommand(query, true);
    
    // Parse the response into lines.
    std::vector<std::string> lines = splitLines(response);
    json commands = json::array();
    {
        if (true)
        {
            bool match = false;
            for (const auto [name, vector] : presets)
            {
                if (isValidPreset(lines, vector))
                {
                    match = true;
                    
                    lines.clear();
                    lines = {name};
                    break;
                }
            }
        }
    }
    
    for (const std::string& line : lines) {
        
        // Parse the line to extract command name and description.
        json cmdObj = parseCommandLine(line);

        std::string newPrefix;
        bool isLine = false;
        if (prefix.empty())
        {
            newPrefix = cmdObj["name"].get<std::string>();
        }
        else
        {
            std::string next = cmdObj["name"].get<std::string>();
            updatePrefix(prefix, next, isLine);
            newPrefix = prefix + " " + next;
        }

        // Handle properties

                                               
        // Recursively query for subcommands using the new prefix.
        json subcommands;
        if (!isLine)
        {
            subcommands = getCommandTree(telnet, newPrefix, depth + 1, maxDepth);
        }
        if (!subcommands.empty()) {
            cmdObj["subcommands"] = subcommands;
        }
        
        // Add this command to the current list.
        commands.push_back(cmdObj);
    }
    
    return commands;
}

void updateNegates(json& existingData, json& node, CLIENT_TYPE& telnet, const std::string& prefix = "", int depth = 0, int maxDepth = 30)
{
    if (depth >= maxDepth || !node.contains("name"))
        return;

    auto now = std::chrono::steady_clock::now();
    if (now - lastBackupTime >= backup)
    {
        std::ofstream backupFile("../Utils/Dir/temp_backup.json");
        backupFile << existingData;
    }

    // Build the prefix for the current node
    std::string next = node["name"].get<std::string>();
    bool end = false;
    updatePrefix(prefix, next, end);
    if (end) return;

    bool isDifferent = false;
    std::string different;



    if (node.contains("subcommands"))
    {
        for (const auto& com : node["subcommands"])
        {
            if (!com.contains("name")) return;
            else if (com.contains("name") && com["name"] == "<interface>")
            {
                isDifferent = true;
                different = " gig 0/0";
            }
            else if (com.contains("name") && com["name"] == "do-exec")
            {
                return;
            }
            else
            {
                for (const auto& preset : presets)
                {
                    if (com.contains("name") && com["name"] == preset.first) return;
                }
            }
        }
    }
    
    std::string currentPrefix = prefix.empty() ? node["name"].get<std::string>() + (isDifferent ? different : "") : prefix + " " + next + (isDifferent ? different : "");

    bool negateHasOnlyCr = false;

    // Only check if node has subcommands
    if (node.contains("subcommands"))
    {
        // Send "no" command
        telnet.sendCommand("\x15");

        // Normal ? output
        std::vector<std::string> normalSubCommands;
        for (auto& sub : node["subcommands"])
        {
            normalSubCommands.push_back(sub["name"].get<std::string>());
        }
        std::string query = "no " + currentPrefix + " ?";
        std::string response = telnet.sendCommand(query, true);
        std::vector<std::string> negateSubCommands = splitLines(response);

        if (negateSubCommands.empty()) return;

        negateHasOnlyCr = (negateSubCommands.size() == 1 && negateSubCommands[0] == "<cr>");
        bool negateHasCr = false;
        bool normalHasCr = false;
        for (auto& sub : negateSubCommands)
        {
            if (sub == "<cr>")
            {
                negateHasCr = true;
                break;
            }
        }
        for (auto& sub : normalSubCommands)
        {
            if (sub == "<cr>")
            {
                normalHasCr = true;
                break;
            }
        }

        bool preset = false;

        if (negateHasOnlyCr && normalSubCommands != negateSubCommands && normalSubCommands.size() > 0)
        {
            if (!node.contains("properties"))
            {
                node["properties"] = json::array();
            }
            node["properties"].push_back("negate_all");
        }
        else if (negateHasCr && !normalHasCr && normalSubCommands.size() > 0)
        {
            if (!node.contains("properties"))
            {
                node["properties"] = json::array();
            }
            node["properties"].push_back("negate");
        }
        else
        {
            std::unordered_set<std::string> normalSet(normalSubCommands.begin(), normalSubCommands.end());

            for (const std::string& line : negateSubCommands)
            {
                if (line == "<cr>") continue;

                json parsedNegCmd = parseCommandLine(line);
                std::string negCmdName = parsedNegCmd["name"];

                if (normalSet.find(negCmdName) == normalSet.end())
                {
                    std::cout << "\n[negate_show] New command found: \"" << negCmdName << "\" under \"" << currentPrefix << "\".\n";
                    std::cout << "Do you want to add this command? (y/n): ";
                    std::string choice;
                    std::getline(std::cin, choice);

                    if (choice == "y" || choice == "Y")
                    {
                        if (!node.contains("properties"))
                        {
                            parsedNegCmd["properties"] = json::array();
                        }
                        parsedNegCmd["properties"].push_back("negate_show");
                        json subcommands = getCommandTree(telnet, "no " + currentPrefix + " " + parsedNegCmd["name"].get<std::string>(), depth + 1, maxDepth);
                        if (!subcommands.empty()) {
                            parsedNegCmd["subcommands"] = subcommands;
                        }
                        node["subcommands"].push_back(parsedNegCmd);
                        std::cout << "Added.\n";
                    }
                    else
                    {
                        std::cout << "Skipped.\n";
                    }
                }
            }

            for (auto& sub : node["subcommands"])
            {
                std::string subName = sub["name"];
                bool foundInNegate = false;
                for (const std::string& line : negateSubCommands)
                {
                    json parsedNegCmd = parseCommandLine(line);
                    if (parsedNegCmd["name"] == subName)
                    {
                        foundInNegate = true;
                        break;
                    }
                }
                if (!foundInNegate && !negateHasOnlyCr)
                {
                    if (!sub.contains("properties"))
                        sub["properties"] = json::array();
                    sub["properties"].push_back("negate_hide");
                }
            }
        }
    }

    std::cout << node.dump(4) << std::endl;

    if (node["name"] == "Hex-string")
    {
        int i = 0;
    }

    // Recurse into subcommands
    if (node.contains("subcommands"))
    {
        auto validate = [](const json& obj) -> bool {
            if (!obj.contains("properties")) return true;
            // Check for "negate_hide" or "negate_show"
            for (const auto& prop : obj["properties"])
            {
                if (prop == "negate_hide" || prop == "negate_show")
                    return false;
            }
            return true;
        };

        if (negateHasOnlyCr) return;
        for (auto& subnode : node["subcommands"])
        {
            std::cout << subnode.dump(4) << std::endl;
            if (validate(subnode))
            {
                updateNegates(existingData, subnode, telnet, currentPrefix, depth + 1, maxDepth);
            }
        }
    }
}

void markUnsupported(json& j)
{
    if (j.is_object())
    {
        j["_unsupported"] = true;
        for (auto& [key, value] : j.items())
            markUnsupported(value);
    }
    else if (j.is_array())
    {
        for (auto& element : j)
            markUnsupported(element);
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " -<mode>[d] [destination] <prompt> [output]\n\n"
                  << "Modes:\n"
                  << "  p   Property extraction\n"
                  << "  c   Command extraction\n"
                  << "  u   Set unsupported\n\n"
                  << "Examples:\n"
                  << "  " << argv[0] << " -pt \"prompt\"\n"
                  << "  " << argv[0] << " -cs \"buh\" ./output.json\n"
                  << "  " << argv[0] << " -psd /dev/ttyUSB0:9600 \"another\"\n";
        return 1;
    }

    std::string flags = argv[1];
    if (flags.size() < 2 || flags[0] != '-') {
        std::cerr << "Error: invalid flag format. Must start with '-' and contain mode/client.\n";
        return 1;
    }

    bool properties = false;
    bool commands = false;
    bool unsupported = false;
    bool dest = false;

    std::string prompt;
    std::string output = "./output.json";
    std::string dest1 = CONNECTION1;
    int dest2 = CONNECTION2;


    for (char f : flags)
    {
        if (f == 'p')
            properties = true;
        else if (f == 'c')
            commands = true;
        else if (f == 'd')
            dest = true;
    }

    int argIndex = 2;

    if (dest)
    {
        std::string destination = argv[argIndex++];
        size_t colon = destination.find(':');
        dest1 = destination.substr(0, colon);
        dest2 = std::stoi(destination.substr(colon + 1));
    }

    prompt = argv[argIndex++];

    if (argc > argIndex)
        output = argv[argIndex];

    std::vector<std::string> lines;
    std::string line;

    json existingData;

    auto getFile = [&]() {
        std::ifstream existingFile(output);
        if (existingFile)
        {
            existingFile >> existingData;
        }
        else
        {
            existingData = json::object();
        }
    };

    getFile();

    if (commands)
    {
        std::cout << "command" << std::endl;
        // Initialize and connect the Telnet client to your Cisco device.
        CLIENT_TYPE client;
        client.connect(dest1, dest2);  // Adjust IP and port as needed.
        
        // Start at the root level (empty prefix) and recursively build the tree.
        json commandTree = getCommandTree(client, "");
        
        // Disconnect from the device once done.
        client.disconnect();

        existingData[prompt] = commandTree;
        
        // Output the complete command tree.
        std::cout << commandTree.dump(4) << std::endl;
    }

    if (properties)
    {
        CLIENT_TYPE client;
        client.connect(dest1, dest2);

        for (auto& node : existingData[prompt])
        {
            updateNegates(existingData, node, client);
        }

        client.disconnect();
    }

    if (unsupported)
    {
        markUnsupported(existingData);
    }

    std::ofstream outfile(output);
    outfile << existingData.dump(4);
    outfile.close();

    return 0;
}
