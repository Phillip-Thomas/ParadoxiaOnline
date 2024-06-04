// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParadoxiaPlayerStateComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Controller.h"
#include "ParadoxiaGameInstance.h"

#include "OWSTravelToMapActor.h"
#include "OWSAPISubsystem.h"
#include "OWS2API.h"

#include "Net/UnrealNetwork.h"
#include "Runtime/Engine/Classes/Kismet/GameplayStatics.h"

// Sets default values for this component's properties
UParadoxiaPlayerStateComponent::UParadoxiaPlayerStateComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	// ...
	GConfig->GetString(
		TEXT("/Script/EngineSettings.GeneralProjectSettings"),
		TEXT("OWSAPICustomerKey"),
		OWSAPICustomerKey,
		GGameIni
	);

	GConfig->GetString(
		TEXT("/Script/EngineSettings.GeneralProjectSettings"),
		TEXT("OWS2APIPath"),
		OWS2APIPath,
		GGameIni
	);

	GConfig->GetString(
		TEXT("/Script/EngineSettings.GeneralProjectSettings"),
		TEXT("OWS2InstanceManagementAPIPath"),
		OWS2InstanceManagementAPIPath,
		GGameIni
	);

	GConfig->GetString(
		TEXT("/Script/EngineSettings.GeneralProjectSettings"),
		TEXT("OWS2CharacterPersistenceAPIPath"),
		OWS2CharacterPersistenceAPIPath,
		GGameIni
	);

	GConfig->GetString(
		TEXT("/Script/EngineSettings.GeneralProjectSettings"),
		TEXT("OWSEncryptionKey"),
		OWSEncryptionKey,
		GGameIni
	);

	UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(this);
	//GameInstance will be null on Editor startup, but will have a valid refernce when playing the game
	if (GameInstance)
	{
		InitializeOWSAPISubsystemOnPlayerControllerComponent();
	}
}

void UParadoxiaPlayerStateComponent::InitializeOWSAPISubsystemOnPlayerControllerComponent()
{
	UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(this);
	GameInstance->GetSubsystem<UOWSAPISubsystem>()->OnNotifyCreateCharacterUsingDefaultCharacterValuesDelegate.BindUObject(this, &UParadoxiaPlayerStateComponent::CreateCharacterUsingDefaultCharacterValuesSuccess);
	GameInstance->GetSubsystem<UOWSAPISubsystem>()->OnErrorCreateCharacterUsingDefaultCharacterValuesDelegate.BindUObject(this, &UParadoxiaPlayerStateComponent::CreateCharacterUsingDefaultCharacterValuesError);
	GameInstance->GetSubsystem<UOWSAPISubsystem>()->OnNotifyLogoutDelegate.BindUObject(this, &UParadoxiaPlayerStateComponent::LogoutSuccess);
	GameInstance->GetSubsystem<UOWSAPISubsystem>()->OnErrorLogoutDelegate.BindUObject(this, &UParadoxiaPlayerStateComponent::LogoutError);
}

void UParadoxiaPlayerStateComponent::CreateCharacterUsingDefaultCharacterValuesSuccess()
{
	OnNotifyCreateCharacterUsingDefaultCharacterValuesDelegate.ExecuteIfBound();
}

void UParadoxiaPlayerStateComponent::CreateCharacterUsingDefaultCharacterValuesError(const FString& ErrorMsg)
{
	OnErrorCreateCharacterUsingDefaultCharacterValuesDelegate.ExecuteIfBound(ErrorMsg);
}

void UParadoxiaPlayerStateComponent::LogoutSuccess()
{
	OnNotifyLogoutDelegate.ExecuteIfBound();
}

void UParadoxiaPlayerStateComponent::LogoutError(const FString& ErrorMsg)
{
	OnErrorLogoutDelegate.ExecuteIfBound(ErrorMsg);
}

// Called when the game starts
void UParadoxiaPlayerStateComponent::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogPersistence, Verbose, TEXT("ParadoxiaPlayerStateComponent - Activated"));

	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	if (bAutoStartPersistence || PlayerState || PlayerState->GetOwningController())
	{
		if (PlayerState->GetOwningController()->IsPlayerController())
		{
			UE_LOG(LogPersistence, Verbose, TEXT("Requesting server login for persistence"));
			LoginAndCreateSessionServer(TestEmail, TestPassword);
		}
	}

}




void UParadoxiaPlayerStateComponent::LoginAndCreateSessionServer(FString Email, FString Password)
{
	FLoginAndCreateSessionJSONPost LoginAndCreateSessionJSONPost(Email, Password);
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(LoginAndCreateSessionJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("PublicAPI", "api/Users/LoginAndCreateSession", PostParameters, &UParadoxiaPlayerStateComponent::OnLoginAndCreateSessionResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("LoginAndCreateSession Error serializing FLoginAndCreateSessionJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnLoginAndCreateSessionResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	FString ErrorMsg;
	TSharedPtr<FJsonObject> JsonObject;
	GetJsonObjectFromResponse(Request, Response, bWasSuccessful, "OnLoginAndCreateSessionResponseReceived", ErrorMsg, JsonObject);
	if (!ErrorMsg.IsEmpty())
	{
		ErrorLoginAndCreateSession(ErrorMsg);
		return;
	}

	TSharedPtr<FLoginAndCreateSession> LoginAndCreateSession = GetStructFromJsonObject<FLoginAndCreateSession>(JsonObject);

	if (!LoginAndCreateSession->ErrorMessage.IsEmpty())
	{
		ErrorLoginAndCreateSession(*LoginAndCreateSession->ErrorMessage);
		return;
	}

	ActiveUserSessionGUID = LoginAndCreateSession->UserSessionGUID;
}

void UParadoxiaPlayerStateComponent::GetJsonObjectFromResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FString CallingMethodName, FString& ErrorMsg, TSharedPtr<FJsonObject>& JsonObject)
{
	if (bWasSuccessful && Response.IsValid())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

		if (FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			ErrorMsg = "";
			return;
		}
		else
		{
			UE_LOG(LogPersistence, Error, TEXT("%s - Error Deserializing JsonObject!"), *CallingMethodName);
			ErrorMsg = CallingMethodName + " - Error Deserializing JsonObject!";
		}
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("%s - Response was unsuccessful or invalid!"), *CallingMethodName);
		ErrorMsg = CallingMethodName + " - Response was unsuccessful or invalid!";
	}
}

