#include <CliSession.h>
#include <CommandProcessor.h>
#include <CliEngine.h>
#include <regex>
#include <Global.h>
#include "Mode.hpp"

// TODO Add new "subcommand_sequence" property, it should allow a recursive chain of commands
// TODO add new "single_use" property that goes with subcommand_sequence
// TODO add new "repeatable" property that goes with subcommand_sequence

CliSession::CliSession(CliEngine& engine, bool enableDebug) : Console(), engine(engine)
{
    // Set debug mode based on the input parameter
    modeConfig.configNode = &engine.root;
    modeConfig.modeHistory.push_back(modeConfig.configNode);
    isDebugModeEnabled = enableDebug;

    // Set initial mode
    changeMode(engine.defaultMode, true);
    initializeProcessingState();

    // Initialize Console
    initConsole();
    commandProcessor = new CommandProcessor(*this);
    commandProcessor->currentVrf = engine.global.getRoutingInstance("default");
    iConsole->print("Initializing Terminal...\n");
}

CliSession::CliSession(CliEngine& engine, std::shared_ptr<IConsole> term) : Console(std::move(term)), engine(engine)
{
    // Set debug mode based on the input parameter
    isDebugModeEnabled = false;

    // Set debug mode based on the input parameter
    modeConfig.configNode = &engine.root;
    modeConfig.modeHistory.push_back(modeConfig.configNode);

    // Set initial mode
    changeMode(engine.defaultMode, true);
    initializeProcessingState();

    // Initialize Console
    initConsole();
    commandProcessor = new CommandProcessor(*this);
    commandProcessor->currentVrf = engine.global.getRoutingInstance("default");
    iConsole->print("Initializing Terminal...\n");
}

CliSession::~CliSession()
{
    if (commandProcessor)
    {
        delete commandProcessor;
    }
}

bool CliSession::handleInput(std::string test)
{
    // Retrieve the hostname from the global settings and reset cursor position
    std::string hostname = engine.global.getHostname();
    cursorPos = 0;
    setPrompt(hostname + currentPrompt);

    // Read the user's input from the terminal
    std::string userCommand = input(test);

    // Handle the Ctrl-Z shortcut to switch to privilegedExec mode
    if (userCommand == "CRT-Z" && modeConfig.currentMode != Mode::userExec) {
        if (!changeMode(Mode::privilegedExec, true))
        {
            iConsole->print("\n");
            return false;
        }
    }

    // Execute commands
    initializeProcessingState();
    if (!executeCommand(userCommand))
    {

        iConsole->print("\n");
        return false;
    }

    // Move to the next line after command execution
    iConsole->print("\n");
    return true;
}

void CliSession::initializeProcessingState()
{
    previousMatch.clear();
    currentPattern.clear();
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
    isCommandInvalid         = false;
    
    if (commandProcessor)
        commandProcessor->negate = false;
}

bool CliSession::detectHelpTriggers(const std::vector<std::string>& parsedWords)
{
    return std::any_of(parsedWords.begin(), parsedWords.end(),
        [](const std::string& word) 
        { 
            return word == "?" || word == "vk_tab"; 
        }
    );
}

bool CliSession::isNoCommand(const std::vector<std::string>& parsedWords)
{
    if (parsedWords.empty()) return false;
    if (parsedWords[0] != "no" || parsedWords.size() < 2) return false;

    // Avoid "exit and conf"
    if (parsedWords[1] == "exit" || parsedWords[1].rfind("conf", 0) == 0) return false;

    // Must not be in userExec or privilegedExec
    return modeConfig.currentMode != Mode::userExec && modeConfig.currentMode != Mode::privilegedExec;
}

bool CliSession::isDoCommand(const std::vector<std::string>& parsedWords)
{
    if (parsedWords.empty()) return false;
    if (parsedWords[0] != "do" || parsedWords.size() < 2) return false;

    // Avoid "exit and conf"
    if (parsedWords[1] == "exit" || parsedWords[1].rfind("conf", 0) == 0) return false;

    // Must not already be in userExec or privilegedExec
    return modeConfig.currentMode != Mode::userExec && modeConfig.currentMode != Mode::privilegedExec;
}

