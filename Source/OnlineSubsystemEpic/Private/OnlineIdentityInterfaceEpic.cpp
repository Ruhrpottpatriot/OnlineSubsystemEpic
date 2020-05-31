#include "OnlineIdentityInterfaceEpic.h"
#include "OnlineSubsystemEpic.h"
#include "OnlineError.h"
#include "Utilities.h"
#include "eos_sdk.h"
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

//-------------------------------
// FOnlineIdentityInterfaceEpic
//-------------------------------
bool FOnlineIdentityInterfaceEpic::Login(int32 LocalUserNum, const FOnlineAccountCredentials& AccountCredentials)
{
	FString errorStr;
	TSharedPtr<FUserOnlineAccountEpic> userAccPtr;

	if (LocalUserNum < 0 || LocalUserNum >= MAX_LOCAL_PLAYERS)
	{
		errorStr = FString::Printf(TEXT("Invalid LocalUserNum=%d"), LocalUserNum);
	}
	else if (AccountCredentials.Id.IsEmpty())
	{
		errorStr = TEXT("Invalid account id, string empty");
	}
	else
	{
		EOS_HAuth authHandle = this->GetEOSAuthHandle();

		// Check if the local user is already logged in
		EOS_EpicAccountId eosId = EOS_Auth_GetLoggedInAccountByIndex(authHandle, LocalUserNum);
		if (eosId)
		{
			if (!EOS_EpicAccountId_IsValid(eosId))
			{
				errorStr = FString::Printf(TEXT("Got account id for user %d, but id is invalid"), LocalUserNum);
			}
			else
			{
				FUniqueNetIdEpic id = FUniqueNetIdEpic(FIdentityUtilities::EpicAccountIDToString(eosId));
				TSharedRef<FUserOnlineAccountEpic>* TempPtr = userAccounts.Find(id);
				if (TempPtr)
				{
					userAccPtr = *TempPtr;
				}
				else
				{
					errorStr = TEXT("Got valid user id, but user info is invalid");
				}
			}
		}
		else
		{
			// Create credentials	
			EOS_Auth_Credentials credentials = {};
			credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;

			ELoginType::Type loginType = ELoginType::FromString(AccountCredentials.Type);
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
			case ELoginType::PersistentAuth:
			{
				UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logging In with Persistent Auth"));
				credentials.Type = EOS_ELoginCredentialType::EOS_LCT_PersistentAuth;
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
				credentials.Token = TCHAR_TO_ANSI(*AccountCredentials.Id);
				credentials.Type = EOS_ELoginCredentialType::EOS_LCT_Developer;
				break;
			}
			case ELoginType::AccountPortal:
			{
				UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logging In with Account Portal"));
				credentials.Type = EOS_ELoginCredentialType::EOS_LCT_AccountPortal;
				break;
			}
			default:
			{
				UE_LOG_ONLINE_IDENTITY(Fatal, TEXT("Login of type %s not supported"), *AccountCredentials.Type);
				return false;
			}
			}

			EOS_Auth_LoginOptions loginOpts;
			memset(&loginOpts, 0, sizeof(loginOpts));
			loginOpts.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
			loginOpts.ScopeFlags = EOS_EAuthScopeFlags::EOS_AS_BasicProfile | EOS_EAuthScopeFlags::EOS_AS_FriendsList | EOS_EAuthScopeFlags::EOS_AS_Presence;
			loginOpts.Credentials = &credentials;

			EOS_Auth_Login(authHandle, &loginOpts, this, &FOnlineIdentityInterfaceEpic::LoginCompleteCallbackFunc);
		}
	}

	if (!errorStr.IsEmpty())
	{
		UE_LOG_ONLINE_IDENTITY(Warning, TEXT("Login request failed. %s"), *errorStr);
		TriggerOnLoginCompleteDelegates(LocalUserNum, false, FUniqueNetIdEpic(), errorStr);
		return false;
	}

	if (userAccPtr)
	{
		TriggerOnLoginCompleteDelegates(LocalUserNum, true, *userAccPtr->GetUserId(), TEXT(""));
	}
	return true;
}

