// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParadoxiaGameInstance.h"
#include "Runtime/Online/HTTP/Public/Http.h"
#include "Runtime/Core/Public/Misc/ConfigCacheIni.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "OWS2API.h"

DEFINE_LOG_CATEGORY(LogPersistence);

void UParadoxiaGameInstance::Init()
{
	UE_LOG(LogPersistence, Verbose, TEXT("GameInstance Init!!!!"))

	Super::Init();

	Http = &FHttpModule::Get();
}

void UParadoxiaGameInstance::StartGameInstance()
{

	UE_LOG(LogPersistence, Verbose, TEXT("GameInstance Init!!!!"))


	Super::StartGameInstance();
}

void UParadoxiaGameInstance::OnStart()
{
	Super::OnStart();

	UE_LOG(LogPersistence, Verbose, TEXT("GameInstance OnStart Triggered"))

		GConfig->GetString(
			TEXT("/Script/EngineSettings.GeneralProjectSettings"),
			TEXT("OWSAPICUSTOMERKEY"),
			OWSAPICustomerKey,
			GGameIni
		);

	GConfig->GetString(
		TEXT("/Script/EngineSettings.GeneralProjectSettings"),
		TEXT("OWS2APIPath"),
		OWS2APIPath,
		GGameIni
	);

	UE_LOG(LogPersistence, Verbose, TEXT("APIPath: %s."), *OWS2APIPath)
	UE_LOG(LogPersistence, Verbose, TEXT("CustomerKEy: %s."), *OWSAPICustomerKey)

		if (bAutoStartPersistence)
		{
			if (bRegisterNewUser)
			{
				Register(NewUserID, NewPassword, NewFirstName, NewLastName);
			}
			if (bAttemptPersistence)
			{
				LoginAndCreateSession(TempUserID, TempPassword);
			}
		}

#if WITH_EDITOR

	if (bEnablePersistentTesting)
	{
		bRequiresCharacterCreation = false;
		LoginAndCreateSession(TestingUserID, TestingPassword);
	}
#endif

}

void UParadoxiaGameInstance::BroadcastLoginMessage(FGameplayTag Channel, const FString& PayloadMessage)
{
	//samples:
	// FGameplayTag LoginTag = FGameplayTag::RequestGameplayTag("UI.Event.login.Registration.Failure");
	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
	FLoginPayload Payload;
	Payload.UserFacingMessage = FText::FromString(PayloadMessage);

	MessageSubsystem.BroadcastMessage(Channel, Payload);
}

FString UParadoxiaGameInstance::EncryptWithAES(FString StringToEncrypt, FString Key)
{
	if (StringToEncrypt.IsEmpty()) return "";
	if (Key.IsEmpty()) return "";

	FString SplitSymbol = "OWS#@!";
	StringToEncrypt.Append(SplitSymbol);

	Key = FMD5::HashAnsiString(*Key);
	TCHAR* KeyTChar = Key.GetCharArray().GetData();

	uint32 Size = StringToEncrypt.Len();
	Size = Size + (FAES::AESBlockSize - (Size % FAES::AESBlockSize));

	uint8* ByteString = new uint8[Size];

	if (StringToBytes(StringToEncrypt, ByteString, Size)) {

		FAES::EncryptData(ByteString, Size, TCHAR_TO_ANSI(KeyTChar));
		StringToEncrypt = FString::FromHexBlob(ByteString, Size);

		delete[] ByteString;
		return StringToEncrypt;
	}

	delete[] ByteString;
	return "";
}