std::string CliSession::executeDoCommand(std::string remainingCommand)
{
    attemptingGlobalCommand = true;
    // Save current state
    std::string previousPrompt = currentPrompt;
    std::string previousMode = modeConfig.currentMode;
    const json* previousCommandTree = &(*workingDirectory);
    nlohmann::ordered_json* prevModeSchema = &(*modeConfig.modeSchema);
    nlohmann::ordered_json* previousConfigNode = &(*modeConfig.configNode);

    // Switch to privileged mode and execute
    changeMode(Mode::privilegedExec, true);
    isGlobalCommandExecution = executeCommand(remainingCommand);

    // Restore old mode / working directory
    changeMode(previousMode, true);
    currentPrompt         = previousPrompt;
    modeConfig.configNode = &(*previousConfigNode);
    workingDirectory      = &(*previousCommandTree);
    modeConfig.modeSchema = &(*prevModeSchema);

    // Return "error" to signify no further processing
    return "error";
}

void CliSession::appendLineBasedCommand(const std::vector<std::string>& parsedWords, size_t currentIndex, std::string& fullyFormattedCommand, std::string& volatileCommand)
{
    fullyFormattedCommand += " " + parsedWords[currentIndex];
    volatileCommand       += " " + parsedWords[currentIndex];

    // Handle help question "?"
    //if (handleHelpQuestion(parsedWords[currentIndex], previousCommandList, inputCommand, formattedOldCommand, fullyFormattedCommand, volatileCommand)) //TODO
    {
        return;
    }

    // Handle "vk_tab" for tab completion
    //if (handleTabCompletion(word, previousCommandList, inputCommand, formattedOldCommand, fullyFormattedCommand, volatileCommand)) //TODO
    {
        return;
    }
}

void CliSession::processNonLineBasedWord(std::string& word, std::vector<Com>& previousCommandList, std::string& formattedOldCommand, std::string& fullyFormattedCommand, std::string& volatileCommand, const std::string& inputCommand, bool& isFirstIteration)
{
    // Reset matching states
    isPatternMatching = false;
    isPatternMatchEnd = false;

    // if processing has already failed, baile out
    if (!isRunning) return;

    // Grab the current list of available commands
    std::vector<Com> availableCommands = getAvailableCommands(word, isFirstIteration, previousCommandList);
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
    if (error && !isGlobalCommand(word) && !isHelpModeActive && isRunning)
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

bool CliSession::handleHelpQuestion(const std::string& word, std::vector<Com>& previousCommandList,
               const std::string& inputCommand, std::string& formattedOldCommand,
               std::string& fullyFormattedCommand, std::string& volatileCommand)
{
    // Return false if "?" is not actually truggered or doesn't apply
    if (word != "?" || isMatchSuccessful || previousCommandList.empty() || isNextWordHelpRequested || endOfCommand)
    {
        if ((word == "?") && isMatchSuccessful && !previousCommandList.empty() && !isNextWordHelpRequested)
        {
            nextLine = inputCommand.substr(0, inputCommand.size());
        }
        else if (!isMatchSuccessful && (word == "?") && ((previousCommandList.size() == 1 && previousCommandList[0].name == "<error>")))
        {
            nextLine = inputCommand.substr(0, inputCommand.size() - 1) + " ";
            std::cout << "\n%" << " Unrecognized command";
            return word == "?";
        }

        return false;
    }

    fullyFormattedCommand += word;
    volatileCommand       += word;

    nextLine = " " + inputCommand.substr(0, inputCommand.size() - 1);
    if (previousCommandList[0].name == "<error>")
    {
        nextLine = inputCommand.substr(0, inputCommand.size() - 1);
        std::cout << "\n%" << " Unrecognized command";
    }
    else if (previousCommandList[0].name != "<cr>")
    {
        displayAvailableCommands(previousCommandList);
    }
    else
    {
        nextLine = inputCommand.substr(0, inputCommand.size());
    }
    return true;
}

bool CliSession::handleTabCompletion(const std::string& word, std::vector<Com>& previousCommandList,
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
            nextLine += " " + engine.maskInput(inputCommand.substr(0, inputCommand.size() - 1), getLastWord(fullyFormattedCommand)) + "  ";
        }
        else
        {
            // Insert after last space
            nextLine = engine.maskInput(" " + inputCommand.substr(0, inputCommand.size() - 1), nextLine.substr(0, static_cast<size_t>(lastSpacePosition)) + " " + getLastWord(fullyFormattedCommand)) + "  ";
        }
    }

    return true;
}

