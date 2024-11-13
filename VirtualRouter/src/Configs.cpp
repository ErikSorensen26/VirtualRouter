#include <Configs.h>

// Function to print a chunk of XML for debugging purposes
void printNodeChunk(const pugi::xml_node& node) 
{
    // Create a temporary xml_document for isolation
    pugi::xml_document temp_doc;

    // Import the subtree to the temporary document
    pugi::xml_node imported_node = temp_doc.append_copy(node);

    // Print the XML chunk with indentation for readability
    temp_doc.save(std::cout, "  "); 
    std::cout << std::endl;
}

// Constructor for Configs class
Configs::Configs() {}

void Configs::initConfigs() 
{
    // Get the singleton instance of Save
    //Save& routingTable = Save::getInstance();

    // Load XML configuration file into doc
    pugi::xml_parse_result config = doc.load_file("../VirtualRouter/Dir/startup-config.xml");
    if (!config) 
    {
        std::cerr << "Error loading XML file: " << config.description() << std::endl;
        return;
    }

    // Get the root node of the XML configuration
    config_node = doc.child("config");
    modeHistory.push_back(config_node);

    // Load JSON data for interface configurations
    json configJson;
    string configFilename = "../VirtualRouter/Configs/Configs.json";
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

// Process XML nodes and generate commands based on their content
void Configs::processNode(const pugi::xml_node& node, std::string command) 
{
    noMoreVol = false; 
    moreChild = false;

    string prevCommand;

    string root = "config"; 

    // Process node name and text
    if (!node.text()) 
    {
        command += " " + std::string(node.name());
        curNonVolCommand += " " + std::string(node.name());
    } 
    else 
    {
        bool contains = false;
        for (string& str : inputs) 
        {
            if (str == node.name()) 
            {
                contains = true;
            }
        }
        if (!contains) 
        {
            command += " " + std::string(node.name());
        }
    }

    // Append attributes to command string
    for (pugi::xml_attribute attr = node.first_attribute(); attr; attr = attr.next_attribute()) 
    {
        command += " " + std::string(attr.value());
    }

    // Append text content to command string
    if (node.text()) 
    {
        command += " " + std::string(node.text().get());
    }

    // If the node has attributes, store command and clear it
    if (node.first_attribute()) 
    {
        recover.push_back(command);
        command.clear();
        nonVolCommand = curNonVolCommand;
        curNonVolCommand.clear();
    }

    // Process child nodes
    bool has_child_elements = false;
    for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) 
    {
        if (child.type() == pugi::node_element) 
        {
            has_child_elements = true;
            std::string child_command = command;
            processNode(child, child_command);
            if (command == nonVolCommand) 
            {
                noMoreVol = true;
            }
            if (noMoreVol) 
            {
                int numChild = 0;
                string num = child.name();
                for (pugi::xml_node node : child.children()) 
                {
                    numChild++;
                }
                if (numChild > 1) 
                { 
                    moreChild = true; 
                }
            }
            if (node.parent().name() != root && !moreChild) 
            {
                return;
            }
        } 
        else if (!has_child_elements && node.parent().name() != root) 
        {
            pugi::xml_node sibling = node.next_sibling();
            while (sibling) 
            {
                if (sibling.type() == pugi::node_element) 
                {
                    std::string sibling_command = command;
                    processNode(sibling, sibling_command);
                    if (command == nonVolCommand) 
                    {
                        noMoreVol = true;
                    }
                    if (noMoreVol) 
                    {
                        int numChild = 0;
                        string num = child.name();
                        for (pugi::xml_node node : child.parent().children()) 
                        {
                            numChild++;
                        }
                        if (numChild > 1) { moreChild = true; }
                    }
                    if (node.parent().name() != root && node.text() && !moreChild) 
                    {
                        return;
                    }
                }
                sibling = sibling.next_sibling();
            }
        }
    }
    // Store final command if no child elements are present
    if (!has_child_elements && !command.empty()) 
    {
        recover.push_back(command);
        command.clear();
        nonVolCommand = curNonVolCommand;
        curNonVolCommand.clear();
    }
}

