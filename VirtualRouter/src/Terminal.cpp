#include <Terminal.h>
#include <fstream>
#include <regex>

#ifdef _WIN32
#include <Windows.h>
#elif __linux__
#include <X11/Xlib.h>
#include <X11/extensions/xtestconst.h>
#endif

/**
 * @brief Constructor for the Terminal class.
 * 
 * Initializes the terminal by setting up debugging options, loading command configurations,
 * setting the default mode, and restoring the previous state if available.
 * 
 * @param enableDebug A boolean flag to enable or disable debug mode.
 */
Terminal::Terminal(bool enableDebug) : Console() {
    // Output a message to indicate terminal initialization
    std::cout << "Initializing Terminal..." << std::endl;

    // Set debug mode based on the input parameter
    isDebugModeEnabled = enableDebug;

    // Initialize default error and carriage return commands
    errorCommand.name = "<error>";
    carriageReturnCommand.name = "<cr>";

	// Load the command tree configuration from a JSON file
    commandTree.clear();
    std::string configFilePath = "../VirtualRouter/Configs/Commands.json";
    std::ifstream configFile(configFilePath);
    if (configFile.is_open()) {
        configFile >> commandTree;  // Parse JSON into commandTree
        configFile.close();
    } else {
        std::cerr << "Failed to open configuration file: " << configFilePath << std::endl;
    }

    // Set the terminal to Global Configuration mode by default
    changeMode(mode.globalConfiguration);

    // Initialize the console and configuration settings
    initConsole();
    initConfigs();

    // Restore the terminal state from saved configurations
    recoverState();
}

/**
 * @brief Restores the terminal's state from previously saved configurations.
 * 
 * This function retrieves saved commands from persistent storage (XML), executes them,
 * and optionally introduces a delay between commands for stability.
 */
void Terminal::recoverState() {
    // Retrieve the list of saved commands from the XML recovery system
    std::vector<std::string> savedCommands = recoverXml();

    // Execute each saved command to restore the terminal's state
    for (std::string& command : savedCommands) {
        executeCommand(command);  // Execute the command
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Add a delay for stability
    }

    // Optionally, switch back to userExec mode if required
    // changeMode(mode.userExec);
}


/**
 * @brief Captures and processes user input in the terminal.
 * 
 * This function reads the user's input, processes IPv6 addresses if applicable,
 * handles shortcuts (e.g., Ctrl-Z for mode switching), and executes valid commands.
 */
void Terminal::handleInput() {
    // Retrieve the hostname from the global settings and reset cursor position
    std::string hostname = Global::getInstance().getHostname();
    cursorPos = 0;
    std::cout << hostname << currentMode;  // Display the prompt with the current mode

#ifdef _WIN32
    // Windows-specific calculation for prompt length
    initialLineLength = hostname.size() + currentMode.size();
#else
    // Linux-specific calculation for prompt length
    initialLineLength = hostname.size() + currentMode.size() + 1;
#endif

    // Read the user's input from the terminal
    std::string userCommand = input();

    // Handle IPv6 address input and print the expanded version
    if (isIPv6Address(userCommand)) {
        std::cout << expandIPv6Address(userCommand);
    }

    // Handle the Ctrl-Z shortcut to switch to privilegedExec mode
    if (userCommand == "CRT-Z" && currentMode != mode.userExec) {
        changeMode(mode.privilegedExec);
    }

    // Execute commands that are not navigation keys (e.g., up/down arrows)
    if (userCommand != "VK_UP" && userCommand != "VK_DOWN") {
        executeCommand(userCommand);
    }

    // Move to the next line after command execution
    std::cout << std::endl;
}

