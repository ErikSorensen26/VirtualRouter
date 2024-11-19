#include "json.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

using json = nlohmann::json;
using namespace std;

struct command {
    string name, description;
};

string getMode() {
    return "(config-router-af-topology-ipv6)#";
}

int getLevel(string& line) {
    int space = 0;
    int indent = 0;
    for (char ch : line) {
        if (ch == ' ') {
           space++; 
        }
        if (space == 4) {
            space = 0;
            indent++;
        }
        if (ch != ' ') {
            return indent;
        }
    }
}

json harvestCommand(string& line) {
    bool noIndent = false;
    bool nam = false;
    bool decSpace = true;
    string name;
    string description;
    json command;
    for (char ch : line) {
        if (ch != ' ') {
            noIndent = true;
        }
        if (nam) {
            if (ch != ' ') {
                decSpace = false;
            }
            if (!decSpace) {
                description += ch;
            }
        } else if (noIndent) {
            if (ch != ' ') {
                name += ch;
            } else {
                nam = true;
            }
        }
    }
    command["name"] = name;
    command["description"] = description;
    return command;
}

void processCommands(vector<string>& lines, size_t& index, int currentLevel, json& parent) {
    while (index < lines.size()) {
        string line = lines[index];
        int level = getLevel(line);
        if (level == currentLevel) {
            json command = harvestCommand(line);
            parent.push_back(command);
            index++;
        } else if (level > currentLevel) {
            json& lastCommand = parent.back();
            if (!lastCommand.contains("subcommands")) {
                lastCommand["subcommands"] = json::array();
            }
            processCommands(lines, index, level, lastCommand["subcommands"]);
        } else {
            break;
        }
    }
}

json readJSONFromFile(const string& filename) {
    ifstream file(filename);
    json jsonData;
    file >> jsonData;
    file.close();
    return jsonData;
}

void writeJSONToFile(const string& filename, const json& jsonData) {
    ofstream file(filename);
    file << jsonData.dump(4);
    file.close();
}

int main() {
    ifstream infile("../Utils/Dir/input.txt");
    if (!infile) 
    {
        std::cerr << "Error: Unable to open input file." << std::endl;
        return 1;
    }

    vector<string> lines;
    string line;

    json existingData;
    ifstream existingFile("../Utils/Dir/output.json");
    if (existingFile)
    {
        existingFile >> existingData;
    }
    else
    {
        existingData = json::object();
    }

    while (getline(infile, line)) {
        lines.push_back(line);
    }
    
    json output = json::array();
    size_t index = 0;
    
    while (index < lines.size()) {
        processCommands(lines, index, 0, output);
    }

    existingData[getMode()] = output;
    
    ofstream outfile("../Utils/Dir/output.json");
    outfile << existingData.dump(4);
    outfile.close();
    
    return 0;
}


