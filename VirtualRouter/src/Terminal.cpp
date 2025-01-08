#include <Terminal.h>
#include <fstream>
#include <regex>

#include <X11/Xlib.h>
#include <X11/extensions/xtestconst.h>

std::string Terminal::defaultMode = "<";

Terminal::Terminal(bool enableDebug) : Console() 
{
    // Set debug mode based on the input parameter
    isDebugModeEnabled = enableDebug;
    initTerminal();
}

Terminal::Terminal(std::shared_ptr<IConsole> term, std::shared_ptr<IFileSystem> fs) : Console(std::move(term), std::move(fs))
{
    // Set debug mode based on the input parameter
    isDebugModeEnabled = false;
}

void Terminal::initTerminal()
{
    initConsole();
    // Output a message to indicate terminal initialization
    std::cout << "Initializing Terminal..." << std::endl;

    // Initialize default error and carriage return commands
    errorCommand.name = "<error>";
    carriageReturnCommand.name = "<cr>";

    // Initialize the console and configuration settings
    // Load the command tree configuration from a JSON file
    commandTree.clear();
    std::string configFileString;
    if (fileSystem->fileExists(COMMAND_TREE))
    {
        std::string content;
        if (fileSystem->readFile(COMMAND_TREE, configFileString))
        {
            try
            {
                commandTree = nlohmann::json::parse(configFileString);
                configFileString.clear();
            }
            catch (json::parse_error& e)
            {
                commandTree = nlohmann::json::object();
                configFileString.clear();
            }
        }
    }
    else 
    {
        std::cerr << "Failed to open command tree: " << COMMAND_TREE << std::endl;
        commandTree = nlohmann::ordered_json::object();
    }

    // Load the JSON order
    configSchema.clear();
    std::string configSchemaString;
    if (fileSystem->fileExists(CONFIG_SCHEMA))
    {
        std::string content;
        if (fileSystem->readFile(CONFIG_SCHEMA, configSchemaString))
        {
            try
            {
                configSchema = nlohmann::ordered_json::parse(configSchemaString);
                configFileString.clear();
            }
            catch (json::parse_error& e)
            {
                commandTree = nlohmann::ordered_json::object();
                configFileString.clear();
            }
        }
    }
    else
    {
        std::cerr << "Failed to open configuration schema file: " << CONFIG_SCHEMA << std::endl;
        configSchema = nlohmann::ordered_json::object();
    }

    // Set the terminal to Global Configuration mode by default
    changeMode(mode.globalConfiguration, true);

    // Restore the terminal state from saved configurations
    recoverState();
}

void Terminal::recoverState() 
{
    // Retrieve the list of saved commands from the JSON recovery system
    std::vector<std::string> savedCommands = recoverConfigs();

    // Execute each saved command to restore the terminal's state
    for (std::string& command : savedCommands) {
        initializeProcessingState();
        executeCommand(command);  // Execute the command
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Add a delay for stability
    }

    // Reset the configuration root as long with the mode
    changeMode(mode.userExec, true);
    configNode = &root;
}

bool Terminal::handleInput(std::string test)
{
    // Retrieve the hostname from the global settings and reset cursor position
    std::string hostname = Global::getInstance().getHostname();
    cursorPos = 0;
    setPrompt(hostname + currentMode);

    // Read the user's input from the terminal
    std::string userCommand = input(test);

    // Handle IPv6 address input and print the expanded version
    if (isIPv6Address(userCommand)) {
        std::cout << expandIPv6Address(userCommand);
    }

    // Handle the Ctrl-Z shortcut to switch to privilegedExec mode
    if (userCommand == "CRT-Z" && currentMode != mode.userExec) {
        if (!changeMode(mode.privilegedExec, true))
        {
            std::cout << std::endl;
            return false;
        }
    }

    // Execute commands
    initializeProcessingState();
    if (!executeCommand(userCommand))
    {
        std::cout << std::endl;
        return false;
    }

    // Move to the next line after command execution
    std::cout << std::endl;
    return true;
}

void Terminal::initializeProcessingState()
{
    currentDirectory         = workingDirectory;
    isNextWordHelpRequested  = false;
    isRunning                = true;
    isMatchSuccessful        = false;
    isHelpModeActive         = false;
    endOfCommand             = false;
    isLineBasedInput         = false;
    attemptingGlobalCommand  = false;
    isGlobalCommandExecution = false;
    isCommandValid           = false;
}

bool Terminal::detectHelpTriggers(const std::vector<std::string>& parsedWords)
{
    return std::any_of(parsedWords.begin(), parsedWords.end(),
        [](const std::string& word) 
        { 
            return word == "?" || word == "vk_tab"; 
        }
    );
}

