#pragma once

#include "CoreMinimal.h"
#include "OnlineSubsystemEpicPackage.h"
#include "OnlineSubsystemTypes.h"
#include "IPAddress.h"
#include "eos_common.h"
#include <OnlineSubsystem.h>
#include <cassert>

#define LOGIN_TYPE_EAS TEXT("EAS")
#define LOGIN_TYPE_CONNECT TEXT("CONNECT")

/**
 * Wraps the native product user id (PUID) and epic account id (EAID).
 *
 * Since the identity interface primarily uses the connect interface
 * to retrieve user data, the internal PUID will always be valid and
 * thus is used as basis for this unique net id. Should the user opt
 * to use the epic account system login flow, the EAID will be valid
 * in addition to the PUID.
 * In general a user should never have to access the PUID or EAID
 * themselves, if this should become necessary at some point, both id's
 * can be accessed via FUserOnlineAccount::GetAuthAttribute() with
 * the following keys:
 * - PUID: AUTH_ATTR_ID_TOKEN
 * - EAID: AUTH_ATTR_EA_TOKEN
 */
class FUniqueNetIdEpic
	: public FUniqueNetId
{
	// Used purely for GetType()
	FName Type = EPIC_SUBSYSTEM;

	EOS_ProductUserId productUserId;

	EOS_EpicAccountId epicAccountId;

public:
	FUniqueNetIdEpic() = default;

	// Define these to increase visibility to public (from parent's protected)
	FUniqueNetIdEpic(FUniqueNetIdEpic&&) = default;
	FUniqueNetIdEpic(const FUniqueNetIdEpic&) = default;
	FUniqueNetIdEpic& operator=(FUniqueNetIdEpic&&) = default;
	FUniqueNetIdEpic& operator=(const FUniqueNetIdEpic&) = default;

	virtual ~FUniqueNetIdEpic() = default;

	/**
	 * Constructs this object with the string value of the specified net id.
	 * While this allows the possibility of conversion from arbitrary unique net ids
	 * to this type, it doesn't mean the new unique net id is valid apart from
	 * grammatical correctness.
	 *
	 * @param Src the id to copy
	 */
	explicit FUniqueNetIdEpic(const FUniqueNetId& OtherId)
		: Type(EPIC_SUBSYSTEM)
		, productUserId(nullptr)
		, epicAccountId(nullptr)
	{
		if (OtherId.GetType() == EPIC_SUBSYSTEM)
		{
			// Conversion is done by looking at the byte representation of the other id.
			// Since we already know that the other id is an FUniqueNetIdEpic
			// we can reason about the size and placement of the bytes.
			uint8 const* bytes = OtherId.GetBytes();

			uint8 type = bytes[0];
			if (type == 0)
			{
				// Not a valid id
				return;
			}
			else if (type == 1)
			{
				char const* buffer = (char const*)(bytes + 1);
				EOS_ProductUserId puid = EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*FString(buffer)));
				check(EOS_ProductUserId_IsValid(puid));
				this->productUserId = puid;
			}
			else if (type == 2)
			{
				char const* buffer = (char const*)(bytes + 1);
				EOS_EpicAccountId eaid = EOS_EpicAccountId_FromString(TCHAR_TO_UTF8(*FString(buffer)));
				check(EOS_EpicAccountId_IsValid(eaid));
				this->epicAccountId = eaid;
			}
			else if (type == 3)
			{
				char const* buffer = (char const*)(bytes + 1);
				EOS_ProductUserId puid = EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*FString(buffer)));
				check(EOS_ProductUserId_IsValid(puid));
				this->productUserId = puid;

				// Move the buffer ptr ahead
				buffer = (char const*)(bytes + 1 + EOS_PRODUCTUSERID_MAX_LENGTH);
				EOS_EpicAccountId eaid = EOS_EpicAccountId_FromString(TCHAR_TO_UTF8(*FString(buffer)));
				check(EOS_EpicAccountId_IsValid(eaid));
				this->epicAccountId = eaid;
			}

			delete bytes;
		}
		else
		{
			UE_LOG_ONLINE(Warning, TEXT("Non compatible FUniqueNetId passed as argument."));
			return;
		}
	}

	/** Create a new id from an existing PUID */
	FUniqueNetIdEpic(const EOS_ProductUserId& InUserId)
		: Type(EPIC_SUBSYSTEM)
		, productUserId(InUserId)
		, epicAccountId(nullptr)
	{
	}
	FUniqueNetIdEpic(EOS_ProductUserId&& InUserId)
		: Type(EPIC_SUBSYSTEM)
		, productUserId(MoveTemp(InUserId))
		, epicAccountId(nullptr)
	{
	}

	/** Create a new net id from an existing EAID */
	FUniqueNetIdEpic(const EOS_EpicAccountId& InEpicAccountId)
		: Type(EPIC_SUBSYSTEM)
		, productUserId(nullptr)
		, epicAccountId(InEpicAccountId)
	{
	}	
	FUniqueNetIdEpic(EOS_EpicAccountId&& InEpicAccountId)
		: Type(EPIC_SUBSYSTEM)
		, productUserId(nullptr)
		, epicAccountId(MoveTemp(InEpicAccountId)) //another thing missed
	{
	}

	/** Create a new net id from an existing PUID and EAID */
	FUniqueNetIdEpic(const EOS_ProductUserId& InProductUserId, const EOS_EpicAccountId& InEpicAccountId)
		: Type(EPIC_SUBSYSTEM)
		, productUserId(InProductUserId)
		, epicAccountId(InEpicAccountId)
	{
	}
	FUniqueNetIdEpic(EOS_ProductUserId&& InProductUserId, EOS_EpicAccountId&& InEpicAccountId)
		: Type(EPIC_SUBSYSTEM)
		, productUserId(MoveTemp(InProductUserId))
		, epicAccountId(MoveTemp(InEpicAccountId))
	{
	}

	virtual FName GetType() const override
	{
		return this->Type;
	}

	virtual const uint8* GetBytes() const override
	{
		// Since the unique net id can be either a PUID or an EAID, we need to tell the user how which of the two is present.
		// This is done by adding a single unsigned 8-bit integer to the front of the byte array. Values for this indicator are:
		// 0: Nothing is present
		// 1: PUID
		// 3: PUID and EAID
		// The values are set as a power of two to enable bitwise operations on them if needed.
		// Should both values be present, the PUId will ALWAYS be first in order

		int32 idSize = this->GetSize();
		uint8* buffer = new uint8[idSize + 1];
		if (!buffer)
		{
			UE_LOG_ONLINE(Warning, TEXT("Couldn't allocate memory for this FUniqueNetId's byte representation"));
			return nullptr;
		}

		// Set which type is contained in the array
		uint8 type = (1 * int(this->IsProductUserIdValid())) + (2 * int(this->IsEpicAccountIdValid()));
		buffer[0] = type;

		if (type == 0)
		{
			UE_LOG_ONLINE(Warning, TEXT("No valid internal account id available."))
				return nullptr;
		}
		else if (type == 1)
		{
			int32_t puidBufSize = EOS_PRODUCTUSERID_MAX_LENGTH;
			char* puidData = (char*)(buffer + 1);
			EOS_EResult result = EOS_ProductUserId_ToString(this->productUserId, puidData, &puidBufSize);
			if (result != EOS_EResult::EOS_Success)
			{
				UE_LOG_ONLINE(Warning, TEXT("Couldn't convert PUID to byte array."));
				return nullptr;
			}
		}
		else if (type == 2)
		{
			int32_t eaidBufSize = EOS_EPICACCOUNTID_MAX_LENGTH;

			char* eaidData = (char*)(buffer + 1);
			EOS_EResult result = EOS_EpicAccountId_ToString(this->epicAccountId, eaidData, &eaidBufSize);
			if (result != EOS_EResult::EOS_Success)
			{
				UE_LOG_ONLINE(Warning, TEXT("Couldn't convert EAID to byte array."));
			}
		}
		else if (type == 3)
		{
			// Convert both ids into a char array.
			// If one conversion fails, the user shouldn't receive anything.

			int32_t puidBufSize = EOS_PRODUCTUSERID_MAX_LENGTH;
			char* puidData = (char*)(buffer + 1);
			EOS_EResult result = EOS_ProductUserId_ToString(this->productUserId, puidData, &puidBufSize);
			if (result != EOS_EResult::EOS_Success)
			{
				UE_LOG_ONLINE(Warning, TEXT("Couldn't convert PUID to byte array."));
				return nullptr;
			}

			int32_t eaidBufSize = EOS_EPICACCOUNTID_MAX_LENGTH;
			char* eaidData = (char*)(buffer + 1 + EOS_PRODUCTUSERID_MAX_LENGTH);
			result = EOS_EpicAccountId_ToString(this->epicAccountId, eaidData, &eaidBufSize);
			if (result != EOS_EResult::EOS_Success)
			{
				UE_LOG_ONLINE(Warning, TEXT("Couldn't convert EAID to byte array."));
				return nullptr;
			}
		}
		else
		{
			// We should NEVER be here.
			checkNoEntry();
			return nullptr;
		}

		return (const uint8*)buffer;
	}

	/**
	 * Returns the size of the internal data stored by this class.
	 * This is either EOS_PRODUCTUSERID_MAX_LENGTH, EOS_EPICACCOUNTID_MAX_LENGTH
	 * or EOS_PRODUCTUSERID_MAX_LENGTH + EOS_EPICACCOUNTID_MAX_LENGTH.
	 */
	virtual int32 GetSize() const override
	{
		return (this->IsProductUserIdValid() ? EOS_PRODUCTUSERID_MAX_LENGTH : 0)
			+ (this->IsEpicAccountIdValid() ? EOS_EPICACCOUNTID_MAX_LENGTH : 0)
			+ sizeof(uint8); // Since we include the type of the returned id(s).
	}

	/**
	  * Returns if either the PUID or the EAID is valid. For more information
	  * use IsProductUserIdValid() or IsEpicAccountIdValid()
	  */
	virtual bool IsValid() const override
	{
		return this->IsProductUserIdValid() || this->IsEpicAccountIdValid();
	}

	/** Checks if the internal ProductUserId is valid. */
	bool IsProductUserIdValid() const
	{
		return (bool)EOS_ProductUserId_IsValid(this->productUserId);
	}

	/** Checks if the underlying EpicAccountId is valid. */
	bool IsEpicAccountIdValid() const
	{
		return (bool)EOS_EpicAccountId_IsValid(this->epicAccountId);
	}

	virtual FString ToString() const override
	{
		FString representation = TEXT("");
		if (this->IsProductUserIdValid() && this->IsEpicAccountIdValid())
		{
			representation = FString::Printf(TEXT("(%s,%s)"), *FUniqueNetIdEpic::ProductUserIdToString(this->productUserId), *FUniqueNetIdEpic::EpicAccountIdToString(this->epicAccountId));
		}
		else if (this->IsProductUserIdValid())
		{
			representation = FString::Printf(TEXT("(%s)"), *FUniqueNetIdEpic::ProductUserIdToString(this->productUserId));
		}
		else
		{
			representation = FString::Printf(TEXT("(%s)"), *FUniqueNetIdEpic::EpicAccountIdToString(this->epicAccountId));
		}

		return representation;
	}

	virtual FString ToDebugString() const override
	{
		bool puidValid = (bool)EOS_ProductUserId_IsValid(this->productUserId);
		bool eaidValid = (bool)EOS_EpicAccountId_IsValid(this->epicAccountId);

		return FString::Printf(TEXT("PUID: %s; EAID: %s"),
			puidValid ? OSS_REDACT(*FUniqueNetIdEpic::ProductUserIdToString(this->productUserId)) : TEXT("INVALID"),
			eaidValid ? OSS_REDACT(*FUniqueNetIdEpic::EpicAccountIdToString(this->epicAccountId)) : TEXT("INVALID"));
	}

	/** Sets the Epic Account Id after the Net Id has been constructed. */
	bool SetEpicAccountId(EOS_EpicAccountId eaid)
	{
		if (EOS_EpicAccountId_IsValid(eaid))
		{
			this->epicAccountId = eaid;
			return true;
		}
		return false;
	}

	/**
	  * Converts this instance to a PUID.
	  * Keep in mind that this returns a non owning pointer
	  * which might become invalid at any given point.
	  */
	EOS_ProductUserId ToProductUserId() const
	{
		bool puidValid = (bool)EOS_ProductUserId_IsValid(this->productUserId);
		check(puidValid);
		return this->productUserId;
	}

	/**
	  * Converts this instance to a EAID.
	  * Keep in mind that, the returned pointer doesn't grant ownership
	  * and might become invalid at any given point. If the instance wasn't
	  * set-up with an EAID, a nullptr is returned.
	  */
	EOS_EpicAccountId ToEpicAccountId() const
	{
		if (this->IsEpicAccountIdValid())
		{
			return this->epicAccountId;
		}
		return nullptr;
	}

	/**
	  * Converts the given ProductUserId to a string.
	  * @param InAccountId - The PUID to convert
	  * @returns - A string representing a valid PUId, empty otherwise.
	  */
	static FString ProductUserIdToString(EOS_ProductUserId InAccountId)
	{
		// Only the mandatory PUID should be considered for a string representation
		static char buffer[EOS_PRODUCTUSERID_MAX_LENGTH];
		int32_t bufferSize = sizeof(buffer);
		EOS_EResult Result = EOS_ProductUserId_ToString(InAccountId, buffer, &bufferSize);

		if (Result == EOS_EResult::EOS_Success)
		{
			return buffer;
		}
		return FString();
	}

	/**
	 * Converts a string into a ProductUserId
	 * @param AccountString - The string representing the PUID
	 * @returns - A PUID if the string was not empty or malformed,
	 *			  nullptr otherwise.
	 */
	static EOS_ProductUserId ProductUserIDFromString(FString const AccountString)
	{
		if (AccountString.IsEmpty())
		{
			return nullptr;
		}

		EOS_ProductUserId id = EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*AccountString));

		if (EOS_ProductUserId_IsValid(id))
		{
			return id;
		}
		return nullptr;
	}


	/** Converts the given EpicAccountId to a string.
	  * @param InAccountId - The EAID to convert
	  * @returns - A string representing a valid EAID, empty otherwise.
	  */
	static FString EpicAccountIdToString(EOS_EpicAccountId InAccountId)
	{
		static char buffer[EOS_EPICACCOUNTID_MAX_LENGTH];
		int32_t bufferSize = sizeof(buffer);
		EOS_EResult Result = EOS_EpicAccountId_ToString(InAccountId, buffer, &bufferSize);

		if (Result == EOS_EResult::EOS_Success)
		{
			return buffer;
		}
		return FString();

	}

	/**
	 * Converts a string into an EpicAccountId
	 * @param AccountString - The string representing the PUID
	 * @returns - A EAID if the string was not empty or malformed,
	 *			  nullptr otherwise.
	 */
	static EOS_EpicAccountId EpicAccountIDFromString(const FString AccountString)
	{
		if (AccountString.IsEmpty())
		{
			return nullptr;
		}

		EOS_EpicAccountId id = EOS_EpicAccountId_FromString(TCHAR_TO_UTF8(*AccountString));
		if (EOS_EpicAccountId_IsValid(id))
		{
			return id;
		}
		return nullptr;
	}

	/** Needed for TMap::GetTypeHash() */
	friend uint32 GetTypeHash(const FUniqueNetIdEpic& A)
	{
		return ((A.productUserId ? ::GetTypeHash(A.productUserId) : 0) * 397) ^ (A.epicAccountId ? ::GetTypeHash(A.epicAccountId) : 0);
	}
};

