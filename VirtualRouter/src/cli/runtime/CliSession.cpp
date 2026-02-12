// CliSession.cpp

#include <regex>
#include <Global.h>

#include "CliSession.h"
#include "CliEngine.h"
#include "cli/modes/Mode.hpp"
#include "cli/modes/contexts/GlobalContext.hpp"

// TODO Add new "subcommand_sequence" property, it should allow a recursive chain of commands
// TODO add new "single_use" property that goes with subcommand_sequence
// TODO add new "repeatable" property that goes with subcommand_sequence

CliSession::CliSession(CliEngine& engine, bool enableDebug) : Console(), execution(*this), engine(engine)
{
    // Set debug mode based on the input parameter
    configNode = &engine.getCommandTree();
    modeHistory.push_back(configNode);
    isDebugModeEnabled = enableDebug;
    changeMode<CliMode::UserExec>();

    // Set initial mode
    initializeProcessingState();

    // Initialize Console
    initConsole();
    iConsole->print("Initializing Terminal...\r\n");
}

CliSession::CliSession(CliEngine& engine, IConsole* term) : Console(term), execution(*this), engine(engine)
{
    // Set debug mode based on the input parameter
    isDebugModeEnabled = false;

    // Set debug mode based on the input parameter
    configNode = &engine.getCommandTree();
    modeHistory.push_back(configNode);
    changeMode<CliMode::UserExec>();

    // Set initial mode
    initializeProcessingState();

    // Initialize Console
    initConsole();
    iConsole->print("Initializing Terminal...\r\n");
}

void CliSession::handlePrompt()
{
    // Retrieve the hostname from the global settings and reset cursor position
    std::string hostname = engine.global.getHostname();
    cursorPos = 0;
    setPrompt(hostname + currentPrompt);

    // Reset insert mode
    insert = false;
    insertString.clear();

    // Save starting cursor position
    cursorPos = 0;

    // Startup Variables
    std::string input;

    // Print the nextLine if something is queued
    if (!nextLine.empty())
    {
        if (nextLine[nextLine.length() - 1] == ' ')
        {
            nextLine.pop_back();
        }
        input = nextLine;
        cursorPos = input.size();
        oldInputLength = input.size();
        iConsole->print(nextLine);
        nextLine.clear();
    }

    inputCacheBuffer = input;
}

bool CliSession::handleInput(std::string test)
{
    // Read the user's input from the terminal
    std::string userCommand = input(test, paginationList.size() > 0);

    // Handle the Ctrl-Z shortcut to switch to privilegedExec mode
    if (userCommand == "CRT-Z" && getMode() != CliMode::UserExec) {
        if (!changeMode<CliMode::PrivilegedExec>())
        {
            iConsole->print("\r\n");
            return false;
        }
    }

    initializeProcessingState();

    if (paginationList.size() > 0)
    {
        handlePagination(userCommand.empty() ? '\x20' : userCommand[0]);
        return true;
    }

    // Execute commands
    if (!executeCommand(userCommand))
    {
        if (paginationList.size() > 0) return false;
        iConsole->print("\r\n");
        handlePrompt();
        return false;
    }

    // Move to the next line after command execution
    if (paginationList.size() > 0)
    {
        handlePagination();
        return false;
    }

    iConsole->print("\r\n");
    handlePrompt();
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
    
    execution.getContext().negate = false;
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
    CliMode currentMode = execution.getMode();
    return currentMode != CliMode::UserExec && currentMode != CliMode::PrivilegedExec;
}

bool CliSession::isDoCommand(const std::vector<std::string>& parsedWords)
{
    if (parsedWords.empty()) return false;
    if (parsedWords[0] != "do" || parsedWords.size() < 2) return false;

    // Avoid "exit and conf"
    if (parsedWords[1] == "exit" || parsedWords[1].rfind("conf", 0) == 0) return false;

    // Must not already be in userExec or privilegedExec
    CliMode currentMode = execution.getMode();
    return currentMode != CliMode::UserExec && currentMode != CliMode::PrivilegedExec;
}