std::string Terminal::normalizeCommand(const std::string& inputCommand) {
    // Return an empty string if the input command is empty
    if (inputCommand.empty()) {
        return "";
    }

    // Normalize the command by converting all characters to lowercase while preserving spaces and tabs
    std::string normalizedCommand;
    for (char character : inputCommand) {
        if (std::isspace(character) || character == '\t') {
            normalizedCommand += character;
        } else {
            normalizedCommand += std::tolower(character);
        }
    }

    // Initialize command processing variables
    currentDirectory = workingDirectory;
    isNextWordHelpRequested = false;

    std::vector<std::string> parsedWords = splitIntoWords(normalizedCommand);
    std::vector<com> previousCommandList;
    std::string formattedOldCommand;
    std::string lastProcessedWord;
    std::string fullyFormattedCommand;
    std::string volatileCommand;

    int currentIndex = 0;
    bool isFirstIteration = true;
    isRunning = true;
	no = false;
    isMatchSuccessful = false;
    isHelpModeActive = false;
    endOfCommand = false;
    isLineBasedInput = false;

    // Check for help triggers ("?" or "vk_tab")
    for (const std::string& word : parsedWords) {
        if (word == "?" || word == "vk_tab") {
            isHelpModeActive = true;
        }
    }

    // Handle "do" and "no" prefix commands
    if (!parsedWords.empty()) {
        if (parsedWords[0] == "do" && parsedWords[1] != "exit" && parsedWords[1] != "conf" &&
            parsedWords[1] != "configure" &&
            currentMode != mode.userExec && currentMode != mode.privilegedExec) {

            // Temporarily switch to privileged mode for "do" commands
            isGlobalCommandExecution = true;
            std::string previousMode = currentMode;
            nlohmann::json previousCommandTree = workingDirectory;
            pugi::xml_node previousConfigNode = config_node;

            changeMode(mode.privilegedExec);
            std::string remainingCommand = inputCommand.substr(2);
            executeCommand(remainingCommand);

            // Restore the previous mode and working directory
            currentDirectory.clear();
            changeMode(previousMode);
            config_node = previousConfigNode;
            workingDirectory = previousCommandTree;

            return "error";
        } else if (parsedWords[0] == "no" && !isHelpModeActive) {
            // Handle "no" commands by normalizing the remainder of the command
            std::string strippedCommand = normalizeCommand(inputCommand.substr(3));
            no = true;
            return strippedCommand;
        }
    }
	if (!parsedWords.empty())
	{
		if (parsedWords[0] == "?" || parsedWords[0] == "vk_tab")
		{
			isMatchSuccessful = true;
		}
	}

    // Process each word in the parsed command
    for (std::string& word : parsedWords) {
        if (isLineBasedInput) {
            fullyFormattedCommand += " " + parsedWords[currentIndex];
            volatileCommand += " " + parsedWords[currentIndex];
        } else {
            isPatternMatching = false;
            isPatternMatchEnd = false;

            if (isRunning) {
                // Retrieve a list of available commands for the current word
                std::vector<com> availableCommands = GetAvailableCommands(currentDirectory, word, isFirstIteration);

                // Handle "?" for command help
                if ((word == "?") && !isMatchSuccessful && !previousCommandList.empty() && !isNextWordHelpRequested && !endOfCommand) {
                    fullyFormattedCommand += word;
                    volatileCommand += word;
                    nextLine = formattedOldCommand + " ";

                    if (previousCommandList[0].name != "<cr>") {
                        displayAvailableCommands(previousCommandList);
                    } else {
                        nextLine = trimString(inputCommand);
                    }
                } else if (word == "vk_tab" && !isNextWordHelpRequested) {
                    // Handle tab completion logic
                    if (!previousCommandList.empty() && previousCommandList.size() != 1) {
                        fullyFormattedCommand += word;
                        volatileCommand += word;
                        nextLine = formattedOldCommand + " ";
                    } else if (previousCommandList.empty()) {
                        nextLine = trimString(inputCommand);
                    } else {
                        nextLine = formattedOldCommand + " ";
                        nextLine = nextLine.substr(0, nextLine.size() - 1);

                        int lastSpacePosition;
                        bool hasSpace = false;
                        for (int charIndex = 0; charIndex <= nextLine.size(); charIndex++) {
                            if (nextLine[charIndex] == ' ') {
                                lastSpacePosition = charIndex;
                                hasSpace = true;
                            }
                        }
                        if (!hasSpace) {
                            lastSpacePosition = 0;
                        }
                        if (lastSpacePosition == 0) {
                            nextLine = nextLine.substr(0, lastSpacePosition) +
                                              getLastWord(fullyFormattedCommand) + "  ";
                        } else {
                            nextLine = nextLine.substr(0, lastSpacePosition) +
                                              " " + getLastWord(fullyFormattedCommand) + "  ";
                        }
                    }
                } else if ((word == "?") && isMatchSuccessful && !previousCommandList.empty() && !isNextWordHelpRequested) {
                    nextLine = inputCommand;
                }
                if (currentDirectory == "error" && currentMode != mode.globalConfiguration && currentMode != mode.userExec && currentMode != mode.privilegedExec && !isHelpModeActive && Functions::lowerCase(inputCommand) != "exit")
                {
                    isGlobalCommandExecution = true;
                    std::string prevMode = currentMode;
                    nlohmann::json prevDirectory = workingDirectory;
                    pugi::xml_node prevXML = config_node;
                    changeMode(mode.globalConfiguration);
                    historyToGlobal();
                    std::string nextCommand = inputCommand;
                    executeCommand(nextCommand);
                    currentDirectory.clear();
                    if (currentMode == mode.globalConfiguration)
                    {
                        if (isCommandExecutionSuccessful)
                        {
                            return "error";
                        }
                        else
                        {
                            changeMode(prevMode);
                            config_node = prevXML;
                            workingDirectory = prevDirectory;
                            if (isCommandExecutionSuccessful)
                            {
                                return "error";
                            }
                        }
                    }
                    else
                    {
                        return "error";
                    }
                }
                if (currentDirectory == "error" && !isGlobalCommand(word) && !isHelpModeActive)
                {
                    isRunning = false;
                    std::cout << std::endl;
                    std::string hostname = Global::getInstance().getHostname();
                    for (char i : hostname)
                    {
                        std::cout << " ";
                    }
                    for (char i : currentMode)
                    {
                        std::cout << " ";
                    }
                    for (char i : formattedOldCommand)
                    {
                        std::cout << " ";
                    }
                    std::cout << " ^" << std::endl;
                    std::cout << "% Invalid input detected at '^' marker." << std::endl;
                }

                isFirstIteration = false;
				
                // Match the input word against available commands
                bool isCommandDone = false;
                if (!isCommandDone) {
                    std::vector<com> matchingCommands;
                    for (const auto& command : availableCommands) {
                        if (isPatternMatching && command.name == currentPattern) {
                            matchingCommands.push_back(command);
                        }
                        if (command.name.size() >= word.size() &&
                            std::equal(word.begin(), word.end(), Functions::lowerCase(command.name).begin())) {
                            matchingCommands.push_back(command);
                        }
                    }
                    previousCommandList = matchingCommands;
                    if (previousCommandList.empty()) {
                        previousCommandList = availableCommands;
                    }

                    if (word == "?" && (isMatchSuccessful || isNextWordHelpRequested) && !endOfCommand) 
                    {
                        displayAvailableCommands(availableCommands);
                        fullyFormattedCommand += word;
                        volatileCommand += word;
                        nextLine = formattedOldCommand + " ";
		        if (availableCommands[0].name != "<error>")
			{
                            // std::cout << isHelpModeActive
			} else {
			    nextLine = inputCommand;
			}

			if (isHelpModeActive)
			{
			    nextLine += " ";
			}
                    } else if (word == "vk_tab" && (availableCommands[0].name == "<error>" || isNextWordHelpRequested)) {
			nextLine = inputCommand;
		    }

                    isMatchSuccessful = false;

                    if (previousCommandList.size() == 1 && !matchingCommands.empty()) {
                        if (isPatternMatching && currentPattern == matchingCommands[0].name) {
                            isMatchSuccessful = true;
                        }
                        if (matchingCommands[0].name == word) {
                            isMatchSuccessful = true;
                        }
                    } else if (word == "?" && (isMatchSuccessful || isNextWordHelpRequested)) {
			nextLine = inputCommand;
		    }

                    if (!isCommandDone && matchingCommands.size() <= 1) {
                        if (isPatternMatching) {
                            fullyFormattedCommand += " " + parsedWords[currentIndex];
                            formattedOldCommand += " " + parsedWords[currentIndex];
                            volatileCommand += " " + currentPattern;
                            previousMatch = matchingCommands[0].name;
                        } else if (matchingCommands.size() == 1) {
                            fullyFormattedCommand += " " + matchingCommands[0].name;
                            formattedOldCommand += " " + word;
                            volatileCommand += " " + word;
                            isCommandDone = true;
                            previousMatch = matchingCommands[0].name;
                        } else if (matchingCommands.empty() && endOfCommand) {
                            fullyFormattedCommand += " " + endCommandString;
                            formattedOldCommand += " " + word;
                            volatileCommand += " " + word;
                            lastProcessedWord = word;
                        } else if (matchingCommands.empty()) {
                            fullyFormattedCommand += " " + word;
                            formattedOldCommand += " " + word;
                            volatileCommand += " " + word;
                            lastProcessedWord = word;
                            if (!isHelpModeActive) {
                                return word;
                            }
                        } else {
			    fullyFormattedCommand += " " + matchingCommands[0].name;
			    formattedOldCommand += " " + word;
			    volatileCommand += " " + word;
			    isCommandDone = true;
			    previousMatch = matchingCommands[0].name;
			}
                    } else {
                        formattedOldCommand += " " + word;
                        volatileCommand += " " + word;
                    }
                }
            }
        }
        currentIndex++;
        lastProcessedWord = word;
    }

    // Final formatting and return
    formattedOldCommand = trimString(formattedOldCommand);
    volatileCommand = trimString(volatileCommand);
    commandHistory = splitIntoWords(volatileCommand);
    fullyFormattedCommand = trimString(fullyFormattedCommand);
    nextLine = trimString(nextLine);

    if (!isCommandValid && !isHelpModeActive && !isPatternMatchEnd && !isLineBasedInput) {
        std::cout << std::endl << "Incomplete Command";
        return "";
    } else {
        return fullyFormattedCommand;
    }
}