bool Terminal::isDoCommand(const std::vector<std::string>& parsedWords)
{
    if (parsedWords.empty()) return false;
    if (parsedWords[0] != "do" || parsedWords.size() < 2) return false;

    // Avoid "exit and conf"
    if (parsedWords[1] == "exit" || parsedWords[1].rfind("conf", 0) == 0) return false;

    // Must not already be in userExec or privilegedExec
    return currentMode != mode.userExec && currentMode != mode.privilegedExec;
}

std::string Terminal::executeDoCommand(std::string remainingCommand)
{
    attemptingGlobalCommand = true;
    // Save current state
    std::string previousMode = currentMode;
    json previousCommandTree = workingDirectory;
    nlohmann::ordered_json* prevModeSchema = modeSchema;
    nlohmann::ordered_json* previousConfigNode = configNode;

    // Switch to privileged mode and execute
    changeMode(mode.privilegedExec, true);
    isGlobalCommandExecution = executeCommand(remainingCommand);

    // Restore old mode / working directory
    changeMode(previousMode, true);
    configNode       = previousConfigNode;
    workingDirectory = previousCommandTree;
    modeSchema       = prevModeSchema;

    // Return "error" to signify no further processing
    return "error";
}

void Terminal::appendLineBasedCommand(const std::vector<std::string>& parsedWords, size_t currentIndex, std::string& fullyFormattedCommand, std::string& volatileCommand)
{
    fullyFormattedCommand += " " + parsedWords[currentIndex];
    volatileCommand       += " " + parsedWords[currentIndex];
}

void Terminal::processNonLineBasedWord(std::string& word, std::vector<Com>& previousCommandList, std::string& formattedOldCommand, std::string& fullyFormattedCommand, std::string& volatileCommand, const std::string& inputCommand, bool& isFirstIteration)
{
    // Reset matching states
    isPatternMatching = false;
    isPatternMatchEnd = false;

    // if processing has already failed, baile out
    if (!isRunning) return;

    // Grab the current list of available commands
    std::vector<Com> availableCommands = getAvailableCommands(currentDirectory, word, isFirstIteration);
    isFirstIteration = false;

    // Handle help question "?"
    if (handleHelpQuestion(word, previousCommandList, inputCommand, formattedOldCommand, fullyFormattedCommand, volatileCommand))
    {
        return;
    }

    // Handle "vk_tab" for tab completion
    if (handleTabCompletion(word, previousCommandList, inputCommand, formattedOldCommand, fullyFormattedCommand, volatileCommand))
    {
        return;
    }

    // If directory is "error", attempt to fix by switching to global config
    if (!attemptGlobalCommand(inputCommand))
    {
        return; // If attemptGlobalCommand returned an error condition, just stop
    }
    
    // Check for incorrect command
    if (currentDirectory == "error" && !isGlobalCommand(word) && !isHelpModeActive && isRunning)
    {
        if (availableCommands.size() > 1)
        {
            handleAmbiguousInputMarker(word);
            return;
        }
        else
        {
            handleInvalidInputMarker(formattedOldCommand);
            return;
        }
    }

    // Attempt to match user's word with the available commands
    bool isCommandDone = false;
    matchCommand(
        inputCommand, word, availableCommands, previousCommandList, 
        formattedOldCommand, fullyFormattedCommand, volatileCommand, 
        isCommandDone
    );
}

bool Terminal::handleHelpQuestion(const std::string& word, std::vector<Com>& previousCommandList,
               const std::string& inputCommand, std::string& formattedOldCommand,
               std::string& fullyFormattedCommand, std::string& volatileCommand)
{
    // Return false if "?" is not actually truggered or doesn't apply
    if (word != "?" || isMatchSuccessful || previousCommandList.empty() || isNextWordHelpRequested || endOfCommand)
    {
        if ((word == "?") && isMatchSuccessful && !previousCommandList.empty() && !isNextWordHelpRequested)
        {
            nextLine = inputCommand;
        }
        return false;
    }

    fullyFormattedCommand += word;
    volatileCommand       += word;

    nextLine = " " + inputCommand.substr(0, inputCommand.size() - 1);

    if (previousCommandList[0].name != "<cr>")
    {
        displayAvailableCommands(previousCommandList);
    }
    else
    {
        nextLine = trimString(inputCommand);
    }
    return true;
}

