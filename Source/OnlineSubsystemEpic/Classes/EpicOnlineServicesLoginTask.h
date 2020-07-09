#pragma once

#include "UObject/Object.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "OnlineSubsystemEpicTypes.h"

#include "EpicOnlineServicesLoginTask.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoginSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoginFailure, const FString&, Error);
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
 * Exposes the epic account login flow to blueprints
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

	/**
	 * Attempts to log in with the specified epic account login flow and credentials
	 */
	UFUNCTION(BlueprintCallable, Category = "Epic Online Services|Identity", meta = (BlueprintInternalUseOnly = "true", DisplayName = "Epic Online Subsystem Login"))
		static UEpicOnlineServicesLoginTask* TryLogin(ELoginType loginType, FString id, FString token, int32 localUserNum);
	/**
	 * Attempts to log in with locally stored credentials
	 */
	UFUNCTION(BlueprintCallable, Category = "Epic Online Services|Identity", meta = (BlueprintInternalUseOnly = "true", DisplayName = "Epic Online Subsystem Auto Login"))
		static UEpicOnlineServicesLoginTask* TryAutoLogin(int32 localUserNum);

	/** UBlueprintAsyncActionBase interface */
	virtual void Activate() override;

	FOnlineAccountCredentials Credentials;
	int32 LocalUserNum;
private:

	void OnLoginCompleteDelegate(int32 localUserNum, bool bWasSuccessful, const FUniqueNetId& userId, const FString& errorString);

	void EndTask();
};


UENUM(BlueprintType)
enum class EConnectLoginType : uint8
{
	Epic,
	Steam,
	PSN			UMETA(DisplayName = "Playstation Network (PSN)"),
	XBL			UMETA(DisplayName = "XBox Live (XBL)"),
	GOG,
	Discord,
	Nintendo,
	NintendoNSA	UMETA(DisplayName = "Nintendo Service Account"),
	UPlay,
	OpenID UMETA(DisplayName = "OpenID Connect"),
	DeviceId,
	Apple
};

/**
 * Exposes the epic account login flow to blueprints
 */
UCLASS(BlueprintType, meta = (ExposedAsyncProxy = AsyncTask))
class ONLINESUBSYSTEMEPIC_API UEpicOnlineServicesConnectLoginTask 
	: public UEpicOnlineServicesIdentityTask
{
	GENERATED_BODY()
private:
	int32 localUserIdx;
	bool createNewAccount;
	FOnlineAccountCredentials credentials;
	
	void OnLoginCompleteDelegate(int32 localUserNum, bool bWasSuccessful, const FUniqueNetId& userId, const FString& errorString);

	void EndTask();

public:

	UPROPERTY(BlueprintAssignable, Category = "Epic Online Services|Identity")
		FOnLoginSuccess OnLoginSuccess;

	UPROPERTY(BlueprintAssignable, Category = "Epic Online Services|Identity")
		FOnLoginFailure OnLoginFailure;

	// Attempts to login using user supplied credentials
	UFUNCTION(BlueprintCallable, Category = "Epic Online Services|Identity", meta = (BlueprintInternalUseOnly = "true", DisplayName = "Connect Login"))
		static UEpicOnlineServicesConnectLoginTask* TryLogin(int32 LocalPlayerNum, EConnectLoginType LoginType, FString id, FString token, bool CreateNew);

	FString ConnectLoginTypeToString(EConnectLoginType LoginType);

	/** UBlueprintAsyncActionBase interface */
	virtual void Activate() override;
};
