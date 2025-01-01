#include <Configs.h>
#include <iostream>
#include <fstream>
#include <Logger.h>

void Configs::printConfig() 
{
    //std::cout << root.dump(4) << std::endl;
}

Configs::Configs() {}

void Configs::initConfigs(const std::string& startupFilename) 
{
    startupFileName = startupFilename;

    // Load JSON configuration file into doc
    std::ifstream startupFile(startupFilename);
    if (startupFile.is_open()) 
    {
        if (startupFile.peek() == std::ifstream::traits_type::eof())
        {
            Logger::getInstance().warn() << "Startup file is empty. Initializing with {}." << std::endl;

            // Set root to an empty JSON object
            root = json::object();

            // Write "{}" back to the file
            std::ofstream outputFile(startupFilename);
            if (outputFile.is_open())
            {
                outputFile << root.dump(4); // Save as formatted JSON
                outputFile.close();
            }
            else
            {
                std::cerr << "Failed to open file: " << startupFilename << std::endl;
            }
        }
        startupFile >> root;
        startupFile.close();
    } 
    else 
    {
        std::cerr << "Failed to open file: " << startupFilename << std::endl;
        root = json::object(); // Default to empty JSON if file cannot be opened
    }

    printConfig();
    // Get the root node of the JSON configuration
    configNode = &root;
    modeHistory.push_back(configNode);

    // Load JSON data for interface configurations
    json configJson;
    std::string configFilename = "../VirtualRouter/Configs/Configs.json";
    std::ifstream configFile(configFilename);
    if (configFile.is_open()) 
    {
        configFile >> configJson;
        configFile.close();
    } 
    else 
    {
        std::cerr << "Failed to open file: " << configFilename << std::endl;
    }
    // Add interface configurations to physicalInterfaces vector
    for (auto obj : configJson["Interface"])
    {
        physicalInterfaces.push_back(obj);
    }

    // Set OUI from JSON data
    OUI = configJson["Mac"]["OUI"];
    // Add Ethernet MAC addresses to macAddressList
    for (auto obj : configJson["Mac"]["Ethernet"]) 
    {
        macAddressList.Ethernet.push_back(obj);
    }
    // Add FastEthernet MAC addresses to macAddressList
    for (auto obj : configJson["Mac"]["FastEthernet"]) 
    {
        macAddressList.FastEthernet.push_back(obj);
    }
    // Add GigabitEthernet MAC addresses to macAddressList
    for (auto obj : configJson["Mac"]["GigabitEthernet"]) 
    {
        macAddressList.GigabitEthernet.push_back(obj);
    }
}

void Configs::processConfigs(nlohmann::ordered_json* currentNode, std::vector<std::string> command, std::vector<std::string>& commandList)
{
    if (!currentNode || currentNode->is_null()) return; // Handle null or invalid JSON nodes
    
    for (auto it = currentNode->begin(); it != currentNode->end(); ++it)
    {
        const std::string& key = it.key();
        nlohmann::ordered_json& value = it.value();

        // Handle mode key (mode change)
        if (value.is_object() && key == MODE_KEY)
        {
            commandList.push_back(joinCommand(command));  // Add current mode
            processConfigs(&value, {}, commandList); // Process commands in the mode
            continue;
        }

        // Volitile keys (check if the value is primitive or explicitly volatile)
        if (isVolitile(key) || value.is_primitive())
        {
            command.push_back(value.is_primitive() ? value.get<std::string>() : key);
            continue;
        }

        // handle arrays
        if (value.is_array())
        {
            for (auto& obj : value)
            {
                if (obj.is_object())
                {
                    // Process each object in the array
                    command.push_back(key); // Add the parent key
                    processConfigs(&obj, command, commandList);
                    command.pop_back();
                }
            }
            continue;
        }

        // Handle objects
        if (value.is_object())
        {
            command.push_back(key); // Add the current key to the command
            processConfigs(&value, command, commandList); // Recurse
            command.pop_back(); // Remove key after processing
            continue;
        }
    }

    // Test if this is the end of a command
    bool endOfCommand = true;
    for (auto it = currentNode->begin(); it != currentNode->end(); ++it)
    {
        if (it.value().is_array() || it.value().is_object())
        {
            endOfCommand = false;
        }
    }

    // Add the completed command to the list if at the end of a command
    if (!command.empty() && endOfCommand)
    {
        commandList.push_back(joinCommand(command));
    }
}

