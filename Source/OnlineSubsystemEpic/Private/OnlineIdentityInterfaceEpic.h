#pragma once

#include "CoreMinimal.h"
#include "OnlineSubsystemTypes.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSubsystemEpicTypes.h"
#include "eos_auth_types.h"

class FOnlineSubsystemEpic;

class FOnlineIdentityInterfaceEpic
	: public IOnlineIdentity
{
public:
	virtual ~FOnlineIdentityInterfaceEpic() = default;

	FOnlineIdentityInterfaceEpic(FOnlineSubsystemEpic * inSubsystem);

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

private:
	FOnlineIdentityInterfaceEpic() = delete;

	FOnlineSubsystemEpic* subsystemEpic;

	/** Ids mapped to locally registered users */
	TMap<TSharedRef<const FUniqueNetIdEpic>, TSharedRef<FUserOnlineAccountEpic>> userAccounts;

	static void LoginCompleteCallbackFunc(const EOS_Auth_LoginCallbackInfo* Data);
	static void LogoutCompleteCallbackFunc(const EOS_Auth_LogoutCallbackInfo* Data);

	EOS_AuthHandle* GetEOSAuthHandle();
};
