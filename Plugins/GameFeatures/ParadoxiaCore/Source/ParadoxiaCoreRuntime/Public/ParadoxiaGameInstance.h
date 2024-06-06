// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "System/LyraGameInstance.h"

#include "Runtime/Core/Public/Misc/AES.h"
#include "Runtime/Core/Public/Misc/Base64.h"
#include "Runtime/Core/Public/Misc/SecureHash.h"
#include "Runtime/Online/HTTP/Public/Http.h"
#include "Runtime/Online/HTTP/Public/HttpModule.h"
#include "Runtime/JsonUtilities/Public/JsonObjectConverter.h"

#include "ParadoxiaGameInstance.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPersistence, Log, All);

// Gameplay Message Login Payload definition
USTRUCT(BlueprintType)
struct FLoginPayload
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(BlueprintReadWrite)
	int32 Code;
	UPROPERTY(BlueprintReadWrite)
	FText UserFacingMessage;
};

USTRUCT()
struct FGetUserSessionJSONPost
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString SelectedCharacterName;
	UPROPERTY()
	FString UserSessionGUID;
};

USTRUCT()
struct FGetUserSession
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString SelectedCharacterName;
	UPROPERTY()
	FString UserSessionGUID;
};

UCLASS()
class PARADOXIACORERUNTIME_API UParadoxiaGameInstance : public ULyraGameInstance
{
	GENERATED_BODY()

private:
	//controls the creation of a default chracter
	bool bRequiresCharacterCreation = false;

	// holds the client current session GUID
	FString ClientUserSessionGUID;

	// holds selected character
	FString SelectedCharacter;

public:

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Setup)
	bool bEnablePersistentTesting = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Setup)
	FString TestingUserID = "test@test.com";
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Setup)
	FString TestingPassword = "TestPassword";
#endif

private:
	FString InternalEmailAddress;
	FString InternalPassword;

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Setup)
		bool bAutoStartPersistence = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Setup)
		bool bRegisterNewUser = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Setup)
		bool bAttemptPersistence = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Setup, meta = (EditCondition = "bRegisterNewUser"))
		FString NewUserID;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Setup, meta = (EditCondition = "bRegisterNewUser"))
		FString NewPassword;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Setup, meta = (EditCondition = "bRegisterNewUser"))
		FString NewFirstName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Setup, meta = (EditCondition = "bRegisterNewUser"))
		FString NewLastName;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Setup, meta = (EditCondition = "bAttemptPersistence"))
		FString TempUserID= "test@test.com";
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Setup, meta = (EditCondition = "bAttemptPersistence"))
		FString TempPassword = "TestPassword";

	FHttpModule* Http;

	virtual void Init() override;
	virtual void StartGameInstance() override;
	virtual void OnStart() override;


	UFUNCTION(BlueprintCallable, Category = "Login")
	void BroadcastLoginMessage(FGameplayTag Channel, const FString& PayloadMessage);

	UFUNCTION(BlueprintCallable, Category = "JSON")
	FString SerializeStructToJSONString(const UStruct* StructToSerialize);

	UFUNCTION(BlueprintCallable, Category = "Encryption")
		static FString EncryptWithAES(FString StringtoEncrypt, FString Key);

	UFUNCTION(BlueprintCallable, Category = "Encryption")
	static FString DecryptWithAES(FString StringToDecrypt, FString Key);

	//LoginAndCreateSession
	UFUNCTION(BlueprintCallable, Category = "Login")
	void LoginAndCreateSession(FString Email, FString Password);

	void OnLoginAndCreateSessionResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	UFUNCTION(BlueprintImplementableEvent, Category = "Login")
	void NotifyLoginAndCreateSession(const FString& UserSessionGUID);

	UFUNCTION(BlueprintImplementableEvent, Category = "Login")
	void ErrorLoginAndCreateSession(const FString& ErrorMsg);


	//----------------------------------------------------------------------------------------------------------
	// Extern login for third party (EOS, STEAM)
	//----------------------------------------------------------------------------------------------------------

	void OnExternalLoginAndCreateSessionResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	UFUNCTION(BlueprintImplementableEvent, Category = "Login")
	void NotifyExternalLoginAndCreateSession(const FString& UserSessionGUID);

	UFUNCTION(BlueprintImplementableEvent, Category = "Login")
	void ErrorExternalLoginAndCreateSession(const FString& ErrorMsg);


//----------------------------------------------------------------------------------------------------------
// get Current
//----------------------------------------------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, category = "Character")
	FString GetClientUserSessionGUID() { return ClientUserSessionGUID; }

	UFUNCTION(BlueprintCallable, category = "Character")
	FString GetClientSelectedCharacterGUID() { return SelectedCharacter; }

	//----------------------------------------------------------------------------------------------------------
	// Register
	//----------------------------------------------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Login")
	void Register(FString Email, FString Password, FString FirstName, FString LastName);

	void OnRegisterResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	void CreateCharacter(FString UserSessionGUID, FString CharacterName, FString DefaultSetName);

	void OnCreateCharacterResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	void OnGetAllCharactersResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	void GetAllCharacters(FString UserSessionGUID);

	void SetSelectedCharacterAndGetUserSession(FString UserSessionGUID, FString CharacterName);

	void OnSetSelectedCharacterAndGetUserSessionResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	UFUNCTION(BlueprintImplementableEvent, Category = "Login")
	void NotifyRegister(const FString& UserSessionGUID);

	UFUNCTION(BlueprintImplementableEvent, Category = "Login")
	void ErrorRegister(const FString& ErrorMsg);

protected:

	UPROPERTY(BlueprintReadWrite)
	FString OWSAPICustomerKey;

	UPROPERTY(BlueprintReadWrite, Category = "Config")
	FString OWS2APIPath = "";

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Login")
	float LoginTimeout = 30.f;

	void ProcessOWS2POSTRequest(FString ApiToCall, FString PostParameters, void (UParadoxiaGameInstance::* InMethodPtr)(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful));
	void GetJsonObjectFromResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FString CallingMethodName, FString& ErrorMsg, TSharedPtr<FJsonObject>& JsonObject);

	template <typename T>
	TSharedPtr<T> GetStructFromJsonObject(TSharedPtr<FJsonObject> JsonObject)
	{
		TSharedPtr<T> Result = MakeShareable(new T);
		FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), T::StaticStruct(), Result.Get(), 0, 0);
		return Result;
	}

};