// Retrieve commands from XML configuration
vector<string> Configs::recoverXml() 
{
    recover.clear(); // Clear previous recover data
    config_node = doc.child("config"); 

    // Process each child node of the root node
    for (pugi::xml_node node = config_node.first_child(); node; node = node.next_sibling()) 
    {
        processNode(node, "");
    }
    return recover;
}

// Save new commands to the XML configuration
void Configs::saveCommand(vector<string>& oldCommand, vector<string>& command, bool& changeMode, bool& isListed) 
{
    if (oldCommand.empty()) 
    { 
        return; 
    } // No commands to process

    pugi::xml_node save = config_node;

    // If not in privileged or user exec mode, handle volatile commands
    if (command[0] != "exit" && currentMode != mode.privilegedExec && currentMode != mode.userExec) 
    {
        int vol = 0;
        for (int index = 0; index < command.size(); index++) 
        {
            if (isVolitile(oldCommand[index])) 
            {
                vol++;
                if (vol > 1) 
                {
                    break;
                }
            } 
            else 
            {
                vol = 0;
            }
        }
        if (no && !changeMode) 
        {
            bool listed = false;
            for (int index = 0; command.size() >= index; ++index) 
            {
                if (isListed) {
                    int childAmount = 0;
                    for (auto child : config_node.child(command[0].c_str()).children()) 
                    {
                        childAmount++;
                    }
                    if (childAmount <= 1) 
                    {
                        config_node.remove_child(config_node.child(command[0].c_str()));
                    } 
                    else 
                    {
                        for (auto child : config_node.child(command[0].c_str()).children(command[1].c_str())) 
                        {
                            vector<string> tempCommand = command;
                            pugi::xml_node node = child;
                            if (!TravelNode(tempCommand, oldCommand, node, save, changeMode, isListed, 2)) 
                            {
                                ReturnToRoot(node, child);
                                node.remove_children();
                                config_node.child(command[0].c_str()).remove_child(node);
                                return;
                            }
                        }
                    }
                }

                // Handle non-listed commands
                if (!isListed) 
                {
                    TravelNode(command, oldCommand, config_node, save, changeMode, isListed, index);
                    if (deleteNodeAndAllChildren(config_node, save)) {
                        ReturnToRoot(config_node, save);
                        return;
                    }
                    ReturnToRoot(config_node, save);
                }
            }
        } 
        else 
        {
            TravelNode(command, oldCommand, config_node, save, changeMode, isListed, 0);
        }
    }

    // Handle mode changes
    if (changeMode && currentMode != mode.privilegedExec && currentMode != mode.userExec) 
    {
        if (command[0] == "exit") 
        {
            modeHistory.pop_back(); 
            config_node = modeHistory[modeHistory.size() - 1]; 
        } 
        else 
        {
            modeHistory.push_back(config_node);
        }
    } 
    else 
    {
        config_node = save; 
    }

    // Print the final XML configuration
    //doc.save(std::cout);
    //std::cout << std::endl;
}

