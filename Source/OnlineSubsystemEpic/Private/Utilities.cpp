#include "Utilities.h"
#include "Interfaces/OnlineIdentityInterface.h"

#ifndef _WIN32
#include <sys/time.h>
#include <time.h>
#else
#include "Windows/WindowsHWrapper.h"
#endif

FIdentityUtilities::FIdentityUtilities() {}

FIdentityUtilities::~FIdentityUtilities() {}

FString FIdentityUtilities::EpicAccountIDToString(EOS_EpicAccountId InAccountId)
{
	if (InAccountId == nullptr)
	{
		return TEXT("NULL");
	}

	static char TempBuffer[EOS_EPICACCOUNTID_MAX_LENGTH];
	int32_t TempBufferSize = sizeof(TempBuffer);
	EOS_EResult Result = EOS_EpicAccountId_ToString(InAccountId, TempBuffer, &TempBufferSize);

	if (Result == EOS_EResult::EOS_Success)
	{
		return TempBuffer;
	}

	UE_LOG_ONLINE_IDENTITY(Warning, TEXT("[EOS SDK] Epic Account Id To String Error: %d"), (int32_t)Result);

	return TEXT("ERROR");

}

FString FIdentityUtilities::ProductUserIDToString(EOS_ProductUserId InAccountId)
{
	if (InAccountId == nullptr)
	{
		return TEXT("NULL");
	}

	static char TempBuffer[EOS_PRODUCTUSERID_MAX_LENGTH];
	int32_t TempBufferSize = sizeof(TempBuffer);
	EOS_EResult Result = EOS_ProductUserId_ToString(InAccountId, TempBuffer, &TempBufferSize);

	if (Result == EOS_EResult::EOS_Success)
	{
		return TempBuffer;
	}

	UE_LOG_ONLINE_IDENTITY(Warning, TEXT("[EOS SDK] Epic Account Id To String Error: %d"), (int32_t)Result);

	return TEXT("ERROR");
}

EOS_EpicAccountId FIdentityUtilities::EpicAccountIDFromString(const FString AccountString)
{
	if (AccountString.IsEmpty())
	{
		return nullptr;
	}

	const char* car = TCHAR_TO_UTF8(*AccountString);
	return EOS_EpicAccountId_FromString(car);
}

EOS_ProductUserId FIdentityUtilities::ProductUserIDFromString(FString const AccountString)
{
	if (AccountString.IsEmpty())
	{
		return nullptr;
	}

	const char* car = TCHAR_TO_UTF8(*AccountString);
	return EOS_ProductUserId_FromString(car);
}

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
