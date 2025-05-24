#include <Configs.h>
#include <iostream>
#include <Logger.h>
#include "Mode.hpp"

MacList Configs::macAddressList = MacList();


void Configs::printConfig() 
{
    //std::cout << root.dump(4) << std::endl;
}

Configs::Configs(IFileSystem* fs) : fileSystem(fs) {}

void Configs::initConfigs(const std::string& filePath) 
{
    // Reset all variables before
    root.clear();
    physicalInterfaces.clear();
    macAddressList.clear();

    if (configSchema.is_null() || !configSchema.is_object())
    {
        configSchema = nlohmann::ordered_json::object();
    }

    startupFileName = filePath;

    // Load JSON configuration file into doc
    if (fileSystem->fileExists(filePath))
    {
        std::string content;
        if (fileSystem->readFile(filePath, content))
        {
            try
            {
                root = nlohmann::ordered_json::parse(content);
            }
            catch (json::parse_error& e)
            {
                root = nlohmann::ordered_json::object();
            }
        }
    }
    else 
    {
        root = nlohmann::ordered_json::object();
    }
    
    // Load JSON data for interface configurations
    if (fileSystem->fileExists(CONFIG_FILE))
    {
        std::string content;
        if (fileSystem->readFile(CONFIG_FILE, content))
        {
            try
            {
                configJson = json::parse(content);
            }
            catch (json::parse_error& e)
            {
                configJson = json::object();
            }
        }
    }
    else 
    {
        configJson = json::object();
    }


    if (configJson.is_object())
    {
        if (configJson.contains("Interface") && configJson["Interface"].is_object())
        {
            // Add interface configurations to physicalInterfaces vector
            for (auto obj : configJson["Interface"])
            {
                std::string type = std::string(obj);
                physicalInterfaces.push_back(obj);
            }
        }

        if (configJson.contains("Mac") && configJson["Mac"].is_object())
        {
            // Set OUI from JSON data
            if (configJson["Mac"].contains("OUI"))
            {
                OUI = configJson["Mac"]["OUI"];
            }

            if (configJson["Mac"].contains("Ethernet"))
            {
                // Add Ethernet MAC addresses to macAddressList
                for (std::string obj : configJson["Mac"]["Ethernet"]) 
                {
                    if (obj.size() == 6 && Functions::isHex(obj))
                    {
                        macAddressList.Ethernet.push_back(obj);
                    }
                }
            }

            if (configJson["Mac"].contains("FastEthernet"))
            {
                // Add FastEthernet MAC addresses to macAddressList
                for (std::string obj : configJson["Mac"]["FastEthernet"]) 
                {
                    if (obj.size() == 6 && Functions::isHex(obj))
                    {
                        macAddressList.FastEthernet.push_back(obj);
                    }
                }
            }

            if (configJson["Mac"].contains("GigabitEthernet"))
            {
                // Add GigabitEthernet MAC addresses to macAddressList
                for (std::string obj : configJson["Mac"]["GigabitEthernet"]) 
                {
                    if (obj.size() == 6 && Functions::isHex(obj))
                    {
                        macAddressList.GigabitEthernet.push_back(obj);
                    }
                }
            }

            if (configJson["Mac"].contains("Loopback"))
            {
                // Add GigabitEthernet MAC addresses to macAddressList
                for (std::string obj : configJson["Mac"]["Loopback"]) 
                {
                    if (obj.size() == 6 && Functions::isHex(obj))
                    {
                        macAddressList.Loopback.push_back(obj);
                    }
                }
            }

            if (configJson["Mac"].contains("PortChannel"))
            {
                // Add GigabitEthernet MAC addresses to macAddressList
                for (std::string obj : configJson["Mac"]["PortChannel"]) 
                {
                    if (obj.size() == 6 && Functions::isHex(obj))
                    {
                        macAddressList.PortChannel.push_back(obj);
                    }
                }
            }
        }
    }
}