bool Configs::TravelNode(vector<string>& command, vector<string> oldCommand, pugi::xml_node& config_node, pugi::xml_node& save, bool& changeMode, bool& isList, int offset) 
{
    // Offset adjustment for special conditions
    int otherOffset = 0;

    // If `no` is true and `isList` is true, adjust the offset
    if (no && isList) 
    {
        otherOffset = offset;
        offset = 0;
    }

    // Declare temporary variables for XML node processing
    pugi::xml_node tempNode;
    pugi::xml_node noNode;
    pugi::xml_node tempNoNode;

    // Flags for tracking the state of node matching and new nodes
    bool runMatch = false;
    bool isNew = false;
    bool cleared = false;
    clear = false;

    // Traverse the command vector starting from the offset
    for (int index = 0 + otherOffset; index < (command.size() - offset); ++index) 
    {
        // If `no` is false, `isList` is true, and we're at the start, navigate to the child node
        if (!no && isList && index == 0 && config_node.child(command[0].c_str())) 
        {
            config_node = config_node.child(command[0].c_str());
        } 
        else 
        {
            // If we are at index 2 and not in change mode or in a list, clear children of the current node
            if (index == 2 && !changeMode && !isList) 
            {
                if (!no) {
                    config_node.remove_children();
                }
            }

            // If the old command is not volatile, handle non-volatile nodes
            if (!isVolitile(oldCommand[index])) 
            {
                if (!isList || no) 
                {
                    // Check if the child node exists; if not, create it
                    pugi::xml_node child = config_node.child(command[index].c_str());
                    if (!child) 
                    {
                        if (no && isList) 
                        {
                            return 1; 
                        }
                        child = config_node.append_child(command[index].c_str());
                    }
                    config_node = child;
                }
                else 
                {
                    // Create child node directly if we are in a list
                    pugi::xml_node child = config_node.child(command[index].c_str());
                    child = config_node.append_child(command[index].c_str());
                    config_node = child;
                }
            } 
            else 
            {
                // Handle volatile nodes
                if (!runMatch) 
                {
                    bool match = true;   
                    bool isNew = false; 
                    int numChild = 0;   

                    // Attempt to match nodes in the parent node
                    for (pugi::xml_node conf_node : config_node.parent().children()) 
                    {
                        match = true;
                        numChild = 0;
                        if (isList && !isNew) 
                        {
                            int ind = index;
                            int forInd = index;
                            pugi::xml_node child = conf_node;
                            for (int num = index; num < command.size(); ++num) 
                            {
                                int chn = 0;
                                if (!match) 
                                {
                                    break;
                                }
                                // Compare command with node children
                                for (pugi::xml_node node : child.children()) 
                                {
                                    if (chn == (num - forInd)) 
                                    {
                                        if (command[ind] == node.text().as_string())
                                        {
                                            match = true;
                                            ++ind;
                                        } 
                                        else if (command[ind] == node.name()) 
                                        {
                                            match = true;
                                            ++ind;
                                            child = node;      
                                            forInd = ind;
                                        } 
                                        else 
                                        {
                                            match = false;
                                        }
                                    }
                                    ++chn;
                                }
                                if (chn == 0) 
                                {
                                    match = false;
                                    break;
                                }
                            }
                            if (match) 
                            {
                                config_node = conf_node; 
                                break;
                            }
                        }
                    }
                    if (!match) 
                    {
                        // Create a new node if no match is found and there are children with text
                        for (pugi::xml_node node : config_node.children()) 
                        {
                            if (node.text()) 
                            {
                                ++numChild;
                            }
                        }
                        if (numChild != 0) 
                        {
                            config_node = config_node.parent().append_child(command[index - 1].c_str());
                            isNew = true;
                        }
                    }
                    runMatch = true; 
                    if (isNew) 
                    { 
                    isNew = true;
                    } 
                }

                // Handle changes based on mode
                if (changeMode) 
                {
                    config_node = config_node.parent(); 
                    pugi::xml_node child;
                    // Look for the child node with the attribute to modify
                    for (pugi::xml_node node : config_node.children(command[index - 1].c_str())) 
                    {
                        if (!node.first_attribute()) 
                        {
                            node.append_attribute(getVolitileValue(oldCommand[index], command[index]).c_str())
                                .set_value(command[index].c_str());
                            child = node;
                            break;
                        }
                        // Check existing attributes for match
                        for (pugi::xml_attribute attr : node.attributes()) 
                        {
                            if (std::string(attr.name()) == getVolitileValue(oldCommand[index], command[index])
                                && std::string(attr.value()) == command[index]) {
                                child = node;
                                break;
                            }
                        }
                    }
                    if (!child) 
                    {
                        if (no && isList) 
                        {
                            return 1; 
                        }
                        // Create a new child node with attribute if not found
                        child = config_node.append_child(command[index - 1].c_str());
                        child.append_attribute(getVolitileValue(oldCommand[index], command[index]).c_str())
                            .set_value(command[index].c_str());
                    }
                    config_node = child; 
                } 
                else if (command.size() <= 2 || oldCommand[index] == "LINE") 
                {
                    // Update attribute value if `changeMode` is false or if the command size is small
                    if (!config_node.first_attribute()) 
                    {
                        config_node.append_attribute(getVolitileValue(oldCommand[index], command[index]).c_str())
                            .set_value(command[index].c_str());
                    } 
                    else 
                    {
                        config_node.attribute(getVolitileValue(oldCommand[index], command[index]).c_str())
                            .set_value(command[index].c_str());
                    }
                } 
                else 
                {
                    // Handle non-volatile attributes or child nodes
                    pugi::xml_node child = config_node.child(getVolitileValue(oldCommand[index], command[index]).c_str());
                    if (!child || (isList && !no)) 
                    {
                        if (no && isList) 
                        {
                            return 1;
                        }
                        child = config_node.append_child(getVolitileValue(oldCommand[index], command[index]).c_str());
                        child.append_child(pugi::node_pcdata).set_value(command[index].c_str());
                    } 
                    else 
                    {
                        if (std::string(child.text().get()) != command[index]) 
                        {
                            child.text().set(command[index].c_str());
                        }
                    }
                    config_node = child.parent();
                }
            }
        }
    }
    return 0;
}

