#pragma once
#include "GameConsoleCommandHandler.h"
#include "GameConsoleCommands.h"
#include "GameConsole.h"

GameConsoleCommandHandler::GameConsoleCommandHandler()
{
    RegisterCommand("help"_h, GameConsoleCommands::HandleHelp);
    RegisterCommand("ping"_h, GameConsoleCommands::HandlePing);
    RegisterCommand("reload"_h, GameConsoleCommands::HandleScriptReload);
}

bool GameConsoleCommandHandler::HandleCommand(GameConsole* gameConsole, std::string& command)
{
    if (command.size() == 0)
        return true;

    std::vector<std::string> splitCommand = StringUtils::SplitString(command);
    u32 hashedCommand = StringUtils::fnv1a_32(splitCommand[0].c_str(), splitCommand[0].size());

    auto commandHandler = commandHandlers.find(hashedCommand);
    if (commandHandler != commandHandlers.end())
    {
        splitCommand.erase(splitCommand.begin());
        return commandHandler->second(gameConsole, splitCommand);
    }
    else
    {
        gameConsole->PrintWarning("Unhandled command: " + command);
        return false;
    }
}