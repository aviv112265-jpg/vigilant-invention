// MISHPACHA - host / find / join multiplayer sessions (OnlineSubsystem Null for LAN, EOS for internet).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "MishpachaSessionSubsystem.generated.h"

USTRUCT(BlueprintType)
struct FMishpachaSessionInfo
{
	GENERATED_BODY()

	/** Pass this to JoinSessionByIndex. */
	UPROPERTY(BlueprintReadOnly, Category = "Online")
	int32 Index = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Online")
	FString HostName;

	UPROPERTY(BlueprintReadOnly, Category = "Online")
	int32 CurrentPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Online")
	int32 MaxPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Online")
	int32 PingMs = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMishpachaSessionResult, bool, bSuccessful);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMishpachaSessionsFound, const TArray<FMishpachaSessionInfo>&, Sessions, bool, bSuccessful);

/**
 * Thin, Blueprint-friendly wrapper over the Online Session interface.
 * Host: HostSession(32, bLAN) -> creates the session, then travels to the map as a listen server.
 * Join: FindSessions -> OnFindSessionsComplete -> JoinSessionByIndex.
 * Works with the Null subsystem out of the box (LAN), and with EOS once configured (see README).
 */
UCLASS()
class MISHPACHA_API UMishpachaSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Online")
	bool HostSession(int32 MaxPlayers = 32, bool bLAN = true, const FString& MapPath = TEXT("/Game/Maps/TelAvir_Greybox"), const FString& GameModePath = TEXT("/Script/Mishpacha.OnlineGameMode"));

	UFUNCTION(BlueprintCallable, Category = "Online")
	bool FindSessions(int32 MaxResults = 50, bool bLAN = true);

	UFUNCTION(BlueprintCallable, Category = "Online")
	bool JoinSessionByIndex(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Online")
	bool DestroySession();

	UFUNCTION(BlueprintPure, Category = "Online")
	bool IsOnlineAvailable() const;

	UPROPERTY(BlueprintAssignable, Category = "Online")
	FOnMishpachaSessionResult OnCreateSessionComplete;

	UPROPERTY(BlueprintAssignable, Category = "Online")
	FOnMishpachaSessionsFound OnFindSessionsComplete;

	UPROPERTY(BlueprintAssignable, Category = "Online")
	FOnMishpachaSessionResult OnJoinSessionComplete;

	UPROPERTY(BlueprintAssignable, Category = "Online")
	FOnMishpachaSessionResult OnDestroySessionComplete;

private:
	IOnlineSessionPtr GetSessionInterface() const;

	void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleFindSessionsComplete(bool bWasSuccessful);
	void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	TSharedPtr<FOnlineSessionSearch> LastSearch;

	FDelegateHandle CreateHandle;
	FDelegateHandle FindHandle;
	FDelegateHandle JoinHandle;
	FDelegateHandle DestroyHandle;

	// Pending host request (used when an old session must be destroyed first).
	bool bHostAfterDestroy = false;
	int32 PendingMaxPlayers = 32;
	bool bPendingLAN = true;
	FString PendingMapPath;
	FString PendingGameModePath;
};