std::string Configs::joinCommand(const std::vector<std::string>& command)
{
    std::ostringstream oss;
    for (size_t i = 0; i < command.size(); ++i)
    {
        if (i > 0) oss << " ";
        oss << command[i];
    }
    return oss.str();
}

std::vector<std::string> Configs::recoverConfigs() 
{
    recover.clear(); // Clear previous recover data

    processConfigs(&root, {}, recover);

    // Process each child node of the root node
    return recover;
}

void Configs::saveConfig()
{
    std::ofstream file(startupFileName);
    if (!file)
    {
        std::cerr << "Error opening file for writing!" << std::endl;
    }
    file << root.dump(4);
    file.close();
}

void Configs::saveCommand(std::vector<std::string>& oldCommand, std::vector<std::string>& command, bool changeMode, bool exitMode, bool& isListed)
{
    if (currentMode == mode.userExec || currentMode == mode.privilegedExec) return;
    
    if (oldCommand.empty() || command.empty()) return;

    nlohmann::ordered_json* currentNode = &(*configNode);

    // Indicates of the subCommand is the first command
    bool firstIsSub = false;

    // Gets main and subcommand
    std::string mainCommand = command[0];
    std::string subCommand = command.size() > 1 ? command[1] : "";

    // Step 1: Navigate or create the nested structure
    if (subCommand.empty() || isVolitile(oldCommand[1]))
    {
        firstIsSub = true;
        subCommand = mainCommand;
        mainCommand.clear();
        // Main command
        if (!currentNode->contains(subCommand))
        {
            insertOrdered(currentNode, subCommand);
            
            (*currentNode)[subCommand] = (isListed ? json::array() : json::object());
        }
        if (currentNode)
        {
            currentNode = &((*currentNode)[subCommand]);
        }

        printConfig();
    }
    else
    {
        // Main command
        if (!currentNode->contains(mainCommand))
        {
            {
                insertOrdered(currentNode, mainCommand);
            }
        }
        if (currentNode)
        {
            currentNode = &((*currentNode)[mainCommand]);
        }
        printConfig();

        // Sub command
        if (!currentNode->contains(subCommand))
        {
            if (!currentNode->contains(subCommand))
            {
                insertOrdered(currentNode, mainCommand, subCommand, isListed);
            }
        }
        currentNode = &((*currentNode)[subCommand]);

        printConfig();
    }

    // Step 2: Create or append the new configuration
    nlohmann::ordered_json newConfig = json::object();
    nlohmann::ordered_json* newConfigDir = &newConfig;

    if (command.size() > 1)
    {
        for (size_t i = firstIsSub ? 1 : 2; i < command.size(); ++i)
        {
            if (isVolitile(oldCommand[i]))
            {
                (*newConfigDir)[getVolitileValue(oldCommand[i], command[i], *newConfigDir)] = command[i];
            }
            else
            {
                if (!newConfigDir->contains(command[i]))
                {
                    (*newConfigDir)[command[i]] = json::object();
                }
                if (newConfigDir)
                {
                    newConfigDir = &((*newConfigDir)[command[i]]);
                }
            }
        }

        if (isListed)
        {
            bool match = false;

            // Ensure currentNode is an array
            if (!currentNode->is_array())
            {
                *currentNode = json::array();
            }
            
            // Check for duplicates
            for (auto& obj : *currentNode)
            {
                std::vector<std::string> volitileValues{};
                bool noMatch = false;
                nlohmann::ordered_json *jsonLookup = &obj;
                for (size_t io = firstIsSub ? 1 : 2; io < command.size(); ++io)
                {
                    if (isVolitile(oldCommand[io]))
                    {
                        // Guess volitile value
                        std::string volitileValue = getVolitileValue(oldCommand[io], command[io], volitileValues);
                        // Cache used volitile value
                        volitileValues.push_back(volitileValue);

                        if (jsonLookup->contains(volitileValue) && (*jsonLookup)[volitileValue] == command[io])
                        {
                            noMatch = false;
                        }
                        else
                        {
                            noMatch = true;
                        }
                    }
                    else
                    {
                        if (jsonLookup->contains(command[io]))
                        {
                            jsonLookup = &((*jsonLookup)[command[io]]);
                        }
                        else
                        {
                            noMatch = true;
                        }
                    }

                    if (noMatch)
                    {
                        break;
                    }
                }
                if (noMatch)
                {
                    continue;
                }

                if (!noMatch)
                {
                    match = true;
                    if (changeMode)
                    {
                        configNode = &obj;
                    }
                    break;
                }
            }
            
            // Append the new configuration of no match
            if (!match)
            {
                currentNode->push_back(newConfig);
                if (changeMode)
                {
                    configNode = &((*currentNode)[currentNode->size() - 1]);
                }
            }

        }
        else
        {
            *currentNode = newConfig;
            if (changeMode)
            {
                configNode = currentNode;
            }
        }
        printConfig();
    }

    // Step 3: Handle 'changeMode' adjustments
    if (changeMode && !exitMode)
    {
        for (size_t i = firstIsSub ? 1 : 2; i < command.size(); ++i)
        {
            if (!isVolitile(oldCommand[i]))
            {
                if (configNode)
                {
                    configNode = &((*newConfigDir)[command[i]]);
                }
            }
        }
        if (!configNode->contains(MODE_KEY))
        {
            (*configNode)[MODE_KEY] = json::object();
        }
        configNode = &((*configNode)[MODE_KEY]);
        modeHistory.push_back(configNode);
    }
    else if (exitMode)
    {
        if (!modeHistory.empty())
        {
            modeHistory.pop_back();
            if (!modeHistory.empty())
            {
                configNode = &(*(modeHistory[modeHistory.size() - 1]));
            }
            else
            {
                Logger::getInstance().warn() << "Mode history empty after pop_back. Resetting to root." << std::endl;
                configNode = &root;
            }
        }
        else
        {
            Logger::getInstance().error() << "Mode history is already empty during exitMode." << std::endl;
            configNode - &root;
        }
    }
    printConfig();
}
    