FString UParadoxiaGameInstance::DecryptWithAES(FString StringToDecrypt, FString Key)
{
	if (StringToDecrypt.IsEmpty()) return "";
	if (Key.IsEmpty()) return "";

	FString SplitSymbol = "OWS#@!";

	Key = FMD5::HashAnsiString(*Key);
	TCHAR* KeyTChar = Key.GetCharArray().GetData();

	uint32 Size = StringToDecrypt.Len();
	Size = Size + (FAES::AESBlockSize - (Size % FAES::AESBlockSize));

	uint8* ByteString = new uint8[Size];

	if (FString::ToHexBlob(StringToDecrypt, ByteString, Size)) {

		FAES::DecryptData(ByteString, Size, TCHAR_TO_ANSI(KeyTChar));
		StringToDecrypt = BytesToString(ByteString, Size);

		FString LeftPart;
		FString RightPart;
		StringToDecrypt.Split(SplitSymbol, &LeftPart, &RightPart, ESearchCase::CaseSensitive, ESearchDir::FromStart);
		StringToDecrypt = LeftPart;

		delete[] ByteString;
		return StringToDecrypt;
	}

	delete[] ByteString;
	return "";
}


FString UParadoxiaGameInstance::SerializeStructToJSONString(const UStruct* StructToSerialize)
{
	FString TempOutputString;
	for (TFieldIterator<FProperty> PropIt(StructToSerialize->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		TempOutputString += Property->GetNameCPP();
	}

	return TempOutputString;
}

void UParadoxiaGameInstance::ProcessOWS2POSTRequest(FString ApiToCall, FString PostParameters, void (UParadoxiaGameInstance::* InMethodPtr)(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful))
{
	Http = &FHttpModule::Get();
	Http->SetHttpTimeout(30); //Set timeout
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(this, InMethodPtr);

	FString OWS2APIPathToUse = OWS2APIPath;

	Request->SetURL(FString(OWS2APIPathToUse + ApiToCall));
	Request->SetVerb("POST");
	Request->SetHeader(TEXT("User-Agent"), "X-UnrealEngine-Agent");
	Request->SetHeader("Content-Type", TEXT("application/json"));
	Request->SetHeader(TEXT("X-CustomerGUID"), OWSAPICustomerKey);
	Request->SetContentAsString(PostParameters);
	Request->ProcessRequest();
}

void UParadoxiaGameInstance::GetJsonObjectFromResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FString CallingMethodName, FString& ErrorMsg, TSharedPtr<FJsonObject>& JsonObject)
{
	// Log the raw response content
	FString ResponseContent = Response->GetContentAsString();
	UE_LOG(LogPersistence, Log, TEXT("%s - Raw Response Content: %s"), *CallingMethodName, *ResponseContent);

	if (bWasSuccessful && Response.IsValid())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseContent);

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





//------------------------------------------------------------------------------------------------
// LOGIN A USER
//------------------------------------------------------------------------------------------------

void UParadoxiaGameInstance::LoginAndCreateSession(FString Email, FString Password)
{
	ClientUserSessionGUID = "";

	FLoginAndCreateSessionJSONPost LoginAndCreateSessionJSONPost(Email, Password);
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(LoginAndCreateSessionJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("api/Users/LoginAndCreateSession", PostParameters, &UParadoxiaGameInstance::OnLoginAndCreateSessionResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("LoginAndCreateSession Error serializing FLoginAndCreateSessionJSONPost!"));
	}
}



