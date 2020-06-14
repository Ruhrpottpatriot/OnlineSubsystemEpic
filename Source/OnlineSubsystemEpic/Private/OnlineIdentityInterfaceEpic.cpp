#include "OnlineIdentityInterfaceEpic.h"
#include "CoreMinimal.h"
#include "OnlineSubsystemEpic.h"
#include "OnlineError.h"
#include "Utilities.h"
#include "HAL/UnrealMemory.h"

#include "eos_sdk.h"
#include "eos_types.h"
#include "eos_auth.h"

//-------------------------------
// FUserOnlineAccountEpic
//-------------------------------
TSharedRef<const FUniqueNetId> FUserOnlineAccountEpic::GetUserId() const
{
	return this->UserIdPtr;
}

FString FUserOnlineAccountEpic::GetRealName() const
{
	FString realName;
	this->GetUserAttribute(USER_ATTR_REALNAME, realName);
	return realName;
}

FString FUserOnlineAccountEpic::GetDisplayName(const FString& Platform /*= FString()*/) const
{
	FString displayName;
	this->GetUserAttribute(USER_ATTR_DISPLAYNAME, displayName);
	return displayName;

}

FString FUserOnlineAccountEpic::GetAccessToken() const
{
	FString authToken;
	this->GetAuthAttribute(AUTH_ATTR_AUTHORIZATION_CODE, authToken);
	return authToken;
}

bool FUserOnlineAccountEpic::GetUserAttribute(const FString& AttrName, FString& OutAttrValue) const
{
	const FString* FoundAttr = UserAttributes.Find(AttrName);
	if (FoundAttr != nullptr)
	{
		OutAttrValue = *FoundAttr;
		return true;
	}
	return false;
}

bool FUserOnlineAccountEpic::SetUserAttribute(const FString& AttrName, const FString& AttrValue)
{
	const FString* FoundAttr = UserAttributes.Find(AttrName);
	if (FoundAttr == nullptr || *FoundAttr != AttrValue)
	{
		UserAttributes.Add(AttrName, AttrValue);
		return true;
	}
	return false;
}

bool FUserOnlineAccountEpic::GetAuthAttribute(const FString& AttrName, FString& OutAttrValue) const
{
	// Handle special eos sdk token types
	if (AttrName == AUTH_ATTR_EA_TOKEN)
	{
		TSharedPtr<const FUniqueNetIdEpic> idPtr = StaticCastSharedPtr<const FUniqueNetIdEpic, const FUniqueNetId, ESPMode::NotThreadSafe>(this->UserIdPtr);
		if (idPtr)
		{
			OutAttrValue = FUniqueNetIdEpic::EpicAccountIdToString(idPtr->ToEpicAccountId());
			return true;
		}
		return false;
	}

	if (AttrName == AUTH_ATTR_ID_TOKEN)
	{
		OutAttrValue = this->UserIdPtr->ToString();
		return true;
	}

	const FString* FoundAttr = AdditionalAuthData.Find(AttrName);
	if (FoundAttr != nullptr)
	{
		OutAttrValue = *FoundAttr;
		return true;
	}
	return false;
}

bool FUserOnlineAccountEpic::SetAuthAttribute(const FString& AttrName, const FString& AttrValue)
{
	this->AdditionalAuthData.Add(AttrName, AttrValue);
	return true;
}


// ---------------------------------------------
// Implementation file only structs
// These structs carry additional informations to the callbacks
// ---------------------------------------------
typedef struct FLoginCompleteAdditionalData
{
	FOnlineIdentityInterfaceEpic* IdentityInterface;
	int32 LocalUserNum;
	EOS_EpicAccountId EpicAccountId;
} FAuthLoginCompleteAdditionalData;

