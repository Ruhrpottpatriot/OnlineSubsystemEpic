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

typedef struct FCreateUserAdditionalData
{
	FOnlineIdentityInterfaceEpic* IdentityInterface;
	int32 LocalUserNum;
} FCreateUserAdditionalData;

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

			FLoginCompleteAdditionalData* newAdditionalData = new FLoginCompleteAdditionalData{
				thisPtr,
				additionalData->LocalUserNum,
				eosId
			};
			EOS_Connect_Login(thisPtr->connectHandle, &loginOptions, newAdditionalData, &FOnlineIdentityInterfaceEpic::EOS_Connect_OnLoginComplete);

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
		error = FString::Printf(TEXT("[EOS SDK] Auth Login Failed - Error Code: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
	}

	// Abort if there was an error
	if (!error.IsEmpty())
	{
		UE_LOG_ONLINE_IDENTITY(Warning, TEXT("Epic Account Service Login failed. Message:\r\n    %s"), *error);
		thisPtr->TriggerOnLoginCompleteDelegates(INDEX_NONE, false, FUniqueNetIdEpic(), error);
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
		if (Data->ContinuanceToken)
		{
			// ToDo: Implementing linking user accounts needs some changes
			// to the FUniqueNetIdEpic type
		}

		userId = FUniqueNetIdEpic(Data->LocalUserId, additionalData->EpicAccountId);
		UE_LOG_ONLINE_IDENTITY(Display, TEXT("Finished logging in user \"%s\""), UTF8_TO_TCHAR(Data->LocalUserId));
	}
	else if (eosResult == EOS_EResult::EOS_InvalidUser)
	{
		if (Data->ContinuanceToken)
		{
			// Getting a continuance token implies the login has failed, however we want to give the caller
			// the ability to restart the login with the continuance token.
			// Thus we create an FUniqueNetIdString from the token and return it to the caller with a failed
			// login indication.
			UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Got invalid user and contiuance token."));
			FUniqueNetIdString continuanceToken = FUniqueNetIdString(UTF8_TO_TCHAR(Data->ContinuanceToken));
			thisPtr->TriggerOnLoginCompleteDelegates(additionalData->LocalUserNum, false, continuanceToken, TEXT(""));
		}
		else
		{
			error = TEXT("[EOS SDK] Got invalid user, but no continuance token.");
		}
	}
	else
	{
		error = FString::Printf(TEXT("[EOS SDK] Connect Login Failed - Error Code: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
	}

	if (!error.IsEmpty())
	{
		UE_LOG_ONLINE_IDENTITY(Warning, TEXT("%s encountered an error. Message:\r\n    %s"), *FString(__FUNCTION__), *error);
		thisPtr->TriggerOnLoginCompleteDelegates(additionalData->LocalUserNum, false, FUniqueNetIdEpic(), error);
	}
	else
	{
		thisPtr->TriggerOnLoginCompleteDelegates(additionalData->LocalUserNum, true, userId, TEXT(""));
	}

	delete(additionalData);
}

void FOnlineIdentityInterfaceEpic::EOS_Connect_OnAuthExpiration(EOS_Connect_AuthExpirationCallbackInfo const* Data)
{
	// ToDo: Make the user see this.
	FString localUser = FUniqueNetIdEpic::ProductUserIdToString(Data->LocalUserId);
	UE_LOG_ONLINE_IDENTITY(Display, TEXT("Auth for user \"%s\" expired"), *localUser);
}

void FOnlineIdentityInterfaceEpic::EOS_Connect_OnLoginStatusChanged(EOS_Connect_LoginStatusChangedCallbackInfo const* Data)
{
	FOnlineIdentityInterfaceEpic* thisPtr = (FOnlineIdentityInterfaceEpic*)Data->ClientData;

	FString localUser = FUniqueNetIdEpic::ProductUserIdToString(Data->LocalUserId);
	ELoginStatus::Type oldStatus = thisPtr->EOSLoginStatusToUELoginStatus(Data->PreviousStatus);
	ELoginStatus::Type newStatus = thisPtr->EOSLoginStatusToUELoginStatus(Data->CurrentStatus);


	// ToDo: Somehow this always crashes
	//UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Login status changed.\r\n%9s: %s\r\n%9s: %s\r\n%9s: %s"), TEXT("User"), *localUser, TEXT("New State"), *ELoginStatus::ToString(newStatus), TEXT("Old State"), *ELoginStatus::ToString(oldStatus));

	FUniqueNetIdEpic netId = FUniqueNetIdEpic(Data->LocalUserId);
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
	FString localUser = FUniqueNetIdEpic::EpicAccountIdToString(Data->LocalUserId);
	UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logout Complete - User: %s"), *localUser);
}

void FOnlineIdentityInterfaceEpic::EOS_Connect_OnUserCreated(EOS_Connect_CreateUserCallbackInfo const* Data)
{
	FCreateUserAdditionalData* additionalData = static_cast<FCreateUserAdditionalData*>(Data->ClientData);
	FOnlineIdentityInterfaceEpic* thisPtr = additionalData->IdentityInterface;
	check(thisPtr);

	if (Data->ResultCode != EOS_EResult::EOS_Success)
	{
		char const* resultStr = EOS_EResult_ToString(Data->ResultCode);
		FString error = FString::Printf(TEXT("[EOS SDK] Create User Failed - Result : %s"), UTF8_TO_TCHAR(Data->LocalUserId), resultStr);
		thisPtr->TriggerOnLoginCompleteDelegates(additionalData->LocalUserNum, false, FUniqueNetIdEpic(), error);
		return;
	}
	
	// Creating a user always means we're not using an epic account
	FUniqueNetIdEpic userId = FUniqueNetIdEpic(Data->LocalUserId);
	UE_LOG_ONLINE_IDENTITY(Display, TEXT("Finished creating user \"%s\""), UTF8_TO_TCHAR(Data->LocalUserId));

	thisPtr->TriggerOnLoginCompleteDelegates(additionalData->LocalUserNum, true, userId, TEXT(""));
}

void FOnlineIdentityInterfaceEpic::EOS_Connect_OnAccountLinked(EOS_Connect_LinkAccountCallbackInfo const* Data)
{
	// ToDo: Implement a way to notify the user that an account was linked
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
		EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST
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
	// The account credentials struct has the following format
	// The "Type" field is a string that encodes the login system and login type for that system.
	// Both parts are separated by a single ":" character. The login system must either be "EAS" or "CONNECT",
	// while the login type is a string representation of "ELoginType" for "EAS"
	// and "EOS_EExternalCredentialType" when using "CONNECT"
	// The "Id" and "Token" fields encode different values depending on the login type set. 
	// If a value in the table is marked as "N/A", no values besides the "Type" field need to be set
	// Note: In the future this might be simplified by a helper class that allows the  fields to be set and offer validation
	// | System    | Type                     | Mapping                                         |
	// |-------- - | ------------------------ | ------------------------------------------------|
	// | EAS       | Password                 | Username -> Id, Password -> Token               |
	// |           | Exchange Code            | Exchange Code -> Id                             |
	// |           | Device Code              | N / A                                           |
	// |           | Developer                | Account Id -> Id                                |
	// |           | Account Portal           | N / A                                           |
	// |           | Persistent Auth          | N / A                                           |
	// |           | External Auth            | External System -> Id, External Token -> Token  |
	// | CONNECT   | Apple, Nintendo (NSA)    | Display Name -> Id                              |
	// |           | Other                    | N / A                                           |


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
				FLoginCompleteAdditionalData* additionalData = new FLoginCompleteAdditionalData{
					this,
					LocalUserNum,
					nullptr
				};
				EOS_Connect_Login(this->connectHandle, &loginOptions, additionalData, &FOnlineIdentityInterfaceEpic::EOS_Connect_OnLoginComplete);

				// Release the auth token
				EOS_Auth_Token_Release(authToken);
			}
			// In any other case we call the epic account endpoint
			// and handle login in the callback
			else
			{
				// Create credentials struct
				EOS_Auth_Credentials credentials = {
					EOS_AUTH_CREDENTIALS_API_LATEST
				};

				// Get which EAS login type we want to use
				ELoginType loginType = FUtils::GetEnumValueFromString<ELoginType>(TEXT("ELoginType"), right);

				// Convert the Id and Token fields to char const* for later use
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
					UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logging In with Host: %s"), *this->subsystemEpic->DevToolAddress);
					credentials.Id = TCHAR_TO_ANSI(*this->subsystemEpic->DevToolAddress);
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
				case ELoginType::ExternalAuth:
				{
					// Set ther type to external and copy the token. These are always the same
					credentials.Type = EOS_ELoginCredentialType::EOS_LCT_ExternalAuth;
					credentials.Token = tokenPtr;

					// Check which external login provider we want to use and then set the external type to the appropriate value
					EExternalLoginType externalLoginType = FUtils::GetEnumValueFromString<EExternalLoginType>(TEXT("EExternalLoginType"), AccountCredentials.Id);
					switch (externalLoginType)
					{
					case EExternalLoginType::Steam:
					{
						credentials.ExternalType = EOS_EExternalCredentialType::EOS_ECT_STEAM_APP_TICKET;
						break;
					}
					default:
						error = FString::Printf(TEXT("Using unsupported external login type: %s"), *AccountCredentials.Id);
						break;
					}
					break;
				}
				default:
				{
					UE_LOG_ONLINE_IDENTITY(Fatal, TEXT("Login of type \"%s\" not supported"), *AccountCredentials.Type);
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
			if (right.Equals(TEXT("Continuance"), ESearchCase::IgnoreCase))
			{
				unimplemented();

				//EOS_Connect_CreateUserOptions createUserOptions = {
				//	EOS_CONNECT_CREATEUSER_API_LATEST,
				//	TCHAR_TO_UTF8(*AccountCredentials.Token)
				//};
				//FCreateUserAdditionalData* additionalData = new FCreateUserAdditionalData{
				//	this,
				//	LocalUserNum
				//};
				//EOS_Connect_CreateUser(this->connectHandle, &createUserOptions, this, &FOnlineIdentityInterfaceEpic::EOS_Connect_OnUserCreated);
			}
			else if (right.Equals(TEXT("Link"), ESearchCase::IgnoreCase))
			{
				unimplemented();
				//EOS_ProductUserId puid = FUniqueNetIdEpic::ProductUserIDFromString(AccountCredentials.Id);

				//EOS_Connect_LinkAccountOptions linkAccountOptions = {
				//	EOS_CONNECT_LINKACCOUNT_API_LATEST,
				//	puid,
				//	TCHAR_TO_UTF8(*AccountCredentials.Token)
				//};
				//FCreateUserAdditionalData* additionalData = new FCreateUserAdditionalData{
				//	this,
				//	LocalUserNum
				//};
				//EOS_Connect_LinkAccount(this->connectHandle, &linkAccountOptions, additionalData, &FOnlineIdentityInterfaceEpic::EOS_Connect_OnAccountLinked);
			}
			else
			{
				// Convert the right part of the type string into a EOS external credentials enum.
				TPair<EOS_EExternalCredentialType, bool > externalTypeTuple = FUtils::ExternalCredentialsTypeFromString(right);
				EOS_EExternalCredentialType externalType = externalTypeTuple.Get<0>();

				// Make sure we have a valid connect type
				if (externalTypeTuple.Get<1>())
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

					FLoginCompleteAdditionalData* additionalData = new FLoginCompleteAdditionalData{
						this,
						LocalUserNum,
						nullptr // Since this is the connect login flow, no EAID is available
					};
					EOS_Connect_Login(this->connectHandle, &loginOptions, additionalData, &FOnlineIdentityInterfaceEpic::EOS_Connect_OnLoginComplete);

					success = true;
				}
				else
				{
					error = FString::Printf(TEXT("\"%s\" is not a recognized connect type"), *right);
				}
			}
		}
		else if (left.IsEmpty() || right.IsEmpty())
		{
			error = TEXT("Must specify login flow.");
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
	credentials.Type = FString("EAS:PersistentAuth");

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
	userAccount->SetUserAttribute(USER_ATTR_LAST_LOGIN_TIME, FString::Printf(TEXT("%d"), externalAccountInfo->LastLoginTime));

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
