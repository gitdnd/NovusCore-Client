#pragma once
#include <NovusTypes.h>

class GameConsole;
class GameConsoleCommands
{
public:
	static bool HandleHelp(GameConsole* gameConsole, std::vector<std::string> subCommands);
	static bool HandlePing(GameConsole* gameConsole, std::vector<std::string> subCommands);
	static bool HandleScriptReload(GameConsole* gameConsole, std::vector<std::string> subCommands);
};