bool Terminal::handleTabCompletion(const std::string& word, std::vector<Com>& previousCommandList,
               const std::string& inputCommand, std::string& formattedOldCommand,
               std::string& fullyFormattedCommand, std::string& volatileCommand)
{
    if (word != "vk_tab" || isNextWordHelpRequested) return false;

    // Multiple suggestions
    if (previousCommandList.size() > 1)
    {
        fullyFormattedCommand += word;
        volatileCommand       += word;
        nextLine = formattedOldCommand + " ";
    }
    // no suggestions
    else if (previousCommandList.empty())
    {
        nextLine = trimString(inputCommand);
    }
    else if (previousCommandList[0].name == "<error>")
    {
        return false;
    }
    // Exactly one suggestion => auto complete
    else
    {
        nextLine = " " + inputCommand.substr(0, inputCommand.size() - 1);
        long lastSpacePosition = static_cast<long>(nextLine.rfind(' '));
        if (lastSpacePosition == -1)
        {
            // No spaces found
            nextLine += " " + maskInput(inputCommand.substr(0, inputCommand.size() - 1), getLastWord(fullyFormattedCommand)) + "  ";
        }
        else
        {
            // Insert after last space
            nextLine = maskInput(" " + inputCommand.substr(0, inputCommand.size() - 1), nextLine.substr(0, static_cast<size_t>(lastSpacePosition)) + " " + getLastWord(fullyFormattedCommand)) + "  ";
        }
    }
    return true;
}

    bool Terminal::attemptGlobalCommand(const std::string& inputCommand)
{
    if (currentDirectory == "error" &&
        currentMode != mode.globalConfiguration &&
        currentMode != mode.userExec &&
        currentMode != mode.privilegedExec &&
        !isHelpModeActive && 
        Functions::lowerCase(inputCommand) != "exit")
    {
        attemptingGlobalCommand = true;
        // Backup
        std::string prevMode        = currentMode;
        auto        prevDirectory   = workingDirectory;
        auto        prevModeSchema  = modeSchema;
        auto        prevConfig      = configNode;

        // Attempt global execution
        changeMode(mode.globalConfiguration, true);
        historyToGlobal();
        std::string inputCommandCopy = inputCommand;
        if (executeCommand(inputCommandCopy))
        {
            isGlobalCommandExecution = true;
        }

        if (currentMode == mode.globalConfiguration)
        {
            if (isCommandExecutionSuccessful)
            {
                return false; // Triggers "error" return
            }
            else
            {
                // Restore
                changeMode(prevMode, true);
                configNode       = prevConfig;
                modeSchema       = prevModeSchema;
                workingDirectory = prevDirectory;
                if (isCommandExecutionSuccessful)
                {
                    return false;
                }
            }
        }
        else
        {
            return false;
        }
    }
    return true;
}

void Terminal::handleInvalidInputMarker(const std::string& formattedOldCommand)
{
    isCommandInvalid = true;
    isRunning = false;
    std::string invalidInput = "\n";

    std::string hostname = Global::getInstance().getHostname();
    // Print spaces for hostname, mode, old command
    invalidInput += std::string(initialLineLength + formattedOldCommand.size(), ' ') + "^\n% Invlid input detected at '^' marker.\n";
    iConsole->print(invalidInput);
}

void Terminal::handleAmbiguousInputMarker(const std::string& ambiguousCommand)
{
    isCommandInvalid = true;
    isCommandValid = false;
    isRunning = false;
    std::string invalidInput = R"(% Ambiguous command: ")" + ambiguousCommand + "\"";
    iConsole->print("\n" + invalidInput);
}

std::string Terminal::maskInput(const std::string& prefix, std::string original)
{
    if (prefix.length() > original.length())
    {
        return original;
    }

    // Replace the beginning of the original string with the prefix
    std::copy(prefix.begin(), prefix.end(), original.begin());

    return original;
}

