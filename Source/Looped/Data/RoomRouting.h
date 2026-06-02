#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RoomRouting.generated.h"

// Kind of room a node represents. Replaces string-based level detection (MapName.Contains).
UENUM(BlueprintType)
enum class ERoomType : uint8
{
	Combat   UMETA(DisplayName = "Combat"),
	Merchant UMETA(DisplayName = "Merchant"),
	Treasure UMETA(DisplayName = "Treasure"),
	Boss     UMETA(DisplayName = "Boss")
};

// Named level pool a slot can draw a random level from. None = use the slot's FixedLevel.
UENUM(BlueprintType)
enum class ERoomPool : uint8
{
	None        UMETA(DisplayName = "None (use Fixed Level)"),
	EarlyCombat UMETA(DisplayName = "Early Combat"),
	LateCombat  UMETA(DisplayName = "Late Combat"),
	Treasure    UMETA(DisplayName = "Treasure")
};

// One editable slot in the run's shape (the "recipe"). Resolves to one FRoomNode at
// generation time: if Pool == None, FixedLevel is used; otherwise a random level is drawn
// from the named pool (no back-to-back repeat).
USTRUCT(BlueprintType)
struct FRunSlotRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run Slot")
	ERoomType Type = ERoomType::Combat;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run Slot")
	ERoomPool Pool = ERoomPool::EarlyCombat;

	// Used only when Pool == None (e.g. the Merchant or Boss level).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run Slot",
		meta = (EditCondition = "Pool == ERoomPool::None"))
	FName FixedLevel = NAME_None;
};

// A resolved room in the generated run path (the "baked result"). Built fresh each run and
// stored in ULoopedGameInstance::CurrentRunPath.
USTRUCT(BlueprintType)
struct FRoomNode
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Run Node")
	FName LevelName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Run Node")
	ERoomType Type = ERoomType::Combat;

	// 1-based position in the generated path.
	UPROPERTY(BlueprintReadOnly, Category = "Run Node")
	int32 RoomIndex = 0;
};

// Data-driven routing config (Primary Data Asset). Edit DA_RunRouting in the editor to change
// the level pools and the ordered run shape. Loaded by ULoopedGameInstance at Init() by path,
// the same clean pattern as DT_PassiveCards — keeps the GameInstance pure C++.
//
// The constructor seeds the approved default 8-room matrix and pools so a freshly created
// DA_RunRouting is immediately playable; everything remains editable per-asset.
UCLASS(BlueprintType)
class LOOPED_API URunRoutingConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	URunRoutingConfig()
	{
		// Combat pools (reuse the existing 5 rooms; the generator avoids back-to-back repeats).
		EarlyCombatPool = { FName(TEXT("L_Room1")), FName(TEXT("L_Room2")), FName(TEXT("L_Room3")) };
		LateCombatPool  = { FName(TEXT("L_Room4")), FName(TEXT("L_Room5")) };

		// Default 8-room shape: 3 early combat -> merchant -> 3 late combat -> boss.
		auto Slot = [](ERoomType InType, ERoomPool InPool, FName InFixed)
		{
			FRunSlotRule R;
			R.Type = InType;
			R.Pool = InPool;
			R.FixedLevel = InFixed;
			return R;
		};
		RunLayout = {
			Slot(ERoomType::Combat,   ERoomPool::EarlyCombat, NAME_None),
			Slot(ERoomType::Combat,   ERoomPool::EarlyCombat, NAME_None),
			Slot(ERoomType::Combat,   ERoomPool::EarlyCombat, NAME_None),
			Slot(ERoomType::Merchant, ERoomPool::None,        FName(TEXT("L_Merchant"))),
			Slot(ERoomType::Combat,   ERoomPool::LateCombat,  NAME_None),
			Slot(ERoomType::Combat,   ERoomPool::LateCombat,  NAME_None),
			Slot(ERoomType::Combat,   ERoomPool::LateCombat,  NAME_None),
			Slot(ERoomType::Boss,     ERoomPool::None,        FName(TEXT("L_FinalBoss"))),
		};
	}

	// Combat level pools. Levels may repeat across a run, but never back-to-back.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pools")
	TArray<FName> EarlyCombatPool;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pools")
	TArray<FName> LateCombatPool;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pools")
	TArray<FName> TreasurePool;

	// The ordered shape of a run. Each entry resolves to one room node at generation time.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Layout")
	TArray<FRunSlotRule> RunLayout;

	// Index into the GENERATED path (0-based) that is FORCED to a Treasure room drawn from
	// TreasurePool, overwriting whatever generated there. Guarantees one treasure beat per run at
	// a fixed position. Set to -1 to disable. Default 5 = the 6th room (a late-combat slot in the
	// default layout). Requires TreasurePool to be non-empty.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Layout")
	int32 TreasureSlotIndex = 5;
};
