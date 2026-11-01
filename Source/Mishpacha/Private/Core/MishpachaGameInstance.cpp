// MISHPACHA - game instance.
#include "Core/MishpachaGameInstance.h"
#include "Save/MishpachaSaveGame.h"
#include "Online/MishpachaSessionSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Mishpacha.h"

UMishpachaSaveGame* UMishpachaGameInstance::GetSave()
{
	if (CurrentSave)
	{
		return CurrentSave;
	}

	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, SaveUserIndex))
	{
		CurrentSave = Cast<UMishpachaSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex));
	}

	if (!CurrentSave)
	{
		CurrentSave = Cast<UMishpachaSaveGame>(UGameplayStatics::CreateSaveGameObject(UMishpachaSaveGame::StaticClass()));
	}
	return CurrentSave;
}

bool UMishpachaGameInstance::SaveToDisk()
{
	UMishpachaSaveGame* Save = GetSave();
	const bool bSaved = Save && UGameplayStatics::SaveGameToSlot(Save, SaveSlotName, SaveUserIndex);
	UE_LOG(LogMishpacha, Log, TEXT("Save to slot '%s': %s"), *SaveSlotName, bSaved ? TEXT("OK") : TEXT("FAILED"));
	return bSaved;
}

void UMishpachaGameInstance::ResetSave()
{
	CurrentSave = Cast<UMishpachaSaveGame>(UGameplayStatics::CreateSaveGameObject(UMishpachaSaveGame::StaticClass()));
}

UMishpachaSessionSubsystem* UMishpachaGameInstance::GetSessions() const
{
	return GetSubsystem<UMishpachaSessionSubsystem>();
}