void Configs::processConfigs(nlohmann::ordered_json* node, std::vector<std::string> path, std::vector<std::string>& commands)
{
    if (!node || node->is_null()) return; // Handle null or invalid JSON nodes
    
    for (auto it = node->begin(); it != node->end(); ++it)
    {
        const std::string& key = it.key();
        nlohmann::ordered_json& value = it.value();

        // Handle mode key (mode change)
        if (value.is_object() && key == MODE_KEY)
        {
            commands.push_back(joinCommand(path));  // Add current mode
            processConfigs(&value, {}, commands); // Process commands in the mode
            continue;
        }

        // Volatile keys (check if the value is primitive or explicitly volatile)
        if (isVolatile(key) || value.is_primitive())
        {
            path.push_back(value.is_primitive() ? value.get<std::string>() : key);
            continue;
        }

        // handle arrays
        if (value.is_array() && value.size() > 0)
        {
            for (auto& obj : value)
            {
                if (obj.is_object())
                {
                    // Process each object in the array
                    path.push_back(key); // Add the parent key
                    processConfigs(&obj, path, commands);
                    path.pop_back();
                }
            }
            continue;
        }
        else if (value.is_array() && value.size() == 0)
        {
            return;
        }

        // Handle objects
        if (value.is_object())
        {
            path.push_back(key); // Add the current key to the command
            processConfigs(&value, path, commands); // Recurse
            path.pop_back(); // Remove key after processing
            continue;
        }
    }

    // Test if this is the end of a command
    bool endOfCommand = true;
    for (auto it = node->begin(); it != node->end(); ++it)
    {
        if (it.value().is_array() || it.value().is_object())
        {
            endOfCommand = false;
        }
    }

    // Add the completed command to the list if at the end of a command
    if (!path.empty() && endOfCommand)
    {
        commands.push_back(joinCommand(path));
    }
}

std::string Configs::joinCommand(const std::vector<std::string>& commandParts)
{
    std::string command;
    for (size_t i = 0; i < commandParts.size(); ++i)
    {
        command += commandParts[i];
        if (i != commandParts.size() - 1)
        {
            command += " ";
        }
    }
    return command;
}

std::vector<std::string> Configs::recoverConfigs(nlohmann::ordered_json* json)
{
    recover.clear(); // Clear previous recover data
    
    if (json)
    {
        processConfigs(json, {}, recover);
    }
    else
    {
        processConfigs(&root, {}, recover);
    }

    // Process each child node of the root node
    return recover;
}

bool Configs::saveConfig()
{
    std::string serialized = root.dump(4);
    if (fileSystem->writeFile(startupFileName, serialized))
    {
        return true;
    }
    return false;
}