std::vector<com> Terminal::GetAvailableCommands(const nlohmann::json& commandTree, const std::string& userInput, bool inPrivilegedMode) {
    // Container for storing available commands
    std::vector<com> availableCommands;

    // Clone the current command directory
    nlohmann::json currentCommandDirectory = currentDirectory;

    // Default response for invalid or unavailable commands
    std::vector<com> noSubCommands = {errorCommand};

    // Variables for handling exact matches
    com exactMatchCommand;
    bool isExactMatch = false;
    bool isValidCommand = false;

    // If the current directory is invalid, return the default error response
    if (currentDirectory == "error") {
        return noSubCommands;
    }

    // Iterate over all commands in the current directory
    nlohmann::json commandNode;
    int matchCount = 0;
    for (const auto& command : currentCommandDirectory) {
        com commandData;
        for (const auto i : command)
        commandData.name = command["name"];
        commandData.description = command["description"];
        availableCommands.push_back(commandData);

        // Check if the user input matches a pattern or specific command
        std::string commandName = command["name"];
        if (matchInputPattern(userInput, commandName) && !endOfCommand) {
            commandNode = command;
            matchCount++;
            if (!isValidCommandDirectory(commandNode)) {
                endOfCommand = true;
            }
        } else if (commandName.size() >= userInput.size()) {
            if (std::equal(userInput.begin(), userInput.end(), Functions::lowerCase(commandName).begin()) && !isExactMatch) {
                commandNode = command;
                matchCount++;
            }
            if (commandName == userInput) {
                isExactMatch = true;
                exactMatchCommand.name = Functions::lowerCase(command["name"]);
                exactMatchCommand.description = command["description"];
                commandNode = command;
            }
        }
    }

    // Handle exact matches and valid commands
    if (isExactMatch) {
        matchCount = 1;
    }
    if (matchCount == 1 && isValidCommandDirectory(commandNode)) {
        currentDirectory = commandNode["subcommands"];
        for (const auto& subCommand : currentDirectory) {
            if (subCommand["name"] == "<cr>") {
                isCommandValid = true;
                isValidCommand = true;
            }
        }
        if (!isValidCommand) {
            isCommandValid = false;
        }
        if (isExactMatch) {
            availableCommands.clear();
            availableCommands.push_back(exactMatchCommand);
        }
    } else if ((isValidCommandDirectory(commandNode) || matchCount != 1) && !isMatchSuccessful) {
        currentDirectory = "error";
    } else if (isExactMatch && !isValidCommandDirectory(commandNode) && !userInput.empty()) {
        endCommandString = Functions::lowerCase(commandNode["name"]);
        endOfCommand = true;
        return noSubCommands;
    }

    // Handle unmatched or invalid commands
    if (matchCount == 0 && !userInput.empty() && currentDirectory == "error" &&
        userInput != "?" && userInput != "vk_tab") {
        return noSubCommands;
    }
    if (matchCount == 0 && !isValidCommandDirectory(commandNode) && userInput != "?" &&
        userInput != "vk_tab" && !isPatternMatching) {
        currentDirectory = "error";
        return noSubCommands;
    }

    return availableCommands;
}

