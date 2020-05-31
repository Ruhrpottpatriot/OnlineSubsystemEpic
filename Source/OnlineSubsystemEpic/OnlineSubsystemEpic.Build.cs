// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class OnlineSubsystemEpic : ModuleRules
{
    public OnlineSubsystemEpic(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDefinitions.Add("ONLINESUBSYSTEMEPIC_PACKAGE=1");
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[] {
                "OnlineSubsystemUtils"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "NetCore",
                "CoreUObject",
                "Engine",
                "OnlineSubsystemEpicLibrary",
                "OnlineSubsystem",
                "Json",
                "Sockets",
                "Projects",
            }
        );


        bEnforceIWYU = true;
    }
}
