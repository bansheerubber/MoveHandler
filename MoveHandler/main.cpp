#include <Windows.h>
#include "detours/detours.h"
#include <Psapi.h>

using namespace MologieDetours;

static unsigned long imageBase;
static unsigned long imageSize;

// sig testing from BlocklandLoader, i don't need all of torque.h
bool sigTest(const char *data, const char *pattern, const char *mask)
{
	for (; *mask; ++data, ++pattern, ++mask)
	{
		if (*mask == 'x' && *data != *pattern)
			return false;
	}

	return *mask == NULL;
}

void *sigFind(const char *pattern, const char *mask)
{
	unsigned long i = imageBase;
	unsigned long end = i + imageSize - strlen(mask);

	for (; i < end; i++)
	{
		if (sigTest((char *)i, pattern, mask))
			return (void *)i;
	}

	return 0;
}

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
Player__processTick_Type Player__processTick; // sub_531A90 in r2001
// func sig: 81 EC ? ? ? ? A1 ? ? ? ? 0F 57 C0

typedef void (*Con__printf_Type)(const char *format, ...);
Con__printf_Type Con__printf; // sub_4A87F0 in r2001
// func sig: 8B 4C 24 ? 8D 44 24 ? 50 6A ? 6A ? E8 ? ? ? ? 83 C4 ? C3 ? ? ? ? ? ? ? ? ? ? 8B 4C 24 ? 8D 44 24 ? 50 51

typedef const char *(*Con__execute_Type)(int argc, const char *argv[]);
Con__execute_Type Con__execute; // sub_4A7870 in r2001
// func sig: 8B 0D ? ? ? ? 56 8B 74 24 ?

typedef void (*Con__execute_o_Type)(void *object, int argc, const char *argv[]);
Con__execute_o_Type Con__execute_o; // sub_4A8B40 in r2001
// func sig: 8B 54 24 ? 81 EC ? ? ? ?

typedef const char *(*Con__getIntArg_Type)(int arg);
Con__getIntArg_Type Con__getIntArg; // sub_4A1030 in r2001
// func sig: 56 6A ? B9 ? ? ? ? E8 ? ? ? ? 8B F0 8B 44 24 ?

typedef char *(__thiscall *StringStack__getArgBuffer_Type)(void *this_, unsigned int arg);
StringStack__getArgBuffer_Type StringStack__getArgBuffer; // sub_4A0D10 in r2001
// func sig: 53 8B 5C 24 ? 56 8B F1 8B 86 ? ? ? ? 8B 96 ? ? ? ?

typedef bool (__fastcall *AIPlayer__getAIMove_Type)(void *, int, Move *);
AIPlayer__getAIMove_Type AIPlayer__getAIMove; // sub_4F3C80 in r2001
// func sig: 81 EC ? ? ? ? 53 8B 9C 24 ? ? ? ? 55 56 8B E9

Detour<Player__processTick_Type> *Player__processTick_Detour;
Detour<AIPlayer__getAIMove_Type> *AIPlayer__getAIMove_Detour;

void *STR = (void *)0x0070CDC8; // remianed unchanged in r2001, don't feel like sigging it

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
		case DLL_PROCESS_ATTACH: {
			MODULEINFO info;
			GetModuleInformation(GetCurrentProcess(), GetModuleHandle(NULL), &info, sizeof MODULEINFO);

			// needed for sigFind
			imageBase = (unsigned long)info.lpBaseOfDll;
			imageSize = info.SizeOfImage;

			// did this sig by hand, so i get 10 points
			Player__processTick = (Player__processTick_Type)sigFind("\x81\xEC\x00\x00\x00\x00\xA1\x00\x00\x00\x00\x0F\x57\xC0", "xx????x????xxx");
			AIPlayer__getAIMove = (AIPlayer__getAIMove_Type)sigFind("\x81\xEC\x00\x00\x00\x00\x53\x8B\x9C\x24\x00\x00\x00\x00\x55\x56\x8B\xE9", "xx????xxxx????xxxx");

			Con__printf = (Con__printf_Type)sigFind("\x8B\x4C\x24\x04\x8D\x44\x24\x08\x50\x6A\x00\x6A\x00\xE8\x0E\xFE\xFF\xFF\x83\xC4\x0C\xC3\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\x8B\x4C\x24\x04\x8D\x44\x24\x0C\x50\x51", "xxx?xxx?xx?x?x????xx?x??????????xxx?xxx?xx");
			Con__execute = (Con__execute_Type)sigFind("\x8B\x0D\x00\x00\x00\x00\x56\x8B\x74\x24\x00", "xx????xxxx?");
			Con__execute_o = (Con__execute_o_Type)sigFind("\x8B\x54\x24\x00\x81\xEC\x00\x00\x00\x00", "xxx?xx????");
			Con__getIntArg = (Con__getIntArg_Type)sigFind("\x56\x6A\x00\xB9\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x8B\xF0\x8B\x44\x24\x00", "xx?x????x????xxxxx?");

			StringStack__getArgBuffer = (StringStack__getArgBuffer_Type)sigFind("\x53\x8B\x5C\x24\x00\x56\x8B\xF1\x8B\x86\x00\x00\x00\x00\x8B\x96\x00\x00\x00\x00", "xxxx?xxxxx????xx????");

			Con__printf("FROGLAND!");
			
			Player__processTick_Detour = new Detour<Player__processTick_Type>(Player__processTick, Player__processTick_Hook);
			AIPlayer__getAIMove_Detour = new Detour<AIPlayer__getAIMove_Type>(AIPlayer__getAIMove, AIPlayer__getAIMove_Hook);
			break;
		}
		
		case DLL_PROCESS_DETACH: {
			delete Player__processTick_Detour;
			delete AIPlayer__getAIMove_Detour;
			break;
		}
	}

	return true;
}