#include "Utilities.h"
#include "Interfaces/OnlineIdentityInterface.h"

#ifndef _WIN32
#include <sys/time.h>
#include <time.h>
#else
#include "Windows/WindowsHWrapper.h"
#endif

const char* FUtils::GetTempDirectory()
{
#ifdef _WIN32
	static char Buffer[1024] = { 0 };
	if (Buffer[0] == 0)
	{
		GetTempPathA(sizeof(Buffer), Buffer);
	}

	return Buffer;

#elif defined(__APPLE__)
	return "/private/var/tmp";
#else
	return "/var/tmp";
#endif
}
