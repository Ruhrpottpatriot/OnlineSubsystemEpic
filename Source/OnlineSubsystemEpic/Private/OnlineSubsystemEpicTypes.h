#pragma once

#include "CoreMinimal.h"
#include "OnlineSubsystemEpicPackage.h"
#include "OnlineSubsystemTypes.h"
#include "IPAddress.h"

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
			return RefreshToken; // Needs to be here....
		}
	}
}

/**
 * Epic implementation of session information
 */
class FOnlineSessionInfoEpic : public FOnlineSessionInfo
{
protected:

	/** Hidden on purpose */
	FOnlineSessionInfoEpic(const FOnlineSessionInfoEpic& Src)
	{
	}

	/** Hidden on purpose */
	FOnlineSessionInfoEpic& operator=(const FOnlineSessionInfoEpic& Src)
	{
		return *this;
	}

PACKAGE_SCOPE:

	/** Constructor */
	FOnlineSessionInfoEpic();

	/** The ip & port that the host is listening on (valid for LAN/GameServer) */
	TSharedPtr<class FInternetAddr> HostAddr;

	/** Unique Id for this session */
	TSharedPtr<FUniqueNetId> SessionId;

public:

	virtual ~FOnlineSessionInfoEpic() {}

	bool operator==(const FOnlineSessionInfoEpic& Other) const
	{
		return false;
	}

	virtual const uint8* GetBytes() const override
	{
		return nullptr;
	}

	virtual int32 GetSize() const override
	{
		return sizeof(uint64) + sizeof(TSharedPtr<class FInternetAddr>);
	}

	virtual bool IsValid() const override
	{
		// LAN case
		return HostAddr.IsValid() && HostAddr->IsValid();
	}

	virtual FString ToString() const override
	{
		return SessionId->ToString();
	}

	virtual FString ToDebugString() const override
	{
		return FString::Printf(TEXT("HostIP: %s SessionId: %s"),
			HostAddr.IsValid() ? *HostAddr->ToString(true) : TEXT("INVALID"),
			*SessionId->ToDebugString());
	}

	virtual const FUniqueNetId& GetSessionId() const override
	{
		return *SessionId;
	}
};
