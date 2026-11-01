// MISHPACHA - ready-to-use NPC characters.
#include "Characters/MishpachaNPCCharacters.h"
#include "AI/MishpachaNPCAIController.h"
#include "AI/MishpachaPoliceAIController.h"
#include "AI/MishpachaCivilianAIController.h"
#include "GameFramework/CharacterMovementComponent.h"

AMishpachaPolicePawn::AMishpachaPolicePawn()
{
	Tags.AddUnique(FName(TEXT("Police")));
	AIControllerClass = AMishpachaPoliceAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	PlaceholderColor = FLinearColor(0.08f, 0.2f, 0.75f, 1.f);
	MaxHealth = 150.f;
	Health = MaxHealth;
}

AMishpachaCivilian::AMishpachaCivilian()
{
	Tags.AddUnique(FName(TEXT("Civilian")));
	AIControllerClass = AMishpachaCivilianAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	PlaceholderColor = FLinearColor(0.75f, 0.72f, 0.62f, 1.f);
	DefaultWeaponClass = nullptr;
	MaxHealth = 100.f;
	Health = MaxHealth;
	JogSpeed = 160.f;
	SprintSpeed = 520.f;
	GetCharacterMovement()->MaxWalkSpeed = JogSpeed;
}

AMishpachaFamilySoldier::AMishpachaFamilySoldier()
{
	Tags.AddUnique(FName(TEXT("Family")));
	AIControllerClass = AMishpachaNPCAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	PlaceholderColor = FLinearColor(0.12f, 0.12f, 0.12f, 1.f);
}
