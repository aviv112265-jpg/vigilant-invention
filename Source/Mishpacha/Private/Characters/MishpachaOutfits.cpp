// MISHPACHA - clothes and hair painted on the Third Person mannequin.
#include "Characters/MishpachaOutfits.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"

namespace
{
	FLinearColor Pick(FRandomStream& Stream, std::initializer_list<FLinearColor> Colors)
	{
		const int32 Index = Stream.RandRange(0, static_cast<int32>(Colors.size()) - 1);
		return *(Colors.begin() + Index);
	}

	const FLinearColor SkinTones[] = {
		FLinearColor(0.78f, 0.56f, 0.42f), FLinearColor(0.68f, 0.46f, 0.32f), FLinearColor(0.55f, 0.36f, 0.24f),
		FLinearColor(0.42f, 0.26f, 0.16f), FLinearColor(0.26f, 0.15f, 0.09f), FLinearColor(0.82f, 0.62f, 0.5f) };
	const FLinearColor HairColors[] = {
		FLinearColor(0.015f, 0.012f, 0.01f), FLinearColor(0.06f, 0.035f, 0.02f), FLinearColor(0.14f, 0.08f, 0.04f),
		FLinearColor(0.5f, 0.36f, 0.18f), FLinearColor(0.3f, 0.3f, 0.3f), FLinearColor(0.35f, 0.12f, 0.04f) };
}

bool FMishpachaOutfit::ForSibling(FName SiblingId, FMishpachaOutfit& Out)
{
	static const FName Rafi(TEXT("Rafi"));
	static const FName Shira(TEXT("Shira"));
	static const FName Omer(TEXT("Omer"));

	Out = FMishpachaOutfit();
	if (SiblingId == Rafi)
	{
		// The head of the family: charcoal pinstripe-dark suit, white shirt, burgundy tie, black classic cut.
		Out.Skin = FLinearColor(0.6f, 0.4f, 0.28f);
		Out.Hair = FLinearColor(0.012f, 0.01f, 0.009f);
		Out.Top = FLinearColor(0.92f, 0.92f, 0.9f);
		Out.Jacket = FLinearColor(0.035f, 0.035f, 0.045f);
		Out.Pants = Out.Jacket;
		Out.Shoes = FLinearColor(0.015f, 0.01f, 0.008f);
		Out.Accent = FLinearColor(0.32f, 0.02f, 0.04f, 1.f);
		Out.bJacket = true;
		Out.HairStyle = 0.f;
		return true;
	}
	if (SiblingId == Shira)
	{
		// The lawyer: navy suit, white blouse, long ginger bob.
		Out.Skin = FLinearColor(0.8f, 0.58f, 0.46f);
		Out.Hair = FLinearColor(0.55f, 0.16f, 0.03f);
		Out.Top = FLinearColor(0.95f, 0.95f, 0.93f);
		Out.Jacket = FLinearColor(0.025f, 0.035f, 0.08f);
		Out.Pants = Out.Jacket;
		Out.Shoes = FLinearColor(0.02f, 0.01f, 0.01f);
		Out.bJacket = true;
		Out.HairStyle = 1.f;
		return true;
	}
	if (SiblingId == Omer)
	{
		// Just out of the army: white tank top, loud swim shorts, flip-flops, brown buzz cut.
		Out.Skin = FLinearColor(0.46f, 0.27f, 0.16f); // army tan
		Out.Hair = FLinearColor(0.12f, 0.065f, 0.03f);
		Out.Top = FLinearColor(0.96f, 0.96f, 0.95f);
		Out.Pants = FLinearColor(0.02f, 0.45f, 0.75f);
		Out.Accent = FLinearColor(1.f, 0.35f, 0.05f, 0.f);
		Out.Shoes = FLinearColor(0.05f, 0.05f, 0.06f);
		Out.bTankTop = true;
		Out.bStripes = true;
		Out.bFlipFlops = true;
		Out.PantsEnd = 58.f;
		Out.HairStyle = 2.f;
		return true;
	}
	return false;
}

