// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParadoxiaGameInstance.h"
#include "Runtime/Online/HTTP/Public/Http.h"
#include "Runtime/Core/Public/Misc/ConfigCacheIni.h"
#include "OWS2API.h"

DEFINE_LOG_CATEGORY(LogPersistence);

void UParadoxiaGameInstance::Init()
{
	Super::Init();

	Http = &FHttpModule::Get();
}

void UParadoxiaGameInstance::StartGameInstance()
{
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

	if (!LoginAndCreateSession->Authenticated || LoginAndCreateSession->UserSessionGUID.IsEmpty())
	{
		ErrorLoginAndCreateSession("Unknown Login Error!  Make sure OWS 2 is running in debug mode in VS 2022 with docker-compose.  Then make sure your OWSAPICustomerKey in DefaultGame.ini matches your CustomerGUID in your database.");
		return;
	}

	NotifyLoginAndCreateSession(LoginAndCreateSession->UserSessionGUID);
}

void UParadoxiaGameInstance::Register(FString Email, FString Password, FString FirstName, FString LastName)
{
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
	if (!ErrorMsg.IsEmpty())
	{
		ErrorRegister(ErrorMsg);
		return;
	}

	TSharedPtr<FLoginAndCreateSession> RegisterAndCreateSession = GetStructFromJsonObject<FLoginAndCreateSession>(JsonObject);

	if (!RegisterAndCreateSession->ErrorMessage.IsEmpty())
	{
		ErrorRegister(*RegisterAndCreateSession->ErrorMessage);
		return;
	}

	NotifyRegister(RegisterAndCreateSession->UserSessionGUID);
}