bool Configs::saveCommand(
    std::vector<std::string>& oldCommand,
    std::vector<std::string>& command,
    ModeConfig& modeConfig,
    bool changeMode,
    bool exitMode,
    bool isListed
)
{
    if (modeConfig.currentMode == Mode::userExec || modeConfig.currentMode == Mode::privilegedExec) return false;
    
    if (oldCommand.empty() || command.empty()) return false;

    nlohmann::ordered_json* currentNode = &(*modeConfig.configNode);

    // Indicates of the subCommand is the first command
    bool firstIsSub = false;

    // Gets main and subcommand
    std::string mainCommand = command[0];
    std::string subCommand = command.size() > 1 ? command[1] : "";

    // Step 1: Navigate or create the nested structure
    if (subCommand.empty() || isVolatile(oldCommand[1]))
    {
        firstIsSub = true;
        subCommand = mainCommand;
        mainCommand.clear();
        // Main command
        if (!currentNode->contains(subCommand))
        {
            insertOrdered(currentNode, modeConfig, subCommand);
            
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
                insertOrdered(currentNode, modeConfig, mainCommand);
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
                insertOrdered(currentNode, modeConfig, mainCommand, subCommand, isListed);
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
            if (isVolatile(oldCommand[i]))
            {
                (*newConfigDir)[getVolatileValue(oldCommand[i], command[i], *newConfigDir)] = command[i];
            }
            else
            {
                if (!newConfigDir->contains(command[i]))
                {
                    (*newConfigDir)[command[i]] = nlohmann::ordered_json::object();
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
                std::vector<std::string> volatileValues{};
                bool noMatch = false;
                nlohmann::ordered_json *jsonLookup = &obj;
                for (size_t io = firstIsSub ? 1 : 2; io < command.size(); ++io)
                {
                    if (isVolatile(oldCommand[io]))
                    {
                        // Guess volatile value
                        std::string volatileValue = getVolatileValue(oldCommand[io], command[io], volatileValues);
                        // Cache used volatile value
                        volatileValues.push_back(volatileValue);
                        if (jsonLookup->contains(volatileValue) && (*jsonLookup)[volatileValue] == command[io])
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
                        modeConfig.configNode = &obj;
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
                    modeConfig.configNode = &((*currentNode)[currentNode->size() - 1]);
                }
            }

        }
        else
        {
            *currentNode = newConfig;
            if (changeMode)
            {
                modeConfig.configNode = currentNode;
            }
        }
        printConfig();
    }

    // Step 3: Handle 'changeMode' adjustments
    if (changeMode && !exitMode)
    {
        for (size_t i = firstIsSub ? 1 : 2; i < command.size(); ++i)
        {
            if (!isVolatile(oldCommand[i]))
            {
                if (modeConfig.configNode)
                {
                    modeConfig.configNode = &((*modeConfig.configNode)[command[i]]);
                }
            }
        }
        if (!modeConfig.configNode->contains(MODE_KEY))
        {
            (*modeConfig.configNode)[MODE_KEY] = nlohmann::ordered_json::object();
        }
        modeConfig.configNode = &((*modeConfig.configNode)[MODE_KEY]);
        modeConfig.modeHistory.push_back(modeConfig.configNode);
    }
    else if (exitMode)
    {
        if (!modeConfig.modeHistory.empty())
        {
            modeConfig.modeHistory.pop_back();
            if (!modeConfig.modeHistory.empty())
            {
                modeConfig.configNode = &(*(modeConfig.modeHistory[modeConfig.modeHistory.size() - 1]));
            }
            else
            {
                Logger::getInstance().warn() << "Mode history empty after pop_back. Resetting to root." << std::endl;
                modeConfig.configNode = &root;
            }
        }
        else
        {
            Logger::getInstance().error() << "Mode history is already empty during exitMode." << std::endl;
            modeConfig.configNode = &root;
        }
    }
    printConfig();

    return true;
}
    
void Configs::insertOrdered(nlohmann::ordered_json* parentNode, ModeConfig& modeConfig, const std::string& mainCommand, const std::string& subCommand, bool isListed)
{
    // Handle main command
    if (!mainCommand.empty() && subCommand.empty())
    {
        if (!modeConfig.modeSchema || !modeConfig.modeSchema->contains(mainCommand))
        {
            return;
        }

        // Insert the main command in order
        const auto& orderArray = *modeConfig.modeSchema; // Top-level order for main commands
        nlohmann::ordered_json tempNode = *parentNode;
        parentNode->clear();

        // First, insert schema-defined keys in order
        for (auto& key : orderArray.items())
        {
            if (tempNode.contains(std::string(key.key())))
            {
                (*parentNode)[key.key()] = tempNode[key.key()];
            }
        }

        // Now, append any extra keys that weren't in modeSchema
        for (auto& key : tempNode.items())
        {
            if (!orderArray.contains(key.key()))
            {
                (*parentNode)[key.key()] = key.value();
            }
        }
    } 


    // Handle SubCommands
    if (!mainCommand.empty() && !subCommand.empty())
    {
        if (!modeConfig.modeSchema || !modeConfig.modeSchema->contains(mainCommand))
        {
            return;
        }

        const auto& subCommands = (*modeConfig.modeSchema)[mainCommand];

        if (!subCommands.is_array())
        {
            throw std::runtime_error("SubCommands for '" + mainCommand + "' must be an array in the schema.");
        }

        if (!parentNode->is_object())
        {
            *parentNode = nlohmann::ordered_json::object();
        }

        nlohmann::ordered_json tempNode = *parentNode; // Store existing data
        parentNode->clear(); // Start fresh

        for (auto& key : subCommands)
        {
            if (tempNode.contains(key))
            {
                (*parentNode)[std::string(key)] = tempNode[std::string(key)];
            }
        }

        // Now, append any extra subcommands that weren't in modeSchema
        for (auto& key : tempNode.items())
        {
            if (std::find(subCommands.begin(), subCommands.end(), key.key()) == subCommands.end())
            {
                (*parentNode)[key.key()] = key.value();
            }
        }
    }
}

bool Configs::deleteConfig(ModeConfig& modeConfig, std::vector<std::string>& oldCommand, std::vector<std::string>& command, bool isListed)
{
    return true;
}

bool Configs::isVolatile(const std::string& command)
{
    // Check if the command matches any in the volatile inputs list
    for (const std::string& str : volatileInputs) 
    {
        if (command == str) {
            return true; 
        }
    }
    
    // Check if the command starts with '<' and is not "<cr>"
    if (command[0] == '<' && command != "<cr>" && command.find('-') != std::string::npos)
    {
        return true;
    }
    
    return false; 
}

std::string Configs::getVolatileValue(std::string& type, std::string& value, nlohmann::ordered_json currentJson)
{
    std::string volatileValue = getVolatileValueHelper(type, value);

    int count = 1;

    for (const auto& obj : currentJson.items())
    {
        if (obj.key() == volatileValue || obj.key().rfind(volatileValue + "_", 0) == 0)
        {
            count++;
        }
    }

    if (count > 1)
    {
        volatileValue += "_" + std::to_string(count);
    }

    return volatileValue;
}

std::string Configs::getVolatileValue(std::string& type, std::string value, std::vector<std::string> volatileValues)
{
    std::string volatileValue = getVolatileValueHelper(type, value);

    int count = 1;
    int highestVolatileNumber = 0;

    for (const auto& str : volatileValues)
    {
        if (str == volatileValue || str.rfind(volatileValue + "_", 0) == 0)
        {
            count++;
            if (str.size() > volatileValue.size() + 1)
            {
                try
                {
                    std::string numberPart = str.substr(volatileValue.size() + 1);
                    int number = std::stoi(numberPart);
                    highestVolatileNumber = std::max(number, highestVolatileNumber);
                }
                catch (const std::invalid_argument&) 
                {
                    std::cerr << "Error: Invalid number format in '" << str << "'\n";
                } 
                catch (const std::out_of_range&) 
                {
                    std::cerr << "Error: Number out of range in '" << str << "'\n";
                }
            }
        }
    }

    if (count > 1)
    {
        volatileValue += "_" + std::to_string(std::max(count, highestVolatileNumber + 1));
    }

    return volatileValue;
}

std::string Configs::getVolatileValueHelper(std::string& command, std::string& com) 
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

        ByteString binaryIp = Functions::byteToBin(Functions::addressToByte(com));
        bool isMask = true;
        bool reversed = false;
        bool endOfOnes = false;

        sscanf(com.c_str(), "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]);
        
        // Calculate if it's a subnet or wildcard mask
        if (binaryIp[binaryIp.size() - 1] == '1' && binaryIp[0] == '0')
        {
            // Reversing for validation
            binaryIp = Functions::reverseBinary(binaryIp);
            reversed = true;
        }

        for (auto& ch : binaryIp) 
        {
            if (ch == '0')
            {
                endOfOnes = true;
            }
            else if (ch == '1' && endOfOnes)
            {
                isMask = false;
                break;
            }
        }

        if (isMask)
        {
            return reversed ? "wildcard" : "subnet";
        }
        else
        {
            return "ip"; 
        }
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
