#pragma once
#include "GameConsoleCommands.h"
#include "GameConsole.h"
#include "../../Utils/ServiceLocator.h"

bool GameConsoleCommands::HandleHelp(GameConsole* gameConsole, std::vector<std::string> subCommands)
{
	gameConsole->Print("-- Help --");
	gameConsole->Print("Available Commands : 'help', 'ping', 'reload'");
	return false;
}

bool GameConsoleCommands::HandlePing(GameConsole* gameConsole, std::vector<std::string> subCommands)
{
	gameConsole->Print("pong");
	return true;
}

bool GameConsoleCommands::HandleScriptReload(GameConsole* gameConsole, std::vector<std::string> subCommands)
{
	moodycamel::ConcurrentQueue<Message>* inputQueue = ServiceLocator::GetMainInputQueue();

	Message reloadMessage;
	reloadMessage.code = MSG_IN_RELOAD;
	inputQueue->enqueue(reloadMessage);

	return false;
}