FMishpachaOutfit FMishpachaOutfit::Civilian(FRandomStream& Stream, bool bFemale)
{
	FMishpachaOutfit Out;
	Out.Skin = SkinTones[Stream.RandRange(0, UE_ARRAY_COUNT(SkinTones) - 1)];
	Out.Hair = HairColors[Stream.RandRange(0, UE_ARRAY_COUNT(HairColors) - 1)];
	Out.HairStyle = bFemale ? 1.f : (Stream.FRand() < 0.4f ? 2.f : 0.f);

	// Tel Aviv summer: T-shirts in every color, shorts or jeans, sandals or sneakers.
	Out.Top = Pick(Stream, { FLinearColor(0.9f, 0.9f, 0.88f), FLinearColor(0.02f, 0.02f, 0.02f), FLinearColor(0.75f, 0.1f, 0.08f),
		FLinearColor(0.05f, 0.25f, 0.6f), FLinearColor(0.95f, 0.75f, 0.1f), FLinearColor(0.1f, 0.5f, 0.3f), FLinearColor(0.85f, 0.4f, 0.6f),
		FLinearColor(0.4f, 0.65f, 0.85f), FLinearColor(0.95f, 0.5f, 0.15f), FLinearColor(0.5f, 0.3f, 0.6f) });
	Out.SleeveEnd = Stream.FRand() < 0.75f ? 27.f : 46.f;
	Out.bTankTop = Stream.FRand() < 0.12f;
	const bool bShorts = Stream.FRand() < 0.5f;
	Out.Pants = bShorts
		? Pick(Stream, { FLinearColor(0.7f, 0.6f, 0.42f), FLinearColor(0.15f, 0.2f, 0.35f), FLinearColor(0.1f, 0.1f, 0.1f), FLinearColor(0.3f, 0.35f, 0.2f) })
		: Pick(Stream, { FLinearColor(0.08f, 0.14f, 0.3f), FLinearColor(0.04f, 0.04f, 0.05f), FLinearColor(0.5f, 0.45f, 0.35f), FLinearColor(0.25f, 0.35f, 0.5f) });
	Out.PantsEnd = bShorts ? 55.f : 10.f;
	Out.Shoes = Pick(Stream, { FLinearColor(0.9f, 0.9f, 0.9f), FLinearColor(0.03f, 0.03f, 0.03f), FLinearColor(0.35f, 0.2f, 0.1f) });
	Out.bFlipFlops = bShorts && Stream.FRand() < 0.4f;
	if (Stream.FRand() < 0.08f)
	{
		// Office worker from the towers.
		Out.bJacket = true;
		Out.Top = FLinearColor(0.85f, 0.9f, 0.95f);
		Out.Jacket = FLinearColor(0.12f, 0.12f, 0.14f);
		Out.Pants = Out.Jacket;
		Out.PantsEnd = 10.f;
		Out.bFlipFlops = false;
		Out.SleeveEnd = 46.f;
	}
	return Out;
}

FMishpachaOutfit FMishpachaOutfit::Religious(FRandomStream& Stream, bool bFemale)
{
	FMishpachaOutfit Out = Civilian(Stream, bFemale);
	Out.bTankTop = false;
	Out.bFlipFlops = false;
	Out.SleeveEnd = 46.f;
	Out.PantsEnd = 10.f;
	Out.Shoes = FLinearColor(0.02f, 0.02f, 0.02f);
	if (bFemale)
	{
		Out.bJacket = false;
		Out.Top = Pick(Stream, { FLinearColor(0.05f, 0.06f, 0.14f), FLinearColor(0.12f, 0.08f, 0.1f), FLinearColor(0.2f, 0.2f, 0.24f) });
		Out.Pants = Out.Top;
		Out.Hair = Pick(Stream, { FLinearColor(0.3f, 0.32f, 0.4f), FLinearColor(0.08f, 0.08f, 0.1f), FLinearColor(0.45f, 0.4f, 0.4f) });
		return Out;
	}
	Out.bJacket = true;
	Out.Top = FLinearColor(0.92f, 0.92f, 0.9f);
	Out.Jacket = FLinearColor(0.02f, 0.02f, 0.025f);
	Out.Pants = Out.Jacket;
	Out.Hair = FLinearColor(0.015f, 0.012f, 0.01f);
	Out.HairStyle = 0.f;
	return Out;
}

