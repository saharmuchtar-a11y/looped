#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "DialogueData.generated.h"

// What a dialogue choice DOES when picked. Applied via existing GameInstance / player systems so the
// dialogue stays a thin layer over the real economy/curse/artifact code. Add rows to extend.
UENUM(BlueprintType)
enum class EDialogueOutcome : uint8
{
	None            UMETA(DisplayName = "None (just branch / close)"),
	AddShards       UMETA(DisplayName = "Add Shards (OutcomeAmount)"),
	Heal            UMETA(DisplayName = "Heal Player (OutcomeAmount)"),
	Damage          UMETA(DisplayName = "Damage Player (OutcomeAmount)"),
	RandomArtifact  UMETA(DisplayName = "Grant Random Artifact"),
	GrantArtifact   UMETA(DisplayName = "Grant Specific Artifact (OutcomeId)"),
	AddCurse        UMETA(DisplayName = "Add Curse (OutcomeId)"),
	// Charges the player real Shards (GI::SpendShards). If they can't afford it the choice whiffs
	// with a "Not enough Shards" message but the dialogue still advances (POC simplicity).
	SpendShards     UMETA(DisplayName = "Spend Shards (OutcomeAmount)"),
	// Closes the dialogue and spawns a FIGHT: OutcomeAmount enemies of DT_Enemies row OutcomeId
	// ("Random" rolls each one). Exit portals stay locked until all are dead; winning pays a random
	// artifact + shards. THE event-room boss-fight/side-quest verb.
	StartFight      UMETA(DisplayName = "Start Fight (OutcomeId=DT_Enemies row, OutcomeAmount=count)"),
	// Levels up one random non-maxed card from the run deck (the StS "upgrade" event verb).
	// Empty deck / everything maxed → consolation shards instead, so the choice never whiffs.
	UpgradeRandomCard UMETA(DisplayName = "Upgrade Random Deck Card"),
	// Removes one random active curse (purifier events). No curses → "nothing to cleanse".
	CleanseCurse    UMETA(DisplayName = "Cleanse Random Curse"),
	// Vorr's Casino: wagers OutcomeAmount Shards — 45% win (double back), 45% lose it,
	// 10% JACKPOT (triple back). Button locks if the player can't cover the wager.
	GambleShards    UMETA(DisplayName = "Gamble Shards (OutcomeAmount = wager)")
};

// One selectable option on a dialogue node.
USTRUCT(BlueprintType)
struct FDialogueChoice
{
	GENERATED_BODY()

	// Button label, e.g. "Take the shard" / "Leave".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FText ChoiceText;

	// Node to show after picking this. None = end the dialogue.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FName NextNode;

	// What this choice grants/costs when picked.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	EDialogueOutcome Outcome = EDialogueOutcome::None;

	// Id for outcomes that need one (curse id / artifact id). Ignored otherwise.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FName OutcomeId;

	// Amount for shards / heal / damage outcomes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	int32 OutcomeAmount = 0;
};

// A single dialogue node (one screen of text + its choices). RowName = node id; NextNode points here.
USTRUCT(BlueprintType)
struct FDialogueNode : public FTableRowBase
{
	GENERATED_BODY()

	// Who's talking (shown above the body). Optional.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FText Speaker;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue", meta = (MultiLine = true))
	FText Body;

	// 1-4 choices. A single "Continue"-style choice (Outcome None, NextNode -> next line or None) is
	// how you do linear lines. Multiple choices = a branching decision.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TArray<FDialogueChoice> Choices;
};
