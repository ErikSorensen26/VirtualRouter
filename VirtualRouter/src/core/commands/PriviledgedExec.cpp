#include "CommandProcessor.h"
#include <CliEngine.h>

bool CommandProcessor::handlePriviledgedExec(const std::vector<std::string>& commandStream)
{
	if (commandStream[0] == "configure" && commandStream[1] == "terminal")
	{
		terminal.changeMode(Mode::globalConfiguration);
		terminal.iConsole->print("\r\nEnter configuration commands, one per line. End with CNTL/Z.");
	}
	else if (commandStream[0] == "exit")
	{
		terminal.exitMode(Mode::userExec);
	}
	else if (commandStream[0] == "show")
	{
		if (commandStream[1] == "history")
		{
			for (std::string str : terminal.history)
			{
				if (str != "")
				{
					terminal.iConsole->print("\r\n " + str);
				}
			}
		}
		else if (commandStream[2] == "clock")
		{
			terminal.iConsole->print("\r\n" + terminal.engine.timeManager.getTime());
		}
	}
	else if (commandStream[0] == "write" && commandStream[1] == "memory")
	{
		terminal.engine.saveConfig();
	}
	else return false;
	return true;
}