/**
 * An enumeration of the different friendship statuses. Modified from eos_friends_types.h
 */
UENUM()
enum class EFriendStatus : uint8
{
	/** The two accounts have no friendship status */
	NotFriends		UMETA(DisplayName = "Not Friends"),
	/** The local account has sent a friend invite to the other account */
	InviteSent		UMETA(DisplayName = "Invite Sent"),
	/** The other account has sent a friend invite to the local account */
	InviteReceived	UMETA(DisplayName = "Invite Received"),
	/** The accounts have accepted friendship */
	Friends			UMETA(DisplayNAme = "Friends")
};

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
	ExternalAuth	UMETA(DisplayName = "External Auth"),
};

UENUM()
enum class EExternalLoginType : uint8
{
	Steam		UMETA(DisplayName = "Steam"),
};


/**
 * User attribution constants for GetUserAttribute()
 */
#define USER_ATTR_COUNTRY TEXT("country")
#define USER_ATTR_PREFERRED_LANGUAGE TEXT("preferred_language")
#define AUTH_ATTR_EA_TOKEN TEXT("eas_id_token")
#define USER_ATTR_LAST_LOGIN_TIME TEXT("last_login_time")

 /**
  * Info associated with an user account generated by this online service
  */
class FUserOnlineAccountEpic :
	public FUserOnlineAccount
{
private:
	/** User Id represented as a FUniqueNetId */
	TSharedRef<const FUniqueNetId> UserIdPtr;

	/** Additional key/value pair data related to auth */
	TMap<FString, FString> AdditionalAuthData;

	/** Additional key/value pair data related to user attribution */
	TMap<FString, FString> UserAttributes;

public:
	virtual TSharedRef<const FUniqueNetId> GetUserId() const override;

	virtual FString GetRealName() const override;
	virtual FString GetDisplayName(const FString& Platform = FString()) const override;
	virtual FString GetAccessToken() const override;
	virtual bool GetUserAttribute(const FString& AttrName, FString& OutAttrValue) const override;
	virtual bool SetUserAttribute(const FString& AttrName, const FString& AttrValue) override;
	virtual bool GetAuthAttribute(const FString& AttrName, FString& OutAttrValue) const override;
	virtual bool SetAuthAttribute(const FString& AttrName, const FString& AttrValue);

	FUserOnlineAccountEpic(TSharedRef<FUniqueNetId const> InUserIdPtr)
		: UserIdPtr(InUserIdPtr)
	{
	}

	virtual ~FUserOnlineAccountEpic() = default;
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
	TSharedPtr<FUniqueNetId const> SessionId;

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
