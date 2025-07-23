#include <CliEngine.h>
#include <CommandProcessor.h>
#include "Mode.hpp"

bool CliSession::executeCommand(std::string &command)
{
	// Reset the command directory
	currentDirectory = workingDirectory;

	isModeChanged = false;
	isExitCommand = false;
	error = false;
	std::string preProcessMode = modeConfig.currentMode;
	
	command = normalizeCommand(command);

	isCommandExecutionSuccessful = false;
	
	// Check if it's is a "do" command
	if (command.empty()) return false;
	if (isGlobalCommandExecution || (isHelpModeActive && isRunning)) return true;
	if (!isRunning || isCommandInvalid || !isCommandValid) return false;

	std::vector<std::string> TEMPcommandStream = splitIntoWords(command);
	std::vector<std::string> commandStream;

	isList = false;
	textLine = false;

	// Compile LINE command
	for (size_t index = 0; index < TEMPcommandStream.size(); index++)
	{
		if (commandProcessor->negate && index == 0) continue;
		if (!textLine)
		{
			commandStream.push_back(TEMPcommandStream[index]);
		}
		else
		{
			commandStream[commandStream.size() - 1] += " " + TEMPcommandStream[index];
		}
		if (index > 0)
		{
			if (commandHistory[index] == "LINE")
			{
				textLine = true;
			}
		}
	}
	if (commandStream.empty())
	{
		return false;
	}

	isCommandExecutionSuccessful = true;

	{
		if (modeConfig.currentMode == Mode::userExec)
			commandProcessor->handleUserExec(commandStream);
		else if (modeConfig.currentMode == Mode::privilegedExec)
			commandProcessor->handlePriviledgedExec(commandStream);
		else if (modeConfig.currentMode == Mode::globalConfiguration)
			commandProcessor->handleGlobalConfiguration(commandStream);
		else if (modeConfig.currentMode == Mode::interface)
			commandProcessor->handleInterfaceConfiguration(commandStream);
		else if (modeConfig.currentMode == Mode::routing || modeConfig.currentMode == Mode::routerAddressFamily || modeConfig.currentMode == Mode::routerAddressFamilyTopology || modeConfig.currentMode == Mode::routingV6)
			commandProcessor->handleRoutingConfiguration(commandStream);
		else if (modeConfig.currentMode == Mode::routerAddressFamilyInterface)
			commandProcessor->handleAddressFamilyInterface(commandStream);
		else if (modeConfig.currentMode == Mode::dhcpConfig)
			commandProcessor->handleDhcpConfiguration(commandStream);
	}

	if (commandStream[0] == "ip" && (commandStream[1] == "route"))
	{
		isList = true;
	}
	if (isList)
	{
		executionHistory.push_back(command);
	}

	bool executeSuccess = false;

	if (preProcessMode != Mode::userExec && preProcessMode != Mode::privilegedExec && command != "error")
	{
		Functions::printVector(commandHistory);
		// cout << "\n" << endl;
		Functions::printVector(commandStream);

		// TODO add "exit" to end of the config
		// Need to put it in command json
		if (isExitCommand)
		{
			commandHistory = commandStream;
		}
		
		executeSuccess = commandProcessor->negate 
		  ? engine.deleteConfig(modeConfig, commandHistory, commandStream, isList)
		  : engine.saveCommand(commandHistory, commandStream, modeConfig, isModeChanged, isExitCommand, isList);
		
		// Check if mode changed
		if (isModeChanged)
		{
			modeConfig.modeSchema = modeConfig.tempModeSchema;
		}
	}
	if (executeSuccess || isCommandValid)
	{
		return true;
	}
	return false;
}
