// MISHPACHA - clothes and hair painted on the Third Person mannequin (M_Outfit, Tools/CreateMaterials.py).
#pragma once

#include "CoreMinimal.h"

class USkeletalMeshComponent;
struct FRandomStream;

struct FMishpachaOutfit
{
	FLinearColor Skin = FLinearColor(0.62f, 0.42f, 0.3f);
	FLinearColor Hair = FLinearColor(0.02f, 0.015f, 0.01f);
	FLinearColor Top = FLinearColor::White;     // shirt / tank top
	FLinearColor Jacket = FLinearColor::Black;
	FLinearColor Pants = FLinearColor(0.1f, 0.1f, 0.12f);
	FLinearColor Shoes = FLinearColor(0.02f, 0.02f, 0.02f);
	FLinearColor Accent = FLinearColor(0.f, 0.f, 0.f, 0.f); // tie (alpha 1), or the swim shorts' print
	float HairStyle = 0.f;   // 0 classic men's cut, 1 long bob, 2 buzz cut
	float SleeveEnd = 46.f;  // how far the sleeves reach out along the arm (cm from the body's center line); 27 = short sleeves
	float PantsEnd = 10.f;   // where the trousers stop (cm above the floor); 55 = shorts
	bool bJacket = false;
	bool bTankTop = false;
	bool bStripes = false;   // loud swim-shorts print
	bool bFlipFlops = false;

	/** Rafi: businessman / boss suit, classic black hair. Shira: lawyer's suit, long ginger bob. Omer: just out of the army - white tank top, swim shorts, flip-flops, brown buzz cut. */
	static bool ForSibling(FName SiblingId, FMishpachaOutfit& Out);
	static FMishpachaOutfit Civilian(FRandomStream& Stream, bool bFemale);
	/** Neve Torah: black and white for the men, long dark dresses and head coverings for the women. */
	static FMishpachaOutfit Religious(FRandomStream& Stream, bool bFemale);
	static FMishpachaOutfit Police();
	static FMishpachaOutfit Rival(FRandomStream& Stream);
	static FMishpachaOutfit FamilySoldier();

	/** Puts M_Outfit with these colors on every material slot. False if the material hasn't been generated. */
	bool Apply(USkeletalMeshComponent* Mesh, bool bFemale) const;
};
