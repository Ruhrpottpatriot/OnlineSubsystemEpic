# OnlineSubsystemEpic
An unofficial integration of the Epic Online Systems SDK with Unreal Engine 4's OnlineSubsystem

### Currently Supported Versions
The plugin is coded against a specific version of the EOS SDK. While Epic promises to deliver a stable ABI, it's best to use the Plugin with the SDK version it was coded against

| Plugin Version | SDK Version |
| -------------- |------------ |
|     0.0.2      |    1.6.0    |
|     0.0.1      |    1.6.0    |

## Installation
1. Clone the repository into your Projects _Plugins_ directory.
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
If you need to directly access the OnlineSubsystem you should get it via the static helper methods in `Online.h`. These helper methods make sure the correct subsystem instance is retrieved (multiple can exist in the editor, and things like logins are tied to a specific instance). Outside of C++ there exists multiple asynchronous blueprint nodes in the _OnlineSubsystemUtils_ plugin. In most cases there is no need to access the online subsystem via `IOnlineSubsystem::Get()`.

### Identity Interface
The identity interface is the central hub for access management. It provides the ability to login and logout a user, check their login status and get their player ids.
This plugin supports two login flows: One, an *Open Id Connect* (Connect) compliant, as well as Epics own account system (EAS) login flow. With this there are some things a user has to consider when logging in a user.
User credentials are passed to the system via the `FOnlineAccountCredentials` class. This class has a `Type` field, which is used to distingush between EAS and Connect.
To switch between the two, the `Type` field has the format `{FlowType}:{LoginType}` where the `FlowType` is either "EAS" or "CONNECT" and `LoginType` one of the following:

|       EAS      |    Connect    |
| -------------- | ------------- |
| Password       | Steam         |
| ExchangeCode   | PSN           |
| PersistentAuth | XBL           |
| DeviceCode     | Discord       |
| Developer      | GOG           |
| RefreshToken   | Nintendo_Id   |
| AccountPortal  | Nintentod_NSA |
|                | Uplay         |
|                | OpenId        |
|                | DeviceId      |
|                | Apple         |

While connect also supports EAS as `LoginType`, this is implicitly assumed by this plugin when using the EAS login flow.

`FOnlineAccountCredentials` class has two additional fields, `Id` and `Token`. When using "CONNECT" login flow the `Token` field stores the access token, while the `Id` field holds additional data, that is needed for the Nintendo and Apple login types.
When using "EAS" as login flow, consult the "OnlineIdentityInterface.h" file to see which field maps to which.


## Implemented Interfaces
* Identity
* Sessions
* UserInfo