void UParadoxiaGameInstance::OnLoginAndCreateSessionResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	FString ErrorMsg;
	TSharedPtr<FJsonObject> JsonObject;
	UE_LOG(LogPersistence, Warning, TEXT("Response Received"));

	GetJsonObjectFromResponse(Request, Response, bWasSuccessful, "OnLoginAndCreateSessionResponseReceived", ErrorMsg, JsonObject);
	if (!ErrorMsg.IsEmpty())
	{
		UE_LOG(LogPersistence, Warning, TEXT("There was an error 1"));
		ErrorLoginAndCreateSession(ErrorMsg);

		const FGameplayTag LoginTag = FGameplayTag::RequestGameplayTag("UI.Event.Login.Failure");
		BroadcastLoginMessage(LoginTag, *ErrorMsg);

		return;
	}

	TSharedPtr<FLoginAndCreateSession> LoginAndCreateSession = GetStructFromJsonObject<FLoginAndCreateSession>(JsonObject);

	if (!LoginAndCreateSession->ErrorMessage.IsEmpty())
	{

		UE_LOG(LogPersistence, Warning, TEXT("There was an error 2"));
		ErrorLoginAndCreateSession(*LoginAndCreateSession->ErrorMessage);

		const FGameplayTag LoginTag = FGameplayTag::RequestGameplayTag("UI.Event.login.Registration.Failure");
		BroadcastLoginMessage(LoginTag, *ErrorMsg);

		return;
	}



	if (!LoginAndCreateSession->Authenticated || LoginAndCreateSession->UserSessionGUID.IsEmpty())
	{

		UE_LOG(LogPersistence, Warning, TEXT("There was an error 3"));
		ErrorLoginAndCreateSession("Unknown Login Error!  Make sure OWS 2 is running in debug mode in VS 2022 with docker-compose.  Then make sure your OWSAPICustomerKey in DefaultGame.ini matches your CustomerGUID in your database.");
		return;
	}


	UE_LOG(LogPersistence, Warning, TEXT("About to Notify login and create session"));
	ClientUserSessionGUID = LoginAndCreateSession->UserSessionGUID;
	NotifyLoginAndCreateSession(LoginAndCreateSession->UserSessionGUID);

	if (bRequiresCharacterCreation)
	{
		// Setting the Character Name to a Unique string
		FString GeneratedCharacterName = FGuid::NewGuid().ToString();
		CreateCharacter(ClientUserSessionGUID, GeneratedCharacterName, "Para");
		SelectedCharacter = GeneratedCharacterName;
		bRequiresCharacterCreation = false;
	}
	else
	{
		GetAllCharacters(ClientUserSessionGUID);
	}
}



//------------------------------------------------------------------------------------------------
// REGISTER A NEW USER
//------------------------------------------------------------------------------------------------

void UParadoxiaGameInstance::Register(FString Email, FString Password, FString FirstName, FString LastName)
{

	InternalEmailAddress = Email;
	InternalPassword = Password;

	FRegisterJSONPost RegisterJSONPost(Email, Password, FirstName, LastName);
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(RegisterJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("api/Users/RegisterUser", PostParameters, &UParadoxiaGameInstance::OnRegisterResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("Register Error serializing FRegisterJSONPost!"));
	}
}

void UParadoxiaGameInstance::OnRegisterResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	FString ErrorMsg;
	TSharedPtr<FJsonObject> JsonObject;
	GetJsonObjectFromResponse(Request, Response, bWasSuccessful, "OnRegisterResponseReceived", ErrorMsg, JsonObject);

	// Log if there was an error message
	if (!ErrorMsg.IsEmpty())
	{
		UE_LOG(LogPersistence, Error, TEXT("Error Message: %s"), *ErrorMsg);
		ErrorRegister(ErrorMsg);

		const FGameplayTag LoginTag = FGameplayTag::RequestGameplayTag("UI.Event.Login.Registration.Failure");
		BroadcastLoginMessage(LoginTag, *ErrorMsg);
		return;
	}

	// Log the raw response content
	FString ResponseContent = Response->GetContentAsString();
	UE_LOG(LogPersistence, Log, TEXT("Raw JSON Response: %s"), *ResponseContent);

	// Log the JSON object
	if (JsonObject.IsValid())
	{
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

		UE_LOG(LogPersistence, Log, TEXT("Deserialized JSON Object: %s"), *JsonString);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("Invalid JSON object received"));
	}

	TSharedPtr<FLoginAndCreateSession> RegisterAndCreateSession = GetStructFromJsonObject<FLoginAndCreateSession>(JsonObject);

	if (RegisterAndCreateSession.IsValid())
	{
		if (!RegisterAndCreateSession->ErrorMessage.IsEmpty())
		{
			UE_LOG(LogPersistence, Error, TEXT("Error Message in Struct: %s"), *RegisterAndCreateSession->ErrorMessage);
			ErrorRegister(*RegisterAndCreateSession->ErrorMessage);
			return;
		}

		bRequiresCharacterCreation = true;
		NotifyRegister(RegisterAndCreateSession->UserSessionGUID);

		// Since we got the success for the registration, create session automatically
		LoginAndCreateSession(InternalEmailAddress, InternalPassword);
		InternalEmailAddress = "";
		InternalPassword = "";
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("Failed to convert JSON object to FLoginAndCreateSession"));
		const FGameplayTag LoginTag = FGameplayTag::RequestGameplayTag("UI.Event.Login.Registration.Failure");
		BroadcastLoginMessage(LoginTag, TEXT("Failed to convert JSON object to FLoginAndCreateSession"));
	}
}