bool CliSession::attemptGlobalCommand(const std::string& inputCommand)
{
    if (error &&
        modeConfig.currentMode != Mode::globalConfiguration &&
        modeConfig.currentMode != Mode::userExec &&
        modeConfig.currentMode != Mode::privilegedExec &&
        !isHelpModeActive && 
        Functions::lowerCase(inputCommand) != "exit")
    {
        attemptingGlobalCommand = true;
        // Backup
        std::string prevPrompt      = currentPrompt;
        std::string prevMode        = modeConfig.currentMode;
        auto        prevDirectory   = workingDirectory;
        auto        prevModeSchema  = modeConfig.modeSchema;
        auto        prevConfig      = modeConfig.configNode;

        // Attempt global execution
        changeMode(Mode::globalConfiguration, true);
        historyToGlobal();
        std::string inputCommandCopy = inputCommand;
        if (executeCommand(inputCommandCopy))
        {
            isGlobalCommandExecution = true;
        }

        if (modeConfig.currentMode == Mode::globalConfiguration)
        {
            if (isCommandExecutionSuccessful)
            {
                return false; // Triggers "error" return
            }
            else
            {
                // Restore
                changeMode(prevMode, true);
                currentPrompt         = prevPrompt;
                modeConfig.configNode = prevConfig;
                modeConfig.modeSchema = prevModeSchema;
                workingDirectory      = prevDirectory;
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

void CliSession::handleInvalidInputMarker(const std::string& formattedOldCommand)
{
    isCommandInvalid = true;
    isRunning = false;
    std::string invalidInput = "\n";

    std::string hostname = engine.global.getHostname();
    // Print spaces for hostname, mode, old command
    invalidInput += std::string(initialLineLength + formattedOldCommand.size(), ' ') + "^\n% Invlid input detected at '^' marker.\n";
    iConsole->print(invalidInput);
}

void CliSession::handleAmbiguousInputMarker(const std::string& ambiguousCommand)
{
    isCommandInvalid = true;
    isCommandValid = false;
    isRunning = false;
    std::string invalidInput = R"(% Ambiguous command: ")" + ambiguousCommand + "\"";
    iConsole->print("\n" + invalidInput);
}

void CliSession::matchCommand(const std::string& inputCommand, const std::string& uWord, 
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
            nextLine = inputCommand.substr(0, inputCommand.size() - 1);
            handleAmbiguousInputMarker(nextLine);
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

std::string CliSession::normalizeCommand(const std::string& inputCommand) 
{
    // Return an empty string if the input command is empty
    if (inputCommand.empty()) return "";

    // Parse the command
    std::vector<std::string> parsedWords = splitIntoWords(inputCommand);
    if (parsedWords.empty()) return "";

    std::vector<Com> previousCommandList;
    std::string formattedOldCommand, lastProcessedWord, fullyFormattedCommand, volatileCommand;
    size_t currentIndex = 0;
    bool isFirstIteration = true;

    // Check for help triggers ("?" or "vk_tab")
    isHelpModeActive = detectHelpTriggers(parsedWords);

    // Handle negateCommand
    if (isNoCommand(parsedWords))
    {
        commandProcessor->negate = true;
    }

    // Handle "do" command
    if (!isHelpModeActive && isDoCommand(parsedWords))
    {
        return executeDoCommand(inputCommand.substr(2));
    } 
    
    // Mark as valid if the first word is "?" or "vk_tab"
    isMatchSuccessful = (!parsedWords.empty() && (parsedWords[0] == "?" || parsedWords[0] == "vk_tab"));

    if (!isHelpModeActive && isGlobalCommand(parsedWords[0]) && !isMatchSuccessful)
    {
        isCommandValid = true;
        return parsedWords[0];
    }

    // Process each word in the parsed command
    for (std::string& word : parsedWords) {
        if (isGlobalCommandExecution)
        {
            // If a global command was succcessfull after falure, return
            return "";
        }
        else if (isLineBasedInput) 
        {
            appendLineBasedCommand(parsedWords, currentIndex, fullyFormattedCommand, volatileCommand);
        }
        else
        {
            processNonLineBasedWord(word, previousCommandList, formattedOldCommand, fullyFormattedCommand, volatileCommand, inputCommand, isFirstIteration);
        }
        
        if (word == "no" && commandProcessor->negate)
        {
            currentDirectory = workingDirectory;
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
    if (!isCommandValid && !isHelpModeActive && !isLineBasedInput && !isCommandInvalid && !isGlobalCommandExecution && isRunning)
    {
        if (previousCommandList.size() > 1)
        {
            handleAmbiguousInputMarker(getLastWord(inputCommand));
            return "";
        }
        else
        {
            isRunning = false;
            iConsole->print("\n% Incomplete Command");
            return "";
        }
    }

    isCommandInvalid = false;

    // Remove loose pointers of any were made
    for (json* ptr : loosePtrs)
    {
        delete ptr;
    }
    loosePtrs.clear();

    // Return the final processed command
    return fullyFormattedCommand;
}

std::vector<Com> CliSession::getAvailableCommands(const std::string& userInput, bool inPrivilegedMode, std::vector<Com>& previousCommands)
{
    Com* previousCommand = previousCommands.empty() ? nullptr : &previousCommands.front();

    // Get lowercase input
    std::string lowerUserInput = Functions::lowerCase(userInput);

    // Container for storing available commands
    std::vector<Com> availableCommands;

    // Clone the current command directory
    const nlohmann::json* currentCommandDirectory = currentDirectory;

    // Default response for invalid or unavailable commands
    std::vector<Com> noSubCommands = {engine.errorCommand};

    // Clear and move the tempDir
    tempDir.clear();

    // Helper lamda to travel to the end of the command
    std::function<void(nlohmann::json*, const nlohmann::json*)> navigateToLastCommand = [&](nlohmann::json* command, const nlohmann::json* nextCommand) {
        if (command->contains("subcommands") && (*command)["subcommands"].size() > 0)
        {
            command = &(*command)["subcommands"][0];
            navigateToLastCommand(command, nextCommand);
        }
        else if ((*command)["name"] != engine.carriageReturnCommand.name)
        {
            (*command)["subcommands"] = *nextCommand;
        }
    };

    // Variables for handling exact matches
    Com exactMatchCommand;
    bool isExactMatch = false;
    bool isValidCommand = false;

    // If the current directory is invalid, return the default error response
    if (error) 
    {
        return noSubCommands;
    }

    // Iterate over all commands in the current directory
    const nlohmann::json* commandNode = nullptr;
    int matchCount = 0;
    bool patternMatched = false;

    bool addCarriage = false;

    // Handle negate property
    if (commandProcessor->negate && previousCommand)
    {
        for (const auto& prop : previousCommand->properties)
        {
            if (prop == "negate")
            {
                addCarriage = true;
            }
            else if (prop == "negate_all")
            {
                return {engine.carriageReturnCommand};
            }
        }
    }

    // Creates next commands directory
    for (const json& command : *currentCommandDirectory)
    {
        std::string commandName = command["name"];

        if (command.contains("properties") && command["properties"].is_array())
        {
            // Process properties
            for (auto prop : command["properties"])
            {
                // Handle recursive property
                if (prop == "recursive") {
                    json* recursiveCommand = new json(command);
                    loosePtrs.push_back(recursiveCommand);
                    json tempRecursiveDir = json::array();

                    for (json commmand : *currentCommandDirectory)
                    {
                        if (command["name"] != commandName)
                        {
                            tempRecursiveDir.push_back(command);
                        }
                    }
                    navigateToLastCommand(recursiveCommand, &tempRecursiveDir);
                    tempDir.push_back(recursiveCommand);
                    continue;
                }
            }
        }

        // Handles move operator ("<>") in command structure
        if (commandName != engine.carriageReturnCommand.name && !engine.isVolatile(commandName) && commandName[0] == '<' && commandName.back() == '>')
        {
            // Next command
            const nlohmann::json* nextCommand = nullptr;
            if (command.contains("subcommands"))
            {
                nextCommand = &command["subcommands"];
            }
            std::string newName = commandName.substr(1, commandName.size() - 2);
            if (engine.getCommandTree().contains(newName))
            {
                for (const json& newCommand : engine.getCommandTree()[newName]) 
                {
                    // Craft new command
                    if (nextCommand)
                    {
                        nlohmann::json* commandPtr = new nlohmann::json(std::move(newCommand));
                        loosePtrs.push_back(commandPtr);
                        navigateToLastCommand(commandPtr, nextCommand);
                        tempDir.push_back(commandPtr);
                    }
                }
            }
        }
        else
        {
            tempDir.push_back(&command);
        }
    }

    for (const json* command : tempDir)
    {
        if (command->contains("name") && command->contains("description"))
        {
            // Handle Command Support
            Com::Support support = (command->contains("support") && (*command)["support"].is_boolean())
                ? ((*command)["support"] == true
                    ? Com::Support::SUPPORTED
                    : Com::Support::PARTIAL)
                : Com::Support::NO_SUPPORT;

            // Handle command properties
            if (command->contains("properties"))
            {
                bool hide = false;
                for (const auto& prop : (*command)["properties"])
                {
                    if (commandProcessor->negate && prop.get<std::string>() == "negate_hide")
                        hide = true;
                    else if (!commandProcessor->negate && prop.get<std::string>() == "negate_show")
                        hide = true;
                }
                if (hide) continue;
            }
            Com commandData;
            commandData.name = (*command)["name"];
            commandData.description = (*command)["description"];
            commandData.support = support;
            if (command->contains("properties"))
            {
                for (const auto& prop : (*command)["properties"])
                {
                    commandData.properties.push_back(prop.get<std::string>());
                }
            }
            availableCommands.push_back(commandData);

            // Check if the user input matches a pattern or specific command
            std::string commandName = (*command)["name"];
            if (!patternMatched && matchInputPattern(lowerUserInput, commandName) && !endOfCommand)
            {
                commandNode = command;
                matchCount++;
                patternMatched = true;
                if (!isValidCommandDirectory(commandNode)) 
                {
                    endOfCommand = true;
                }
            } 
            else if (!engine.isVolatile(commandName) && commandName.size() >= userInput.size())
            {
                if (std::equal(lowerUserInput.begin(), lowerUserInput.end(), Functions::lowerCase(commandName).begin()) && !isExactMatch) 
                {
                    commandNode = command;
                    matchCount++;
                }
                if (commandName == lowerUserInput) 
                {
                    isExactMatch = true;
                    exactMatchCommand.name = Functions::lowerCase((*command)["name"]);
                    exactMatchCommand.description = (*command)["description"];
                    if (command->contains("properties"))
                    {
                        for (const auto& prop : (*command)["properties"])
                        {
                            exactMatchCommand.properties.push_back(prop.get<std::string>());
                        }
                    }
                    commandNode = command;
                }
            }
        }
    }

    // Handle negate property
    if (addCarriage)
    {
        availableCommands.push_back(engine.carriageReturnCommand);
    }

    // Handle exact matches and valid commands
    if (isExactMatch) 
    {
        matchCount = 1;
    }
    if (matchCount == 1 && isValidCommandDirectory(commandNode)) 
    {
        currentDirectory = &((*commandNode)["subcommands"]);
        
        for (const auto& subCommand : *currentDirectory) 
        {
            if (subCommand["name"] == engine.carriageReturnCommand.name) 
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
        error = true;
    } 
    else if (isExactMatch && !isValidCommandDirectory(commandNode) && !userInput.empty() && !(commandProcessor->negate && userInput == "no"))
    {
        endCommandString = Functions::lowerCase((*commandNode)["name"]);
        endOfCommand = true;
        return noSubCommands;
    }
    else
    {
        endOfCommand = false;
    }

    // Handle unmatched or invalid commands
    if (matchCount == 0 && !userInput.empty() && error &&
        lowerUserInput != "?" && lowerUserInput != "vk_tab") 
    {
        return noSubCommands;
    }
    if (matchCount == 0 && !isValidCommandDirectory(commandNode) && lowerUserInput != "?" &&
        lowerUserInput != "vk_tab" && !isPatternMatching) 
    {
        error = true;
        return noSubCommands;
    }

    return availableCommands;
}

bool CliSession::isGlobalCommand(std::string &commandName)
{
    // Iterate through the list of global commands
    for (const std::string &globalCommand : engine.globalCommandList)
    {
        if (commandName == globalCommand)
        {
            return true;
        }
    }
    return false;
}

void CliSession::displayAvailableCommands(std::vector<Com> commandList)
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
        if (command.name != engine.errorCommand.name)
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
                Color color;
                switch (command.support)
                {
                    case Com::Support::SUPPORTED:
                        color = Color::WHITE;
                        break;
                    case Com::Support::PARTIAL:
                        color = Color::YELLOW;
                        break;
                    case Com::Support::NO_SUPPORT:
                        color = Color::RED;
                }
                iConsole->print(display, color);
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

std::string CliSession::getLastWord(const std::string &input)
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

std::vector<std::string> CliSession::splitIntoWords(const std::string &str)
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

std::string CliSession::trimString(std::string str)
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

bool CliSession::matchInputPattern(const std::string &userInput, const std::string &expectedPattern)
{
    if (previousMatch.empty())
    {
        return false;
    }

    if (expectedPattern == "WORD" && userInput != "?" && userInput != "vk_tab")
    {
        // Specific validations based on previousMatch
        if (previousMatch == "hostname") {
            if (!std::regex_match(userInput, std::regex("^[A-Za-z0-9]([A-Za-z0-9\\-\\.]*[A-Za-z0-9])?$"))) {
                return false; // Invalid hostname
            }
        } 
        else if (previousMatch == "name" || previousMatch == "vrf" || previousMatch == "route-map" || previousMatch == "policy-map" || 
                 previousMatch == "group" || previousMatch == "class" || previousMatch == "pool" || previousMatch == "context" || 
                 previousMatch == "vdpn-group") {
            if (!std::regex_match(userInput, std::regex("^[A-Za-z0-9\\-]+$"))) {
                return false; // Invalid name-like userInputs
            }
        } 
        else if (previousMatch == "password" || previousMatch == "secret" || previousMatch == "key-string" || 
                 previousMatch == "encryption type") {
            if (!std::regex_match(userInput, std::regex("^[ -~]+$"))) {
                return false; // Invalid password/secret
            }
        } 
        else if (previousMatch == "input" || previousMatch == "output") {
            if (!std::regex_match(userInput, std::regex("^[A-Za-z]+[0-9\\/\\:]+$"))) {
                return false; // Invalid interface
            }
        } 
        else if (previousMatch == "7" || previousMatch == "5") {
            if (!std::regex_match(userInput, std::regex("^[0-9]+$"))) {
                return false; // Invalid numeric value
            }
        } 
        else if (previousMatch == "filename" || previousMatch == "flash" || previousMatch == "tftp" || previousMatch == "dir" || previousMatch == "view") {
            if (!std::regex_match(userInput, std::regex("^[A-Za-z0-9_\\-\\.\\/]+$"))) {
                return false; // Invalid filename
            }
        } 
        else if (previousMatch == "community" || previousMatch == "as number") {
            if (!std::regex_match(userInput, std::regex("^[0-9]+:[0-9]+$"))) {
                return false; // Invalid community or AS number
            }
        }
        else if (!std::regex_match(userInput, std::regex("^[A-Za-z0-9_\\-\\.\\/]+$"))) {
            return false; // Invalid general WORD
        }

        // ==========================================

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
        if (Functions::isIPv6Address(userInput))
        {
            currentPattern = expectedPattern;
            isPatternMatching = true;
            return true;
        }
    }

    if (expectedPattern == "X:X:X:X::X/<0-128>")
    {
        if (Functions::isIPv6AddressWithMask(userInput))
        {
            currentPattern = expectedPattern;
            isPatternMatching = true;
            return true;
        }
    }

    if (expectedPattern == "H.H.H")
    {
        if (Functions::isMACAddress(userInput))
        {
            currentPattern = expectedPattern;
            isPatternMatching = true;
            return true;
        }
    }

    if (expectedPattern[0] == '<' && expectedPattern != engine.carriageReturnCommand.name)
    {
        uint32_t min, max;
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

bool CliSession::isNumeric(const std::string &input)
{
    if (input.empty() || (!std::isdigit(input[0]) && input[0] != '-' && input[0] != '+'))
    {
        return false;
    }

    char *endPtr;
    std::strtol(input.c_str(), &endPtr, 10);

    return (*endPtr == '\0');
}

bool CliSession::isValidCommandDirectory(const nlohmann::json *directory)
{
    if (directory && directory->is_object())
    {
        return directory->contains("subcommands");
    }
    return false;
}

bool CliSession::handlePagination(size_t &lineNum)
{
    if (engine.paginationCount > 0 && lineNum % engine.paginationCount == 0 && lineNum != 0)
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

bool CliSession::changeMode(std::string &newMode, bool processing)
{
    // Check if the mode exists and has valid commands
    if (!engine.getCommandTree().contains(newMode) || !engine.getCommandTree()[newMode].is_array())
    {
        return false;
    }

    prevMode = modeConfig.currentMode;
    modeConfig.currentMode = newMode;
    currentPrompt = newMode;
    workingDirectory = &engine.getCommandTree()[modeConfig.currentMode];
    currentDirectory = workingDirectory;
    isModeChanged = true;

    if (engine.configSchema.contains(modeConfig.currentMode))
    {
        if (processing)
        {
            modeConfig.modeSchema = &(engine.configSchema[modeConfig.currentMode]);
        }
        else
        {
            modeConfig.tempModeSchema = &(engine.configSchema[modeConfig.currentMode]);
        }
    }
    else
    {
        //std::cout << "\nMode: \"" << newMode << "\" not found in schema";
    }


    if (!modeConfig.modeHistory.empty() && modeConfig.configNode != modeConfig.modeHistory.back())
    {
        modeConfig.modeHistory.push_back(modeConfig.configNode);
    }
    else
    {
        modeConfig.modeHistory.clear();
        modeConfig.modeHistory.push_back(&engine.root);
    }
    return true;
}

void CliSession::configureInterfaceMode(std::string& type) 
{
    currentSubMode = type;
    if (type == "Ethernet" || type == "FastEthernet" || type == "GigabitEthernet" || type == "Loopback")
    {
        type = "Ethernet";
    }
    changeMode(Mode::interface);
    if (workingDirectory->size() > 0 && (*workingDirectory)[0].contains(type))
    {
        workingDirectory = &(*workingDirectory)[0][type];
    }
    if (modeConfig.tempModeSchema->contains(type))
    {
        modeConfig.tempModeSchema = &((*modeConfig.tempModeSchema)[type]);
    }
}

void CliSession::configureRoutingMode(std::string type, bool classicV6) 
{
    if (classicV6)
    {
        changeMode(Mode::routingV6);
    }
    else
    {
        changeMode(Mode::routing);
    }
    currentSubMode = type;
    if (workingDirectory->size() > 0 && (*workingDirectory)[0].contains(currentSubMode))
    {
        workingDirectory = &(*workingDirectory)[0][currentSubMode];
    }
    if (modeConfig.tempModeSchema->contains(currentSubMode))
    {
        modeConfig.tempModeSchema = &((*modeConfig.tempModeSchema)[currentSubMode]);
    }
}

void CliSession::configureAddressFamily(AddressFamily af)
{
    std::string addressFamily;

    switch (af)
    {
        case AddressFamily::IPv4:
            addressFamily = "ipv4";
            break;
        case AddressFamily::IPv6:
            addressFamily = "ipv6";
            break;
    }

    if (workingDirectory->size() > 0 && (*workingDirectory)[0].contains(addressFamily))
    {
        workingDirectory = &(*workingDirectory)[0][addressFamily];
    }
}

void CliSession::historyToGlobal() 
{
    prevConfig = modeConfig.configNode; 
    modeConfig.modeHistory.clear();
    modeConfig.modeHistory.push_back(&engine.root); 
    modeConfig.configNode = &engine.root;
}