void Terminal::matchCommand(const std::string& inputCommand, const std::string& uWord, 
               const std::vector<Com>& availableCommands, std::vector<Com>& previousCommandList, 
               std::string& formattedOldCommand, std::string& fullyFormattedCommand,
               std::string& volatileCommand, bool& isCommandDone)
{
    std::string word = Functions::lowerCase(uWord);
    // Build a list of commands that match the user-typed 'word'.
    std::vector<Com> matchingCommands;
    for (const auto& command : availableCommands)
    {
        // If patteru matching is enabled and the command name matches the current pattern
        if (isPatternMatching && command.name == currentPattern)
        {
            matchingCommands.push_back(command);
        }
        // Or if the user-typed word is a prefix of the command name
        if (command.name.size() >= word.size() && 
            std::equal(word.begin(), word.end(), Functions::lowerCase(command.name).begin()))
        matchingCommands.push_back(command);
    }

    // if no specific match was found, fall back to the entire 'availableCommands'.
    previousCommandList = matchingCommands.empty() ? availableCommands : matchingCommands;

    // Handle "?" or "vk_tab" after partial match:
    if (word == "?" && (isMatchSuccessful || isNextWordHelpRequested) && !endOfCommand)
    {
        // Display possible commands and append "?"
        displayAvailableCommands(availableCommands);
        fullyFormattedCommand += word;
        volatileCommand       += word;

        // Typically set nextline to old command + space
        nextLine = formattedOldCommand + " ";
        // If the first command is <error>, revert to raw input
        if (!availableCommands.empty() && availableCommands[0].name == "<error>")
        {
            nextLine = inputCommand;
        }

        // If in help mode, an extra space is appended for later
        if (isHelpModeActive)
        {
            nextLine += " ";
        }

        // We displayed help, so reset success
        isMatchSuccessful = false;
        return; // This completes processing of this word
    }
    else if (word == "vk_tab" && !previousCommandList.empty())
    {
        // If there's an <error> or the user requested help, use the raw input
        if (previousCommandList[0].name == "<error>" || isNextWordHelpRequested)
        {
            // Set the nextLine but shave off the "\t"
            nextLine = inputCommand.substr(0, inputCommand.size() - 1) + " ";
        }
        
        isMatchSuccessful = false;
        return;
    }

    // Reset isMatchSuccessful now that "?" / "vk_tab" is handled
    isMatchSuccessful = false;

    // Check if exactly one match remains
    if (previousCommandList.size() == 1 && !matchingCommands.empty())
    {
        // If pattern-matching is on and the command is exactly currentPattern
        if (isPatternMatching && currentPattern == matchingCommands[0].name)
        {
            isMatchSuccessful = true;
        }
        // If typed word matches the command name exactly
        if (matchingCommands[0].name == word)
        {
            isMatchSuccessful = true;
        }
    }
    // If the user typed "?" again after partial match
    else if (word == "?" && (isMatchSuccessful || isNextWordHelpRequested))
    {
        // Set the nextLine to the raw input
        nextLine = inputCommand;
    }

    // If this word isn't done yet, decide how to append matched commands
    if (!isCommandDone && matchingCommands.size() <= 1)
    {
        // (A) If we're pattern-matching, use the user's typed pattern
        if (isPatternMatching && !matchingCommands.empty())
        {
            fullyFormattedCommand += " " + uWord;
            formattedOldCommand   += " " + uWord;
            volatileCommand       += " " + currentPattern;
            previousMatch          = matchingCommands[0].name;
        }
        // (B) If there is exactly one match
        else if (matchingCommands.size() == 1)
        {
            fullyFormattedCommand += " " + matchingCommands[0].name;
            formattedOldCommand   += " " + word;
            volatileCommand       += " " + word;
            isCommandDone          = true;
            previousMatch          = matchingCommands[0].name;
        }
        // (C) If no matches but the command is flagged as endOfCommand
        else if (matchingCommands.empty() && endOfCommand)
        {
            fullyFormattedCommand += " " + endCommandString;
            formattedOldCommand   += " " + word;
            volatileCommand       += " " + word;
        }
        // (D) If no matches at all (not endOfCommand)
        else if (matchingCommands.empty())
        {
            fullyFormattedCommand += " " + word;
            formattedOldCommand   += " " + word;
            volatileCommand       += " " + word;

            // If its not a help scenario, some code returns the typed word or ends here
            if (!isHelpModeActive)
            {
                fullyFormattedCommand = word;
            }
        }
        // (E) If multiple matches but the first is a valid guess
        else
        {
            fullyFormattedCommand += " " + matchingCommands[0].name;
            formattedOldCommand   += " " + word;
            volatileCommand       += " " + word;
            isCommandDone          = true;
            previousMatch          = matchingCommands[0].name;
        }
    }
    else
    {
        // Command is partially matched or has multiple possibilities
        formattedOldCommand += " " + word;
        volatileCommand     += " " + word;
    }
}

