#include "OnlineSubsystemEpic.h"
#include "OnlineIdentityInterfaceEpic.h"
#include <string>

IOnlineSessionPtr FOnlineSubsystemEpic::GetSessionInterface() const
{
	return nullptr;
}

IOnlineFriendsPtr FOnlineSubsystemEpic::GetFriendsInterface() const
{
	return nullptr;
}

IOnlinePartyPtr FOnlineSubsystemEpic::GetPartyInterface() const
{
	return nullptr;
}

IOnlineGroupsPtr FOnlineSubsystemEpic::GetGroupsInterface() const
{
	return nullptr;
}

IOnlineSharedCloudPtr FOnlineSubsystemEpic::GetSharedCloudInterface() const
{
	return nullptr;
}

IOnlineUserCloudPtr FOnlineSubsystemEpic::GetUserCloudInterface() const
{
	return nullptr;
}

IOnlineEntitlementsPtr FOnlineSubsystemEpic::GetEntitlementsInterface() const
{
	return nullptr;
}

IOnlineLeaderboardsPtr FOnlineSubsystemEpic::GetLeaderboardsInterface() const
{
	return nullptr;
}

IOnlineVoicePtr FOnlineSubsystemEpic::GetVoiceInterface() const
{
	return nullptr;
}

IOnlineExternalUIPtr FOnlineSubsystemEpic::GetExternalUIInterface() const
{
	return nullptr;
}

IOnlineTimePtr FOnlineSubsystemEpic::GetTimeInterface() const
{
	return nullptr;
}

IOnlineIdentityPtr FOnlineSubsystemEpic::GetIdentityInterface() const
{
	return this->IdentityInterface;
}

IOnlineTitleFilePtr FOnlineSubsystemEpic::GetTitleFileInterface() const
{
	return nullptr;
}

IOnlineStorePtr FOnlineSubsystemEpic::GetStoreInterface() const
{
	return nullptr;
}

IOnlineStoreV2Ptr FOnlineSubsystemEpic::GetStoreV2Interface() const
{
	return nullptr;
}

IOnlinePurchasePtr FOnlineSubsystemEpic::GetPurchaseInterface() const
{
	return nullptr;
}

IOnlineEventsPtr FOnlineSubsystemEpic::GetEventsInterface() const
{
	return nullptr;
}

IOnlineAchievementsPtr FOnlineSubsystemEpic::GetAchievementsInterface() const
{
	return nullptr;
}

IOnlineSharingPtr FOnlineSubsystemEpic::GetSharingInterface() const
{
	return nullptr;
}

IOnlineUserPtr FOnlineSubsystemEpic::GetUserInterface() const
{
	return nullptr;
}

IOnlineMessagePtr FOnlineSubsystemEpic::GetMessageInterface() const
{
	return nullptr;
}

IOnlinePresencePtr FOnlineSubsystemEpic::GetPresenceInterface() const
{
	return nullptr;
}

IOnlineChatPtr FOnlineSubsystemEpic::GetChatInterface() const
{
	return nullptr;
}

IOnlineStatsPtr FOnlineSubsystemEpic::GetStatsInterface() const
{
	return nullptr;
}

IOnlineTurnBasedPtr FOnlineSubsystemEpic::GetTurnBasedInterface() const
{
	return nullptr;
}

IOnlineTournamentPtr FOnlineSubsystemEpic::GetTournamentInterface() const
{
	return nullptr;
}

