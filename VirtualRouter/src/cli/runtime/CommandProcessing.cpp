// CommandProcessing.cpp

#include "CliSession.h"
#include "CliEngine.h"

namespace cli
{
std::vector<std::string> CliSession::compileCommandStream(const std::string& command)
{
	std::vector<std::string> rawTokens = splitIntoWords(command);
	std::vector<std::string> tokens;

	textLine = false;

	for (size_t i = 0; i < rawTokens.size(); i++)
	{
		if (execution.getContext().negate && i == 0)
			continue;

		if (!textLine)
			tokens.push_back(rawTokens[i]);
		else
			tokens.back() += " " + rawTokens[i];

		if (i > 0 && commandHistory[i] == "LINE")
			textLine = true;
	}

	return tokens;
}

bool CliSession::executeModeParser(const std::vector<std::string>& tokens)
{
	return execution.execute(tokens);
}

bool CliSession::processConfigPersistence(const std::vector<std::string>& tokens, CliMode preMode)
{
	if (preMode == CliMode::UserExec || preMode == CliMode::PrivilegedExec)
		return false; // No configs modified in exec modes

	if (tokens.empty() || tokens[0] == "error")
		return false;

	if (isExitCommand)
		commandHistory = tokens;

	/*bool ok = execution.getContext().negate
		? engine.deleteConfig(commandHistory, tokens, isList)
		: engine.saveCommand(commandHistory, tokens, isModeChanged, isExitCommand, isList);*/

	return true;
}

bool CliSession::executeCommand(std::string &command)
{
	// Reset the command directory
	currentDirectory = workingDirectory;
	isModeChanged = false;
	isExitCommand = false;
	error = false;
	isList = false;
	textLine = false;
	isCommandExecutionSuccessful = false;

	CliMode preMode = execution.getMode();

	command = normalizeCommand(command);
	if (command.empty()) return false;
	if (isGlobalCommandExecution) return true;
	if (isHelpModeActive && isRunning) return true;
	if (!isRunning || isCommandInvalid || !isCommandValid) return false;

	auto tokens = compileCommandStream(command);
	if (tokens.empty()) return false;

	isCommandExecutionSuccessful = executeModeParser(tokens);

	if (tokens.size() >= 2 && tokens[0] == "ip" && tokens[1] == "route")
	{
		isList = true;
		executionHistory.push_back(command);
	}

	bool configSuccess = processConfigPersistence(tokens, preMode);

	return configSuccess || isCommandValid || isCommandExecutionSuccessful;
}
}