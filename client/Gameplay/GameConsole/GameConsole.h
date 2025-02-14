#pragma once
#include <NovusTypes.h>
#include <Utils/ConcurrentQueue.h>
#include <Utils/DebugHandler.h>
#include <CVar/CVarSystem.h>

class GameConsoleCommandHandler;
class GameConsole
{
public:
	GameConsole();
	~GameConsole();

	void Render(f32 deltaTime);
	void Clear();
	void Toggle();

public:
	template <typename... Args>
	void Print(const std::string& string, Args... args)
	{
		char buffer[FormatBufferSize];
		i32 length = StringUtils::FormatString(buffer, FormatBufferSize, string.c_str(), args...);

		_linesToAppend.enqueue(std::string(buffer, length));

		if (*CVarSystem::Get()->GetIntCVar("gameconsole.DuplicateToTerminal"_h))
		{
			DebugHandler::Print(string, args...);
		}
	}

	template <typename... Args>
	void PrintSuccess(const std::string& string, Args... args)
	{
		char buffer[FormatBufferSize];
		i32 length = StringUtils::FormatString(buffer, FormatBufferSize, string.c_str(), args...);

		_linesToAppend.enqueue("[Success] : " + std::string(buffer, length));

		if (*CVarSystem::Get()->GetIntCVar("gameconsole.DuplicateToTerminal"_h))
		{
			DebugHandler::PrintSuccess(string, args...);
		}
	}

	template <typename... Args>
	void PrintWarning(const std::string& string, Args... args)
	{
		char buffer[FormatBufferSize];
		i32 length = StringUtils::FormatString(buffer, FormatBufferSize, string.c_str(), args...);

		_linesToAppend.enqueue("[Warning] : " + std::string(buffer, length));

		if (*CVarSystem::Get()->GetIntCVar("gameconsole.DuplicateToTerminal"_h))
		{
			DebugHandler::PrintWarning(string, args...);
		}
	}

	template <typename... Args>
	void PrintError(const std::string& string, Args... args)
	{
		char buffer[FormatBufferSize];
		i32 length = StringUtils::FormatString(buffer, FormatBufferSize, string.c_str(), args...);

		_linesToAppend.enqueue("[Error] : " + std::string(buffer, length));

		if (*CVarSystem::Get()->GetIntCVar("gameconsole.DuplicateToTerminal"_h))
		{
			DebugHandler::PrintError(string, args...);
		}
	}

	template <typename... Args>
	void PrintFatal(const std::string& string, Args... args)
	{
		char buffer[FormatBufferSize];
		i32 length = StringUtils::FormatString(buffer, FormatBufferSize, string.c_str(), args...);

		_linesToAppend.enqueue("[Fatal] : " + std::string(buffer, length));

		if (*CVarSystem::Get()->GetIntCVar("gameconsole.DuplicateToTerminal"_h))
		{
			DebugHandler::PrintFatal(string, args...);
		}
		else
		{
			ReleaseModeBreakpoint();
		}
	}

private:
	void Enable();
	void Disable();

private:
	static constexpr size_t FormatBufferSize = 256;
	f32 _visibleProgressTimer = 0;

	std::string _searchText;
	std::vector<std::string> _lines;
	moodycamel::ConcurrentQueue<std::string> _linesToAppend;

	GameConsoleCommandHandler* _commandHandler = nullptr;
};