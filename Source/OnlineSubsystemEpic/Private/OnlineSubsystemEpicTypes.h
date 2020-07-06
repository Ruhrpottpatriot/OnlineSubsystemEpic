#pragma once

#include "CoreMinimal.h"
#include "OnlineSubsystemEpicPackage.h"
#include "OnlineSubsystemTypes.h"
#include "IPAddress.h"
#include "eos_common.h"

#define LOGIN_TYPE_EAS TEXT("EAS")
#define LOGIN_TYPE_CONNECT TEXT("CONNECT")

/**
 * Wraps the native product user id (PUID) and epic account id (EAID).
 *
 * Since the identity interface primarily uses the connect interface
 * to retrieve user data, the internal PUID will always be valid and
 * thus is used as basis for this unique net id. Should the user opt
 * to use the epic accout system login flow, the EAID will be valid
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

	// Always valid
	EOS_ProductUserId productUserId;

	// Only valid when using EAS login flow
	EOS_EpicAccountId epicAccountId;

public:
	FUniqueNetIdEpic() = default;

	// Define these to increase visibility to public (from parent's protected)
	FUniqueNetIdEpic(FUniqueNetIdEpic&&) = default;
	FUniqueNetIdEpic(const FUniqueNetIdEpic&) = default;
	FUniqueNetIdEpic& operator=(FUniqueNetIdEpic&&) = default;
	FUniqueNetIdEpic& operator=(const FUniqueNetIdEpic&) = default;

	virtual ~FUniqueNetIdEpic() = default;

	/** Create a new id from a given string. */
	explicit FUniqueNetIdEpic(const FString& InUniqueNetId)
		: Type(EPIC_SUBSYSTEM)
		, epicAccountId(nullptr)
	{
		check(!InUniqueNetId.IsEmpty());
		this->productUserId = EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*InUniqueNetId));
	}

	/** Create a new id from an existing PUID */
	explicit FUniqueNetIdEpic(const EOS_ProductUserId& InUserId)
		: Type(EPIC_SUBSYSTEM)
		, productUserId(InUserId)
		, epicAccountId(nullptr)
	{
	}
	explicit FUniqueNetIdEpic(EOS_ProductUserId&& InUserId)
		: Type(EPIC_SUBSYSTEM)
		, productUserId(MoveTemp(InUserId))
		, epicAccountId(nullptr)
	{
	}

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
		, epicAccountId(nullptr)
	{
		if (OtherId.GetType() == EPIC_SUBSYSTEM)
		{
			uint8 const* bytes = OtherId.GetBytes();

			EOS_ProductUserId puid = EOS_ProductUserId_FromString((char const*)bytes);
			check(EOS_ProductUserId_IsValid(puid));
			this->productUserId = puid;

			// If the size of the other id is greater than a PUID's max length
			// we assume that OtherId has a valid EAID
			if (OtherId.GetSize() > EOS_PRODUCTUSERID_MAX_LENGTH)
			{
				EOS_EpicAccountId eaid = EOS_EpicAccountId_FromString((char const*)(bytes + EOS_PRODUCTUSERID_MAX_LENGTH));
				check(EOS_EpicAccountId_IsValid(eaid));
				this->epicAccountId = eaid;
			}
		}
		else
		{
			// Construct a PUID from the string representation of OtherId
			// Only guarantees grammatical correctness
			FString idString = OtherId.ToString();
			check(!idString.IsEmpty());

			EOS_ProductUserId puid = ProductUserIDFromString(idString);
			check(EOS_ProductUserId_IsValid(puid));

			this->productUserId = puid;
		}
	}

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
		int32 buffLength = this->GetSize();
		uint8* idData = new uint8[buffLength];
		if (!idData)
		{
			return nullptr;
		}

		// Convert the PUID to a char array with size EOS_PRODUCTUSERID_MAX_LENGTH and store it at position 0
		int32_t puidBufSize = EOS_PRODUCTUSERID_MAX_LENGTH;
		char* puidData = (char*)idData;
		EOS_EResult Result = EOS_ProductUserId_ToString(this->productUserId, puidData, &puidBufSize);

		// Only take the EAID into account if it's valid
		if (buffLength > EOS_PRODUCTUSERID_MAX_LENGTH)
		{
			// Convert the epic account id to a char array with EOS_EPICACCOUNTID_MAX_LENGTH length
			// and store it at position EOS_PRODUCTUSERID_MAX_LENGTH
			int32_t eaidBufSize = EOS_EPICACCOUNTID_MAX_LENGTH;
			char* eaidData = (char*)(idData + EOS_PRODUCTUSERID_MAX_LENGTH);
			Result = EOS_ProductUserId_ToString(this->productUserId, eaidData, &eaidBufSize);
		}

		return (const uint8*)idData;
	}

	virtual int32 GetSize() const override
	{
		// The size of the data is the maximal length of the PUID and the maximal size of the EAID, if the latter is valid.
		return EOS_PRODUCTUSERID_MAX_LENGTH + (this->epicAccountId ? EOS_EPICACCOUNTID_MAX_LENGTH : 0);
	}

	virtual bool IsValid() const override
	{
		return (bool)EOS_ProductUserId_IsValid(this->productUserId);
	}

	/** Check if the underlying EpicAccountId is valid. */
	bool IsEpicAccountIdValid() const
	{
		return (bool)EOS_EpicAccountId_IsValid(this->epicAccountId);
	}

	virtual FString ToString() const override
	{
		return FUniqueNetIdEpic::ProductUserIdToString(this->productUserId);
	}

	virtual FString ToDebugString() const override
	{
		bool puidValid = (bool)EOS_ProductUserId_IsValid(this->productUserId);
		bool eaidValid = (bool)EOS_EpicAccountId_IsValid(this->epicAccountId);

		return FString::Printf(TEXT("PUID: %s; EAID: %s"),
			puidValid ? *FUniqueNetIdEpic::ProductUserIdToString(this->productUserId) : TEXT("INVALID"),
			eaidValid ? *FUniqueNetIdEpic::EpicAccountIdToString(this->epicAccountId) : TEXT("INVALID"));
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
	EOS_ProductUserId ToProdcutUserId() const
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
		// ToDo: Find a better hash representation than just the PUID string
		return ::GetTypeHash(A.ToString());
	}
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
