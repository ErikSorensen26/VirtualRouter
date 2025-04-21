#include <TelnetManager.hpp>   // Your existing Telnet code
#include <json.hpp>           // nlohmann::json header
#include <Functions.h>

#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <fstream>

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

bool isValidPreset(std::vector<std::string> base, std::vector<std::string> canidates)
{
    for (const auto& canidate : canidates)
    {
        bool found = false;
        for (const auto& str : base)
        {
            if (str.find(canidate) == 0)
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
json getCommandTree(TelnetClient& telnet, const std::string& prefix, int depth = 0, int maxDepth = 30) {
    // Stop if we reach the maximum recursion depth.
    if (depth >= maxDepth)
        return json::array();
    
    // Build the query string.
    // If prefix is empty, simply query "?"
    // Otherwise, append a space and then "?"
    telnet.SendCommand("\x15");
    std::string query = prefix.empty() ? "?" : prefix + " ?";
    std::string response = telnet.SendCommand(query, true);
    
    // Parse the response into lines.
    std::vector<std::string> lines = splitLines(response);
    json commands = json::array();
    std::vector<std::pair<std::string, std::vector<std::string>>> presets{
        {"<interface>", {"GigabitEthernet", "LISP", "Ethernet"}},
        {"<cef>", {"IPv4-to-MPLS", "IPv6-to-MPLS", "MPLS-end-of-stack", "MPLS-non-end-of-stack"}},
        {"<dscp>", {"af11", "cs2", "af12", "cs3", "cs5"}},
        {"<ntp>", {"burst", "iburst", "key", "prefer", "minpoll", "version"}},
        {"<traps>", {"bfd", "eigrp", "aaa_server", "config", "channel", "pfr", "ip", "isis", "pim"}},
        {"<file-protocols>", {"ftp", "rcp", "scp", "sftp", "tftp"}},
        {"<snmp-groups>", {"access", "context", "notify", "read", "write"}},
        {"<monitor-pwoam>", {"detail", "error", "event", "exact-match"}},
        {"<monitor-l2vpn>", {"bfd", "detail", "error", "event"}},
    };
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
            else if (next[0] == '<' && Functions::isDecimal(std::string(1, next[1])))
            {
                size_t start = next.find('<');
                size_t dash = next.find('-');

                if (start != std::string::npos && dash != std::string::npos)
                {
                    next = next.substr(start + 1, dash - start - 1);
                }
            }
            else if (next[0] == '<' && next[1] == '-' && Functions::isDecimal(std::string(1, next[2])))
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

            if (prefix == "dspu" || prefix == "dlsw" || prefix == "privilege" || prefix == "logging" || prefix == "no" || prefix == "translate" || prefix == "username" || prefix == "access-list" || prefix == "alias" || prefix == "crypto" || prefix == "default" || next == "access-list" || next == "community-list" || next == "extcommunity-list" || prefix == "ip host" || prefix == "ip name-server" || prefix == "ip sla" || prefix == "do-exec" || next == "range")
            {
                //isLine = true;
            }

            if (prefix == "clear" || prefix == "debug" || prefix == "show")
            {
                //isLine = true;
            }

            if (prefix == "access-expression" || prefix == "apollo" || prefix == "appletalk" || prefix == "backup" || prefix == "bridge-group" || prefix == "clns" || prefix == "cmns" || prefix == "decnet" || prefix == "dspu" || prefix == "fras" || prefix == "iso-igrp" || prefix == "lat" || prefix == "llc2" || prefix == "mop" || prefix == "netbios" || prefix == "sap-priority" || prefix == "smrp" || prefix == "sna" || prefix == "tarp" || prefix == "vines" || prefix == "vpdn" || prefix == "ctunnel" || prefix == "media" || prefix == "media-type" || prefix == "location" || prefix == "snapshot" || prefix == "source" || prefix == "topology" || prefix == "transmit-interface" || prefix == "tx-ring-limit" || prefix == "vnet" || prefix == "xconnect" || prefix == "history" || prefix == "ip dhcp" || prefix == "ip security" || prefix == "rate-limit")
            {
                //isLine = true;
            }

            newPrefix = prefix + " " + next;
        }
                                               
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

int main() {
    std::string mode = "ipROute";

    std::vector<std::string> lines;
    std::string line;

    json existingData;
    std::ifstream existingFile("../Dir/temp.json");
    if (existingFile)
    {
        existingFile >> existingData;
    }
    else
    {
        existingData = json::object();
    }

    try {
        // Initialize and connect the Telnet client to your Cisco device.
        TelnetClient telnet;
        telnet.Connect("127.0.0.1", 5032);  // Adjust IP and port as needed.
        
        // Start at the root level (empty prefix) and recursively build the tree.
        json commandTree = getCommandTree(telnet, "");
        
        // Disconnect from the device once done.
        telnet.Disconnect();

        existingData[mode] = commandTree;
        
        // Output the complete command tree.
        std::cout << commandTree.dump(4) << std::endl;
    }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    std::ofstream outfile("../Utils/Dir/temp.json");
    outfile << existingData.dump(4);
    outfile.close();
    
    return 0;
}