bool Terminal::isGlobalCommand(std::string& commandName) {
    // Iterate through the list of global commands
    for (const std::string& globalCommand : globalCommandList) {
        if (globalCommand.size() >= commandName.size() &&
            std::equal(commandName.begin(), commandName.end(), globalCommand.begin())) {
            return true;
        }
    }
    return false;
}

void Terminal::displayAvailableCommands(std::vector<com> commandList) {
    uint8_t maxNameLength = 0;
    int lineCount = 0;

    // Find the longest command name for formatting
    for (const com& command : commandList) {
        if (command.name.size() > maxNameLength) {
            maxNameLength = command.name.size();
        }
    }

    // Print each command with aligned descriptions
    for (const com& command : commandList) {
        if (command.name != "<error>") {
            if (handlePagination(lineCount)) {
                std::cout << "\n  " << command.name;
                int nameLength = command.name.size();
                for (int i = 0; i <= (maxNameLength - nameLength + 5); i++) {
                    std::cout << " ";
                }
                // Uncomment if you want to display descriptions
                std::cout << command.description;
                lineCount++;
            } else {
                return;
            }
        } else {
            return;
        }
    }
}

std::string Terminal::getLastWord(const std::string& input) {
    std::istringstream stream(input);
    std::string stringword;
    std::string stringlastWord;
    while (stream >> stringword) {
        stringlastWord = stringword;
    }

    return stringlastWord;
}

