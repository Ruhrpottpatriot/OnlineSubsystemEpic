
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

/**
 * Online subsystem module class  (Epic Implementation)
 * Code related to the loading of the OnlineSubsystemEpic module
 */
class FOnlineSubsystemEpicModule : public IModuleInterface
{

private:
	/** Class responsible for creating instance(s) of the subsystem */
	class FOnlineFactoryEpic* OnlineFactory;

	/** Handle to the test dll we will load */
	void* EpicOnlineServiceSDKLibraryHandle;

public:
	FOnlineSubsystemEpicModule()
		: OnlineFactory(nullptr)
	{}

	virtual ~FOnlineSubsystemEpicModule() = default;

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}

	virtual bool SupportsAutomaticShutdown() override
	{
		return false;
	}
};
