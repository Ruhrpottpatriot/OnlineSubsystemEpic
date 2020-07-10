# OnlineSubsystemEpic
An unofficial integration of the Epic Online Systems SDK with Unreal Engine 4's OnlineSubsystem

### Currently Supported Versions
The plugin is coded against a specific version of the EOS SDK. While Epic promises to deliver a stable ABI, it's best to use the Plugin with the SDK version it was coded against

|   Plugin Version   | SDK Version |
| ------------------ |------------ |
|       0.0.3        |    1.7.0    |
|       0.0.2        |    1.6.0    |
|       0.0.1        |    1.6.0    |


## Implemented Interfaces
Use this section to see which functions of the EOS SDK are already implemented. For some interfaces it might be, that they are not completely mapped to the SDK functions, either because the SDK doesn't support the functionality, or the OSS doesn't. There are three symbols, you can use to check if the interface meets your expectations:
* :heavy_check_mark: - The interface is completely implemented and works as the OSS documentations describes.
* :warning: - Parts of the interface are implemented. See the notes field for additional information
* :x: - The interface isn't implemented

Currently there exists 36 interfaces in the online subsystem, of which 26 are publicly available. These interfaces and their implementation status are

|  Subsystem Interface  | Implementation Status |       Additional Notes       |
| --------------------- | --------------------- | ---------------------------- |
| OnlineSession         |       :warning:       | See [Known Limitations](KnownLimitations.md) |
| OnlineFriends         |         :x:           |
| OnlineParty           |         :x:           |
| OnlineGroups          |         :x:           |
| OnlineSharedCloud     |         :x:           |
| OnlineUserCloud       |         :x:           |
| OnlineEntitlements    |         :x:           |
| OnlineLeaderboards    |         :x:           |
| OnlineVoice           |         :x:           |
| OnlineExternalUI      |         :x:           |
| OnlineTime            |         :x:           |
| OnlineIdentity        |       :warning:       | See [Known Limitations](KnownLimitations.md) |
| OnlineTitleFile       |         :x:           |
| OnlineStore           |         :x:           |
| OnlineStoreV2         |         :x:           |
| OnlinePurchase        |         :x:           |
| OnlineEvents          |         :x:           |
| OnlineAchievements    |         :x:           |
| OnlineSharing         |         :x:           |
| OnlineUser            |       :warning:       | See [Known Limitations](KnownLimitations.md) |
| OnlineMessage         |         :x:           |
| OnlinePresence        |       :warning:       | See [Known Limitations](KnownLimitations.md) |
| OnlineChat            |         :x:           |
| OnlineStats           |         :x:           |
| OnlineTurnBased       |         :x:           |
| OnlineTournament      |         :x:           |


## Installation
1. Clone the repository into your Projects _Plugins_ directory.
2. Download the EOS C++ SDK from the Developer Portal
3. Drop the _Bin_, _Include_ and _Lib_ from inside the SDK folder into _Source\ThirdParty\OnlineSubsystemEpicLibrary_

To make the SDK initialize as intended a few properties have to be set. As not to hardcode them, they are set inside two .ini files, which can easily be changed without a recompile. Keep in mind, that some of these properties contain sensitive information and **mustn't** be made public. Therefore it's best to keep it out of source control. Later versions might include different retrieval mechanisms.

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

Additionally the SDK supports a a few optional settings for further customization:
DefaultEngine.ini
```ini
; Overrides the internal country code, max 4 characters in length.
CountryCode = <ISO3166CountryCode>
; Overrides the locale used by the SDK, max 9 characters in length
LocaleCode = <ISO639LanguageCode>
; Encryption key, 64 hex characters long. Used only by player data storage
EncryptionKey = <Custom64CharacterLongEncryptionKey>
; Sets the directory used for SDK side caching. The path is created if missing
CacheDirectory = <AbsoluteCachePath>
; A budget, measured in milliseconds, for ticking tasks to do their work.
; When the budget is met or exceeded (or if no work available), the tasks will return.
; This allows the SDK to amortize the cost of work across multiple frames in the event that a lot of work is queued for processing.
; Zero is interpreted as "perform all available work"
; The codomain of this variable the same as an unsigned 32-bit integer,
; although it's represented as a double in this config, since UE4 doesn't allow
; retrieval of unsigned 32-bit integers from code.
TickBudget = <DurationInMs>
; Indicates the SDK should skip initialization of the overlay, used by the in-app purchase flow and social overlay.
; Implied by LoadingInEditor
DisableOverlay = <true>/<false>
; Indicates the SDK should skip initialization of the social overlay.
; Implied by DisableOverlay
DisableSocialOverlay = <true>/<false>
; Change if the Developer Auth Tool doesn't live on the local machine
; or the port 9999 is not available. Default: 127.0.0.1:9999
DevToolAddress=<IPv4 or IPv6 Address>
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

#### Continuance Tokens
**Note::** This is currently not supported as the EOS SDK gives no possibility to convert a continuance token to and from strings.

When using the connect interface, there might not be a user to login with. The interface remedies that, that it return a continuance token with which the caller can restart the login process. This library supports this in multiple ways.
In *C++* the OnLoginCompleteDelegate is called regardless if the task completed successful or not. If the original call completed without errors, the delegate will have set the `bWasSuccessful` parameter set to `true` and will contain the local user index, and the users unique net id. If the user doesn't exist but the login process can be restarted by using a continuance token the `bWasSuccessful` parameter is set to `false` and the unique net id will contain the *continuance token*. The process then can be restarted by calling the `IOnlineIdentityInterface::Login(int32, const FOnlineAccountCredentials&)` function, where the `FOnlineAccountCredentials` parameter is initialized with the following parameters:
* Id: *empty*
* Token: `<continuance token>`
* Type: CONNECT::Continuance

If this call succeeds the OnLoginCompleteDelegate is called with the `bWasSuccessful` parameter set to `true`, the local user index, and the users unique net id. Note, that since a continuance token was used to create an account, the unique net id will only contain a product user id and never an epic account id.
Should the call fail, the delegate will be called with the local user index, the `bWasSuccessful` parameter set to `false`, an invalid net id and an error message.

In *Blueprints* the caller doesn't need to do anything. The BP-Node will take the login details and a boolean asking whether to create a new user. The node then will internally call the appropriate C++ functions.