void UParadoxiaPlayerStateComponent::GetPlayerNameAndCharacter(ACharacter* Character, FString& PlayerName)
{
	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());

	//Get the player name from the Player State
	PlayerName = PlayerState->GetPlayerName();
	//Trim whitespace from the start and end
	PlayerName = PlayerName.TrimStartAndEnd();

	Character = Cast<ACharacter>(PlayerState->GetPlayerController()->GetPawn());
}

APlayerState* UParadoxiaPlayerStateComponent::GetPlayerState() const
{
	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());

	return PlayerState;
}

void UParadoxiaPlayerStateComponent::TravelToMap(const FString& URL, const bool SeamlessTravel)
{
	APlayerController* PlayerController = Cast<APlayerController>(GetPlayerState()->GetPlayerController());

	UE_LOG(LogPersistence, Warning, TEXT("TravelToMap: %s"), *URL);
	PlayerController->ClientTravel(URL, TRAVEL_Absolute, false, FGuid());
}

void UParadoxiaPlayerStateComponent::TravelToMap2(const FString& ServerAndPort, const float X, const float Y, const float Z, const float RX, const float RY,
	const float RZ, const FString& PlayerName, const bool SeamlessTravel)
{
	APlayerController* PlayerController = Cast<APlayerController>(GetOwner());

	if (!GetWorld())
	{
		return;
	}

	UParadoxiaGameInstance* GameInstance = Cast<UParadoxiaGameInstance>(GetWorld()->GetGameInstance());

	if (!GameInstance)
	{
		return;
	}

	if (!GetPlayerState())
	{
		UE_LOG(LogPersistence, Error, TEXT("Invalid OWS Player State!  You can no longer use TravelToMap2 to connect to a server from the Select Character screen!"));

		return;
	}

	//Build our connection string
	FString IDData = FString::SanitizeFloat(X)
		+ "|" + FString::SanitizeFloat(Y)
		+ "|" + FString::SanitizeFloat(Z)
		+ "|" + FString::SanitizeFloat(RX)
		+ "|" + FString::SanitizeFloat(RY)
		+ "|" + FString::SanitizeFloat(RZ)
		+ "|" + FGenericPlatformHttp::UrlEncode(GetPlayerState()->GetPlayerName())
		+ "|" + ActiveUserSessionGUID;

	//Encrypte the connection string using the key found in DefaultGame.ini called OWSEncryptionKey
	FString EncryptedIDData = GameInstance->EncryptWithAES(IDData, OWSEncryptionKey);

	//The encrypted connection string is sent to the UE server as the ID parameter
	FString URL = ServerAndPort
		+ FString(TEXT("?ID=")) + EncryptedIDData;

	//This is not an actual warning.  Yellow text is just easier to read.
	UE_LOG(LogPersistence, Warning, TEXT("TravelToMap: %s"), *URL);
	PlayerController->ClientTravel(URL, TRAVEL_Absolute, false, FGuid());
}

/*
Valid values for ApiModuleToCall:
	PublicAPI
	InstanceManagementAPI
	CharacterPersistanceAPI
*/
void UParadoxiaPlayerStateComponent::ProcessOWS2POSTRequest(FString ApiModuleToCall, FString ApiToCall, FString PostParameters, void (UParadoxiaPlayerStateComponent::* InMethodPtr)(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful))
{
	Http = &FHttpModule::Get();
	Http->SetHttpTimeout(TravelTimeout); //Set timeout
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(this, InMethodPtr);

	FString OWS2APIPathToUse = "";

	if (ApiModuleToCall == "PublicAPI")
	{
		OWS2APIPathToUse = OWS2APIPath;
	}
	else if (ApiModuleToCall == "InstanceManagementAPI")
	{
		OWS2APIPathToUse = OWS2InstanceManagementAPIPath;
	}
	else if (ApiModuleToCall == "CharacterPersistenceAPI")
	{
		OWS2APIPathToUse = OWS2CharacterPersistenceAPIPath;
	}
	else //When an ApiModuleToCall is not specified, use the PublicAPI
	{
		OWS2APIPathToUse = OWS2APIPath;
	}

	Request->SetURL(FString(OWS2APIPathToUse + ApiToCall));
	Request->SetVerb("POST");
	Request->SetHeader(TEXT("User-Agent"), "X-UnrealEngine-Agent");
	Request->SetHeader("Content-Type", TEXT("application/json"));
	Request->SetHeader(TEXT("X-CustomerGUID"), OWSAPICustomerKey);
	Request->SetContentAsString(PostParameters);
	Request->ProcessRequest();
}