// Deletes a given node and all its children based on specific conditions
bool Configs::deleteNodeAndAllChildren(pugi::xml_node& node, pugi::xml_node& save) 
{
    // Check if the node has siblings (either previous or next) or if it is valid
    if ((node.previous_sibling() || node.next_sibling()) && node) 
    {
        // Remove all children of the node
        node.remove_children();
        
        // Check if the node has attributes and if 'no' is true or if the save node's name matches the parent's name and 'no' is true
        if ((node.first_attribute() != nullptr && no) || (save.name() == node.parent().name() && no)) 
        {
            pugi::xml_node parent = node.parent(); 
            if (parent) 
            {
                // Remove the node from its parent
                parent.remove_child(node);
                node = parent;
            } 
            else 
            {
                // If no parent, set node to an invalid state
                node = pugi::xml_node();
            }
            return true;
        }
    } 
    else if (node.name() == save.name()) 
    {
        // If the node's name matches the save node's name, remove all children of the node
        node.remove_children();
        return true; 
    }

    return false;
}

// Checks if a command string is volatile based on certain conditions
bool Configs::isVolitile(string& command) 
{
    // Check if the root node's name is "config" to set configMode flag
    if (config_node.name() == "config") 
    {
        configMode = true;
    } 
    else 
    {
        configMode = false;
    }
    
    // Check if the command matches any in the volatile inputs list
    for (const string& str : volitileInputs) 
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

// Determines the volatile type based on command and format
string Configs::getVolitileValue(string& command, string& com) 
{
    // Return "value" for commands of type "WORD" and "LINE"
    if (command == "WORD" || command == "LINE") 
    {
        return "value";
    }
    
    // Parse IP address in "A.B.C.D" format
    if (command == "A.B.C.D") 
    {
        vector<int> ip{0, 0, 0, 0};
    #ifdef _WIN32
        // Windows-specific IP parsing
        sscanf_s(com.c_str(), "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]);
    #else
        // Non-Windows IP parsing
        sscanf(com.c_str(), "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]);
    #endif
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

// Updates the global configuration history to reflect the current configuration state
void Configs::historyToGlobal() 
{
    prevConfig = config_node; 
    modeHistory.clear();
    modeHistory.push_back(config_node.root().child("config")); 
    config_node = config_node.root().child("config");
}

// Traverses from a node up to the root node and updates the node to match the root
void Configs::ReturnToRoot(pugi::xml_node& node, pugi::xml_node& root) 
{
    // Traverse up the tree until the node matches the root's name
    while (node.name() != root.name()) 
    {
        node = node.parent(); // Move to the parent node
    }
}