std::vector<std::string> Terminal::splitIntoWords(const std::string& str) {
    std::vector<std::string> words;
    std::string currentWord;
    bool isInsideWord = false;
    bool isPreviousSpace = false;
    // Iterate through each character in the string
    for (char ch : str) {
        if (!std::isspace(ch) && ch != '?' && ch != '\t') {
            currentWord += ch;
            isInsideWord = true;
            isPreviousSpace = false;
        } else if (ch == '?') {
            if (!currentWord.empty()) {
                words.push_back(currentWord);
            }
            currentWord = ch;
            if (isPreviousSpace) {
                isNextWordHelpRequested = true;
            }
            isPreviousSpace = false;
        } else if (ch == '\t') {
            if (!currentWord.empty()) {
                words.push_back(currentWord);
            }
            currentWord = "vk_tab";
            if (isPreviousSpace) {
                isNextWordHelpRequested = true;
            }
            isPreviousSpace = false;
        } else if (isInsideWord) {
            words.push_back(currentWord);
            currentWord.clear();
            isInsideWord = false;
            isPreviousSpace = true;
        }
    }

    // Push the last word if any
    if (!currentWord.empty()) {
        words.push_back(currentWord);
    }

    return words;
}

std::string Terminal::trimString(std::string str) {
    std::string newstr = str;
    for (int ch = 0; ch <= str.size(); ch++) {
	if (isspace(str[ch])) {
	    newstr = newstr.substr(1);
	} else {
	    break;
	}
    }
    return newstr;
}