std::string Terminal::normalizeCommand(const std::string& inputCommand) 
{
    // Return an empty string if the input command is empty
    if (inputCommand.empty()) return "";

    // Normalize to lowerCase
    //std::string normalizedCommand = Functions::lowerCase(inputCommand);

    // Parse the command
    std::vector<std::string> parsedWords = splitIntoWords(inputCommand);

    std::vector<Com> previousCommandList;
    std::string formattedOldCommand, lastProcessedWord, fullyFormattedCommand, volatileCommand;
    size_t currentIndex = 0;
    bool isFirstIteration = true;

    // Check for help triggers ("?" or "vk_tab")
    isHelpModeActive = detectHelpTriggers(parsedWords);

    // Handle "do" command
    if (isDoCommand(parsedWords))
    {
        return executeDoCommand(inputCommand.substr(2));
    } 
    
    // Mark as valid if the first word is "?" or "vk_tab"
    isMatchSuccessful = (!parsedWords.empty() && (parsedWords[0] == "?" || parsedWords[0] == "vk_tab"));

    if (isGlobalCommand(parsedWords[0]) && !isMatchSuccessful)
    {
        return parsedWords[0];
    }

    // Process each word in the parsed command
    for (std::string& word : parsedWords) {
        if (isGlobalCommandExecution)
        {
            // If a global command was succcessfull after falure, return
            return "";
        }
        else if (isLineBasedInput) {
            appendLineBasedCommand(parsedWords, currentIndex, fullyFormattedCommand, volatileCommand);
        }
        else
        {
            processNonLineBasedWord(word, previousCommandList, formattedOldCommand, fullyFormattedCommand, volatileCommand, inputCommand, isFirstIteration);
        }
    }

    // Final trumming/formatting
    formattedOldCommand   = trimString(formattedOldCommand);
    volatileCommand       = trimString(volatileCommand);
    fullyFormattedCommand = trimString(fullyFormattedCommand);
    nextLine              = trimString(nextLine);

    // Update command history
    commandHistory = splitIntoWords(volatileCommand);

    // Check if command is incomplete
    if (!isCommandValid && !isHelpModeActive && !isPatternMatchEnd && !isLineBasedInput && !isCommandInvalid && !isGlobalCommandExecution && isRunning)
    {
        isRunning = false;
        iConsole->print("\nIncomplete Command");
        return "";
    }

    isCommandInvalid = false;

    // Return the final processed command
    return fullyFormattedCommand;
}

std::vector<Com> Terminal::getAvailableCommands(const nlohmann::json& commandTree, const std::string& userInput, bool inPrivilegedMode) 
{
    // Get lowercase input
    std::string lowerUserInput = Functions::lowerCase(userInput);

    // Container for storing available commands
    std::vector<Com> availableCommands;

    // Clone the current command directory
    nlohmann::json currentCommandDirectory = currentDirectory;

    // Default response for invalid or unavailable commands
    std::vector<Com> noSubCommands = {errorCommand};

    // Variables for handling exact matches
    Com exactMatchCommand;
    bool isExactMatch = false;
    bool isValidCommand = false;

    // If the current directory is invalid, return the default error response
    if (currentDirectory == "error") 
    {
        return noSubCommands;
    }

    // Iterate over all commands in the current directory
    nlohmann::json commandNode;
    int matchCount = 0;
    for (const json& command : currentCommandDirectory) 
    {
        if (command.is_object())
        {
            Com commandData;
            commandData.name = command["name"];
            commandData.description = command["description"];
            availableCommands.push_back(commandData);

            // Check if the user input matches a pattern or specific command
            std::string commandName = command["name"];
            if (matchInputPattern(lowerUserInput, commandName) && !endOfCommand) 
            {
                commandNode = command;
                matchCount++;
                if (!isValidCommandDirectory(commandNode)) 
                {
                    endOfCommand = true;
                }
            } 
            else if (commandName.size() >= userInput.size()) 
            {
                if (std::equal(lowerUserInput.begin(), lowerUserInput.end(), Functions::lowerCase(commandName).begin()) && !isExactMatch) 
                {
                    commandNode = command;
                    matchCount++;
                }
                if (commandName == lowerUserInput) 
                {
                    isExactMatch = true;
                    exactMatchCommand.name = Functions::lowerCase(command["name"]);
                    exactMatchCommand.description = command["description"];
                    commandNode = command;
                }
            }
        }
    }

    // Handle exact matches and valid commands
    if (isExactMatch) 
    {
        matchCount = 1;
    }
    if (matchCount == 1 && isValidCommandDirectory(commandNode)) 
    {
        currentDirectory = commandNode["subcommands"];
        for (const auto& subCommand : currentDirectory) 
        {
            if (subCommand["name"] == "<cr>") 
            {
                isCommandValid = true;
                isValidCommand = true;
            }
        }
        if (!isValidCommand) 
        {
            isCommandValid = false;
        }
        if (isExactMatch) 
        {
            availableCommands.clear();
            availableCommands.push_back(exactMatchCommand);
        }
    } 
    else if ((isValidCommandDirectory(commandNode) || matchCount != 1) && !isMatchSuccessful) 
    {
        currentDirectory = "error";
    } 
    else if (isExactMatch && !isValidCommandDirectory(commandNode) && !userInput.empty()) 
    {
        endCommandString = Functions::lowerCase(commandNode["name"]);
        endOfCommand = true;
        return noSubCommands;
    }

    // Handle unmatched or invalid commands
    if (matchCount == 0 && !userInput.empty() && currentDirectory == "error" &&
        lowerUserInput != "?" && lowerUserInput != "vk_tab") 
    {
        return noSubCommands;
    }
    if (matchCount == 0 && !isValidCommandDirectory(commandNode) && lowerUserInput != "?" &&
        lowerUserInput != "vk_tab" && !isPatternMatching) 
    {
        currentDirectory = "error";
        return noSubCommands;
    }

    return availableCommands;
}