void FOnlineIdentityInterfaceEpic::LoginCompleteCallbackFunc(const EOS_Auth_LoginCallbackInfo* Data)
{
	// Make sure the received data is valid on a low level
	check(Data);

	// To raise the login complete delegates the interface itself has to be retrieved from the returned data
	FOnlineIdentityInterfaceEpic* thisPtr = (FOnlineIdentityInterfaceEpic*)Data->ClientData;
	check(thisPtr);

	// Get a handle to the auth interface for some convenience functions
	EOS_HAuth authHandle = EOS_Platform_GetAuthInterface(thisPtr->subsystemEpic->PlatformHandle);
	check(authHandle);

	// Transform the retrieved ID into a UniqueNetId
	FUniqueNetIdEpic id = FUniqueNetIdEpic(FIdentityUtilities::EpicAccountIDToString(Data->LocalUserId));

	FString errorString;
	int32 localIdx;
	if (Data->ResultCode == EOS_EResult::EOS_Success)
	{
		int32 const accCount = EOS_Auth_GetLoggedInAccountsCount(authHandle);
		for (localIdx = 0; localIdx < accCount; ++localIdx)
		{
			EOS_EpicAccountId eosID = EOS_Auth_GetLoggedInAccountByIndex(authHandle, localIdx);
			FUniqueNetIdEpic epicId = FUniqueNetIdEpic(FIdentityUtilities::EpicAccountIDToString(eosID));
			if (id == epicId)
			{
				break;
			}
		}

		// ToDo: This only holds the most basic information about an account, since EOS doesn't give us anything else
		TSharedRef<FUserOnlineAccountEpic> userPtr = MakeShareable(new FUserOnlineAccountEpic(id));
		thisPtr->userAccounts.Add(id, userPtr);

		UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Login Complete - User ID: %s"), *id.ToString());
	}
	else if (Data->ResultCode == EOS_EResult::EOS_OperationWillRetry)
	{
		UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Login retrying..."));
		return;
	}
	else
	{
		localIdx = INDEX_NONE;
		//ToDo: Implement PinGrantCode and MFA
		auto resultStr = ANSI_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode));
		errorString = FString::Printf(TEXT("[EOS SDK] Login Failed - Error Code: %s"), resultStr);
	}

	if (!errorString.IsEmpty())
	{
		UE_LOG_ONLINE_IDENTITY(Warning, TEXT("Login request failed. %s"), *errorString);
		thisPtr->TriggerOnLoginCompleteDelegates(localIdx, false, FUniqueNetIdEpic(), errorString);
	}
	else
	{
		thisPtr->TriggerOnLoginCompleteDelegates(localIdx, true, id, TEXT(""));
	}
}

FOnlineIdentityInterfaceEpic::FOnlineIdentityInterfaceEpic(FOnlineSubsystemEpic* inSubsystem)
	: subsystemEpic(inSubsystem)
{
}

bool FOnlineIdentityInterfaceEpic::AutoLogin(int32 LocalUserNum)
{
	if (LocalUserNum != 0) {
		UE_LOG_ONLINE_IDENTITY(Fatal, TEXT("FOnlineIdentityInterfaceEpic::AutoLogin not implemented for more than 1 local user."));
		return false;
	}

	EOS_Auth_Credentials Credentials;
	Credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
	Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_PersistentAuth;
	Credentials.Id = NULL;
	Credentials.Token = NULL;

	EOS_Auth_LoginOptions LoginOptions;
	memset(&LoginOptions, 0, sizeof(LoginOptions));
	LoginOptions.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
	LoginOptions.Credentials = &Credentials;

	EOS_HAuth AuthHandle = GetEOSAuthHandle();
	EOS_Auth_Login(AuthHandle, &LoginOptions, this, &FOnlineIdentityInterfaceEpic::LoginCompleteCallbackFunc);
	return true;
}

TSharedPtr<const FUniqueNetId> FOnlineIdentityInterfaceEpic::CreateUniquePlayerId(const FString& Str)
{
	return MakeShareable(new FUniqueNetIdEpic(Str));
}

TSharedPtr<const FUniqueNetId> FOnlineIdentityInterfaceEpic::CreateUniquePlayerId(uint8* Bytes, int32 Size)
{
	if (Bytes && Size > 0)
	{
		FString StrId(Size, (TCHAR*)Bytes);
		return MakeShareable(new FUniqueNetIdEpic(StrId));
	}
	return nullptr;
}

TArray<TSharedPtr<FUserOnlineAccount>> FOnlineIdentityInterfaceEpic::GetAllUserAccounts() const
{
	TArray<TSharedPtr< FUserOnlineAccount>> accounts;
	for (auto acc : this->userAccounts)
	{
		accounts.Add(acc.Value);
	}
	return accounts;
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
	return TEXT("");
}