FMishpachaOutfit FMishpachaOutfit::Police()
{
	// Israel Police: light-blue shirt, dark navy trousers, black boots.
	FMishpachaOutfit Out;
	Out.Top = FLinearColor(0.35f, 0.55f, 0.8f);
	Out.SleeveEnd = 28.f;
	Out.Pants = FLinearColor(0.02f, 0.03f, 0.08f);
	Out.Shoes = FLinearColor(0.01f, 0.01f, 0.01f);
	Out.HairStyle = 2.f;
	return Out;
}

FMishpachaOutfit FMishpachaOutfit::Rival(FRandomStream& Stream)
{
	// The rival crew: black and red, gold-chain energy.
	FMishpachaOutfit Out = Civilian(Stream, false);
	Out.Top = Pick(Stream, { FLinearColor(0.02f, 0.02f, 0.02f), FLinearColor(0.45f, 0.02f, 0.02f) });
	Out.Pants = FLinearColor(0.03f, 0.03f, 0.03f);
	Out.PantsEnd = 10.f;
	Out.bFlipFlops = false;
	Out.bJacket = Stream.FRand() < 0.5f;
	Out.Jacket = FLinearColor(0.08f, 0.02f, 0.02f);
	Out.SleeveEnd = 46.f;
	return Out;
}

FMishpachaOutfit FMishpachaOutfit::FamilySoldier()
{
	FMishpachaOutfit Out;
	Out.Top = FLinearColor(0.02f, 0.02f, 0.02f);
	Out.Jacket = FLinearColor(0.05f, 0.05f, 0.06f);
	Out.Pants = Out.Jacket;
	Out.bJacket = true;
	Out.HairStyle = 2.f;
	return Out;
}

bool FMishpachaOutfit::Apply(USkeletalMeshComponent* Mesh, bool bFemale) const
{
	static UMaterialInterface* Material = nullptr;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mishpacha/Materials/M_Outfit.M_Outfit"), nullptr, LOAD_Quiet | LOAD_NoWarn);
		if (Material)
		{
			Material->AddToRoot();
		}
	}
	if (!Mesh || !Material)
	{
		return false;
	}

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Material, Mesh);
	MID->SetVectorParameterValue(TEXT("Skin"), Skin);
	MID->SetVectorParameterValue(TEXT("Hair"), Hair);
	MID->SetVectorParameterValue(TEXT("Top"), Top);
	MID->SetVectorParameterValue(TEXT("Jacket"), Jacket);
	MID->SetVectorParameterValue(TEXT("Pants"), Pants);
	MID->SetVectorParameterValue(TEXT("Shoes"), Shoes);
	MID->SetVectorParameterValue(TEXT("Accent"), Accent);
	MID->SetScalarParameterValue(TEXT("HairStyle"), HairStyle);
	MID->SetScalarParameterValue(TEXT("SleeveEnd"), SleeveEnd);
	MID->SetScalarParameterValue(TEXT("PantsEnd"), PantsEnd);
	MID->SetScalarParameterValue(TEXT("JacketOn"), bJacket ? 1.f : 0.f);
	MID->SetScalarParameterValue(TEXT("TankTop"), bTankTop ? 1.f : 0.f);
	MID->SetScalarParameterValue(TEXT("Stripes"), bStripes ? 1.f : 0.f);
	MID->SetScalarParameterValue(TEXT("FlipFlops"), bFlipFlops ? 1.f : 0.f);
	MID->SetScalarParameterValue(TEXT("Tie"), Accent.A);
	// Manny and Quinn share head and shoulder heights; the material's numbers fit both.
	MID->SetScalarParameterValue(TEXT("HeightScale"), 1.f);
	for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
	{
		Mesh->SetMaterial(Slot, MID);
	}
	return true;
}