bool Terminal::matchInputPattern(const std::string& userInput, const std::string& expectedPattern) {
    if (expectedPattern == "WORD" && userInput != "?" && userInput != "vk_tab") {
        currentPattern = expectedPattern;
        isPatternMatching = true;
        return true;
    }

    if (expectedPattern == "LINE" && userInput != "?" && userInput != "vk_tab") {
        currentPattern = expectedPattern;
        isPatternMatching = true;
        isLineBasedInput = true;
        return true;
    }

    if (expectedPattern == "A.B.C.D" && userInput != "?" && userInput != "vk_tab") {
        int oct1, oct2, oct3, oct4;
    #ifdef _WIN32
        sscanf_s(userInput.c_str(), "%d.%d.%d.%d", &oct1, &oct2, &oct3, &oct4);
    #else
        sscanf(userInput.c_str(), "%d.%d.%d.%d", &oct1, &oct2, &oct3, &oct4);
    #endif
        std::vector<int> octets = {oct1, oct2, oct3, oct4};
        bool isValidIP = true;
        for (int octet : octets) {
            if (octet < 0 || octet > 255) {
                isValidIP = false;
            }
        }
        if (isValidIP) {
            currentPattern = expectedPattern;
            isPatternMatching = true;
            return true;
        }
    }

    if (expectedPattern == "X:X:X:X::X") {
        if (isIPv6Address(userInput)) {
            currentPattern = expectedPattern;
            isPatternMatching = true;
            return true;
        }
    }

    if (expectedPattern == "X:X:X:X::X/<0-128>") {
        if (isIPv6AddressWithMask(userInput)) {
            currentPattern = expectedPattern;
            isPatternMatching = true;
            return true;
        }
    }

    if (expectedPattern == "H.H.H") {
        if (isMACAddress(userInput)) {
            currentPattern = expectedPattern;
            isPatternMatching = true;
            return true;
        }
    }

    if (expectedPattern[0] == '<') {
        int min, max;
    #ifdef _WIN32
        sscanf_s(expectedPattern.c_str(), "<%d-%d>", &min, &max);
    #else
        sscanf(expectedPattern.c_str(), "<%d-%d>", &min, &max);
    #endif
        if (isNumeric(userInput)) {
            int number = std::stoi(userInput);
            if (number >= min && number <= max) {
                currentPattern = expectedPattern;
                isPatternMatching = true;
                isPatternMatchEnd = true;
                return true;
            }
        }
    }

    return false;
}

bool Terminal::isNumeric(const std::string& input) {
    if (input.empty() || (!std::isdigit(input[0]) && input[0] != '-' && input[0] != '+')) {
        return false;
    }

    char* endPtr;
    std::strtol(input.c_str(), &endPtr, 10);

    return (*endPtr == '\0');
}

bool Terminal::isValidCommandDirectory(nlohmann::json& directory) {
    return directory.contains("subcommands");
}

