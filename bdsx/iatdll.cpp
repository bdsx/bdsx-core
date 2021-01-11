#include "stdafx.h"
#include "iatdll.h"

#include <KRWin/handle.h>

using namespace kr;

IatDll g_iat;

IatDll::IatDll() noexcept:
	module(win::Module::current()),
	chakra(module, "chakra.dll"),
	ws2_32(module, "WS2_32.dll")
{
}
IatDll::~IatDll() noexcept
{
}

