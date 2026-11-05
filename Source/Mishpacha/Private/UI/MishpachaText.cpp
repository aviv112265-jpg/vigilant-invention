// MISHPACHA - Hebrew support for the canvas HUD.
#include "UI/MishpachaText.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/Package.h"

namespace MishpachaText
{
	static bool IsHebrewChar(TCHAR C) { return (C >= 0x0590 && C <= 0x05FF) || (C >= 0xFB1D && C <= 0xFB4F); }
	static bool IsLatinOrDigit(TCHAR C) { return FChar::IsAlnum(C) && !IsHebrewChar(C); }

	bool HasHebrew(const FString& Text)
	{
		for (const TCHAR C : Text)
		{
			if (IsHebrewChar(C))
			{
				return true;
			}
		}
		return false;
	}

	static TCHAR Mirror(TCHAR C)
	{
		switch (C)
		{
		case TEXT('('): return TEXT(')');
		case TEXT(')'): return TEXT('(');
		case TEXT('['): return TEXT(']');
		case TEXT(']'): return TEXT('[');
		case TEXT('<'): return TEXT('>');
		case TEXT('>'): return TEXT('<');
		default: return C;
		}
	}

	FString ToVisualOrder(const FString& Text)
	{
		if (!HasHebrew(Text))
		{
			return Text;
		}

		// 1) Directions: Hebrew = R, Latin/digits = L, the rest neutral (N). Number punctuation between digits stays L.
		const int32 Len = Text.Len();
		TArray<TCHAR> Dir;
		Dir.SetNumUninitialized(Len);
		for (int32 I = 0; I < Len; ++I)
		{
			const TCHAR C = Text[I];
			Dir[I] = IsHebrewChar(C) ? TEXT('R') : (IsLatinOrDigit(C) ? TEXT('L') : TEXT('N'));
		}
		for (int32 I = 1; I + 1 < Len; ++I)
		{
			const TCHAR C = Text[I];
			if (Dir[I] == TEXT('N') && (C == TEXT('.') || C == TEXT(',') || C == TEXT(':') || C == TEXT('/') || C == TEXT('%') || C == TEXT('-'))
				&& Dir[I - 1] == TEXT('L') && Dir[I + 1] == TEXT('L'))
			{
				Dir[I] = TEXT('L');
			}
		}

		// 2) Neutrals between two L's are L; everything else neutral takes the paragraph direction (R).
		for (int32 I = 0; I < Len; ++I)
		{
			if (Dir[I] != TEXT('N'))
			{
				continue;
			}
			int32 End = I;
			while (End < Len && Dir[End] == TEXT('N'))
			{
				++End;
			}
			const TCHAR Before = I > 0 ? Dir[I - 1] : TEXT('R');
			const TCHAR After = End < Len ? Dir[End] : TEXT('R');
			const TCHAR Resolved = (Before == TEXT('L') && After == TEXT('L')) ? TEXT('L') : TEXT('R');
			for (int32 J = I; J < End; ++J)
			{
				Dir[J] = Resolved;
			}
			I = End - 1;
		}

		// 3) Runs right to left; inside an R run the characters are reversed (and mirrored), L runs read as is.
		FString Out;
		Out.Reserve(Len);
		int32 RunEnd = Len;
		while (RunEnd > 0)
		{
			int32 RunStart = RunEnd - 1;
			while (RunStart > 0 && Dir[RunStart - 1] == Dir[RunEnd - 1])
			{
				--RunStart;
			}
			if (Dir[RunEnd - 1] == TEXT('L'))
			{
				Out += Text.Mid(RunStart, RunEnd - RunStart);
			}
			else
			{
				for (int32 I = RunEnd - 1; I >= RunStart; --I)
				{
					Out.AppendChar(Mirror(Text[I]));
				}
			}
			RunEnd = RunStart;
		}
		return Out;
	}

