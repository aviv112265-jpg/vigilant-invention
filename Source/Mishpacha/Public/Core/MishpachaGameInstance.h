// MISHPACHA - game instance: owns the story save.
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "MishpachaGameInstance.generated.h"

class UMishpachaSaveGame;
class UMishpachaSessionSubsystem;

UCLASS()
class MISHPACHA_API UMishpachaGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	/** Returns the current save, loading it from disk (or creating a new one) on first use. */
	UFUNCTION(BlueprintCallable, Category = "Save")
	UMishpachaSaveGame* GetSave();

	UFUNCTION(BlueprintCallable, Category = "Save")
	bool SaveToDisk();

	/** Starts a brand new story (the file on disk is overwritten on the next save). */
	UFUNCTION(BlueprintCallable, Category = "Save")
	void ResetSave();

	/** Hosting / finding / joining multiplayer sessions. */
	UFUNCTION(BlueprintPure, Category = "Online")
	UMishpachaSessionSubsystem* GetSessions() const;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Save")
	FString SaveSlotName = TEXT("Mishpacha_Story");

	UPROPERTY(EditDefaultsOnly, Category = "Save")
	int32 SaveUserIndex = 0;

private:
	UPROPERTY()
	TObjectPtr<UMishpachaSaveGame> CurrentSave;
};