std::string CliSession::executeDoCommand(std::string remainingCommand)
{
    attemptingGlobalCommand = true;
    // Save current state
    std::string previousPrompt = currentPrompt;
    const json* previousCommandTree = &(*workingDirectory);
    const nlohmann::ordered_json* previousConfigNode = &(*configNode);

    // Switch to privileged mode and execute
    changeMode<CliMode::PrivilegedExec>();
    isGlobalCommandExecution = executeCommand(remainingCommand);

    // Restore old mode / working directory
    execution.revert();
    currentPrompt         = previousPrompt;
    configNode            = &(*previousConfigNode);
    workingDirectory      = &(*previousCommandTree);

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
            iConsole->print(std::string("\r\n%") + " Unrecognized command");
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
        iConsole->print(std::string("\r\n%") + " Unrecognized command");
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
    CliMode currentMode = getMode();
    if (error &&
        currentMode != CliMode::GlobalConfiguration &&
        currentMode != CliMode::UserExec &&
        currentMode != CliMode::PrivilegedExec &&
        !isHelpModeActive && 
        Functions::lowerCase(inputCommand) != "exit")
    {
        attemptingGlobalCommand = true;
        // Backup
        std::string       prevPrompt      = currentPrompt;
        auto              prevDirectory   = workingDirectory;
        auto              preConfig      = configNode;

        // Attempt global execution
        changeMode<CliMode::GlobalConfiguration>(engine.global, *engine.global.getRoutingInstance("default"));
        historyToGlobal();
        std::string inputCommandCopy = inputCommand;
        if (executeCommand(inputCommandCopy))
        {
            isGlobalCommandExecution = true;
        }

        if (getMode() == CliMode::GlobalConfiguration)
        {
            if (isCommandExecutionSuccessful)
            {
                return false; // Triggers "error" return
            }
            else
            {
                // Restore
                execution.revert();
                currentPrompt         = prevPrompt;
                configNode = preConfig;
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
    std::string invalidInput = "\r\n";

    std::string hostname = engine.global.getHostname();
    // Print spaces for hostname, mode, old command
    invalidInput += std::string(initialLineLength + formattedOldCommand.size(), ' ') + "^\r\n% Invlid input detected at '^' marker.\r\n";
    iConsole->print(invalidInput);
}

void CliSession::handleAmbiguousInputMarker(const std::string& ambiguousCommand)
{
    isCommandInvalid = true;
    isCommandValid = false;
    isRunning = false;
    std::string invalidInput = R"(% Ambiguous command: ")" + ambiguousCommand + "\"";
    iConsole->print("\r\n" + invalidInput);
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
        execution.getContext().negate = true;
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
        
        if (word == "no" && execution.getContext().negate)
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
            iConsole->print("\r\n% Incomplete Command");
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
    const nlohmann::ordered_json* currentCommandDirectory = currentDirectory;

    // Default response for invalid or unavailable commands
    std::vector<Com> noSubCommands = {engine.errorCommand};

    // Clear and move the tempDir
    tempDir.clear();

    // Helper lamda to travel to the end of the command
    std::function<void(nlohmann::ordered_json*, const nlohmann::ordered_json*)> navigateToLastCommand = [&](nlohmann::ordered_json* command, const nlohmann::ordered_json* nextCommand) {
        if (command->contains(CLI_JSON_SUBCOMMAND_ARRAY) && (*command)[CLI_JSON_SUBCOMMAND_ARRAY].size() > 0)
        {
            command = &(*command)[CLI_JSON_SUBCOMMAND_ARRAY][0];
            navigateToLastCommand(command, nextCommand);
        }
        else if ((*command)[CLI_JSON_COMMAND_NAME] != engine.carriageReturnCommand.name)
        {
            (*command)[CLI_JSON_SUBCOMMAND_ARRAY] = *nextCommand;
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
    const nlohmann::ordered_json* commandNode = nullptr;
    int matchCount = 0;
    bool patternMatched = false;

    bool addCarriage = false;

    // Handle negate property
    if (execution.getContext().negate && previousCommand)
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
        std::string commandName = command[CLI_JSON_COMMAND_NAME];

        if (command.contains(CLI_JSON_COMMAND_PROPERTIES) && command[CLI_JSON_COMMAND_PROPERTIES].is_array())
        {
            // Process properties
            for (auto prop : command[CLI_JSON_COMMAND_PROPERTIES])
            {
                // Handle recursive property
                if (prop == "recursive") {
                    json* recursiveCommand = new json(command);
                    loosePtrs.push_back(recursiveCommand);
                    json tempRecursiveDir = json::array();

                    for (json commmand : *currentCommandDirectory)
                    {
                        if (command[CLI_JSON_COMMAND_NAME] != commandName)
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
            const nlohmann::ordered_json* nextCommand = nullptr;
            if (command.contains(CLI_JSON_SUBCOMMAND_ARRAY))
            {
                nextCommand = &command[CLI_JSON_SUBCOMMAND_ARRAY];
            }
            std::string newName = commandName.substr(1, commandName.size() - 2);
            if (engine.getCommandTree()[VARIABLE_OBJ].contains(newName))
            {
                for (const json& newCommand : engine.getCommandTree()[VARIABLE_OBJ][newName]) 
                {
                    // Craft new command
                    if (nextCommand)
                    {
                        nlohmann::ordered_json* commandPtr = new nlohmann::ordered_json(std::move(newCommand));
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
        if (command->contains(CLI_JSON_COMMAND_NAME) && command->contains(CLI_JSON_DESCRIPTION))
        {
            // Handle Command Support
            Com::Support support = (command->contains(CLI_JSON_SUPPORT_STATUS) && (*command)[CLI_JSON_SUPPORT_STATUS].is_boolean())
                ? ((*command)[CLI_JSON_SUPPORT_STATUS] == true
                    ? Com::Support::SUPPORTED
                    : Com::Support::PARTIAL)
                : Com::Support::NO_SUPPORT;

            // Handle command properties
            if (command->contains(CLI_JSON_COMMAND_PROPERTIES))
            {
                bool hide = false;
                for (const auto& prop : (*command)[CLI_JSON_COMMAND_PROPERTIES])
                {
                    if (execution.getContext().negate && prop.get<std::string>() == "negate_hide")
                        hide = true;
                    else if (!execution.getContext().negate && prop.get<std::string>() == "negate_show")
                        hide = true;
                }
                if (hide) continue;
            }
            Com commandData;
            commandData.name = (*command)[CLI_JSON_COMMAND_NAME];
            commandData.description = (*command)[CLI_JSON_DESCRIPTION];
            commandData.support = support;
            if (command->contains(CLI_JSON_COMMAND_PROPERTIES))
            {
                for (const auto& prop : (*command)[CLI_JSON_COMMAND_PROPERTIES])
                {
                    commandData.properties.push_back(prop.get<std::string>());
                }
            }
            availableCommands.push_back(commandData);

            // Check if the user input matches a pattern or specific command
            std::string commandName = (*command)[CLI_JSON_COMMAND_NAME];
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
                    exactMatchCommand.name = Functions::lowerCase((*command)[CLI_JSON_COMMAND_NAME]);
                    exactMatchCommand.description = (*command)[CLI_JSON_DESCRIPTION];
                    if (command->contains(CLI_JSON_COMMAND_PROPERTIES))
                    {
                        for (const auto& prop : (*command)[CLI_JSON_COMMAND_PROPERTIES])
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
        currentDirectory = &((*commandNode)[CLI_JSON_SUBCOMMAND_ARRAY]);
        
        for (const auto& subCommand : *currentDirectory) 
        {
            if (subCommand[CLI_JSON_COMMAND_NAME] == engine.carriageReturnCommand.name) 
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
    else if (isExactMatch && !isValidCommandDirectory(commandNode) && !userInput.empty() && !(execution.getContext().negate && userInput == "no"))
    {
        endCommandString = Functions::lowerCase((*commandNode)[CLI_JSON_COMMAND_NAME]);
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
    // Print each command with aligned descriptions
    paginationList = commandList;
    maxNameLength = 0;

    // Find the longest command name for formatting
    for (const Com &command : paginationList)
    {
        if (command.name.size() > maxNameLength)
        {
            maxNameLength = command.name.size();
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

bool CliSession::isValidCommandDirectory(const nlohmann::ordered_json *directory)
{
    if (directory && directory->is_object())
    {
        return directory->contains(CLI_JSON_SUBCOMMAND_ARRAY);
    }
    return false;
}

bool CliSession::handlePagination(char nextch)
{
    if (nextch != '\0')
    {
        maxCommandLength = getTerminalWidth() - initialLineLength;

        if (nextch == '\x20')
        {
            iConsole->print("\033[2k\033[1G");
            iConsole->print("\033[1A");
        }
        else if (nextch == 'q')
        {
            iConsole->print("\033[2k\033[1G");
            iConsole->print(std::string(10, ' '));
            iConsole->print("\033[2k\033[1G");
            paginationList.clear();
            handlePrompt();
            return false;
        }
    }

    size_t paginationSize = engine.paginationCount == 0 ? paginationList.size() : engine.paginationCount;

    for (int i = 0; i < paginationList.size() && i < paginationSize; i++)
    {
        const Com &command = paginationList[i];
        if (command.name != engine.errorCommand.name)
        {
            std::string display;
            display += "\r\n  " + command.name;
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
        }
    }

    if (paginationList.size() > paginationSize)
    {
        paginationList.erase(paginationList.begin(), paginationList.begin() + engine.paginationCount);
        paginationList.shrink_to_fit();
        iConsole->print("\r\n  --More--");
        iConsole->flush();
    }
    else
    {
        paginationList.clear();
        iConsole->print("\r\n");
        handlePrompt();
    }
    return true;
}

bool CliSession::setCommandDirectory(std::span<const std::string_view>& dir)
{
    // Check if the mode exists and has valid commands
    auto& base = engine.getCommandTree();
    if (dir.size() == 0 || !base.contains(dir[0]) || !base[dir[0]].is_array())
        return false;

    bool prompt = false;
    for (auto& step : dir)
    {
        if (!prompt)
        {
            workingDirectory = &base[step];
            currentPrompt = step;
            prompt = true;
        }
        else
            workingDirectory = &(*workingDirectory)[0][step];
    }
    currentDirectory = workingDirectory;
    isModeChanged = true;

    if (!modeHistory.empty() && configNode != modeHistory.back())
    {
        modeHistory.push_back(configNode);
    }
    else
    {
        modeHistory.clear();
        modeHistory.push_back(&engine.getCommandTree());
    }
    return true;
}

CliMode CliSession::getMode()
{
    return execution.getMode();
}

void CliSession::historyToGlobal() 
{
    prevConfig = configNode; 
    modeHistory.clear();
    modeHistory.push_back(&engine.getCommandTree()); 
    configNode = &engine.getCommandTree();
}
