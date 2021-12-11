#pragma once
#include "GameConsoleCommands.h"
#include "GameConsole.h"

#include "../../Utils/ServiceLocator.h"
#include "../../ECS/Components/Singletons/LocalplayerSingleton.h"
#include "../../ECS/Components/Network/ConnectionSingleton.h"

#include <entt.hpp>
#include <Gameplay/ECS/Components/Transform.h>

#include <Networking/NetStructures.h>
#include "../../ECS/Components/Rendering/ModelDisplayInfo.h"
#include "../../ECS/Components/Singletons/NDBCSingleton.h"

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

bool GameConsoleCommands::HandleTele(GameConsole* gameConsole, std::vector<std::string> subCommands)
{
	size_t numSubCommands = subCommands.size();
	if (numSubCommands < 3 || numSubCommands > 4)
	{
		gameConsole->PrintError("Incorrect Usage! (tele 'X' 'Y' 'Z', ('O'))");
		return true;
	}

	entt::registry* registry = ServiceLocator::GetGameRegistry();
	LocalplayerSingleton& localplayerSingleton = registry->ctx<LocalplayerSingleton>();

	if (localplayerSingleton.entity == entt::null)
	{
		gameConsole->PrintError("Failed to teleport, localplayer not initialized");
		return true;
	}

	Transform& transform = registry->get<Transform>(localplayerSingleton.entity);
	transform.position = vec3(std::stof(subCommands[0]), std::stof(subCommands[1]), std::stof(subCommands[2]));

	// Orientation is an optional parameter
	if (numSubCommands == 4)
	{
		transform.rotation.z = std::stof(subCommands[3]);
	}

	registry->emplace_or_replace<TransformIsDirty>(localplayerSingleton.entity);

	return true;
}

bool GameConsoleCommands::HandleGoto(GameConsole* gameConsole, std::vector<std::string> subCommands)
{
	if (subCommands.size() != 1)
	{
		gameConsole->PrintError("Incorrect Usage! (goto 'Name')");
		return true;
	}

	entt::registry* registry = ServiceLocator::GetGameRegistry();
	ConnectionSingleton& connectionSingleton = registry->ctx<ConnectionSingleton>();

	if (connectionSingleton.gameConnection && connectionSingleton.gameConnection->IsConnected())
	{
		const std::string& location = subCommands[0];

		std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<512>();
		buffer->Put(Opcode::CMSG_GOTO);
		buffer->PutU16(static_cast<u16>(location.length() + 1u));
		buffer->PutString(location);

		connectionSingleton.gameConnection->Send(buffer);
	}
	else
	{
		gameConsole->PrintError("You must be connected to a game server in order to use (goto 'Name')");
	}

	return true;
}

bool GameConsoleCommands::HandleStoreLoc(GameConsole* gameConsole, std::vector<std::string> subCommands)
{
	if (subCommands.size() != 1)
	{
		gameConsole->PrintError("Incorrect Usage! (storeloc 'Name')");
		return true;
	}

	entt::registry* registry = ServiceLocator::GetGameRegistry();
	ConnectionSingleton& connectionSingleton = registry->ctx<ConnectionSingleton>();

	if (connectionSingleton.gameConnection && connectionSingleton.gameConnection->IsConnected())
	{
		const std::string& location = subCommands[0];

		std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<512>();
		buffer->Put(Opcode::CMSG_STORELOC);
		buffer->PutU16(static_cast<u16>(location.length() + 1u));
		buffer->PutString(location);

		connectionSingleton.gameConnection->Send(buffer);
	}
	else
	{
		gameConsole->PrintError("You must be connected to a game server in order to use (storeloc 'Name')");
	}

	return true;
}

bool GameConsoleCommands::HandleMorph(GameConsole* gameConsole, std::vector<std::string> subCommands)
{
	if (subCommands.size() != 1)
	{
		gameConsole->PrintError("Incorrect Usage! (morph 'displayId')");
		return true;
	}

	entt::registry* registry = ServiceLocator::GetGameRegistry();

	LocalplayerSingleton& localplayerSingleton = registry->ctx<LocalplayerSingleton>();
	if (localplayerSingleton.entity != entt::null)
	{
		u32 displayId = std::stoi(subCommands[0]);

		NDBCSingleton& ndbcSingleton = registry->ctx<NDBCSingleton>();
		NDBC::File* creatureDisplayInfoFile = ndbcSingleton.GetNDBCFile("CreatureDisplayInfo");
		NDBC::CreatureDisplayInfo* creatureDisplayInfo = creatureDisplayInfoFile->GetRowById<NDBC::CreatureDisplayInfo>(displayId);

		if (creatureDisplayInfo)
		{
			registry->remove<ModelDisplayInfo>(localplayerSingleton.entity);
			registry->emplace_or_replace<ModelDisplayInfo>(localplayerSingleton.entity, ModelType::Creature, displayId);
		}
		else
		{
			gameConsole->PrintError("Invalid displayId provided!");
		}
	}

	return true;
}
