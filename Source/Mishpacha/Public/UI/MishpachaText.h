// MISHPACHA - Hebrew support for the canvas HUD: right-to-left reordering, Hebrew-capable font, language.
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

namespace MishpachaText
{
	/** True if the string has any Hebrew letters. */
	MISHPACHA_API bool HasHebrew(const FString& Text);

	/**
	 * The canvas draws characters left to right, so a Hebrew line is turned into visual order first:
	 * Hebrew runs reversed (brackets mirrored), Latin words and numbers kept as they read, runs in
	 * right-to-left order. A simplified bidi algorithm - enough for UI lines.
	 */
	MISHPACHA_API FString ToVisualOrder(const FString& Text);

	/** Roboto for Latin + Noto Sans Hebrew for Hebrew, when /Game/Mishpacha/Fonts has the Hebrew faces (else plain Roboto). */
	MISHPACHA_API FSlateFontInfo MakeFont(const UObject* RobotoFont, int32 Size, bool bBold);

	MISHPACHA_API bool HasHebrewFont();

	/** Display name for a sibling id (Rafi / Shira / Omer -> localized, e.g. רפי), upper-cased for Latin. Other ids pass through. */
	MISHPACHA_API FText SiblingName(FName SiblingId, bool bUpper = true);

	/** "en" / "he": applies the culture (all LOCTEXT strings switch live) and remembers it. */
	MISHPACHA_API void SetLanguage(const FString& Culture);
	MISHPACHA_API FString GetLanguage();

	/** At startup: the saved language (default Hebrew when the Hebrew font is there, else English). */
	MISHPACHA_API void ApplySavedLanguage();
}
