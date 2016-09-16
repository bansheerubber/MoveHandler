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
Detour<Player__processTick_Type> *Player__processTick_Detour;

typedef void (*Con__printf_Type)(const char *format, ...);
Con__printf_Type Con__printf = (Con__printf_Type)0x004A87F0;

typedef void (*Con__execute_Type)(int argc, const char *argv[]);
Con__execute_Type Con__execute = (Con__execute_Type)0x004A7870;

typedef void (*Con__execute_o_Type)(void *object, int argc, const char *argv[]);
Con__execute_o_Type Con__execute_o = (Con__execute_o_Type)0x004A8B40;

typedef const char *(*Con__getIntArg_Type)(int arg);
Con__getIntArg_Type Con__getIntArg = (Con__getIntArg_Type)0x004A1030;

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

void __fastcall Player__processTick_Hook(void *this_, int edx, const Move *move)
{
	unsigned int netFlags = *(DWORD *)((DWORD)this_ + 68);
	if (!(netFlags & 2)) /* NetFlags::IsGhost */
	{
		int thisId = ((int *)this_)[8];
		const char *argv[3];
		argv[0] = "onPlayerProcessTick";
		argv[1] = Con__getIntArg(thisId);
		argv[2] = Con__getIntArg(move->id);
		Con__execute(3, argv);
	}
	return Player__processTick_Detour->GetOriginalFunction()(this_, edx, move);
}

bool __stdcall DllMain(HINSTANCE instance, unsigned long reason, void *)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		Player__processTick_Detour = new Detour<Player__processTick_Type>(Player__processTick, Player__processTick_Hook);
		break;
	case DLL_PROCESS_DETACH:
		delete Player__processTick_Detour;
		break;
	}

	return true;
}