bool Terminal::isGlobalCommand(std::string &commandName)
{
    // Iterate through the list of global commands
    for (const std::string &globalCommand : globalCommandList)
    {
        if (commandName == globalCommand)
        {
            return true;
        }
    }
    return false;
}

void Terminal::displayAvailableCommands(std::vector<Com> commandList)
{
    size_t maxNameLength = 0;
    size_t lineCount = 0;

    // Find the longest command name for formatting
    for (const Com &command : commandList)
    {
        if (command.name.size() > maxNameLength)
        {
            maxNameLength = command.name.size();
        }
    }

    // Print each command with aligned descriptions
    for (const Com &command : commandList)
    {
        if (command.name != "<error>")
        {
            if (handlePagination(lineCount))
            {
                std::string display;
                display += "\n  " + command.name;
                size_t nameLength = command.name.size();
                for (size_t i = 0; i <= (maxNameLength - nameLength + 5); i++)
                {
                    display +=" ";
                }
                // Uncomment if you want to display descriptions
                display += command.description;
                iConsole->print(display);
                lineCount++;
            }
            else
            {
                return;
            }
        }
        else
        {
            return;
        }
    }
}

std::string Terminal::getLastWord(const std::string &input)
{
    std::istringstream stream(input);
    std::string stringword;
    std::string stringlastWord;
    while (stream >> stringword)
    {
        stringlastWord = stringword;
    }

    return stringlastWord;
}

std::vector<std::string> Terminal::splitIntoWords(const std::string &str)
{
    // Initialize function variables
    std::vector<std::string> words;
    std::string currentWord;
    bool isInsideWord = false;
    bool isPreviousSpace = false;

    // Iterate through each character in the string
    for (char ch : str)
    {
        // Check if the char is a special character
        if (!std::isspace(ch) && ch != '?' && ch != '\t')
        {
            currentWord += ch;
            isInsideWord = true;
            isPreviousSpace = false;
        }
        else if (ch == '?')
        {
            if (!currentWord.empty())
            {
                words.push_back(currentWord);
            }

            currentWord = ch;

            if (isPreviousSpace)
            {
                isNextWordHelpRequested = true;
            }

            isPreviousSpace = false;
        }
        else if (ch == '\t')
        {
            if (!currentWord.empty())
            {
                words.push_back(currentWord);
            }

            currentWord = "vk_tab";

            if (isPreviousSpace)
            {
                isNextWordHelpRequested = true;
            }

            isPreviousSpace = false;
        }
        else if (isInsideWord)
        {
            words.push_back(currentWord);
            currentWord.clear();
            isInsideWord = false;
            isPreviousSpace = true;
        }
    }

    // Push the last word if any
    if (!currentWord.empty())
    {
        words.push_back(currentWord);
    }

    return words;
}

std::string Terminal::trimString(std::string str)
{
    std::string newstr = str;
    for (size_t ch = 0; ch <= str.size(); ch++)
    {
        if (isspace(str[ch]))
        {
            newstr = newstr.substr(1);
        }
        else
        {
            break;
        }
    }
    return newstr;
}

bool Terminal::matchInputPattern(const std::string &userInput, const std::string &expectedPattern)
{
    if (expectedPattern == "WORD" && userInput != "?" && userInput != "vk_tab")
    {
        currentPattern = expectedPattern;
        isPatternMatching = true;
        return true;
    }

    if (expectedPattern == "LINE" && userInput != "?" && userInput != "vk_tab")
    {
        currentPattern = expectedPattern;
        isPatternMatching = true;
        isLineBasedInput = true;
        return true;
    }

    if (expectedPattern == "A.B.C.D" && userInput != "?" && userInput != "vk_tab")
    {
        int oct1, oct2, oct3, oct4;
        int charsRead = 0;

        if (sscanf(userInput.c_str(), "%d.%d.%d.%d%n", &oct1, &oct2, &oct3, &oct4, &charsRead) == 4 && charsRead == static_cast<int>(userInput.length()))
        {
            std::vector<int> octets = {oct1, oct2, oct3, oct4};
            bool isValidIP = true;
            for (int octet : octets)
            {
                if (octet < 0 || octet > 255)
                {
                    isValidIP = false;
                }
            }
            if (isValidIP)
            {
                currentPattern = expectedPattern;
                isPatternMatching = true;
                return true;
            }
        }
    }

    if (expectedPattern == "X:X:X:X::X")
    {
        if (isIPv6Address(userInput))
        {
            currentPattern = expectedPattern;
            isPatternMatching = true;
            return true;
        }
    }

    if (expectedPattern == "X:X:X:X::X/<0-128>")
    {
        if (isIPv6AddressWithMask(userInput))
        {
            currentPattern = expectedPattern;
            isPatternMatching = true;
            return true;
        }
    }

    if (expectedPattern == "H.H.H")
    {
        if (isMACAddress(userInput))
        {
            currentPattern = expectedPattern;
            isPatternMatching = true;
            return true;
        }
    }

    if (expectedPattern[0] == '<')
    {
        int min, max;
        sscanf(expectedPattern.c_str(), "<%d-%d>", &min, &max);
        if (isNumeric(userInput))
        {
            int number = std::stoi(userInput);
            if (number >= min && number <= max)
            {
                currentPattern = expectedPattern;
                isPatternMatching = true;
                isPatternMatchEnd = true;
                return true;
            }
        }
    }

    return false;
}