bool FOnlineSubsystemEpic::Init()
{
	if (this->IsInit)
	{
		UE_LOG_ONLINE(Warning, TEXT("Tried initializing already initialized subsystem"));
		return false;
	}
	bool hasInvalidParams = false;

	// Get the Developer tool port.
	// If this isn't set, we default to 9999
	if (!GConfig->GetInt(
		TEXT("OnlineSubsystemEpic"),
		TEXT("DevToolPort"),
		this->devToolPort,
		GEngineIni))
	{
		UE_LOG_ONLINE(Verbose, TEXT("DevToolPort not set in ini, defaulting to 9999"));
		this->devToolPort = 9999;
	}

	// Read project name and version from config files
	FString productId;
	if (!GConfig->GetString(
		TEXT("OnlineSubsystemEpic"),
		TEXT("ProductId"),
		productId,
		GEngineIni))
	{
		UE_LOG_ONLINE(Warning, TEXT("Product Id is empty, add your product id from Epic Games DevPortal to the config files."));
		hasInvalidParams = true;
	}

	FString sandboxId;
	if (!GConfig->GetString(
		TEXT("OnlineSubsystemEpic"),
		TEXT("SandboxId"),
		sandboxId,
		GEngineIni))
	{
		UE_LOG_ONLINE(Warning, TEXT("Sandbox Id is empty, add your sandbox id from Epic Games DevPortal to the config files."));
		hasInvalidParams = true;
	}

	FString deploymentId;
	if (!GConfig->GetString(
		TEXT("OnlineSubsystemEpic"),
		TEXT("DeploymentId"),
		deploymentId,
		GEngineIni))
	{
		UE_LOG_ONLINE(Warning, TEXT("Deployment Id is empty, add your deployment id from Epic Games DevPortal to the config files."));
		hasInvalidParams = true;
	}

	FString clientId;
	GConfig->GetString(
		TEXT("OnlineSubsystemEpic"),
		TEXT("ClientCredentialsId"),
		clientId,
		GEngineIni
	);
	FString clientSecret;
	GConfig->GetString(
		TEXT("OnlineSubsystemEpic"),
		TEXT("ClientCredentialsSecret"),
		clientSecret,
		GEngineIni
	);
	if (clientId.IsEmpty() || clientSecret.IsEmpty())
	{
		UE_LOG_ONLINE(Warning, TEXT("[EOS SDK] Client credentials are invalid, check clientid and clientsecret!"));
		hasInvalidParams = true;
	}

	if (hasInvalidParams)
	{
		return false;
	}


	// Create platform instance
	EOS_Platform_Options PlatformOptions = {};
	PlatformOptions.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	PlatformOptions.Reserved = nullptr;
	PlatformOptions.ProductId = TCHAR_TO_ANSI(*productId);
	PlatformOptions.SandboxId = TCHAR_TO_ANSI(*sandboxId);
	PlatformOptions.ClientCredentials.ClientId = TCHAR_TO_ANSI(*clientId);
	PlatformOptions.ClientCredentials.ClientSecret = TCHAR_TO_ANSI(*clientSecret);
	PlatformOptions.bIsServer = EOS_FALSE;
	static std::string EncryptionKey(64, '1');
	PlatformOptions.EncryptionKey = EncryptionKey.c_str();
	PlatformOptions.OverrideCountryCode = nullptr;
	PlatformOptions.OverrideLocaleCode = nullptr;
	PlatformOptions.DeploymentId = TCHAR_TO_ANSI(*deploymentId);
#if UE_EDITOR
	// The platform overlay causes rendering artifacts in the editor,
	// this flag ensures it is not loaded in the editor or PIE
	PlatformOptions.Flags = EOS_PF_LOADING_IN_EDITOR;
#else
	PlatformOptions.Flags = 0;
#endif
	PlatformOptions.CacheDirectory = FUtils::GetTempDirectory();

	this->PlatformHandle = EOS_Platform_Create(&PlatformOptions);
	if (!this->PlatformHandle)
	{
		UE_LOG_ONLINE(Warning, TEXT("[EOS SDK] Platform Create Failed!"));
		return false;
	}

	IdentityInterface = MakeShareable(new FOnlineIdentityInterfaceEpic(this));

	this->IsInit = true;
	return true;
}

bool FOnlineSubsystemEpic::Shutdown()
{
	this->IsInit = false;
	this->PlatformHandle = nullptr;


#define DESTRUCT_INTERFACE(Interface) \
	if (Interface.IsValid()) \
	{ \
		ensure(Interface.IsUnique()); \
		Interface = nullptr; \
	}

	// Destruct the interfaces
	DESTRUCT_INTERFACE(IdentityInterface);

#undef DESTRUCT_INTERFACE

	return true;
}

FString FOnlineSubsystemEpic::GetAppId() const
{
	FString productId;
	GConfig->GetString(
		TEXT("OnlineSubsystemEpic"),
		TEXT("ProductId"),
		productId,
		GEngineIni
	);
	return productId;
}

FText FOnlineSubsystemEpic::GetOnlineServiceName() const
{
	return NSLOCTEXT("OnlineSubsystemEpic", "OnlineServiceName", "Epic");
}

bool FOnlineSubsystemEpic::Tick(float DeltaTime)
{
	if (this->PlatformHandle)
	{
		EOS_Platform_Tick(this->PlatformHandle);
	}

	return true;
}