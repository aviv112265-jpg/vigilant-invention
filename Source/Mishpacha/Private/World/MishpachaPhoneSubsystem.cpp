// MISHPACHA - phone calls.
#include "World/MishpachaPhoneSubsystem.h"
#include "World/MishpachaEmpireSubsystem.h"
#include "Mishpacha.h"
#include "Characters/MishpachaCharacter.h"
#include "Core/MishpachaGameInstance.h"
#include "Core/MishpachaPlayerController.h"
#include "Core/MishpachaPlayerState.h"
#include "Family/FamilySubsystem.h"
#include "Heat/HeatComponent.h"
#include "Missions/MissionSubsystem.h"
#include "Save/MishpachaSaveGame.h"
#include "Engine/World.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "MishpachaPhone"

bool UMishpachaPhoneSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMishpachaPhoneSubsystem::Deinitialize()
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CallTimer);
		World->GetTimerManager().ClearTimer(LineTimer);
	}
	Super::Deinitialize();
}

void UMishpachaPhoneSubsystem::BuildCalls()
{
	auto Add = [this](const TCHAR* Id, const TCHAR* Mission, bool bBusiness, const FText& Caller, TArray<FText> Lines)
	{
		FMishpachaPhoneCall& Call = Calls.AddDefaulted_GetRef();
		Call.Id = Id;
		Call.RequiredMission = Mission ? FName(Mission) : NAME_None;
		Call.bRequiresBusiness = bBusiness;
		Call.Caller = Caller;
		Call.Lines = MoveTemp(Lines);
	};
	const FText Mom = LOCTEXT("CallerMom", "Mom");
	const FText Moshe = LOCTEXT("CallerMoshe", "Moshe");
	const FText Itzik = LOCTEXT("CallerItzik", "Itzik");
	const FText Ezra = LOCTEXT("CallerEzra", "Uncle Ezra");
	const FText Unknown = LOCTEXT("CallerUnknown", "Blocked Number");
	const FText Accountant = LOCTEXT("CallerAccountant", "The Accountant");

	Add(TEXT("Mom_Dinner"), nullptr, false, Mom, {
		LOCTEXT("MomDinner1", "You're eating? All three of you look thin to me."),
		LOCTEXT("MomDinner2", "Friday, at my place. Everyone. No excuses and no phones at the table.") });
	Add(TEXT("Itzik_Paid"), TEXT("Rafi_01"), false, Itzik, {
		LOCTEXT("ItzikPaid1", "Tell Rafi it's all there. Three months. Counted it twice."),
		LOCTEXT("ItzikPaid2", "And tell him the pomegranates are on me. Please.") });
	Add(TEXT("Ezra_Shoes"), TEXT("Omer_01"), false, Ezra, {
		LOCTEXT("EzraShoes1", "I heard you delivered some shoes. Heavy shoes, I heard."),
		LOCTEXT("EzraShoes2", "Your father started with shoes too. Be smarter than him.") });
	Add(TEXT("Moshe_Garage"), TEXT("Rafi_02"), false, Moshe, {
		LOCTEXT("MosheGarage1", "My garage is my garage again. I owe you people."),
		LOCTEXT("MosheGarage2", "Bring any car by. On the house. Don't tell me where it came from.") });
	Add(TEXT("Unknown_Hospital"), TEXT("Shira_02"), false, Unknown, {
		LOCTEXT("UnknownHospital1", "Nice visit at the hospital. We saw the whole thing."),
		LOCTEXT("UnknownHospital2", "Your family isn't the only one with friends in this city.") });
	Add(TEXT("Accountant_Books"), nullptr, true, Accountant, {
		LOCTEXT("AccountantBooks1", "The new place is on the books. On the clean books."),
		LOCTEXT("AccountantBooks2", "Buy more and I can make the numbers smile. Rival crews, that I can't fix.") });
	Add(TEXT("Mom_News"), TEXT("Family_02"), false, Mom, {
		LOCTEXT("MomNews1", "I saw the news. I'm not asking anything."),
		LOCTEXT("MomNews2", "Your father would be proud, God help us. Friday, seven o'clock.") });
}

