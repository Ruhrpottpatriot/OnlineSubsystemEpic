#pragma once

#include "CoreMinimal.h"
#include "OnlineSubsystemEpicPackage.h"
#include "OnlineSubsystemTypes.h"
#include "IPAddress.h"

TEMP_UNIQUENETIDSTRING_SUBCLASS(FUniqueNetIdEpic, EPIC_SUBSYSTEM);

UENUM()
enum class ELoginType : uint8
{
	Password		UMETA(DisplayName = "Password"),
	ExchangeCode	UMETA(DisplayName = "Exchange Code"),
	DeviceCode		UMETA(DisplayName = "Device Code"),
	Developer		UMETA(DisplayName = "Developer"),
	RefreshToken	UMETA(DisplayName = "Refresh Token"),
	AccountPortal	UMETA(DisplayName = "Account Portal"),
	PersistentAuth	UMETA(DisplayName = "Persistent Auth"),
};

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
