#include <Windows.h>
#include "detours/detours.h"

using namespace MologieDetours;

enum MoveConstants
{
	MaxTriggerKeys = 6,
};

struct Move
{
	// packed storage rep, set in clamp
	int px, py, pz;
	unsigned int pyaw, ppitch, proll;
	float x, y, z;          // float -1 to 1
	float yaw, pitch, roll; // 0-2PI
	unsigned int id;               // sync'd between server & client - debugging tool.
	unsigned int sendCount;

	bool freeLook;
	bool trigger[MaxTriggerKeys];
};

typedef void (__fastcall *Player__processTick_Type)(void *, int, const Move *);
Player__processTick_Type Player__processTick = (Player__processTick_Type)0x00531A90;

typedef void (*Con__printf_Type)(const char *format, ...);
Con__printf_Type Con__printf = (Con__printf_Type)0x004A87F0;

typedef const char *(*Con__execute_Type)(int argc, const char *argv[]);
Con__execute_Type Con__execute = (Con__execute_Type)0x004A7870;

typedef void (*Con__execute_o_Type)(void *object, int argc, const char *argv[]);
Con__execute_o_Type Con__execute_o = (Con__execute_o_Type)0x004A8B40;

typedef const char *(*Con__getIntArg_Type)(int arg);
Con__getIntArg_Type Con__getIntArg = (Con__getIntArg_Type)0x004A1030;

typedef char *(__thiscall *StringStack__getArgBuffer_Type)(void *this_, unsigned int arg);
StringStack__getArgBuffer_Type StringStack__getArgBuffer = (StringStack__getArgBuffer_Type)0x004A0D10;

typedef bool (__fastcall *AIPlayer__getAIMove_Type)(void *, int, Move *);
AIPlayer__getAIMove_Type AIPlayer__getAIMove = (AIPlayer__getAIMove_Type)0x004F3C80;

Detour<Player__processTick_Type> *Player__processTick_Detour;
Detour<AIPlayer__getAIMove_Type> *AIPlayer__getAIMove_Detour;

void *STR = (void *)0x0070CDC8;

void Con__executef_o(void *object, int argc, ...)
{
	const char *argv[128];

	va_list args;
	va_start(args, argc);
	for (int i = 0; i < argc; i++)
		argv[i + 1] = va_arg(args, const char *);
	va_end(args);
	argv[0] = argv[1];
	argc++;

	return Con__execute_o(object, argc, argv);
}

void moveToString(const Move *const move, char *const buffer, size_t size)
{
	if (!move)
	{
		buffer[0] = '0';
		buffer[1] = 0;
		return;
	}
	unsigned int flags = move->freeLook ? 1 : 0;
	for (int i = 0; i < MaxTriggerKeys; i++)
	{
		if (move->trigger[i])
			flags |= 1 << (1 + i);
	}
	sprintf_s(buffer, size, "%f %f %f %f %f %f %u",
		move->x, move->y, move->z,
		move->yaw, move->pitch, move->roll,
		flags);
}

void stringToMove(Move *const move, const char *const buffer, size_t size)
{
	unsigned int flags = 0;
	sscanf_s(buffer, "%f %f %f %f %f %f %u",
		&move->x, &move->y, &move->z,
		&move->yaw, &move->pitch, &move->roll,
		&flags);
	move->freeLook = (flags & 1) != 0;
	for (int i = 0; i < MaxTriggerKeys; i++)
		move->trigger[i] = (flags & (1 << (1 + i))) != 0;
}

void __fastcall Player__processTick_Hook(void *this_, int edx, const Move *move)
{
	unsigned int netFlags = *(DWORD *)((DWORD)this_ + 68);
	if (!(netFlags & 2)) // Is it a ghost?
	{
		int thisId = ((int *)this_)[8];
		const char *argv[3];
		int inputs = 0;
		argv[0] = "onPlayerProcessTick";
		argv[1] = Con__getIntArg(thisId);
		char *buffer = StringStack__getArgBuffer(STR, 300);
		moveToString(move, buffer, 300);
		argv[2] = buffer;
		Con__execute(3, argv);
	}
	return Player__processTick_Detour->GetOriginalFunction()(this_, edx, move);
}

bool __fastcall AIPlayer__getAIMove_Hook(void *this_, int edx, Move *move)
{
	int thisId = ((int *)this_)[8];
	const char *argv[2];
	argv[0] = "onGetAIMove";
	argv[1] = Con__getIntArg(thisId);
	const char *result = Con__execute(2, argv);
	if (!result[0])
		return AIPlayer__getAIMove_Detour->GetOriginalFunction()(this_, edx, move);
	if (result[0] == '0' && result[1] == 0)
		return false;
	stringToMove(move, result, strlen(result) + 1);
	return true;
}

bool __stdcall DllMain(HINSTANCE instance, unsigned long reason, void *)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		Player__processTick_Detour = new Detour<Player__processTick_Type>(Player__processTick, Player__processTick_Hook);
		AIPlayer__getAIMove_Detour = new Detour<AIPlayer__getAIMove_Type>(AIPlayer__getAIMove, AIPlayer__getAIMove_Hook);
		break;
	case DLL_PROCESS_DETACH:
		delete Player__processTick_Detour;
		delete AIPlayer__getAIMove_Detour;
		break;
	}

	return true;
}