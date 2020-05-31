#pragma once

#include "UObject/Object.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "OnlineSubsystemEpicTypes.h"

#include "EpicOnlineServicesLoginTask.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLoginSuccess, FString, PlayerId, FString, PlayerNickName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoginFailure);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoginRequresMFA);

UCLASS(MinimalAPI)
class UEpicOnlineServicesIdentityTask : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	IOnlineIdentityPtr GetIdentityInterface();
protected:
	FDelegateHandle DelegateHandle;
};


/**
 * Blueprint node to manage login workflow
 */
UCLASS(BlueprintType, meta = (ExposedAsyncProxy = AsyncTask))
class ONLINESUBSYSTEMEPIC_API UEpicOnlineServicesLoginTask : public UEpicOnlineServicesIdentityTask
{
	GENERATED_BODY()

public:
	UEpicOnlineServicesLoginTask();

	UPROPERTY(BlueprintAssignable, Category = "Epic Online Services|Identity")
		FOnLoginSuccess OnLoginSuccess;

	UPROPERTY(BlueprintAssignable, Category = "Epic Online Services|Identity")
		FOnLoginFailure OnLoginFailure;

	UPROPERTY(BlueprintAssignable, Category = "Epic Online Services|Identity")
		FOnLoginFailure OnLoginRequiresMFA;

	// Attempts to login using user supplied credentials
	UFUNCTION(BlueprintCallable, Category = "Epic Online Services|Identity", meta = (BlueprintInternalUseOnly = "true", DisplayName = "Epic Online Subsystem Login"))
		static UEpicOnlineServicesLoginTask* TryLogin(ELoginType loginType, FString id, FString token, int32 localUserNum);

	// Attempts to login using locally stored credentials
	UFUNCTION(BlueprintCallable, Category = "Epic Online Services|Identity", meta = (BlueprintInternalUseOnly = "true", DisplayName = "Epic Online Subsystem Auto Login"))
		static UEpicOnlineServicesLoginTask* TryAutoLogin(int32 localUserNum);

	/** UBlueprintAsyncActionBase interface */
	virtual void Activate() override;

	void setDelegate(IOnlineIdentityPtr identityInterface);	
	FOnlineAccountCredentials Credentials;
	int32 LocalUserNum;
private:

	void OnLoginCompleteDelegate(int32 localUserNum, bool bWasSuccessful, const FUniqueNetId& userId, const FString& errorString);

	void EndTask();
};