//------------------------------------------------------------------------------------------------
// LOGIN A USER
//------------------------------------------------------------------------------------------------

void UParadoxiaGameInstance::CreateCharacter(FString UserSessionGUID, FString CharacterName, FString DefaultSetName)
{
	FCreateCharacterUsingDefaultCharacterValues CreateCharacterJSONPost;
	CreateCharacterJSONPost.UserSessionGUID = UserSessionGUID;
	CreateCharacterJSONPost.CharacterName = CharacterName;
	CreateCharacterJSONPost.DefaultSetName = DefaultSetName;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(CreateCharacterJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("api/Users/CreateCharacterUsingDefaultChracterValues", PostParameters, &UParadoxiaGameInstance::OnCreateCharacterResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("CreateCharacter Error serializing CreateCharacterUsingDefaultCharacterValuesJSONPost!!!"))
	}
}

void  UParadoxiaGameInstance::OnCreateCharacterResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		FString ResponseString = Response->GetContentAsString();

		FCreateCharacter CreateCharacter;

		if (!FJsonObjectConverter::JsonObjectStringToUStruct(ResponseString, &CreateCharacter, 0, 0))
		{
			UE_LOG(LogPersistence, Error,TEXT("Could no deserialize CreateCharacterUSingDefaultValues JSON to CreateCharacter struct!"))

			const FGameplayTag LoginTag = FGameplayTag::RequestGameplayTag("UI.Event.Login.Registration.Failure");
			BroadcastLoginMessage(LoginTag, "Unexpected Error Occured, See Logs.");

			return;
		}

		if (!CreateCharacter.ErrorMessage.IsEmpty())
		{
			UE_LOG(LogPersistence, Error, TEXT("Character Creation failed: %s"), *CreateCharacter.ErrorMessage)

			const FGameplayTag LoginTag = FGameplayTag::RequestGameplayTag("UI.Event.Login.Registration.Failure");
			BroadcastLoginMessage(LoginTag, *CreateCharacter.ErrorMessage);

			return;
		}

		UE_LOG(LogPersistence, Verbose, TEXT("OnCreateCharacterResponseReceived Success!"));

		const FGameplayTag LoginTag = FGameplayTag::RequestGameplayTag("UI.Event.Login.Registration.Successful");
		BroadcastLoginMessage(LoginTag, "");

		SetSelectedCharacterAndGetUserSession(ClientUserSessionGUID, SelectedCharacter);
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnCreateCharacterResponseReceived Error accessing login server!"))
	}
}


//------------------------------------------------------------------------------------------------
// Get All Characters
//------------------------------------------------------------------------------------------------