bool Terminal::isNumeric(const std::string &input)
{
    if (input.empty() || (!std::isdigit(input[0]) && input[0] != '-' && input[0] != '+'))
    {
        return false;
    }

    char *endPtr;
    std::strtol(input.c_str(), &endPtr, 10);

    return (*endPtr == '\0');
}

bool Terminal::isValidCommandDirectory(nlohmann::json &directory)
{
    return directory.contains("subcommands");
}

bool Terminal::handlePagination(size_t &lineNum)
{
    if (paginationCount > 0 && lineNum % paginationCount == 0 && lineNum != 0)
    {
        std::cout << "\n  --More--";
        while (true)
        {

            maxCommandLength = getTerminalWidth() - initialLineLength;

            if (kbhit())
            {
                char nextch = static_cast<char>(getchar());
                if (nextch == '\x20')
                {
                    std::cout << "\033[2k\033[1G";
                    moveCursorUp(1);
                    return true;
                }
                else if (nextch == 'q')
                {
                    std::cout << "\033[2k\033[1G";
                    std::cout << std::string(10, ' ');
                    std::cout << "\033[2k\033[1G";
                    moveCursorUp(1);
                    return false;
                }
            }
        }
    }
    else
    {
        return true;
    }
}

bool Terminal::changeMode(std::string &newMode, bool processing)
{
    // Check if the mode exists and has valid commands
    if (!commandTree.contains(newMode) || !commandTree[newMode].is_array())
    {
        return false;
    }

    prevMode = currentMode;
    currentMode = newMode;
    workingDirectory = commandTree[currentMode];
    currentDirectory = workingDirectory;
    isModeChanged = true;

    if (configSchema.contains(currentMode))
    {
        if (configSchema.contains(currentMode))
        {
            if (processing)
            {
                modeSchema = &(configSchema[currentMode]);
            }
            else
            {
                tempModeSchema = &(configSchema[currentMode]);
            }
        }
    }

    if (!modeHistory.empty() && configNode != modeHistory.back())
    {
        modeHistory.push_back(configNode);
    }
    else
    {
        modeHistory.clear();
        modeHistory.push_back(&root);
    }
    return true;
}

std::vector<std::string> Terminal::tokenize(const std::string& input, char delimiter) 
{
    // Vector to store the resulting tokens
    std::vector<std::string> tokens;

    // Temporary string to store each token during iteration
    std::string token;

    // Use an input string stream for easy parsing
    std::istringstream tokenStream(input);

    // Split the string based on the delimiter
    while (std::getline(tokenStream, token, delimiter)) 
    {
        tokens.push_back(token);
    }

    return tokens;
}

std::string Terminal::padWithZeros(const std::string& input) 
{
    std::ostringstream paddedStream;
    paddedStream << std::setfill('0') << std::setw(4) << input;
    return paddedStream.str();
}

std::string Terminal::expandIPv6Address(const std::string& ipv6Address) 
{
    std::string ip, prefix;

    size_t slashPos = ipv6Address.find('/');
    if (slashPos != std::string::npos) 
    {
        ip = ipv6Address.substr(0, slashPos);
        prefix = ipv6Address.substr(slashPos);
    } 
    else 
    {
        ip = ipv6Address;
    }

    std::string expandedIP = ip;
    size_t doubleColonPos = expandedIP.find("::");
    if (doubleColonPos != std::string::npos) 
    {
        std::vector<std::string> frontSegments = tokenize(expandedIP.substr(0, doubleColonPos), ':');
        std::vector<std::string> backSegments = tokenize(expandedIP.substr(doubleColonPos + 2), ':');

        size_t hextetCount = frontSegments.size() + backSegments.size();
        std::string zeroSegments((8 - hextetCount), '0');
        expandedIP.clear();

        for (const std::string& segment : frontSegments) 
        {
            expandedIP += padWithZeros(segment) + ":";
        }
        expandedIP += zeroSegments;
        for (const std::string& segment : backSegments) 
        {
            expandedIP += padWithZeros(segment) + ":";
        }
        if (!expandedIP.empty() && expandedIP.back() == ':') 
        {
            expandedIP.pop_back();
        }
    } 
    else 
    {
        std::vector<std::string> segments = tokenize(expandedIP, ':');
        expandedIP.clear();
        for (const std::string& segment : segments) 
        {
            expandedIP += padWithZeros(segment) + ":";
        }
        if (!expandedIP.empty() && expandedIP.back() == ':') 
        {
            expandedIP.pop_back();
        }
    }

    return expandedIP + prefix;
}