	static UFontFace* LoadFace(const TCHAR* Path)
	{
		return LoadObject<UFontFace>(nullptr, Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
	}

	static void AddFace(FTypeface& Typeface, const TCHAR* Name, const UFontFace* Face)
	{
		FTypefaceEntry& Entry = Typeface.Fonts.AddDefaulted_GetRef();
		Entry.Name = Name;
		Entry.Font = FFontData(Face);
	}

	// The canvas only draws with a UFont, so the Roboto + Hebrew composite lives in a transient runtime-cached one.
	static UFont* GetHebrewUIFont()
	{
		static UFont* Font = nullptr;
		static bool bTried = false;
		if (bTried)
		{
			return Font;
		}
		bTried = true;

		UFontFace* Regular = LoadFace(TEXT("/Engine/EngineFonts/Faces/RobotoRegular.RobotoRegular"));
		UFontFace* Bold = LoadFace(TEXT("/Engine/EngineFonts/Faces/RobotoBold.RobotoBold"));
		UFontFace* HebrewRegular = LoadFace(TEXT("/Game/Mishpacha/Fonts/NotoSansHebrew-Regular.NotoSansHebrew-Regular"));
		UFontFace* HebrewBold = LoadFace(TEXT("/Game/Mishpacha/Fonts/NotoSansHebrew-Bold.NotoSansHebrew-Bold"));
		if (!Regular || !Bold || !HebrewRegular)
		{
			return nullptr; // plain Roboto
		}
		for (UFontFace* Face : { Regular, Bold, HebrewRegular, HebrewBold })
		{
			if (Face)
			{
				Face->AddToRoot();
			}
		}

		Font = NewObject<UFont>(GetTransientPackage(), TEXT("MishpachaHebrewUIFont"));
		Font->AddToRoot();
		Font->FontCacheType = EFontCacheType::Runtime;
		Font->LegacyFontName = TEXT("Regular");
		FCompositeFont& Composite = Font->GetMutableInternalCompositeFont();
		AddFace(Composite.DefaultTypeface, TEXT("Regular"), Regular);
		AddFace(Composite.DefaultTypeface, TEXT("Bold"), Bold);

		FCompositeSubFont& Hebrew = Composite.SubTypefaces.AddDefaulted_GetRef();
		Hebrew.CharacterRanges.Add(FInt32Range(FInt32Range::BoundsType::Inclusive(0x0590), FInt32Range::BoundsType::Inclusive(0x05FF)));
		Hebrew.CharacterRanges.Add(FInt32Range(FInt32Range::BoundsType::Inclusive(0xFB1D), FInt32Range::BoundsType::Inclusive(0xFB4F)));
		Hebrew.CharacterRanges.Add(FInt32Range(FInt32Range::BoundsType::Inclusive(0x20AA), FInt32Range::BoundsType::Inclusive(0x20AA))); // shekel sign
		AddFace(Hebrew.Typeface, TEXT("Regular"), HebrewRegular);
		AddFace(Hebrew.Typeface, TEXT("Bold"), HebrewBold ? HebrewBold : HebrewRegular);
		return Font;
	}

	bool HasHebrewFont()
	{
		return GetHebrewUIFont() != nullptr;
	}

	FText SiblingName(FName SiblingId, bool bUpper)
	{
		// Same keys as the built-in family in StoryGameMode, so Hebrew.tsv translates them.
		if (SiblingId.IsNone())
		{
			return FText::GetEmpty();
		}
		static const FName Rafi(TEXT("Rafi"));
		static const FName Shira(TEXT("Shira"));
		static const FName Omer(TEXT("Omer"));
		const FText Name = SiblingId == Rafi ? NSLOCTEXT("MishpachaFamily", "RafiName", "Rafi")
			: SiblingId == Shira ? NSLOCTEXT("MishpachaFamily", "ShiraName", "Shira")
			: SiblingId == Omer ? NSLOCTEXT("MishpachaFamily", "OmerName", "Omer")
			: FText::FromName(SiblingId);
		return bUpper ? FText::FromString(Name.ToString().ToUpper()) : Name;
	}

	FSlateFontInfo MakeFont(const UObject* RobotoFont, int32 Size, bool bBold)
	{
		const FName Typeface = bBold ? FName(TEXT("Bold")) : FName(TEXT("Regular"));
		if (const UFont* Hebrew = GetHebrewUIFont())
		{
			return FSlateFontInfo(Hebrew, float(Size), Typeface);
		}
		return FSlateFontInfo(RobotoFont, float(Size), Typeface);
	}

	static const TCHAR* ConfigSection = TEXT("Mishpacha");

	void SetLanguage(const FString& Culture)
	{
		FInternationalization::Get().SetCurrentCulture(Culture);
		GConfig->SetString(ConfigSection, TEXT("Language"), *Culture, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	FString GetLanguage()
	{
		return FInternationalization::Get().GetCurrentCulture()->GetTwoLetterISOLanguageName();
	}

	bool IsRightToLeft()
	{
		return GetLanguage() == TEXT("he");
	}

	void ApplySavedLanguage()
	{
		FString Culture;
		if (!GConfig->GetString(ConfigSection, TEXT("Language"), Culture, GGameUserSettingsIni) || Culture.IsEmpty())
		{
			Culture = HasHebrewFont() ? TEXT("he") : TEXT("en");
		}
		if (Culture == TEXT("he") && !HasHebrewFont())
		{
			Culture = TEXT("en"); // never show Hebrew without glyphs for it
		}
		FInternationalization::Get().SetCurrentCulture(Culture);
	}
}
