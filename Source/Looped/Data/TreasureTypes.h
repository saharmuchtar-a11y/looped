#pragma once

#include "CoreMinimal.h"
#include "TreasureTypes.generated.h"

// What a treasure pedestal offers. First three are wired this sprint; the rest are scaffolded
// (selectable in the editor, but AcceptPedestal treats them as no-ops + a "coming soon" label).
UENUM(BlueprintType)
enum class ETreasureRewardType : uint8
{
	CleanRelic        UMETA(DisplayName = "Clean Relic (non-cursed run artifact)"),
	CursedRelic       UMETA(DisplayName = "Cursed Bargain (powerful cursed artifact)"),
	CardBundle        UMETA(DisplayName = "Card Bundle (N cards this run)"),
	EchoBargain       UMETA(DisplayName = "Echo Bargain (TODO)"),
	PurifyingFountain UMETA(DisplayName = "Purifying Fountain (TODO)"),
	DeckTrimmer       UMETA(DisplayName = "Deck Trimmer (TODO)")
};