void Configs::insertOrdered(nlohmann::ordered_json* parentNode, const std::string& mainCommand, const std::string& subCommand, bool isListed)
{
    // Handle main command
    if (!mainCommand.empty() && subCommand.empty())
    {
        if (!modeSchema->contains(mainCommand))
        {
            (*parentNode)[mainCommand] = nlohmann::ordered_json::object();
            return;
        }

        // Insert the main command in order
        const auto& orderArray = *modeSchema; // Top-level order for main commands
        
        if (parentNode->is_object())
        {
            // Handle objects
            if (!parentNode->contains(mainCommand))
            {
                nlohmann::ordered_json tempNode(*parentNode);
                parentNode->clear();
                bool inserted = false;

                for (auto& key : orderArray.items())
                {
                    if (key.key() == mainCommand)
                    {
                        (*parentNode)[mainCommand] = nlohmann::ordered_json::object();
                        inserted = true;
                    }
                    if (tempNode.contains(std::string(key.key())))
                    {
                        (*parentNode)[std::string(key.key())] = tempNode[std::string(key.key())];
                    }
                }

                if (!inserted)
                {
                    (*parentNode)[mainCommand] = nlohmann::ordered_json::object();
                }
            }
        }
    } 


    // Handle SubCommands
    if (!mainCommand.empty() && !subCommand.empty())
    {
        if (!modeSchema->contains(mainCommand))
        {
            (*parentNode)[subCommand] = isListed ? nlohmann::ordered_json::array() : nlohmann::ordered_json::object();
            return;
        }

        const auto& subCommands = (*modeSchema)[mainCommand];
        if (!subCommands.is_array())
        {
            throw std::runtime_error("SubCommands for '" + mainCommand + "' must be an array in the schema.");
        }

        if (!parentNode->is_object())
        {
            *parentNode = nlohmann::ordered_json::object();
        }

        if (!parentNode->contains(subCommand))
        {
            if (parentNode->is_object())
            {
                nlohmann::ordered_json tempNode = *parentNode;
                parentNode->clear();
                bool inserted = false;

                for (auto& key : subCommands)
                {
                    if (key == subCommand)
                    {
                        (*parentNode)[subCommand] = isListed ? nlohmann::ordered_json::array() : nlohmann::ordered_json::object();
                        inserted = true;
                    }
                    if (tempNode.contains(key))
                    {
                        (*parentNode)[std::string(key)] = tempNode[std::string(key)];
                    }
                }

                if (!inserted)
                {
                    (*parentNode)[subCommand] = isListed ? nlohmann::ordered_json::array() : nlohmann::ordered_json::object();
                }
            }
        }
    }
}