// -----------------------------
// EOS Callbacks
// -----------------------------
void FOnlineIdentityInterfaceEpic::EOS_Auth_OnLoginComplete(EOS_Auth_LoginCallbackInfo const* Data)
{
	// To raise the login complete delegates the interface itself has to be retrieved from the returned data
	FLoginCompleteAdditionalData* additionalData = (FLoginCompleteAdditionalData*)Data->ClientData;

	FOnlineIdentityInterfaceEpic* thisPtr = additionalData->IdentityInterface;
	check(thisPtr);

	FString error;

	if (Data->ResultCode == EOS_EResult::EOS_Success)
	{
		EOS_EpicAccountId eosId = EOS_Auth_GetLoggedInAccountByIndex(thisPtr->authHandle, additionalData->LocalUserNum);
		if (EOS_EpicAccountId_IsValid(eosId))
		{
			EOS_Auth_Token* authToken = nullptr;

			EOS_Auth_CopyUserAuthTokenOptions copyAuthTopkenOptions = {
				EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST
			};
			EOS_Auth_CopyUserAuthToken(thisPtr->authHandle, &copyAuthTopkenOptions, eosId, &authToken);

			EOS_Connect_Credentials connectCrendentials = {
				EOS_CONNECT_CREDENTIALS_API_LATEST,
				authToken->AccessToken,
				EOS_EExternalCredentialType::EOS_ECT_EPIC
			};
			EOS_Connect_LoginOptions loginOptions = {
				EOS_CONNECT_LOGIN_API_LATEST,
				&connectCrendentials,
				nullptr
			};
			EOS_Connect_Login(thisPtr->connectHandle, &loginOptions, thisPtr, &FOnlineIdentityInterfaceEpic::EOS_Connect_OnLoginComplete);

			// Release the auth token
			EOS_Auth_Token_Release(authToken);
		}
		else
		{
			error = TEXT("[EOS SDK] Invalid epic user id");
		}
	}
	else
	{
		error = FString::Printf(TEXT("[EOS SDK] Login Failed - Error Code: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
	}

	// Abort if there was an error
	if (!error.IsEmpty())
	{
		UE_LOG_ONLINE_IDENTITY(Warning, TEXT("Epic Account Service Login failed. Message:\r\n    %s"), *error);
		thisPtr->TriggerOnLoginCompleteDelegates(0, false, FUniqueNetIdEpic(), error);
	}

	delete(additionalData);
}

void FOnlineIdentityInterfaceEpic::EOS_Connect_OnLoginComplete(EOS_Connect_LoginCallbackInfo const* Data)
{
	FLoginCompleteAdditionalData* additionalData = (FLoginCompleteAdditionalData*)Data->ClientData;

	FOnlineIdentityInterfaceEpic* thisPtr = additionalData->IdentityInterface;
	check(thisPtr);

	FString error;

	FUniqueNetIdEpic userId;
	EOS_EResult eosResult = Data->ResultCode;
	if (eosResult == EOS_EResult::EOS_Success)
	{
		userId = FUniqueNetIdEpic(Data->LocalUserId, additionalData->EpicAccountId);
		UE_LOG_ONLINE_IDENTITY(Display, TEXT("Finished logging in user \"%s\""), UTF8_TO_TCHAR(Data
			->LocalUserId));
	}
	else if (eosResult == EOS_EResult::EOS_InvalidUser)
	{
		if (Data->ContinuanceToken)
		{
			// ToDo: FInd a way to let the user either create or link a user account.
			error = TEXT("[EOS SDK] Got invalid user, but no continuance token. Creating user account?");
		}
		else
		{
			error = TEXT("[EOS SDK] Got invalid user, but no continuance token.");
		}
	}
	else
	{
		error = FString::Printf(TEXT("[EOS SDK] Login Failed - Error Code: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
	}

	if (!error.IsEmpty())
	{
		UE_LOG_ONLINE_IDENTITY(Warning, TEXT("%s encountered an error. Message:\r\n    %s"), *FString(__FUNCTION__), *error);
		thisPtr->TriggerOnLoginCompleteDelegates(additionalData->LocalUserNum, false, FUniqueNetIdEpic(), error);
	}
	else
	{
		thisPtr->TriggerOnLoginCompleteDelegates(additionalData->LocalUserNum, true, userId, FString());
	}

	delete(additionalData);
}

void FOnlineIdentityInterfaceEpic::EOS_Connect_OnAuthExpiration(EOS_Connect_AuthExpirationCallbackInfo const* Data)
{
	FOnlineIdentityInterfaceEpic* thisPtr = (FOnlineIdentityInterfaceEpic*)Data->ClientData;

	thisPtr->TriggerOnLoginFlowLogoutDelegates();

	// ToDo: Make the user see this.
	UE_LOG_ONLINE_IDENTITY(Display, TEXT("Auth for user \"%s\" expired"), UTF8_TO_TCHAR(Data->LocalUserId));
}

void FOnlineIdentityInterfaceEpic::EOS_Connect_OnLoginStatusChanged(EOS_Connect_LoginStatusChangedCallbackInfo const* Data)
{
	FOnlineIdentityInterfaceEpic* thisPtr = (FOnlineIdentityInterfaceEpic*)Data->ClientData;

	FString localUser = UTF8_TO_TCHAR(Data->LocalUserId);
	ELoginStatus::Type oldStatus = thisPtr->EOSLoginStatusToUELoginStatus(Data->PreviousStatus);
	ELoginStatus::Type newStatus = thisPtr->EOSLoginStatusToUELoginStatus(Data->CurrentStatus);

	UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Login status changed.\r\n%9s: %s\r\n%9s: %s\r\n%9s: %s"), TEXT("User"), localUser, TEXT("New State"), ELoginStatus::Type(newStatus), TEXT("Old State"), ELoginStatus::ToString(oldStatus));

	FUniqueNetIdEpic netId = FUniqueNetIdEpic(localUser);
	FPlatformUserId localUserNum = thisPtr->GetPlatformUserIdFromUniqueNetId(netId);

	thisPtr->TriggerOnLoginStatusChangedDelegates(localUserNum, oldStatus, newStatus, netId);
}

void FOnlineIdentityInterfaceEpic::EOS_Auth_OnLogoutComplete(const EOS_Auth_LogoutCallbackInfo* Data)
{
	if (Data->ResultCode != EOS_EResult::EOS_Success)
	{
		char const* resultStr = EOS_EResult_ToString(Data->ResultCode);
		UE_LOG_ONLINE_IDENTITY(Warning, TEXT("[EOS SDK] Logout Failed - User: %s, Result : %s"), UTF8_TO_TCHAR(Data->LocalUserId), resultStr);
		return;
	}

	FOnlineIdentityInterfaceEpic* thisPtr = (FOnlineIdentityInterfaceEpic*)Data->ClientData;
	check(thisPtr);

	int32 idIdx = thisPtr->GetPlatformUserIdFromUniqueNetId(FUniqueNetIdEpic(UTF8_TO_TCHAR(Data->LocalUserId)));

	thisPtr->TriggerOnLogoutCompleteDelegates(idIdx, true);
	UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logout Complete - User: %s"), UTF8_TO_TCHAR(Data->LocalUserId));
}

//-------------------------------
// FOnlineIdentityInterfaceEpic
//-------------------------------
FOnlineIdentityInterfaceEpic::FOnlineIdentityInterfaceEpic(FOnlineSubsystemEpic* inSubsystem)
	: subsystemEpic(inSubsystem)
{
	this->authHandle = EOS_Platform_GetAuthInterface(inSubsystem->PlatformHandle);
	this->connectHandle = EOS_Platform_GetConnectInterface(inSubsystem->PlatformHandle);

	EOS_Connect_AddNotifyAuthExpirationOptions expirationOptions = {
	};
	this->notifyAuthExpiration = EOS_Connect_AddNotifyAuthExpiration(this->connectHandle, &expirationOptions, this, &FOnlineIdentityInterfaceEpic::EOS_Connect_OnAuthExpiration);

	EOS_Connect_AddNotifyLoginStatusChangedOptions loginStatusChangedOptions = {
		EOS_CONNECT_ADDNOTIFYLOGINSTATUSCHANGED_API_LATEST
	};
	this->notifyLoginStatusChangedId = EOS_Connect_AddNotifyLoginStatusChanged(this->connectHandle, &loginStatusChangedOptions, this, &FOnlineIdentityInterfaceEpic::EOS_Connect_OnLoginStatusChanged);
}

FOnlineIdentityInterfaceEpic::~FOnlineIdentityInterfaceEpic()
{
	EOS_Connect_RemoveNotifyLoginStatusChanged(this->connectHandle, this->notifyLoginStatusChangedId);
	EOS_Connect_RemoveNotifyAuthExpiration(this->connectHandle, this->notifyAuthExpiration);
}

bool FOnlineIdentityInterfaceEpic::Login(int32 LocalUserNum, const FOnlineAccountCredentials& AccountCredentials)
{
	FString error;
	bool success = false;

	if (0 <= LocalUserNum && LocalUserNum < MAX_LOCAL_PLAYERS)
	{
		// Check if we are using the epic account system or plain connect
		FString left, right;
		AccountCredentials.Type.Split(TEXT(":"), &left, &right);
		if (left.Equals(TEXT("EAS"), ESearchCase::IgnoreCase))
		{
			EOS_EpicAccountId eosId = EOS_Auth_GetLoggedInAccountByIndex(this->authHandle, LocalUserNum);

			// If we already have a valid EAID, we can go straight to connect
			if (EOS_EpicAccountId_IsValid(eosId))
			{
				EOS_Auth_Token* authToken = nullptr;

				EOS_Auth_CopyUserAuthTokenOptions copyAuthTopkenOptions = {
					EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST
				};
				EOS_Auth_CopyUserAuthToken(this->authHandle, &copyAuthTopkenOptions, eosId, &authToken);

				EOS_Connect_Credentials connectCrendentials = {
					EOS_CONNECT_CREDENTIALS_API_LATEST,
					authToken->AccessToken,
					EOS_EExternalCredentialType::EOS_ECT_EPIC
				};
				EOS_Connect_LoginOptions loginOptions = {
					EOS_CONNECT_LOGIN_API_LATEST,
					&connectCrendentials,
					nullptr
				};
				EOS_Connect_Login(this->connectHandle, &loginOptions, this, &FOnlineIdentityInterfaceEpic::EOS_Connect_OnLoginComplete);

				// Release the auth token
				EOS_Auth_Token_Release(authToken);
			}
			// In any other case we call the epic account endpoint
			// and handle login in the callback
			else
			{
				// Create credentials
				EOS_Auth_Credentials credentials = {};
				credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
				ELoginType const loginType = GetEnumValueFromString<ELoginType>("ELoginType", right);
				char const* idPtr = TCHAR_TO_ANSI(*AccountCredentials.Id);
				char const* tokenPtr = TCHAR_TO_ANSI(*AccountCredentials.Token);

				switch (loginType)
				{
				case ELoginType::Password:
				{
					UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logging In as User Id: %s"), idPtr);
					credentials.Id = idPtr;
					credentials.Token = tokenPtr;
					credentials.Type = EOS_ELoginCredentialType::EOS_LCT_Password;
					break;
				}
				case ELoginType::ExchangeCode:
				{
					UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logging In with Exchange Code"));
					credentials.Token = idPtr;
					credentials.Type = EOS_ELoginCredentialType::EOS_LCT_ExchangeCode;
					break;
				}
				case ELoginType::DeviceCode:
				{
					UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logging In with Device Code"));
					credentials.Type = EOS_ELoginCredentialType::EOS_LCT_DeviceCode;
					break;
				}
				case ELoginType::Developer:
				{
					FString hostStr = FString::Printf(TEXT("127.0.0.1:%d"), this->subsystemEpic->devToolPort);

					UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logging In with Host: %s"), *hostStr);
					credentials.Id = TCHAR_TO_ANSI(*hostStr);
					credentials.Token = idPtr;
					credentials.Type = EOS_ELoginCredentialType::EOS_LCT_Developer;
					break;
				}
				case ELoginType::AccountPortal:
				{
					UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logging In with Account Portal"));
					credentials.Type = EOS_ELoginCredentialType::EOS_LCT_AccountPortal;
					break;
				}
				case ELoginType::PersistentAuth:
				{
					UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logging In with Persistent Auth"));
					credentials.Type = EOS_ELoginCredentialType::EOS_LCT_PersistentAuth;
					break;
				}
				default:
				{
					UE_LOG_ONLINE_IDENTITY(Fatal, TEXT("Login of type %s not supported"), *AccountCredentials.Type);
					return false;
				}
				}

				EOS_Auth_LoginOptions loginOpts = {};
				loginOpts.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
				loginOpts.ScopeFlags = EOS_EAuthScopeFlags::EOS_AS_BasicProfile | EOS_EAuthScopeFlags::EOS_AS_FriendsList | EOS_EAuthScopeFlags::EOS_AS_Presence;
				loginOpts.Credentials = &credentials;

				FLoginCompleteAdditionalData* additionalData = new FLoginCompleteAdditionalData{
					this,
					LocalUserNum
				};
				EOS_Auth_Login(authHandle, &loginOpts, additionalData, &FOnlineIdentityInterfaceEpic::EOS_Auth_OnLoginComplete);
			}
			success = true;
		}
		else if (left.Equals(TEXT("CONNECT"), ESearchCase::IgnoreCase))
		{
			EOS_EExternalCredentialType externalType = EOS_EExternalCredentialType::EOS_ECT_EPIC;
			if (right.Equals(TEXT("steam"), ESearchCase::IgnoreCase))
			{
				externalType = EOS_EExternalCredentialType::EOS_ECT_STEAM_APP_TICKET;
			}
			else if (right.Equals(TEXT("psn"), ESearchCase::IgnoreCase))
			{
				externalType = EOS_EExternalCredentialType::EOS_ECT_PSN_ID_TOKEN;
			}
			else if (right.Equals(TEXT("xbl"), ESearchCase::IgnoreCase))
			{
				externalType = EOS_EExternalCredentialType::EOS_ECT_XBL_XSTS_TOKEN;
			}
			else if (right.Equals(TEXT("discord"), ESearchCase::IgnoreCase))
			{
				externalType = EOS_EExternalCredentialType::EOS_ECT_DISCORD_ACCESS_TOKEN;
			}
			else if (right.Equals(TEXT("gog"), ESearchCase::IgnoreCase))
			{
				externalType = EOS_EExternalCredentialType::EOS_ECT_GOG_SESSION_TICKET;
			}
			else if (right.Equals(TEXT("nintendo_id"), ESearchCase::IgnoreCase))
			{
				externalType = EOS_EExternalCredentialType::EOS_ECT_NINTENDO_ID_TOKEN;
			}
			else if (right.Equals(TEXT("nintendo_nsa"), ESearchCase::IgnoreCase))
			{
				externalType = EOS_EExternalCredentialType::EOS_ECT_NINTENDO_NSA_ID_TOKEN;
			}
			else if (right.Equals(TEXT("uplay"), ESearchCase::IgnoreCase))
			{
				externalType = EOS_EExternalCredentialType::EOS_ECT_UPLAY_ACCESS_TOKEN;
			}
			else if (right.Equals(TEXT("openid"), ESearchCase::IgnoreCase))
			{
				externalType = EOS_EExternalCredentialType::EOS_ECT_OPENID_ACCESS_TOKEN;
			}
			else if (right.Equals(TEXT("device"), ESearchCase::IgnoreCase))
			{
				externalType = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
			}
			else if (right.Equals(TEXT("apple"), ESearchCase::IgnoreCase))
			{
				externalType = EOS_EExternalCredentialType::EOS_ECT_APPLE_ID_TOKEN;
			}

			// Make sure we have a valid connect type
			if (externalType != EOS_EExternalCredentialType::EOS_ECT_EPIC)
			{
				EOS_Connect_Credentials connectCrendentials = {
					   EOS_CONNECT_CREDENTIALS_API_LATEST,
					   TCHAR_TO_UTF8(*AccountCredentials.Token),
					   externalType
				};

				// We need to check which external credentials type is used,
				// as Apple and Nintendo require additional data.
				EOS_Connect_LoginOptions loginOptions;
				if (externalType == EOS_EExternalCredentialType::EOS_ECT_APPLE_ID_TOKEN
					|| externalType == EOS_EExternalCredentialType::EOS_ECT_NINTENDO_ID_TOKEN
					|| externalType == EOS_EExternalCredentialType::EOS_ECT_NINTENDO_NSA_ID_TOKEN)
				{
					EOS_Connect_UserLoginInfo loginInfo = {
						EOS_CONNECT_USERLOGININFO_API_LATEST,
						TCHAR_TO_UTF8(*AccountCredentials.Id)
					};
					loginOptions = {
					   EOS_CONNECT_LOGIN_API_LATEST,
					   &connectCrendentials,
					   &loginInfo
					};
				}
				else
				{
					loginOptions = {
					   EOS_CONNECT_LOGIN_API_LATEST,
					   &connectCrendentials,
					   nullptr
					};
				}

				EOS_Connect_Login(this->connectHandle, &loginOptions, this, &FOnlineIdentityInterfaceEpic::EOS_Connect_OnLoginComplete);

				success = true;
			}
			else
			{
				error == FString::Printf(TEXT("\"%s\" is not a recognized connect type"), *right);
			}
		}
		else
		{
			error = FString::Printf(TEXT("\"%s\" is not a recognized login flow."), *left);
		}
	}
	else
	{
		error = FString::Printf(TEXT("\"%d\" is outside the range of allowed user indices [0 - %d["), LocalUserNum, MAX_LOCAL_PLAYERS);
	}

	if (!success)
	{
		UE_CLOG_ONLINE_IDENTITY(!error.IsEmpty(), Warning, TEXT("%s encountered an error. Message:\r\n    %s"), *FString(__FUNCTION__), *error);
		this->TriggerOnLoginCompleteDelegates(LocalUserNum, false, FUniqueNetIdEpic(), error);
	}

	return success;
}

bool FOnlineIdentityInterfaceEpic::AutoLogin(int32 LocalUserNum)
{
	FOnlineAccountCredentials credentials;
	credentials.Type = GetEnumValueAsString<ELoginType>("ELoginType", ELoginType::PersistentAuth);

	return Login(LocalUserNum, credentials);
}

TSharedPtr<const FUniqueNetId> FOnlineIdentityInterfaceEpic::CreateUniquePlayerId(const FString& Str)
{
	return MakeShared<FUniqueNetIdEpic>(Str);
}

TSharedPtr<const FUniqueNetId> FOnlineIdentityInterfaceEpic::CreateUniquePlayerId(uint8* Bytes, int32 Size)
{
	if (Bytes && Size > 0)
	{
		FString StrId(Size, (TCHAR*)Bytes);
		return this->CreateUniquePlayerId(StrId);
	}
	return nullptr;
}

TArray<TSharedPtr<FUserOnlineAccount>> FOnlineIdentityInterfaceEpic::GetAllUserAccounts() const
{
	TArray<TSharedPtr< FUserOnlineAccount>> accounts;

	int32 loggedInCount = EOS_Connect_GetLoggedInUsersCount(this->connectHandle);
	for (int32 i = 0; i < loggedInCount; ++i)
	{
		EOS_ProductUserId puid = EOS_Connect_GetLoggedInUserByIndex(this->connectHandle, i);

		TSharedPtr<FUserOnlineAccount> userAccount = this->OnlineUserAcccountFromPUID(puid);

		accounts.Add(userAccount);
	}
	return accounts;
}

TSharedPtr<FUserOnlineAccount> FOnlineIdentityInterfaceEpic::GetUserAccount(const FUniqueNetId& UserId) const
{
	TSharedRef<FUniqueNetIdEpic const> epicNetId = StaticCastSharedRef<FUniqueNetIdEpic const>(UserId.AsShared());
	EOS_ProductUserId puid = epicNetId->ToProdcutUserId();

	return this->OnlineUserAcccountFromPUID(puid);
}

FString FOnlineIdentityInterfaceEpic::GetAuthToken(int32 LocalUserNum) const
{
	TSharedPtr<const FUniqueNetId> id = this->GetUniquePlayerId(LocalUserNum);
	if (id.IsValid())
	{
		TSharedPtr<FUserOnlineAccount> acc = this->GetUserAccount(*id);
		if (acc.IsValid())
		{
			return acc->GetAccessToken();
		}
	}
	return FString();
}

FString FOnlineIdentityInterfaceEpic::GetAuthType() const
{
	return EPIC_SUBSYSTEM.ToString();
}

ELoginStatus::Type FOnlineIdentityInterfaceEpic::GetLoginStatus(const FUniqueNetId& UserId) const
{
	FUniqueNetIdEpic epicUserId = (FUniqueNetIdEpic)UserId;
	if (epicUserId.IsValid())
	{
		EOS_ELoginStatus loginStatus = EOS_Connect_GetLoginStatus(this->connectHandle, epicUserId.ToProdcutUserId());
		switch (loginStatus)
		{
		case EOS_ELoginStatus::EOS_LS_NotLoggedIn:
			return  ELoginStatus::NotLoggedIn;
		case EOS_ELoginStatus::EOS_LS_UsingLocalProfile:
			return ELoginStatus::UsingLocalProfile;
		case EOS_ELoginStatus::EOS_LS_LoggedIn:
			return ELoginStatus::LoggedIn;
		default:
			checkNoEntry();
		}
	}
	return ELoginStatus::NotLoggedIn;
}

ELoginStatus::Type FOnlineIdentityInterfaceEpic::GetLoginStatus(int32 LocalUserNum) const
{
	TSharedPtr<const FUniqueNetId> id = this->GetUniquePlayerId(LocalUserNum);
	if (id.IsValid())
	{
		return this->GetLoginStatus(*id);
	}
	return ELoginStatus::NotLoggedIn;
}

FPlatformUserId FOnlineIdentityInterfaceEpic::GetPlatformUserIdFromUniqueNetId(const FUniqueNetId& UniqueNetId) const
{
	for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i)
	{
		auto id = this->GetUniquePlayerId(i);
		if (id.IsValid() && (*id == UniqueNetId))
		{
			return i;
		}
	}

	return PLATFORMUSERID_NONE;
}

FString FOnlineIdentityInterfaceEpic::GetPlayerNickname(const FUniqueNetId& UserId) const
{
	TSharedPtr<FUserOnlineAccount> acc = this->GetUserAccount(UserId);
	if (!acc.IsValid())
	{
		return TEXT("");
	}
	return acc->GetDisplayName();
}

FString FOnlineIdentityInterfaceEpic::GetPlayerNickname(int32 LocalUserNum) const
{
	TSharedPtr<const FUniqueNetId> id = this->GetUniquePlayerId(LocalUserNum);
	if (!id.IsValid())
	{
		return TEXT("");
	}
	return this->GetPlayerNickname(*id);
}

TSharedPtr<const FUniqueNetId> FOnlineIdentityInterfaceEpic::GetUniquePlayerId(int32 LocalUserNum) const
{
	EOS_ProductUserId puid = EOS_Connect_GetLoggedInUserByIndex(this->connectHandle, LocalUserNum);
	EOS_EpicAccountId eaid = EOS_Auth_GetLoggedInAccountByIndex(this->authHandle, LocalUserNum);

	if (EOS_ProductUserId_IsValid(puid))
	{
		// We don't care if the EAID is invalid
		return MakeShared<FUniqueNetIdEpic>(puid, eaid);
	}
	return nullptr;
}

void FOnlineIdentityInterfaceEpic::GetUserPrivilege(const FUniqueNetId& LocalUserId, EUserPrivileges::Type Privilege, const FOnGetUserPrivilegeCompleteDelegate& Delegate)
{
	// ToDo: Implement actual check against the backend
	Delegate.ExecuteIfBound(LocalUserId, Privilege, (uint32)EPrivilegeResults::NoFailures);
}

bool FOnlineIdentityInterfaceEpic::Logout(int32 LocalUserNum)
{
	FString error;

	TSharedPtr<const FUniqueNetIdEpic> id = StaticCastSharedPtr<const FUniqueNetIdEpic>(this->GetUniquePlayerId(LocalUserNum));
	if (id && id->IsValid())
	{
		EOS_EpicAccountId eaid = id->ToEpicAccountId();
		if (EOS_EpicAccountId_IsValid(eaid))
		{

			EOS_Auth_LogoutOptions logoutOpts = {
				EOS_AUTH_LOGOUT_API_LATEST,
				eaid
			};

			EOS_EpicAccountId epicId = EOS_Auth_GetLoggedInAccountByIndex(authHandle, LocalUserNum);
			logoutOpts.LocalUserId = epicId;
			EOS_Auth_Logout(authHandle, &logoutOpts, this, &FOnlineIdentityInterfaceEpic::EOS_Auth_OnLogoutComplete);
		}
		else
		{
			UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] No valid epic account id for logout found."));
		}

		// Remove the user id from the local cache
		TSharedRef<const FUniqueNetIdEpic> idRef = id.ToSharedRef();
		///this->userAccounts.Remove(idRef);
	}
	else
	{
		error = FString::Printf(TEXT("%s: No valid user id found for local user %d."), *FString(__FUNCTION__), LocalUserNum);
	}

	if (!error.IsEmpty())
	{
		UE_LOG_ONLINE_IDENTITY(Warning, TEXT("%s encountered an error. Message:\r\n    %s"), *FString(__FUNCTION__), *error);
		TriggerOnLogoutCompleteDelegates(LocalUserNum, false);
	}

	return error.IsEmpty();
}

void FOnlineIdentityInterfaceEpic::RevokeAuthToken(const FUniqueNetId& LocalUserId, const FOnRevokeAuthTokenCompleteDelegate& Delegate)
{
	UE_LOG_ONLINE_IDENTITY(Fatal, TEXT("FOnlineIdentityInterfaceEpic::RevokeAuthToken not implemented"));
}

//-------------------------------
// Utility Methods
//-------------------------------

TSharedPtr<FUserOnlineAccount> FOnlineIdentityInterfaceEpic::OnlineUserAcccountFromPUID(EOS_ProductUserId const& puid) const
{
	EOS_Connect_ExternalAccountInfo* externalAccountInfo = nullptr;

	EOS_Connect_CopyProductUserInfoOptions copyUserInfoOptions = {
		EOS_CONNECT_COPYPRODUCTUSERINFO_API_LATEST,
		puid
	};
	EOS_Connect_CopyProductUserInfo(this->connectHandle, &copyUserInfoOptions, &externalAccountInfo);

	TSharedPtr<FUserOnlineAccountEpic> userAccount = nullptr;

	// Check if this user account is also owned by EPIC and if, make calls to the Auth interface
	if (externalAccountInfo->AccountIdType == EOS_EExternalAccountType::EOS_EAT_EPIC)
	{
		EOS_EpicAccountId eaid = EOS_EpicAccountId_FromString(externalAccountInfo->AccountId);
		if (EOS_EpicAccountId_IsValid(eaid))
		{
			EOS_Auth_Token* authToken = nullptr;

			EOS_Auth_CopyUserAuthTokenOptions copyUserAuthTokenOptions = {
				EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST
			};
			EOS_EResult eosResult = EOS_Auth_CopyUserAuthToken(this->authHandle, &copyUserAuthTokenOptions, eaid, &authToken);
			if (eosResult == EOS_EResult::EOS_Success)
			{
				TSharedRef<FUniqueNetId> netid = MakeShared<FUniqueNetIdEpic>(puid, eaid);
				userAccount = MakeShared<FUserOnlineAccountEpic>(netid);

				userAccount->SetAuthAttribute(AUTH_ATTR_REFRESH_TOKEN, UTF8_TO_TCHAR(authToken->RefreshToken));
				// ToDo: Discussion needs to happen, what else to include into the UserOnlineAccount
			}
			else
			{
				UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] No auth token for EAID %s"), UTF8_TO_TCHAR(externalAccountInfo->AccountId));
			}

			EOS_Auth_Token_Release(authToken);
		}
		else
		{
			UE_LOG_ONLINE_IDENTITY(Display, TEXT("External account id was epic, but account id is invalid"));
		}
	}

	// Check if we already created a user account with EPIC data
	if (!userAccount)
	{
		TSharedRef<FUniqueNetId> netid = MakeShared<FUniqueNetIdEpic>(puid);
		userAccount = MakeShared<FUserOnlineAccountEpic>(netid);
	}

	userAccount->SetUserAttribute(USER_ATTR_DISPLAYNAME, UTF8_TO_TCHAR(externalAccountInfo->DisplayName));
	userAccount->SetUserLocalAttribute(USER_ATTR_LAST_LOGIN_TIME, FString::Printf(TEXT("%s"), externalAccountInfo->LastLoginTime));

	EOS_Connect_ExternalAccountInfo_Release(externalAccountInfo);

	return userAccount;
}

ELoginStatus::Type FOnlineIdentityInterfaceEpic::EOSLoginStatusToUELoginStatus(EOS_ELoginStatus LoginStatus)
{
	switch (LoginStatus)
	{
	case EOS_ELoginStatus::EOS_LS_NotLoggedIn:
		return ELoginStatus::NotLoggedIn;
	case EOS_ELoginStatus::EOS_LS_UsingLocalProfile:
		return ELoginStatus::UsingLocalProfile;
	case EOS_ELoginStatus::EOS_LS_LoggedIn:
		return ELoginStatus::LoggedIn;
	default:
		checkNoEntry();
	}

	return ELoginStatus::NotLoggedIn;
}
