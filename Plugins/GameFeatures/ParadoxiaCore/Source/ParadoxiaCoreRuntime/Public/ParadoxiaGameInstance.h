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

UCLASS()
class PARADOXIACORERUNTIME_API UParadoxiaGameInstance : public ULyraGameInstance
{
	GENERATED_BODY()

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

	UFUNCTION(BlueprintCallable, Category = "JSON")
	FString SerializeStructToJSONString(const UStruct* StructToSerialize);

	UFUNCTION(BlueprintCallable, Category = "Encryption")
		static FString EncryptWithAES(FString StringtoEncrypt, FString Key);

	UFUNCTION(BlueprintCallable, Category = "Encryption")
	static FString DecryptWithAES(FString StringToDecrypt, FString Key);



	UPROPERTY(BlueprintReadWrite)
		FString OWSAPICustomerKey;

	UPROPERTY(BlueprintReadWrite, Category = "Config")
		FString OWS2APIPath = "";

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Login")
		float LoginTimeout = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sessions")
	FString ClientUserSessionGUID;

	//LoginAndCreateSession
	UFUNCTION(BlueprintCallable, Category = "Login")
	void LoginAndCreateSession(FString Email, FString Password);

	void OnLoginAndCreateSessionResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	UFUNCTION(BlueprintImplementableEvent, Category = "Login")
	void NotifyLoginAndCreateSession(const FString& UserSessionGUID);

	UFUNCTION(BlueprintImplementableEvent, Category = "Login")
	void ErrorLoginAndCreateSession(const FString& ErrorMsg);

	void OnExternalLoginAndCreateSessionResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	UFUNCTION(BlueprintImplementableEvent, Category = "Login")
	void NotifyExternalLoginAndCreateSession(const FString& UserSessionGUID);

	UFUNCTION(BlueprintImplementableEvent, Category = "Login")
	void ErrorExternalLoginAndCreateSession(const FString& ErrorMsg);

	//Register
	UFUNCTION(BlueprintCallable, Category = "Login")
	void Register(FString Email, FString Password, FString FirstName, FString LastName);

	void OnRegisterResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	UFUNCTION(BlueprintImplementableEvent, Category = "Login")
	void NotifyRegister(const FString& UserSessionGUID);

	UFUNCTION(BlueprintImplementableEvent, Category = "Login")
	void ErrorRegister(const FString& ErrorMsg);

protected:
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