bool Terminal::handlePagination(int& lineNum) {
    if (lineNum % 10 == 0 && lineNum != 0) {
        std::cout << "\n  --More--";
    #ifdef _WIN32
	HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hInput, &mode);
    SetConsoleMode(hInput, mode & (~ENABLE_PROCESSED_INPUT));

	DWORD read;
    INPUT_RECORD ir;
    DWORD written;

	while (true) {

	maxCommandLength = getTerminalWidth() - initialLineLength;

	ReadConsoleInput(hInput, &ir, 1, &read);

	if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
	    if (ir.Event.KeyEvent.wVirtualKeyCode == VK_SPACE) {
		while (getCursorPosition().X != 0) {
		    moveCursorLeft(1);
		    cout << " ";
		    moveCursorLeft(1);
		}
		moveCursorLeft(1);
		return true;
	    } else if (ir.Event.KeyEvent.uChar.AsciiChar == 'q') {
	        while (getCursorPosition().X != 0) {
		    moveCursorLeft(1);
		    cout << " ";
		    moveCursorLeft(1);
		}
	        moveCursorLeft(1);
		return false;
	    }
	}
    }
    #else



    while (true) {

	maxCommandLength = getTerminalWidth() - initialLineLength;

	if (kbhit()) {
	    char nextch = getchar();
	    if (nextch == '\x20') {
		while (getCursorPosition().col != 1) {
		    moveCursorLeft(1);
                    std::cout << " ";
		    moveCursorLeft(1);
		}
		moveCursorLeft(1);
		return true;
		} else if (nextch == 'q') {
		    while (getCursorPosition().col != 1) {
			moveCursorLeft(1);
                        std::cout << " ";
			moveCursorLeft(1);
		    }
		    moveCursorLeft(1);
		    return false;
		}
	    }
	}

    #endif

    } else {
	return true;
    }
}

void Terminal::changeMode(std::string& newMode) {
	prevMode = currentMode;
	currentMode = newMode;
	workingDirectory = commandTree[currentMode];
	isModeChanged = true;
}

std::vector<std::string> Terminal::tokenize(const std::string& input, char delimiter) {
    // Vector to store the resulting tokens
    std::vector<std::string> tokens;

    // Temporary string to store each token during iteration
    std::string token;

    // Use an input string stream for easy parsing
    std::istringstream tokenStream(input);

    // Split the string based on the delimiter
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }

    return tokens;
}

std::string Terminal::padWithZeros(const std::string& input) {
    std::ostringstream paddedStream;
    paddedStream << std::setfill('0') << std::setw(4) << input;
    return paddedStream.str();
}

std::string Terminal::expandIPv6Address(const std::string& ipv6Address) {
    std::string ip, prefix;
    bool hasPrefix = false;

    size_t slashPos = ipv6Address.find('/');
    if (slashPos != std::string::npos) {
        ip = ipv6Address.substr(0, slashPos);
        prefix = ipv6Address.substr(slashPos);
        hasPrefix = true;
    } else {
        ip = ipv6Address;
    }

    std::string expandedIP = ip;
    size_t doubleColonPos = expandedIP.find("::");
    if (doubleColonPos != std::string::npos) {
        std::vector<std::string> frontSegments = tokenize(expandedIP.substr(0, doubleColonPos), ':');
        std::vector<std::string> backSegments = tokenize(expandedIP.substr(doubleColonPos + 2), ':');

        int hextetCount = frontSegments.size() + backSegments.size();
        std::string zeroSegments((8 - hextetCount), '0');
        expandedIP.clear();

        for (const std::string& segment : frontSegments) {
            expandedIP += padWithZeros(segment) + ":";
        }
        expandedIP += zeroSegments;
        for (const std::string& segment : backSegments) {
            expandedIP += padWithZeros(segment) + ":";
        }
        if (!expandedIP.empty() && expandedIP.back() == ':') {
            expandedIP.pop_back();
        }
    } else {
        std::vector<std::string> segments = tokenize(expandedIP, ':');
        expandedIP.clear();
        for (const std::string& segment : segments) {
            expandedIP += padWithZeros(segment) + ":";
        }
        if (!expandedIP.empty() && expandedIP.back() == ':') {
            expandedIP.pop_back();
        }
    }

    return expandedIP + prefix;
}

bool Terminal::isIPv6Address(const std::string& address) {
    std::regex ipRegex("((([0-9A-Fa-f]{1,4}):){7}([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,7}:|(([0-9A-Fa-f]{1,4}):){1,6}:([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,5}((:[0-9A-Fa-f]{1,4}){1,2})|(([0-9A-Fa-f]{1,4}):){1,4}((:[0-9A-Fa-f]{1,4}){1,3})|(([0-9A-Fa-f]{1,4}):){1,3}((:[0-9A-Fa-f]{1,4}){1,4})|(([0-9A-Fa-f]{1,4}):){1,2}((:[0-9A-Fa-f]{1,4}){1,5})|([0-9A-Fa-f]{1,4}):((:[0-9A-Fa-f]{1,4}){1,6})|:((:[0-9A-Fa-f]{1,4}){1,7}|:)|fe80:(:[0-9A-Fa-f]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9A-Fa-f]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))");
    return std::regex_match(address, ipRegex);
}