ELoginStatus::Type FOnlineIdentityInterfaceEpic::GetLoginStatus(const FUniqueNetId& UserId) const
{
	TSharedPtr<FUserOnlineAccount> acc = this->GetUserAccount(UserId);
	if (acc.IsValid() && acc->GetUserId()->IsValid())
	{
		return ELoginStatus::LoggedIn;
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
	// Get the auth and connect handle
	EOS_HPlatform platform = this->subsystemEpic->PlatformHandle;
	check(platform);

	EOS_HAuth authHandle = EOS_Platform_GetAuthInterface(platform);
	check(authHandle);

	EOS_EpicAccountId eosId = EOS_Auth_GetLoggedInAccountByIndex(authHandle, LocalUserNum);
	if (eosId && EOS_EpicAccountId_IsValid(eosId))
	{
		return MakeShareable(new FUniqueNetIdEpic(FIdentityUtilities::EpicAccountIDToString(eosId)));
	}
	return nullptr;
}

TSharedPtr<FUserOnlineAccount> FOnlineIdentityInterfaceEpic::GetUserAccount(const FUniqueNetId& UserId) const
{
	const TSharedRef<FUserOnlineAccountEpic>* acc = this->userAccounts.Find(FUniqueNetIdEpic(UserId));
	if (acc)
	{
		return *acc;
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
	TSharedPtr<const FUniqueNetId> id = GetUniquePlayerId(LocalUserNum);
	if (id.IsValid())
	{
		// Get the auth and connect handle
		EOS_HPlatform platform = this->subsystemEpic->PlatformHandle;
		check(platform);

		EOS_HAuth authHandle = EOS_Platform_GetAuthInterface(platform);
		check(authHandle);

		EOS_Auth_LogoutOptions logoutOpts = {};
		logoutOpts.ApiVersion = EOS_AUTH_LOGOUT_API_LATEST;

		EOS_EpicAccountId epicId = EOS_Auth_GetLoggedInAccountByIndex(authHandle, LocalUserNum);
		logoutOpts.LocalUserId = epicId;
		EOS_Auth_Logout(authHandle, &logoutOpts, this, &FOnlineIdentityInterfaceEpic::LogoutCompleteCallbackFunc);
	}

	UE_LOG_ONLINE(Display, TEXT("Tried logging out user, which was not logged in"));
	TriggerOnLogoutCompleteDelegates(0, false);
	return false;
}

void FOnlineIdentityInterfaceEpic::LogoutCompleteCallbackFunc(const EOS_Auth_LogoutCallbackInfo* Data)
{
	checkf(Data, TEXT("Logout complete allback called, but no data was returned"));

	FUniqueNetIdEpic id = FUniqueNetIdEpic(FIdentityUtilities::EpicAccountIDToString(Data->LocalUserId));
	if (Data->ResultCode != EOS_EResult::EOS_Success)
	{
		char const* resultStr = EOS_EResult_ToString(Data->ResultCode);

		UE_LOG_ONLINE_IDENTITY(Warning, TEXT("[EOS SDK] Logout Failed - User: %s, Result : %s"), *id.ToString(), resultStr);
		return;
	}

	FOnlineIdentityInterfaceEpic* thisPtr = (FOnlineIdentityInterfaceEpic*)Data->ClientData;
	check(thisPtr);

	int32 idIdx = thisPtr->GetPlatformUserIdFromUniqueNetId(id);

	thisPtr->userAccounts.Remove(id);

	thisPtr->TriggerOnLogoutCompleteDelegates(idIdx, true);
}

EOS_AuthHandle* FOnlineIdentityInterfaceEpic::GetEOSAuthHandle()
{
	EOS_HPlatform platform = this->subsystemEpic->PlatformHandle;
	check(platform);

	EOS_HAuth authHandle = EOS_Platform_GetAuthInterface(platform);
	check(authHandle);

	return authHandle;
}

void FOnlineIdentityInterfaceEpic::RevokeAuthToken(const FUniqueNetId& LocalUserId, const FOnRevokeAuthTokenCompleteDelegate& Delegate)
{
	UE_LOG_ONLINE_IDENTITY(Fatal, TEXT("FOnlineIdentityInterfaceEpic::RevokeAuthToken not implemented"));
}
