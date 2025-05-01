#include "CommandProcessor.h"

bool CommandProcessor::handleUserExec(const std::vector<std::string>& commandStream)
{
	if (commandStream[0] == "enable")
	{
		terminal.changeMode(Mode::privilegedExec);
	}
	else if (commandStream[0] == "exit")
	{
		exit(1);
	}
	else return false;
	return true;
}