void UMishpachaPhoneSubsystem::Start()
{
	if (Calls.Num() == 0)
	{
		BuildCalls();
	}
	Schedule(FMath::FRandRange(CallDelay.X * 0.5f, CallDelay.X));
}

void UMishpachaPhoneSubsystem::Schedule(float Delay)
{
	GetWorld()->GetTimerManager().SetTimer(CallTimer, this, &UMishpachaPhoneSubsystem::OnTimer, Delay, false);
}

void UMishpachaPhoneSubsystem::OnTimer()
{
	const bool bCalled = TryCall();
	Schedule(bCalled ? FMath::FRandRange(CallDelay.X, CallDelay.Y) : 45.f);
}

AMishpachaPlayerController* UMishpachaPhoneSubsystem::GetPlayer() const
{
	const UFamilySubsystem* Family = GetWorld()->GetSubsystem<UFamilySubsystem>();
	return Cast<AMishpachaPlayerController>(Family ? Family->GetControllingPlayer() : nullptr);
}

bool UMishpachaPhoneSubsystem::TryCall()
{
	AMishpachaPlayerController* PC = GetPlayer();
	const AMishpachaCharacter* Character = PC ? Cast<AMishpachaCharacter>(PC->GetPawn()) : nullptr;
	const AMishpachaPlayerState* PS = PC ? PC->GetPlayerState<AMishpachaPlayerState>() : nullptr;
	const UHeatComponent* Heat = PS ? PS->GetHeatComponent() : nullptr;
	const UMissionSubsystem* Missions = GetWorld()->GetSubsystem<UMissionSubsystem>();
	UMishpachaGameInstance* GI = GetWorld()->GetGameInstance<UMishpachaGameInstance>();
	UMishpachaSaveGame* Save = GI ? GI->GetSave() : nullptr;
	const UMishpachaEmpireSubsystem* Empire = GetWorld()->GetSubsystem<UMishpachaEmpireSubsystem>();

	// In a car counts as quiet too, so check the player, not the pawn type.
	const bool bBusy = !PC || !Save || !Missions || Missions->IsMissionActive() || (Heat && Heat->GetHeatLevel() > 0) || (Character && Character->IsDead())
		|| GetWorld()->GetTimerManager().IsTimerActive(LineTimer);
	if (bBusy)
	{
		return false;
	}

	for (int32 Index = 0; Index < Calls.Num(); ++Index)
	{
		const FMishpachaPhoneCall& Call = Calls[Index];
		const bool bUnlocked = (Call.RequiredMission.IsNone() || Missions->IsMissionCompleted(Call.RequiredMission))
			&& (!Call.bRequiresBusiness || (Empire && Empire->GetOwnedCount() > 0));
		if (bUnlocked && !Save->PlayedCalls.Contains(Call.Id))
		{
			Save->PlayedCalls.Add(Call.Id);
			CallIndex = Index;
			LineIndex = 0;
			UE_LOG(LogMishpacha, Log, TEXT("Phone: %s calling (%s)"), *Call.Caller.ToString(), *Call.Id.ToString());
			PC->ClientPhoneRing();
			GetWorld()->GetTimerManager().SetTimer(LineTimer, this, &UMishpachaPhoneSubsystem::NextLine, 2.f, false);
			return true;
		}
	}
	return false;
}

void UMishpachaPhoneSubsystem::NextLine()
{
	AMishpachaPlayerController* PC = GetPlayer();
	if (!PC || !Calls.IsValidIndex(CallIndex) || !Calls[CallIndex].Lines.IsValidIndex(LineIndex))
	{
		return;
	}
	const FMishpachaPhoneCall& Call = Calls[CallIndex];
	PC->ClientPhoneLine(Call.Caller, Call.Lines[LineIndex], SecondsPerLine);
	++LineIndex;
	if (Call.Lines.IsValidIndex(LineIndex))
	{
		GetWorld()->GetTimerManager().SetTimer(LineTimer, this, &UMishpachaPhoneSubsystem::NextLine, SecondsPerLine, false);
	}
}

#undef LOCTEXT_NAMESPACE
