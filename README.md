# OnlineSubsystemEpic
An unofficial integration of the Epic Online Systems SDK with Unreal Engine 4's OnlineSubsystem

## Installation
1. Clone the repository into your Pojects _Plugins_ directory.
2. Download the EOS C++ SDK from the Developer Portal
3. Drop the _Bin_, _Include_ and _Lib_ from inside the SDK folder into _Source\ThirdParty\OnlineSubsystemEpicLibrary_

To make the SDK initialize as intended a few properties have to be set. To no hardcode them in code they are set inside two .ini files, which can easily be changed without a recompile. Keep in mind, that these properties contain sensitive information and shoulnd't be made public. Therefore it's best to keep it out of source control. Later versions might include different retrieval mechanisms.

DefaultGame.ini
```ini
[/Script/EngineSettings.GeneralProjectSettings]
ProjectID = <YourProjectID>
ProjectName = <YourProjectName>
ProjectVersion = <YourProjectVersion>
```
DefaultEngine.ini
```ini
[OnlineSubsystem]
DefaultPlatformService=Epic

[OnlineSubsystemEpic]
; The product id for the running application, found on the dev portal
ProductId = <ProductId>
; The sandbox id for the running application, found on the dev portal
SandboxId = <SandboxId>
; The deployment id for the running application, found on the dev portal
DeploymentId = <DeploymentId>
; Client id of the service permissions entry
ClientCredentialsId = <ClientCredentialsId>
; Client secret for accessing the set of permissions
ClientCredentialsSecret = <ClientCredentialsSecret>
```


## Usage
This plugin is used like any other OnlineSubsystem Plugin already existing. This means, that most of the time you won't need to directly interface with the system directly, but can let the engine classes handle the calls.
If you need to directly access the OnlineSubsystem you should get the static pointer to it via `IOnlineSubsystem::Get()` and go on from there.

## Limitations
Currently only the `IOnlineIdentity` interface is implemented and inside said interface only _Developer_ login is working. MFA and PinGrantCode are also not supported currently.
If you want more feature you have to wait, or submit a pull request ;)