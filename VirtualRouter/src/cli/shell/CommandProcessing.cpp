#include <CliEngine.h>
#include <CommandProcessor.h>

#include <UserExecCommands.h>
#include <PrivilegedExecCommands.h>
#include <GlobalCommands.h>

std::vector<std::string> CliSession::compileCommandStream(const std::string& command)
{
	std::vector<std::string> rawTokens = splitIntoWords(command);
	std::vector<std::string> tokens;

	textLine = false;

	for (size_t i = 0; i < rawTokens.size(); i++)
	{
		if (modeConfig.modeConfig && modeConfig.modeConfig->negate && i == 0)
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
	switch (modeConfig.currentMode)
	{
		case CliMode::UserExec:
		{
			if (auto* ctx = dynamic_cast<Cli::UserExecContext*>(modeConfig.modeConfig))
				return Cli::UserExecCommands::execute(*ctx, tokens);
			break;
		}
		case CliMode::PrivilegedExec:
		{
			if (auto* ctx = dynamic_cast<Cli::PrivilegedExecContext*>(modeConfig.modeConfig))
				return Cli::PrivilegedExecCommands::execute(*ctx, tokens);
			break;
		}
		case CliMode::GlobalConfiguration:
		{
			if (auto* ctx = dynamic_cast<Cli::GlobalContext*>(modeConfig.modeConfig))
				return Cli::GlobalCommands::execute(*ctx, tokens);
			break;
		}
		case CliMode::Interface:
		case CliMode::Routing:
		case CliMode::RoutingV6:
		case CliMode::RouterAddressFamily:
		case CliMode::RouterAddressFamilyTopology:
		case CliMode::RouterAddressFamilyInterface:
		case CliMode::DhcpConfig:
		default:
			return false;
	}
		/*if (modeConfig.currentMode == Mode::userExec)
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
			commandProcessor->handleDhcpConfiguration(commandStream);*/
}

bool CliSession::processConfigPersistence(const std::vector<std::string>& tokens, CliMode preMode)
{
	if (preMode == CliMode::UserExec || preMode == CliMode::PrivilegedExec)
		return false; // No configs modified in exec modes

	if (tokens.empty() || tokens[0] == "error")
		return false;

	if (isExitCommand)
		commandHistory = tokens;

	bool ok = modeConfig.modeConfig->negate
		? engine.deleteConfig(modeConfig, commandHistory, tokens, isList)
		: engine.saveCommand(commandHistory, tokens, modeConfig, isModeChanged, isExitCommand, isList);

	if (isModeChanged)
		modeConfig.modeSchema = modeConfig.tempModeSchema;

	return ok;
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

	CliMode preMode = modeConfig.currentMode;

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
