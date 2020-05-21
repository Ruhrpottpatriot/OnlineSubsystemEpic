#pragma once

#include "CoreMinimal.h"
#include "OnlineSubsystemEpicPackage.h"
#include "OnlineSubsystemTypes.h"

TEMP_UNIQUENETIDSTRING_SUBCLASS(FUniqueNetIdEpic, EPIC_SUBSYSTEM);

namespace ELoginType
{
	enum Type
	{
		Password,
		ExchangeCode,
		PersistentAuth,
		DeviceCode,
		Developer,
		RefreshToken,
		AccountPortal
	};

	inline const TCHAR* ToString(ELoginType::Type LoginType)
	{
		switch (LoginType)
		{
		case Password: return TEXT("Password");
		case ExchangeCode: return TEXT("ExchangeCode");
		case PersistentAuth: return TEXT("PersistentAuth");
		case DeviceCode: return TEXT("DeviceCode");
		case Developer: return TEXT("Developer");
		case RefreshToken: return TEXT("RefreshToken");
		case AccountPortal: return TEXT("AccountPortal");
		default: checkNoEntry();
		};
	}

	ELoginType::Type FromString(FString LoginTypeString)
	{
		if (LoginTypeString == TEXT("Password")) return Type::Password;
		else if (LoginTypeString == TEXT("ExchangeCode")) return Type::ExchangeCode;
		else if (LoginTypeString == TEXT("PersistentAuth")) return Type::PersistentAuth;
		else if (LoginTypeString == TEXT("DeviceCode")) return Type::DeviceCode;
		else if (LoginTypeString == TEXT("Developer")) return Type::Developer;
		else if (LoginTypeString == TEXT("RefreshToken")) return Type::RefreshToken;
		else if (LoginTypeString == TEXT("AccountPortal")) return Type::AccountPortal;
		else
		{
			verifyf(false, TEXT("LoginType: %s is not supported"), *LoginTypeString);
			return AccountPortal; // Needs to be here....
		}
	}
}
