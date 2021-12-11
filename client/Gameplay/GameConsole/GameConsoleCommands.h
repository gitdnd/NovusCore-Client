#pragma once
#include <NovusTypes.h>

class GameConsole;
class GameConsoleCommands
{
public:
	static bool HandleHelp(GameConsole* gameConsole, std::vector<std::string> subCommands);
	static bool HandlePing(GameConsole* gameConsole, std::vector<std::string> subCommands);
	static bool HandleScriptReload(GameConsole* gameConsole, std::vector<std::string> subCommands);
	static bool HandleTele(GameConsole* gameConsole, std::vector<std::string> subCommands);
	static bool HandleGoto(GameConsole* gameConsole, std::vector<std::string> subCommands);
	static bool HandleStoreLoc(GameConsole* gameConsole, std::vector<std::string> subCommands);
	static bool HandleMorph(GameConsole* gameConsole, std::vector<std::string> subCommands);
};