void Configs::deleteConfig(nlohmann::ordered_json& obj, std::vector<std::string>& oldCommand, std::vector<std::string>& command, bool isListed)
{
}

bool Configs::isVolitile(const std::string& command) 
{
    // Check if the root node's name is "config" to set configMode flag
    if (configNode && * configNode == root) 
    {
        configMode = true;
    } 
    else 
    {
        configMode = false;
    }
    
    // Check if the command matches any in the volatile inputs list
    for (const std::string& str : volitileInputs) 
    {
        if (command == str) {
            return true; 
        }
    }
    
    // Check if the command starts with '<' and is not "<cr>"
    if (command[0] == '<' && command != "<cr>") 
    {
        return true;
    }
    
    return false; 
}

std::string Configs::getVolitileValue(std::string& command, std::string& com, nlohmann::ordered_json currentJson)
{
    std::string value = getVolitileValueHelper(command, com);

    int count = 1;

    for (const auto& obj : currentJson.items())
    {
        if (obj.key() == value || obj.key().rfind(value + "_", 0) == 0)
        {
            count++;
        }
    }

    if (count > 0)
    {
        value += "_" + std::to_string(count);
    }

    return value;
}

std::string Configs::getVolitileValue(std::string& command, std::string com, std::vector<std::string> volitileValues)
{
    std::string value = getVolitileValueHelper(command, com);

    int count = 1;

    for (const auto& str : volitileValues)
    {
        if (str == value || str.rfind(value + "_", 0) == 0)
        {
            count++;
        }
    }

    if (count > 0)
    {
        value += "_" + std::to_string(count);
    }

    return value;
}

std::string Configs::getVolitileValueHelper(std::string& command, std::string& com) 
{
    // Return "value" for commands of type "WORD" and "LINE"
    if (command == "WORD" || command == "LINE") 
    {
        return "value";
    }
    
    // Parse IP address in "A.B.C.D" format
    if (command == "A.B.C.D") 
    {
        std::vector<int> ip{0, 0, 0, 0};
        // Non-Windows IP parsing
        sscanf(com.c_str(), "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]);
        for (int num : ip) 
        {
            if (num == 255) 
            {
                return "subnet"; 
            }
        }
        return "ip"; 
    }
    
    // Return "ipv6" for IPv6 address formats
    if (command == "X:X:X:X::X" || command == "X:X:X:X::X/<0-128>") 
    {
        return "ipv6";
    }
    
    // Return "mac" for MAC address format
    if (command == "H.H.H") 
    {
        return "mac";
    }
    
    // Return "xyz" for XYZ coordinate format
    if (command == "x/y/x") 
    {
        return "xyz";
    }
    
    // Return "id" for commands starting with '<'
    if (command[0] == '<') 
    {
        return "id";
    }
    
    return ""; // Return empty string for unknown commands
}

void Configs::historyToGlobal() 
{
    prevConfig = configNode; 
    modeHistory.clear();
    modeHistory.push_back(&root); 
    configNode = &root;

}

void Configs::returnToRoot(pugi::xml_node& node, pugi::xml_node& root) 
{
    // Traverse up the tree until the node matches the root's name
    while (node.name() != root.name()) 
    {
        node = node.parent(); // Move to the parent node
    }
}
