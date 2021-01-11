#pragma once

#include <KRWin/hook.h>

class IatDll
{
public:
	IatDll() noexcept;
	~IatDll() noexcept;

	kr::win::Module* module;
	kr::hook::IATHookerList chakra;
	kr::hook::IATHookerList ws2_32;
};

extern IatDll g_iat;
