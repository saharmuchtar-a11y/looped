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
	AddCurse        UMETA(DisplayName = "Add Curse (OutcomeId)")
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