//SetSelectedCharacterAndConnectToLastZone - Set character name and get user session
void UParadoxiaPlayerStateComponent::SetSelectedCharacterAndConnectToLastZone(FString UserSessionGUID, FString SelectedCharacterName)
{
	//Trim whitespace
	SelectedCharacterName.TrimStartAndEndInline();

	FSetSelectedCharacterAndConnectToLastZoneJSONPost SetSelectedCharacterAndConnectToLastZoneJSONPost;
	SetSelectedCharacterAndConnectToLastZoneJSONPost.UserSessionGUID = UserSessionGUID;
	SetSelectedCharacterAndConnectToLastZoneJSONPost.SelectedCharacterName = SelectedCharacterName;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(SetSelectedCharacterAndConnectToLastZoneJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("PublicAPI", "api/Users/SetSelectedCharacterAndGetUserSession", PostParameters, &UParadoxiaPlayerStateComponent::OnSetSelectedCharacterAndConnectToLastZoneResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("SetSelectedCharacterAndConnectToLastZone Error serializing SetSelectedCharacterAndConnectToLastZoneJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnSetSelectedCharacterAndConnectToLastZoneResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		UE_LOG(LogPersistence, Verbose, TEXT("OnSetSelectedCharacterAndConnectToLastZone Success!"));

		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

		if (FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			ServerTravelUserSessionGUID = JsonObject->GetStringField("UserSessionGUID");
			ServerTravelCharacterName = JsonObject->GetStringField("CharName");
			ServerTravelX = JsonObject->GetNumberField("X");
			ServerTravelY = JsonObject->GetNumberField("Y");
			ServerTravelZ = JsonObject->GetNumberField("Z");
			ServerTravelRX = JsonObject->GetNumberField("RX");
			ServerTravelRY = JsonObject->GetNumberField("RY");
			ServerTravelRZ = JsonObject->GetNumberField("RZ");

			UE_LOG(LogPersistence, Log, TEXT("OnSetSelectedCharacterAndConnectToLastZone location is %f, %f, %f"), ServerTravelX, ServerTravelY, ServerTravelZ);

			if (ServerTravelCharacterName.IsEmpty())
			{
				//Call Error BP Event

				return;
			}

			TravelToLastZoneServer(ServerTravelCharacterName);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("OnSetSelectedCharacterAndConnectToLastZone Server returned no data!"));
		}
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnSetSelectedCharacterAndConnectToLastZone Error accessing server!"));
	}
}

//TravelToLastZoneServer
void UParadoxiaPlayerStateComponent::TravelToLastZoneServer(FString CharacterName)
{
	FTravelToLastZoneServerJSONPost TravelToLastZoneServerJSONPost;
	TravelToLastZoneServerJSONPost.CharacterName = CharacterName;
	TravelToLastZoneServerJSONPost.ZoneName = "GETLASTZONENAME";
	TravelToLastZoneServerJSONPost.PlayerGroupType = 0;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(TravelToLastZoneServerJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("PublicAPI", "api/Users/GetServerToConnectTo", PostParameters, &UParadoxiaPlayerStateComponent::OnTravelToLastZoneServerResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("TravelToLastZoneServer Error serializing TravelToLastZoneServerJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnTravelToLastZoneServerResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	FString ServerAndPort;

	if (!GetWorld())
	{
		return;
	}

	UParadoxiaGameInstance* GameInstance = Cast<UParadoxiaGameInstance>(GetWorld()->GetGameInstance());

	if (!GameInstance)
	{
		return;
	}

	if (bWasSuccessful)
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

		if (FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			FString ServerIP = JsonObject->GetStringField("serverip");
			FString Port = JsonObject->GetStringField("port");

			if (ServerIP.IsEmpty() || Port.IsEmpty())
			{
				UE_LOG(LogPersistence, Error, TEXT("OnTravelToLastZoneServerResponseReceived Cannot Get Server IP and Port!"));
				return;
			}

			ServerAndPort = ServerIP + FString(TEXT(":")) + Port.Left(4);

			UE_LOG(LogPersistence, Warning, TEXT("OnTravelToLastZoneServerResponseReceived ServerAndPort: %s"), *ServerAndPort);

			//Encrypt data to send
			FString IDData = FString::SanitizeFloat(ServerTravelX)
				+ "|" + FString::SanitizeFloat(ServerTravelY)
				+ "|" + FString::SanitizeFloat(ServerTravelZ)
				+ "|" + FString::SanitizeFloat(ServerTravelRX)
				+ "|" + FString::SanitizeFloat(ServerTravelRY)
				+ "|" + FString::SanitizeFloat(ServerTravelRZ)
				+ "|" + FGenericPlatformHttp::UrlEncode(ServerTravelCharacterName)
				+ "|" + ServerTravelUserSessionGUID;
			FString EncryptedIDData = GameInstance->EncryptWithAES(IDData, OWSEncryptionKey);

			FString URL = ServerAndPort
				+ FString(TEXT("?ID=")) + EncryptedIDData;

			TravelToMap(URL, false);
		}
		else
		{
			UE_LOG(LogPersistence, Error, TEXT("OnTravelToLastZoneServerResponseReceived Server returned no data!"));
		}
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnTravelToLastZoneServerResponseReceived Error accessing server!"));
	}
}

//GetZoneServerToTravelTo
void UParadoxiaPlayerStateComponent::GetZoneServerToTravelTo(FString CharacterName, TEnumAsByte<ERPGSchemeToChooseMap::SchemeToChooseMap> SelectedSchemeToChooseMap, int32 WorldServerID, FString ZoneName)
{
	FTravelToLastZoneServerJSONPost TravelToLastZoneServerJSONPost;
	TravelToLastZoneServerJSONPost.CharacterName = CharacterName;
	TravelToLastZoneServerJSONPost.ZoneName = ZoneName;
	TravelToLastZoneServerJSONPost.PlayerGroupType = 0;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(TravelToLastZoneServerJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("PublicAPI", "api/Users/GetServerToConnectTo", PostParameters, &UParadoxiaPlayerStateComponent::OnGetZoneServerToTravelToResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("GetMapServerToTravelTo Error serializing TravelToLastZoneServerJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnGetZoneServerToTravelToResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	FString ServerAndPort;

	if (bWasSuccessful)
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

		if (FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			FString ServerIP = JsonObject->GetStringField("serverip");
			FString Port = JsonObject->GetStringField("port");

			if (ServerIP.IsEmpty() || Port.IsEmpty())
			{
				OnErrorGetZoneServerToTravelToDelegate.ExecuteIfBound(TEXT("Cannot connect to server!"));
				return;
			}

			ServerAndPort = ServerIP + FString(TEXT(":")) + Port.Left(4);

			UE_LOG(LogTemp, Warning, TEXT("ServerAndPort: %s"), *ServerAndPort);

			OnNotifyGetZoneServerToTravelToDelegate.ExecuteIfBound(ServerAndPort);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("OnGetZoneServerToTravelToResponseReceived Server returned no data!"));
			OnErrorGetZoneServerToTravelToDelegate.ExecuteIfBound(TEXT("There was a problem connecting to the server.  Please try again."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("OnGetZoneServerToTravelToResponseReceived Error accessing server!"));
		OnErrorGetZoneServerToTravelToDelegate.ExecuteIfBound(TEXT("Unknown error connecting to server!"));
	}
}

void UParadoxiaPlayerStateComponent::SavePlayerLocation()
{
	//Not implemented
}

void UParadoxiaPlayerStateComponent::OnSavePlayerLocationResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		UE_LOG(LogPersistence, Verbose, TEXT("OnSavePlayerLocationResponseReceived Success!"));
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnSavePlayerLocationResponseReceived Error accessing server!"));
	}
}

//GetAllCharacters
void UParadoxiaPlayerStateComponent::GetAllCharacters(FString UserSessionGUID)
{
	FGetAllCharactersJSONPost GetAllCharactersJSONPost;
	GetAllCharactersJSONPost.UserSessionGUID = UserSessionGUID;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(GetAllCharactersJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("PublicAPI", "api/Users/GetAllCharacters", PostParameters, &UParadoxiaPlayerStateComponent::OnGetAllCharactersResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("GetAllCharacters Error serializing GetAllCharactersJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnGetAllCharactersResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		TArray<FUserCharacter> UsersCharactersData;
		if (FJsonObjectConverter::JsonArrayStringToUStruct(Response->GetContentAsString(), &UsersCharactersData, 0, 0))
		{
			OnNotifyGetAllCharactersDelegate.ExecuteIfBound(UsersCharactersData);
		}
		else
		{
			OnErrorGetAllCharactersDelegate.ExecuteIfBound(TEXT("OnGetAllCharactersResponseReceived Error Parsing JSON!"));
		}
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnGetAllCharactersResponseReceived Error accessing login server!"));
		OnErrorGetAllCharactersDelegate.ExecuteIfBound(TEXT("Unknown error connecting to server!"));
	}
}

//GetCharacterStats
void UParadoxiaPlayerStateComponent::GetCharacterStats(FString CharName)
{
	FGetCharacterStatsJSONPost GetCharacterStatsJSONPost;
	GetCharacterStatsJSONPost.CharacterName = CharName;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(GetCharacterStatsJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("CharacterPersistenceAPI", "api/Characters/GetByName", PostParameters, &UParadoxiaPlayerStateComponent::OnGetCharacterStatsResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("GetCharacterStats Error serializing GetCharacterStatsJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnGetCharacterStatsResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

		if (FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			OnNotifyGetCharacterStatsDelegate.ExecuteIfBound(JsonObject);
		}
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnGetAllCharactersResponseReceived Error accessing server!"));
		OnErrorGetCharacterStatsDelegate.ExecuteIfBound(TEXT("Unknown error connecting to server!"));
	}
}

//GetCharacterDataAndCustomData - This makes a call to the OWS Public API and is usable from the Character Selection screen.
void UParadoxiaPlayerStateComponent::GetCharacterDataAndCustomData(FString UserSessionGUID, FString CharName)
{
	FGetCharacterDataAndCustomData GetCharacterDataAndCustomDataJSONPost;
	GetCharacterDataAndCustomDataJSONPost.UserSessionGUID = UserSessionGUID;
	GetCharacterDataAndCustomDataJSONPost.CharacterName = CharName;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(GetCharacterDataAndCustomDataJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("OWSPublicAPI", "api/Characters/ByName", PostParameters, &UParadoxiaPlayerStateComponent::OnGetCharacterDataAndCustomDataResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("GetCharacterDataAndCustomData Error serializing GetCharacterStatsJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnGetCharacterDataAndCustomDataResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

		if (FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			OnNotifyGetCharacterDataAndCustomDataDelegate.ExecuteIfBound(JsonObject);
		}
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnGetCharacterDataAndCustomDataResponseReceived Error accessing server!"));
		OnErrorGetCharacterDataAndCustomDataDelegate.ExecuteIfBound(TEXT("Unknown error connecting to server!"));
	}
}

//Update Character Stats
void UParadoxiaPlayerStateComponent::UpdateCharacterStats(FString JSONString)
{
	ProcessOWS2POSTRequest("CharacterPersistenceAPI", "api/Characters/UpdateCharacterStats", JSONString, &UParadoxiaPlayerStateComponent::OnUpdateCharacterStatsResponseReceived);
}

void UParadoxiaPlayerStateComponent::OnUpdateCharacterStatsResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	FString ErrorMsg;
	TSharedPtr<FJsonObject> JsonObject;
	GetJsonObjectFromResponse(Request, Response, bWasSuccessful, "OnUpdateCharacterStatsResponseReceived", ErrorMsg, JsonObject);
	if (!ErrorMsg.IsEmpty())
	{
		OnErrorUpdateCharacterStatsDelegate.ExecuteIfBound(ErrorMsg);
		return;
	}

	TSharedPtr<FSuccessAndErrorMessage> SuccessAndErrorMessage = GetStructFromJsonObject<FSuccessAndErrorMessage>(JsonObject);

	if (!SuccessAndErrorMessage->ErrorMessage.IsEmpty())
	{
		OnErrorUpdateCharacterStatsDelegate.ExecuteIfBound(*SuccessAndErrorMessage->ErrorMessage);
		return;
	}

	OnNotifyUpdateCharacterStatsDelegate.ExecuteIfBound();
}

//GetCustomCharacterData
void UParadoxiaPlayerStateComponent::GetCustomCharacterData(FString CharName)
{
	FGetCustomCharacterDataJSONPost GetCustomCharacterDataJSONPost;
	GetCustomCharacterDataJSONPost.CharacterName = CharName;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(GetCustomCharacterDataJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("CharacterPersistenceAPI", "api/Characters/GetCustomData", PostParameters, &UParadoxiaPlayerStateComponent::OnGetCustomCharacterDataResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("GetCustomCharacterData Error serializing GetCharacterStatsJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnGetCustomCharacterDataResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

		if (FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			OnNotifyGetCustomCharacterDataDelegate.ExecuteIfBound(JsonObject);
		}
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnGetCustomCharacterDataResponseReceived Error accessing server!"));
		OnErrorGetCustomCharacterDataDelegate.ExecuteIfBound(TEXT("Unknown error connecting to server!"));
	}
}

//AddOrUpdateCustomCharacterData
void UParadoxiaPlayerStateComponent::AddOrUpdateCustomCharacterData(FString CharName, FString CustomFieldName, FString CustomValue)
{
	FAddOrUpdateCustomCharacterDataJSONPost AddOrUpdateCustomCharacterDataJSONPost;
	AddOrUpdateCustomCharacterDataJSONPost.AddOrUpdateCustomCharacterData.CharacterName = CharName;
	AddOrUpdateCustomCharacterDataJSONPost.AddOrUpdateCustomCharacterData.CustomFieldName = CustomFieldName;
	AddOrUpdateCustomCharacterDataJSONPost.AddOrUpdateCustomCharacterData.FieldValue = CustomValue;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(AddOrUpdateCustomCharacterDataJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("CharacterPersistenceAPI", "api/Characters/AddOrUpdateCustomData", PostParameters, &UParadoxiaPlayerStateComponent::OnAddOrUpdateCustomCharacterDataResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("AddOrUpdateCustomCharacterData Error serializing GetCharacterStatsJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnAddOrUpdateCustomCharacterDataResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		OnNotifyAddOrUpdateCustomCharacterDataDelegate.ExecuteIfBound();
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnAddOrUpdateCustomCharacterDataResponseReceived Error accessing login server!"));
		OnErrorAddOrUpdateCustomCharacterDataDelegate.ExecuteIfBound(TEXT("Unknown error connecting to server!"));
	}
}


void UParadoxiaPlayerStateComponent::SaveAllPlayerData()
{
	//Not implemented
}

void UParadoxiaPlayerStateComponent::OnSaveAllPlayerDataResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		UE_LOG(LogPersistence, Verbose, TEXT("OnSaveAllPlayerDataResponseReceived Success!"));
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnSaveAllPlayerDataResponseReceived Error accessing server!"));
	}
}

void UParadoxiaPlayerStateComponent::GetChatGroupsForPlayer()
{
	//Not implemented
}

void UParadoxiaPlayerStateComponent::OnGetChatGroupsForPlayerResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		UE_LOG(LogPersistence, Verbose, TEXT("OnGetChatGroupsForPlayerResponseReceived Success!"));

		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

		if (FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			if (JsonObject->GetStringField("success") == "true")
			{
				TArray<TSharedPtr<FJsonValue>> Rows = JsonObject->GetArrayField("rows");

				TArray<FChatGroup> ChatGroups;

				for (int RowNum = 0; RowNum != Rows.Num(); RowNum++) {
					FChatGroup tempChatGroup;
					TSharedPtr<FJsonObject> tempRow = Rows[RowNum]->AsObject();
					tempChatGroup.ChatGroupID = tempRow->GetIntegerField("ChatGroupID");
					tempChatGroup.ChatGroupName = tempRow->GetStringField("ChatGroupName");

					ChatGroups.Add(tempChatGroup);
				}

				OnNotifyGetChatGroupsForPlayerDelegate.ExecuteIfBound(ChatGroups);
			}
		}
		else
		{
			UE_LOG(LogPersistence, Warning, TEXT("OnGetChatGroupsForPlayerResponseReceived:  Either there were no chat groups or the JSON failed to Deserialize!"));
		}
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnGetChatGroupsForPlayerResponseReceived Error accessing server!"));
		OnErrorGetChatGroupsForPlayerDelegate.ExecuteIfBound(TEXT("OnGetChatGroupsForPlayerResponseReceived Error accessing server!"));
	}
}

void UParadoxiaPlayerStateComponent::GetCharacterStatuses()
{
	//Not Implemented
}

void UParadoxiaPlayerStateComponent::OnGetCharacterStatusesResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

		if (FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			//CharacterName = JsonObject->GetStringField("CharacterName");
		}
		else
		{
			UE_LOG(LogPersistence, Error, TEXT("OnGetCharacterStatusesResponseReceived Server returned no data!"));
		}
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnGetCharacterStatusesResponseReceived Error accessing server!"));
	}
}

/***** Abilities *****/

//AddAbilityToCharacter
void UParadoxiaPlayerStateComponent::AddAbilityToCharacter(FString CharName, FString AbilityName, int32 AbilityLevel, FString CustomJSON)
{
	FAddAbilityToCharacterJSONPost AddAbilityToCharacterJSONPost;
	AddAbilityToCharacterJSONPost.CharacterName = CharName;
	AddAbilityToCharacterJSONPost.AbilityName = AbilityName;
	AddAbilityToCharacterJSONPost.AbilityLevel = AbilityLevel;
	AddAbilityToCharacterJSONPost.CharHasAbilitiesCustomJSON = CustomJSON;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(AddAbilityToCharacterJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("CharacterPersistenceAPI", "api/Abilities/AddAbilityToCharacter", PostParameters, &UParadoxiaPlayerStateComponent::OnAddAbilityToCharacterResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("AddAbilityToCharacter Error serializing AddAbilityToCharacterJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnAddAbilityToCharacterResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		OnNotifyAddAbilityToCharacterDelegate.ExecuteIfBound();
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnAddAbilityToCharacterResponseReceived Error accessing server!"));
		OnErrorAddAbilityToCharacterDelegate.ExecuteIfBound(TEXT("Unknown error connecting to server!"));
	}
}

//GetCharacterAbilities
void UParadoxiaPlayerStateComponent::GetCharacterAbilities(FString CharName)
{
	FCharacterNameJSONPost CharacterNameJSONPost;
	CharacterNameJSONPost.CharacterName = CharName;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(CharacterNameJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("CharacterPersistenceAPI", "api/Abilities/GetCharacterAbilities", PostParameters, &UParadoxiaPlayerStateComponent::OnGetCharacterAbilitiesResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("GetCharacterAbilities Error serializing GetCharacterStatsJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnGetCharacterAbilitiesResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

		if (FJsonSerializer::Deserialize(Reader, JsonArray))
		{
			TArray<FAbility> Abilities;

			FJsonObjectConverter::JsonArrayToUStruct(JsonArray, &Abilities, 0, 0);

			OnNotifyGetCharacterAbilitiesDelegate.ExecuteIfBound(Abilities);
		}
		else
		{
			UE_LOG(LogPersistence, Error, TEXT("OnGetCharacterAbilitiesResponseReceived Server returned no data!"));
			OnErrorGetCharacterAbilitiesDelegate.ExecuteIfBound(TEXT("OnGetCharacterAbilitiesResponseReceived Server returned no data!"));
		}
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnGetCharacterAbilitiesResponseReceived Error accessing server!"));
		OnErrorGetCharacterAbilitiesDelegate.ExecuteIfBound(TEXT("OnGetCharacterAbilitiesResponseReceived Error accessing server!"));
	}
}

//GetAbilityBars
void UParadoxiaPlayerStateComponent::GetAbilityBars(FString CharName)
{
	FCharacterNameJSONPost CharacterNameJSONPost;
	CharacterNameJSONPost.CharacterName = CharName;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(CharacterNameJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("CharacterPersistenceAPI", "api/Abilities/GetAbilityBars", PostParameters, &UParadoxiaPlayerStateComponent::OnGetAbilityBarsResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("GetAbilityBars Error serializing GetCharacterStatsJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnGetAbilityBarsResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

		if (FJsonSerializer::Deserialize(Reader, JsonArray))
		{
			TArray<FAbilityBar> AbilityBars;

			FJsonObjectConverter::JsonArrayToUStruct(JsonArray, &AbilityBars, 0, 0);

			OnNotifyGetAbilityBarsDelegate.ExecuteIfBound(AbilityBars);
		}
		else
		{
			UE_LOG(LogPersistence, Error, TEXT("OnGetAbilityBarsResponseReceived Server returned no data!"));
			OnErrorGetAbilityBarsDelegate.ExecuteIfBound(TEXT("OnGetAbilityBarsResponseReceived Server returned no data!"));
		}
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnGetAbilityBarsResponseReceived Error accessing server!"));
		OnErrorGetAbilityBarsDelegate.ExecuteIfBound(TEXT("OnGetAbilityBarsResponseReceived Error accessing API server!"));
	}
}

//UpdateAbilityOnCharacter
void UParadoxiaPlayerStateComponent::UpdateAbilityOnCharacter(FString CharName, FString AbilityName, int32 AbilityLevel, FString CustomJSON)
{
	FUpdateAbilityOnCharacterJSONPost UpdateAbilityOnCharacterJSONPost;
	UpdateAbilityOnCharacterJSONPost.CharacterName = CharName;
	UpdateAbilityOnCharacterJSONPost.AbilityName = AbilityName;
	UpdateAbilityOnCharacterJSONPost.AbilityLevel = AbilityLevel;
	UpdateAbilityOnCharacterJSONPost.CharHasAbilitiesCustomJSON = CustomJSON;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(UpdateAbilityOnCharacterJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("CharacterPersistenceAPI", "api/Abilities/UpdateAbilityOnCharacter", PostParameters, &UParadoxiaPlayerStateComponent::OnUpdateAbilityOnCharacterResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("UpdateAbilityOnCharacter Error serializing UpdateAbilityOnCharacterJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnUpdateAbilityOnCharacterResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		OnNotifyAddAbilityToCharacterDelegate.ExecuteIfBound();
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnUpdateAbilityOnCharacterResponseReceived Error accessing server!"));
		OnErrorAddAbilityToCharacterDelegate.ExecuteIfBound(TEXT("Unknown error accessing API server!"));
	}
}

//RemoveAbilityFromCharacter
void UParadoxiaPlayerStateComponent::RemoveAbilityFromCharacter(FString CharName, FString AbilityName)
{
	FRemoveAbilityFromCharacterJSONPost RemoveAbilityFromCharacterJSONPost;
	RemoveAbilityFromCharacterJSONPost.CharacterName = CharName;
	RemoveAbilityFromCharacterJSONPost.AbilityName = AbilityName;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(RemoveAbilityFromCharacterJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("CharacterPersistenceAPI", "api/Abilities/RemoveAbilityFromCharacter", PostParameters, &UParadoxiaPlayerStateComponent::OnRemoveAbilityFromCharacterResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("RemoveAbilityFromCharacter Error serializing RemoveAbilityFromCharacterJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnRemoveAbilityFromCharacterResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		OnNotifyRemoveAbilityFromCharacterDelegate.ExecuteIfBound();
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnRemoveAbilityFromCharacterResponseReceived Error accessing server!"));
		OnErrorRemoveAbilityFromCharacterDelegate.ExecuteIfBound(TEXT("Unknown error accessing API server!"));
	}
}

//PlayerLogout
void UParadoxiaPlayerStateComponent::PlayerLogout(FString CharName)
{
	//CharName will be empty if the user has not connected to a server yet.  This logout method is not intended to be used during the character selection screen.
	if (CharName.IsEmpty())
	{
		return;
	}

	FCharacterNameJSONPost CharacterNameJSONPost;
	CharacterNameJSONPost.CharacterName = CharName;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(CharacterNameJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("CharacterPersistenceAPI", "api/Characters/PlayerLogout", PostParameters, &UParadoxiaPlayerStateComponent::OnPlayerLogoutResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("PlayerLogout Error serializing GetCharacterStatsJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnPlayerLogoutResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

		if (FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			UE_LOG(LogPersistence, Verbose, TEXT("Logout Successful!"));
			OnNotifyPlayerLogoutDelegate.ExecuteIfBound();
		}
		else
		{
			UE_LOG(LogPersistence, Error, TEXT("OnPlayerLogoutResponseReceived Server returned no data!"));
			OnErrorPlayerLogoutDelegate.ExecuteIfBound(TEXT("Server returned no data!"));
		}
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnPlayerLogoutResponseReceived Error accessing server!"));
		OnErrorPlayerLogoutDelegate.ExecuteIfBound(TEXT("Error accessing server!"));
	}
}

//CreateCharacter
void UParadoxiaPlayerStateComponent::CreateCharacter(FString UserSessionGUID, FString CharacterName, FString ClassName)
{
	FCreateCharacterJSONPost CreateCharacterJSONPost;
	CreateCharacterJSONPost.UserSessionGUID = UserSessionGUID;
	CreateCharacterJSONPost.CharacterName = CharacterName;
	CreateCharacterJSONPost.ClassName = ClassName;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(CreateCharacterJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("PublicAPI", "api/Users/CreateCharacter", PostParameters, &UParadoxiaPlayerStateComponent::OnCreateCharacterResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("CreateCharacter Error serializing CreateCharacterJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnCreateCharacterResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		FString ResponseString = Response->GetContentAsString();

		FCreateCharacter CreateCharacter;

		if (!FJsonObjectConverter::JsonObjectStringToUStruct(ResponseString, &CreateCharacter, 0, 0))
		{
			OnErrorCreateCharacterDelegate.ExecuteIfBound(TEXT("Could not deserialize CreateCharacter JSON to CreateCharacter struct!"));
			return;
		}

		if (!CreateCharacter.ErrorMessage.IsEmpty())
		{
			OnErrorCreateCharacterDelegate.ExecuteIfBound(*CreateCharacter.ErrorMessage);
			return;
		}

		UE_LOG(LogPersistence, Verbose, TEXT("OnCreateCharacterResponseReceived Success!"));
		OnNotifyCreateCharacterDelegate.ExecuteIfBound(CreateCharacter);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnCreateCharacterResponseReceived Error accessing login server!"));
		OnErrorCreateCharacterDelegate.ExecuteIfBound(TEXT("Unknown error connecting to server!"));
	}
}

//Create Character Using Default Character Values
void UParadoxiaPlayerStateComponent::CreateCharacterUsingDefaultCharacterValues(FString UserSessionGUID, FString CharacterName, FString DefaultSetName)
{
	UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(this);
	GameInstance->GetSubsystem<UOWSAPISubsystem>()->CreateCharacterUsingDefaultCharacterValues(UserSessionGUID, CharacterName, DefaultSetName);
}

//Logout
void UParadoxiaPlayerStateComponent::Logout(FString UserSessionGUID)
{
	UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(this);
	GameInstance->GetSubsystem<UOWSAPISubsystem>()->Logout(UserSessionGUID);
}

//Remove Character
void UParadoxiaPlayerStateComponent::RemoveCharacter(FString UserSessionGUID, FString CharacterName)
{
	FRemoveCharacterJSONPost RemoveCharacterJSONPost;
	RemoveCharacterJSONPost.UserSessionGUID = UserSessionGUID;
	RemoveCharacterJSONPost.CharacterName = CharacterName;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(RemoveCharacterJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("PublicAPI", "api/Users/RemoveCharacter", PostParameters, &UParadoxiaPlayerStateComponent::OnRemoveCharacterResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("RemoveCharacter Error serializing RemoveCharacterJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnRemoveCharacterResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		FString ResponseString = Response->GetContentAsString();
		FSuccessAndErrorMessage SuccessAndErrorMessage;
		if (!FJsonObjectConverter::JsonObjectStringToUStruct(ResponseString, &SuccessAndErrorMessage, 0, 0))
		{
			UE_LOG(LogPersistence, Error, TEXT("OnRemoveCharacterResponseReceived Error deserializing SuccessAndErrorMessage!"));
			OnErrorRemoveCharacterDelegate.ExecuteIfBound(TEXT("OnRemoveCharacterResponseReceived Error deserializing SuccessAndErrorMessage!"));
		}

		if (!SuccessAndErrorMessage.ErrorMessage.IsEmpty())
		{
			OnErrorRemoveCharacterDelegate.ExecuteIfBound(*SuccessAndErrorMessage.ErrorMessage);
			return;
		}

		UE_LOG(LogPersistence, Verbose, TEXT("OnRemoveCharacterResponseReceived Success!"));
		OnNotifyRemoveCharacterDelegate.ExecuteIfBound();
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnRemoveCharacterResponseReceived Error accessing server!"));
		OnErrorRemoveCharacterDelegate.ExecuteIfBound(TEXT("OnRemoveCharacterResponseReceived Error accessing server!"));
	}
}

//GetPlayerGroupsCharacterIsIn
void UParadoxiaPlayerStateComponent::GetPlayerGroupsCharacterIsIn(FString UserSessionGUID, FString CharacterName, int32 PlayerGroupTypeID)
{
	FGetPlayerGroupsCharacterIsInJSONPost GetPlayerGroupsCharacterIsInJSONPost;
	GetPlayerGroupsCharacterIsInJSONPost.UserSessionGUID = UserSessionGUID;
	GetPlayerGroupsCharacterIsInJSONPost.CharacterName = CharacterName;
	GetPlayerGroupsCharacterIsInJSONPost.PlayerGroupTypeID = PlayerGroupTypeID;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(GetPlayerGroupsCharacterIsInJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("PublicAPI", "api/Users/GetPlayerGroupsCharacterIsIn", PostParameters, &UParadoxiaPlayerStateComponent::OnGetPlayerGroupsCharacterIsInResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("GetPlayerGroupsCharacterIsIn Error serializing GetPlayerGroupsCharacterIsInJSONPost!"));
	}
}

void UParadoxiaPlayerStateComponent::OnGetPlayerGroupsCharacterIsInResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

		if (FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			FString Success = JsonObject->GetStringField("success");

			if (Success == "true")
			{
				if (JsonObject->HasField("rows"))
				{
					TArray<TSharedPtr<FJsonValue>> Rows = JsonObject->GetArrayField("rows");
					TArray<FPlayerGroup> tempPlayerGroups;

					for (int RowNum = 0; RowNum != Rows.Num(); RowNum++) {
						FPlayerGroup tempPlayerGroup;
						TSharedPtr<FJsonObject> tempRow = Rows[RowNum]->AsObject();
						tempPlayerGroup.PlayerGroupID = tempRow->GetNumberField("PlayerGroupID");
						tempPlayerGroup.PlayerGroupName = tempRow->GetStringField("PlayerGroupName");
						tempPlayerGroup.PlayerGroupTypeID = tempRow->GetNumberField("PlayerGroupTypeID");
						tempPlayerGroup.ReadyState = tempRow->GetNumberField("ReadyState");
						tempPlayerGroup.TeamNumber = tempRow->GetNumberField("TeamNumber");

						FDateTime OutDateTime;
						FDateTime::Parse(tempRow->GetStringField("DateAdded"), OutDateTime);
						tempPlayerGroup.DateAdded = OutDateTime;

						tempPlayerGroups.Add(tempPlayerGroup);
					}

					OnNotifyGetPlayerGroupsCharacterIsInDelegate.ExecuteIfBound(tempPlayerGroups);
				}
				OnErrorGetPlayerGroupsCharacterIsInDelegate.ExecuteIfBound(TEXT("OnGetPlayerGroupsCharacterIsInResponseReceived No rows in JSON!"));
			}
			else
			{
				FString ErrorMessage = JsonObject->GetStringField("errmsg");
				OnErrorGetPlayerGroupsCharacterIsInDelegate.ExecuteIfBound(ErrorMessage);
			}
		}
		else
		{
			UE_LOG(LogPersistence, Error, TEXT("OnGetPlayerGroupsCharacterIsInResponseReceived Server returned no data!"));
			OnErrorGetPlayerGroupsCharacterIsInDelegate.ExecuteIfBound(TEXT("OnGetPlayerGroupsCharacterIsInResponseReceived Server returned no data!"));
		}
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnGetPlayerGroupsCharacterIsInResponseReceived Error accessing server!"));
		OnErrorGetPlayerGroupsCharacterIsInDelegate.ExecuteIfBound(TEXT("OnGetPlayerGroupsCharacterIsInResponseReceived Error accessing server!"));
	}
}

//LaunchZoneInstance
void UParadoxiaPlayerStateComponent::LaunchZoneInstance(FString CharacterName, FString ZoneName, ERPGPlayerGroupType::PlayerGroupType GroupType)
{
	FLaunchZoneInstance LaunchZoneInstance;
	LaunchZoneInstance.CharacterName = CharacterName;
	LaunchZoneInstance.ZoneName = ZoneName;
	LaunchZoneInstance.PlayerGroupTypeID = GroupType;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(LaunchZoneInstance, PostParameters))
	{
		ProcessOWS2POSTRequest("PublicAPI", "api/Users/GetServerToConnectTo", PostParameters, &UParadoxiaPlayerStateComponent::OnLaunchZoneInstanceResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("LaunchZoneInstance Error serializing LaunchZoneInstance!"));
	}
}

void UParadoxiaPlayerStateComponent::OnLaunchZoneInstanceResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	FString ServerAndPort;

	if (bWasSuccessful)
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

		if (FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			FString ServerIP = JsonObject->GetStringField("serverip");
			FString Port = JsonObject->GetStringField("port");

			ServerAndPort = ServerIP + FString(TEXT(":")) + Port;

			UE_LOG(LogPersistence, Verbose, TEXT("OnLaunchZoneInstanceResponseReceived: %s:%s"), *ServerIP, *Port);

			OnNotifyLaunchZoneInstanceDelegate.ExecuteIfBound(ServerAndPort);
		}
		else
		{
			UE_LOG(LogPersistence, Error, TEXT("OnLaunchZoneInstanceResponseReceived Server returned no data!  This usually means the dungeon server instance failed to spin up before the timeout was reached."));
			OnErrorLaunchZoneInstanceDelegate.ExecuteIfBound(TEXT("Server returned no data!  This usually means the dungeon server instance failed to spin up before the timeout was reached."));
		}
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnLaunchZoneInstanceResponseReceived Error accessing server!"));
		OnErrorLaunchZoneInstanceDelegate.ExecuteIfBound(TEXT("Error accessing server!"));
	}
}