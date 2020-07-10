#pragma once

#include "CoreMinimal.h"
#include "OnlineSubsystemTypes.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSubsystemEpicTypes.h"
#include "eos_sdk.h"

class FOnlineSubsystemEpic;

class FOnlineIdentityInterfaceEpic
	: public IOnlineIdentity
{
	FOnlineSubsystemEpic* subsystemEpic;

	EOS_HConnect connectHandle;

	EOS_HAuth authHandle;

	EOS_NotificationId notifyLoginStatusChangedId;

	EOS_NotificationId notifyAuthExpiration;

	FOnlineIdentityInterfaceEpic() = delete;

	static void EOS_Connect_OnLoginComplete(EOS_Connect_LoginCallbackInfo const* Data);
	static void EOS_Connect_OnAuthExpiration(EOS_Connect_AuthExpirationCallbackInfo const* Data);
	static void EOS_Connect_OnLoginStatusChanged(EOS_Connect_LoginStatusChangedCallbackInfo const* Data);
	static void EOS_Auth_OnLoginComplete(EOS_Auth_LoginCallbackInfo const* Data);
	static void EOS_Auth_OnLogoutComplete(const EOS_Auth_LogoutCallbackInfo* Data);
	static void EOS_Connect_OnUserCreated(EOS_Connect_CreateUserCallbackInfo const* Data);
	static void EOS_Connect_OnAccountLinked(EOS_Connect_LinkAccountCallbackInfo const* Data);

	TSharedPtr<FUserOnlineAccount> OnlineUserAcccountFromPUID(EOS_ProductUserId const& PUID) const;
	ELoginStatus::Type EOSLoginStatusToUELoginStatus(EOS_ELoginStatus LoginStatus);

public:
	virtual ~FOnlineIdentityInterfaceEpic();

	FOnlineIdentityInterfaceEpic(FOnlineSubsystemEpic* inSubsystem);

	virtual bool Login(int32 LocalUserNum, const FOnlineAccountCredentials& AccountCredentials) override;
	bool AutoLogin(int32 LocalUserNum) override;
	TSharedPtr<const FUniqueNetId> CreateUniquePlayerId(const FString& Str) override;
	TSharedPtr<const FUniqueNetId> CreateUniquePlayerId(uint8* Bytes, int32 Size) override;
	TArray<TSharedPtr<FUserOnlineAccount>> GetAllUserAccounts() const override;
	FString GetAuthToken(int32 LocalUserNum) const override;
	FString GetAuthType() const override;
	ELoginStatus::Type GetLoginStatus(const FUniqueNetId& UserId) const override;
	ELoginStatus::Type GetLoginStatus(int32 LocalUserNum) const override;
	FPlatformUserId GetPlatformUserIdFromUniqueNetId(const FUniqueNetId& UniqueNetId) const override;
	FString GetPlayerNickname(const FUniqueNetId& UserId) const override;
	FString GetPlayerNickname(int32 LocalUserNum) const override;
	TSharedPtr<const FUniqueNetId> GetUniquePlayerId(int32 LocalUserNum) const override;
	TSharedPtr<FUserOnlineAccount> GetUserAccount(const FUniqueNetId& UserId) const override;
	void GetUserPrivilege(const FUniqueNetId& LocalUserId, EUserPrivileges::Type Privilege, const FOnGetUserPrivilegeCompleteDelegate& Delegate) override;
	bool Logout(int32 LocalUserNum) override;
	void RevokeAuthToken(const FUniqueNetId& LocalUserId, const FOnRevokeAuthTokenCompleteDelegate& Delegate) override;
};