bool Terminal::isIPv6AddressWithMask(const std::string& addressWithMask) {
    std::regex ipRegex("((([0-9A-Fa-f]{1,4}):){7}([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,7}:|(([0-9A-Fa-f]{1,4}):){1,6}:([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,5}((:[0-9A-Fa-f]{1,4}){1,2})|(([0-9A-Fa-f]{1,4}):){1,4}((:[0-9A-Fa-f]{1,4}){1,3})|(([0-9A-Fa-f]{1,4}):){1,3}((:[0-9A-Fa-f]{1,4}){1,4})|(([0-9A-Fa-f]{1,4}):){1,2}((:[0-9A-Fa-f]{1,4}){1,5})|([0-9A-Fa-f]{1,4}):((:[0-9A-Fa-f]{1,4}){1,6})|:((:[0-9A-Fa-f]{1,4}){1,7}|:)|fe80:(:[0-9A-Fa-f]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9A-Fa-f]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))/(12[0-8]|1[01][0-9]|[1-9]?[0-9])");
    return std::regex_match(addressWithMask, ipRegex);
}

bool Terminal::isMACAddress(const std::string& macAddress) {
    std::regex macRegex("^([0-9A-Fa-f]{1,4}[:-]?){6}([0-9A-Fa-f]{1,4})$");
    return std::regex_match(macAddress, macRegex);
}

enum InterfaceMode
{
	
};

void Terminal::configureInterfaceMode(std::string& type) {
	if (type == "Dialer") {changeMode(mode.dialer); currentSubMode = type;}
	else if (type == "Ethernet") {changeMode(mode.ethernet); activeInterfaces = &interfaceList["EthernetList"]; currentSubMode = type;}
	else if (type == "FastEthernet") {changeMode(mode.fastEthernet); activeInterfaces = &interfaceList["FastEthernetList"]; currentSubMode = type;}
	else if (type == "GigabitEthernet") {changeMode(mode.gigabitEthernet); activeInterfaces = &interfaceList["GigabitList"]; currentSubMode = type;}
	else if (type == "Loopback") {changeMode(mode.loopback); activeInterfaces = &interfaceList["LoopbackList"]; currentSubMode = type;}
	else if (type == "Portchannel") {changeMode(mode.portchannel); activeInterfaces = &interfaceList["PortchannelList"]; currentSubMode = type;}
	else if (type == "Tunnel") {changeMode(mode.tunnel); activeInterfaces = &interfaceList["TunnelList"]; currentSubMode = type;}
	else if (type == "Virtual-Template") {changeMode(mode.virtualTemplate); activeInterfaces = &interfaceList["VirtualTemplateList"]; currentSubMode = type;}
	else if (type == "Vlan") {changeMode(mode.vlan); activeInterfaces = &interfaceList["VlanList"]; currentSubMode = type;}
	workingDirectory = workingDirectory[0][type];
}

void Terminal::configureRoutingMode(RoutingMode type) {
	if (type == RoutingMode::BGP) {changeMode(mode.bgp); currentSubMode = "bgp";} 
	else if (type == RoutingMode::EIGRP_CLASSIC) {changeMode(mode.eigrp_classic); currentSubMode = "eigrp_classic";} 
	else if (type == RoutingMode::EIGRP_NAMED) {changeMode(mode.eigrp_named); currentSubMode = "eigrp_named";} 
	else if (type == RoutingMode::OSPF) {changeMode(mode.ospf); currentSubMode = "ospf";}
	else if (type == RoutingMode::RIP) {changeMode(mode.rip); currentSubMode = "rip";}
	workingDirectory = workingDirectory[0][currentSubMode];
}