bool Terminal::isIPv6Address(const std::string& address) 
{
    std::regex ipRegex("((([0-9A-Fa-f]{1,4}):){7}([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,7}:|(([0-9A-Fa-f]{1,4}):){1,6}:([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,5}((:[0-9A-Fa-f]{1,4}){1,2})|(([0-9A-Fa-f]{1,4}):){1,4}((:[0-9A-Fa-f]{1,4}){1,3})|(([0-9A-Fa-f]{1,4}):){1,3}((:[0-9A-Fa-f]{1,4}){1,4})|(([0-9A-Fa-f]{1,4}):){1,2}((:[0-9A-Fa-f]{1,4}){1,5})|([0-9A-Fa-f]{1,4}):((:[0-9A-Fa-f]{1,4}){1,6})|:((:[0-9A-Fa-f]{1,4}){1,7}|:)|fe80:(:[0-9A-Fa-f]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9A-Fa-f]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))");
    return std::regex_match(address, ipRegex);
}

bool Terminal::isIPv6AddressWithMask(const std::string& addressWithMask) 
{
    std::regex ipRegex("((([0-9A-Fa-f]{1,4}):){7}([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,7}:|(([0-9A-Fa-f]{1,4}):){1,6}:([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,5}((:[0-9A-Fa-f]{1,4}){1,2})|(([0-9A-Fa-f]{1,4}):){1,4}((:[0-9A-Fa-f]{1,4}){1,3})|(([0-9A-Fa-f]{1,4}):){1,3}((:[0-9A-Fa-f]{1,4}){1,4})|(([0-9A-Fa-f]{1,4}):){1,2}((:[0-9A-Fa-f]{1,4}){1,5})|([0-9A-Fa-f]{1,4}):((:[0-9A-Fa-f]{1,4}){1,6})|:((:[0-9A-Fa-f]{1,4}){1,7}|:)|fe80:(:[0-9A-Fa-f]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9A-Fa-f]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))/(12[0-8]|1[01][0-9]|[1-9]?[0-9])");
    return std::regex_match(addressWithMask, ipRegex);
}

bool Terminal::isMACAddress(const std::string& macAddress) 
{
    std::regex macRegex("^([0-9A-Fa-f]{1,4}[:-]?){6}([0-9A-Fa-f]{1,4})$");
    return std::regex_match(macAddress, macRegex);
}

InterfaceType Terminal::getInterfaceType(std::string& type)
{
    if (type == "Dialer") {return InterfaceType::DIALER;}
    else if (type == "Ethernet") {return InterfaceType::ETHERNET;}
    else if (type == "FastEthernet") {return InterfaceType::FAST_ETHERNET;}
    else if (type == "GigabitEthernet") {return InterfaceType::GIGABIT_ETHERNET;}
    else if (type == "Loopback") {return InterfaceType::LOOPBACK;}
    else if (type == "Portchannel") {return InterfaceType::PORT_CHANNEL;}
    else if (type == "Tunnel") {return InterfaceType::TUNNEL;}
    else if (type == "Virtual-Template") {return InterfaceType::VIRTUAL_TEMPLATE;}
    else if (type == "Vlan") {return InterfaceType::VLAN;}
    Logger::getInstance().warn() << "Undefined Interface type detected: " << type << std::endl;
    return InterfaceType::UNDEFINED;
}

void Terminal::configureInterfaceMode(std::string& type) 
{
    changeMode(mode.interface);
    std::shared_lock<std::shared_mutex> lock(interfaceListMutex);
    activeInterfaces = &interfaceList[getInterfaceType(type)]; 
    currentSubMode = type;
    if (workingDirectory.size() > 0 && workingDirectory[0].contains(type))
    {
        workingDirectory = workingDirectory[0][type];
    }
    if (tempModeSchema->contains(type))
    {
        tempModeSchema = &((*tempModeSchema)[type]);
    }
}

void Terminal::configureRoutingMode(std::string type) {
    changeMode(mode.routing);
    currentSubMode = type;
    if (workingDirectory.size() > 0 && workingDirectory[0].contains(currentSubMode))
    {
        workingDirectory = workingDirectory[0][currentSubMode];
    }
    if (tempModeSchema->contains(currentSubMode))
    {
        tempModeSchema = &((*tempModeSchema)[currentSubMode]);
    }
}