void UParadoxiaGameInstance::GetAllCharacters(FString UserSessionGUID)
{
	FGetAllCharactersJSONPost GetAllCharactersJSONPost;
	GetAllCharactersJSONPost.UserSessionGUID = UserSessionGUID;
	FString PostParameters = "";
	if (FJsonObjectConverter::UStructToJsonObjectString(GetAllCharactersJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("api/Users/GetAllCharacters", PostParameters, &UParadoxiaGameInstance::OnGetAllCharactersResponseReceived);
	}
	else {
		UE_LOG(LogPersistence, Error, TEXT("GetAllCharacters Error serializing GetAllCharactersJSONPOST!"))
	}
}

void UParadoxiaGameInstance::OnGetAllCharactersResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{

		TArray<FUserCharacter> UserCharacters;

		if (!FJsonObjectConverter::JsonArrayStringToUStruct(Response->GetContentAsString(), &UserCharacters, 0, 0))
		{
			UE_LOG(LogPersistence, Error, TEXT("Could no deserialize GetAllCharacters JSON to USerCharacter Struc!"))
			return;
		}

		UE_LOG(LogPersistence, Verbose, TEXT("OnGetAllCharactersResponseReceived Success!"))

			if (!UserCharacters.IsEmpty())
			{
				UE_LOG(LogPersistence, Verbose, TEXT("Requesting Selection of existing character: %s"), *UserCharacters[0].CharacterName);
				SetSelectedCharacterAndGetUserSession(ClientUserSessionGUID, UserCharacters[0].CharacterName);
			}
			else
			{
				UE_LOG(LogPersistence, Error, TEXT("OnGetAllCharactersResponseReceived any empty array!"))

				const FGameplayTag LoginTag = FGameplayTag::RequestGameplayTag("UI.Event.Login.Failure");
				BroadcastLoginMessage(LoginTag, "OnGetAllCharactersResponseReceived any empty array!");

			}
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("OnGetAllCharactersResponseReceived error accessing login server!"))
	}
}



//------------------------------------------------------------------------------------------------
// Get All Characters
//------------------------------------------------------------------------------------------------

void UParadoxiaGameInstance::SetSelectedCharacterAndGetUserSession(FString UserSessionGUID, FString CharacterName)
{
	FSetSelectedCharacterAndConnectToLastZoneJSONPost SetSelectedCharacterAndGetUserSessionJSONPost;
	SetSelectedCharacterAndGetUserSessionJSONPost.UserSessionGUID = UserSessionGUID;
	SetSelectedCharacterAndGetUserSessionJSONPost.SelectedCharacterName = CharacterName;
	FString PostParameters = "";

	UE_LOG(LogPersistence, Verbose, TEXT("Requesting select Character on User Session: %s"), *UserSessionGUID);
	UE_LOG(LogPersistence, Verbose, TEXT("Requesting Selection of character: %s"), *CharacterName);

	if (FJsonObjectConverter::UStructToJsonObjectString(SetSelectedCharacterAndGetUserSessionJSONPost, PostParameters))
	{
		ProcessOWS2POSTRequest("api/Users/SetSelectedCharacterAndGetUerSession", PostParameters, &UParadoxiaGameInstance::OnSetSelectedCharacterAndGetUserSessionResponseReceived);
	}
	else
	{
		UE_LOG(LogPersistence, Verbose, TEXT("SetSelectedCharacterAndGetUSerSession Error serializing GetUserSessionJSONPOst!"));
	}
}

void UParadoxiaGameInstance::OnSetSelectedCharacterAndGetUserSessionResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		FString ResponseString = Response->GetContentAsString();

		// Parse the response string into a JSON object
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseString);

		if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
		{
			FGetUserSession UserSessionStruct;

			if (!FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), &UserSessionStruct, 0, 0))
			{
				UE_LOG(LogPersistence, Error, TEXT("Could not deserialize SetSelectedCharacterAndGetUserSession JSON to User Session struct!"));
				return;
			}

			UE_LOG(LogPersistence, Error, TEXT("SetSelectedCharacterAndGetUserSession Success!"));
			SelectedCharacter = UserSessionStruct.SelectedCharacterName;
			ClientUserSessionGUID = UserSessionStruct.UserSessionGUID;

			UE_LOG(LogPersistence, Verbose, TEXT("User Session is: %s"), *ClientUserSessionGUID);
			UE_LOG(LogPersistence, Verbose, TEXT("Selected Character is: %s"), *SelectedCharacter);

			const FGameplayTag LoginTag = FGameplayTag::RequestGameplayTag("UI.Event.Login.Successful");
			BroadcastLoginMessage(LoginTag, "");
		}
		else
		{
			UE_LOG(LogPersistence, Error, TEXT("Failed to parse SetSelectedCharacterAndGetUserSession response JSON!"));
		}
	}
	else
	{
		UE_LOG(LogPersistence, Error, TEXT("SetSelectedCharacterAndGetUserSession error accessing login server!"));
	}
}

