// MISHPACHA - host / find / join multiplayer sessions.
#include "Online/MishpachaSessionSubsystem.h"
#include "Mishpacha.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace MishpachaSessionKeys
{
	// Same values as the engine's SEARCH_PRESENCE / SEARCH_LOBBIES macros (spelled out to avoid header churn between versions).
	static const FName SearchPresence(TEXT("PRESENCESEARCH"));
	static const FName SearchLobbies(TEXT("LOBBYSEARCH"));
	static const FName GameModeKey(TEXT("MISHPACHA_MODE"));
}

IOnlineSessionPtr UMishpachaSessionSubsystem::GetSessionInterface() const
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	IOnlineSubsystem* OnlineSubsystem = World ? Online::GetSubsystem(World) : IOnlineSubsystem::Get();
	if (!OnlineSubsystem)
	{
		return IOnlineSessionPtr();
	}
	return OnlineSubsystem->GetSessionInterface();
}

bool UMishpachaSessionSubsystem::IsOnlineAvailable() const
{
	return GetSessionInterface().IsValid();
}

bool UMishpachaSessionSubsystem::HostSession(int32 MaxPlayers, bool bLAN, const FString& MapPath, const FString& GameModePath)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		UE_LOG(LogMishpacha, Warning, TEXT("HostSession: no online subsystem available."));
		OnCreateSessionComplete.Broadcast(false);
		return false;
	}

	PendingMaxPlayers = FMath::Clamp(MaxPlayers, 2, 64);
	bPendingLAN = bLAN;
	PendingMapPath = MapPath;
	PendingGameModePath = GameModePath;

	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		// Leave the old session first, then host again from HandleDestroySessionComplete.
		bHostAfterDestroy = true;
		return DestroySession();
	}

	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = PendingMaxPlayers;
	Settings.NumPrivateConnections = 0;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bIsLANMatch = bLAN;
	Settings.bUsesPresence = !bLAN;
	Settings.bAllowJoinViaPresence = !bLAN;
	Settings.bUseLobbiesIfAvailable = !bLAN;
	Settings.Set(MishpachaSessionKeys::GameModeKey, FString(TEXT("FreeRoam")), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UMishpachaSessionSubsystem::HandleCreateSessionComplete));

	if (!Sessions->CreateSession(0, NAME_GameSession, Settings))
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		OnCreateSessionComplete.Broadcast(false);
		return false;
	}
	return true;
}

void UMishpachaSessionSubsystem::HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
	}

	OnCreateSessionComplete.Broadcast(bWasSuccessful);
	if (!bWasSuccessful)
	{
		UE_LOG(LogMishpacha, Warning, TEXT("CreateSession failed."));
		return;
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World)
	{
		const FString TravelURL = FString::Printf(TEXT("%s?listen?game=%s"), *PendingMapPath, *PendingGameModePath);
		UE_LOG(LogMishpacha, Log, TEXT("Hosting session (%d players) -> ServerTravel %s"), PendingMaxPlayers, *TravelURL);
		World->ServerTravel(TravelURL);
	}
}

bool UMishpachaSessionSubsystem::FindSessions(int32 MaxResults, bool bLAN)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		OnFindSessionsComplete.Broadcast(TArray<FMishpachaSessionInfo>(), false);
		return false;
	}

	LastSearch = MakeShared<FOnlineSessionSearch>();
	LastSearch->MaxSearchResults = FMath::Max(1, MaxResults);
	LastSearch->bIsLanQuery = bLAN;
	if (!bLAN)
	{
		LastSearch->QuerySettings.Set(MishpachaSessionKeys::SearchPresence, true, EOnlineComparisonOp::Equals);
		LastSearch->QuerySettings.Set(MishpachaSessionKeys::SearchLobbies, true, EOnlineComparisonOp::Equals);
	}

	FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UMishpachaSessionSubsystem::HandleFindSessionsComplete));

	if (!Sessions->FindSessions(0, LastSearch.ToSharedRef()))
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		OnFindSessionsComplete.Broadcast(TArray<FMishpachaSessionInfo>(), false);
		return false;
	}
	return true;
}

void UMishpachaSessionSubsystem::HandleFindSessionsComplete(bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
	}

	TArray<FMishpachaSessionInfo> Results;
	if (bWasSuccessful && LastSearch.IsValid())
	{
		for (int32 Index = 0; Index < LastSearch->SearchResults.Num(); ++Index)
		{
			const FOnlineSessionSearchResult& Result = LastSearch->SearchResults[Index];
			FMishpachaSessionInfo Info;
			Info.Index = Index;
			Info.HostName = Result.Session.OwningUserName;
			Info.MaxPlayers = Result.Session.SessionSettings.NumPublicConnections;
			Info.CurrentPlayers = Info.MaxPlayers - Result.Session.NumOpenPublicConnections;
			Info.PingMs = Result.PingInMs;
			Results.Add(Info);
		}
	}
	OnFindSessionsComplete.Broadcast(Results, bWasSuccessful);
}

bool UMishpachaSessionSubsystem::JoinSessionByIndex(int32 Index)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || !LastSearch.IsValid() || !LastSearch->SearchResults.IsValidIndex(Index))
	{
		OnJoinSessionComplete.Broadcast(false);
		return false;
	}

	JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UMishpachaSessionSubsystem::HandleJoinSessionComplete));

	if (!Sessions->JoinSession(0, NAME_GameSession, LastSearch->SearchResults[Index]))
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
		OnJoinSessionComplete.Broadcast(false);
		return false;
	}
	return true;
}

void UMishpachaSessionSubsystem::HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	}

	const bool bSuccess = Result == EOnJoinSessionCompleteResult::Success;
	OnJoinSessionComplete.Broadcast(bSuccess);
	if (!bSuccess || !Sessions.IsValid())
	{
		UE_LOG(LogMishpacha, Warning, TEXT("JoinSession failed (result %d)."), static_cast<int32>(Result));
		return;
	}

	FString ConnectString;
	APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	if (PC && Sessions->GetResolvedConnectString(SessionName, ConnectString))
	{
		UE_LOG(LogMishpacha, Log, TEXT("Joining session at %s"), *ConnectString);
		PC->ClientTravel(ConnectString, ETravelType::TRAVEL_Absolute);
	}
}

bool UMishpachaSessionSubsystem::DestroySession()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		OnDestroySessionComplete.Broadcast(false);
		return false;
	}

	DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &UMishpachaSessionSubsystem::HandleDestroySessionComplete));

	if (!Sessions->DestroySession(NAME_GameSession))
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
		bHostAfterDestroy = false;
		OnDestroySessionComplete.Broadcast(false);
		return false;
	}
	return true;
}

void UMishpachaSessionSubsystem::HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
	}

	OnDestroySessionComplete.Broadcast(bWasSuccessful);
	if (bHostAfterDestroy)
	{
		bHostAfterDestroy = false;
		if (bWasSuccessful)
		{
			HostSession(PendingMaxPlayers, bPendingLAN, PendingMapPath, PendingGameModePath);
		}
	}
}
