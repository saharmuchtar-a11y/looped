#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
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

// One row in DT_RoomTypes — a data-driven room TYPE the fork system can offer. Adding a new type
// is just adding a row (same pattern as cards/curses). RowName = the type Id ("Combat", "Merchant").
USTRUCT(BlueprintType)
struct FRoomTypeData : public FTableRowBase
{
	GENERATED_BODY()

	// Text shown on the portal's hovering sign ("Fight", "Merchant", "?").
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Type")
	FText DisplayLabel;

	// Which legacy room behavior this type maps to (drives the GameMode: Combat = spawn room HUD,
	// Treasure = reset pedestal picks, Boss = spawn boss). Lets the data-driven type stay compatible
	// with the existing per-type room setup. e.g. Combat->Combat, Merchant->Merchant, Event->Treasure.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Type")
	ERoomType MappedType = ERoomType::Combat;

	// Levels this type can load; the fork picks one at random (avoids back-to-back repeats).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Type")
	TArray<FName> LevelPool;

	// FLOOR 1 WARM-UP: the first rooms of a fresh run draw from here when non-empty (gentle
	// arenas only — "the first 2-3 fight rooms need to be easy", Sahar 2026-07-07). Floors 2/3
	// are exempt on purpose: down there the loop owes you nothing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Type")
	TArray<FName> LevelPoolFloor1Early;

	// FLOORS 2/3: optional per-floor pools — when non-empty, that floor draws from here instead
	// of LevelPool (empty = fall back). Lets each floor own its maps without new type rows.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Type")
	TArray<FName> LevelPoolFloor2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Type")
	TArray<FName> LevelPoolFloor3;

	// Relative odds this type is offered in a fork. Equal weights = equal chance; raise/lower to make
	// a type common or rare (e.g. a future "?" Event set low). 0 = effectively never offered.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Type")
	float Weight = 1.0f;

	// If false, this type is never offered in a random fork (e.g. Boss / scripted-only types).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Type")
	bool bOfferableInForks = true;

	// If true, this type is never offered while the player is ALREADY in one of its levels —
	// stops casino→casino (or rest→rest) chains without limiting normal repeats like fights.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Type")
	bool bNoBackToBack = false;

	// Tint for the portal's hovering sign text (Fight=red, ?=yellow, Merchant=teal, etc.).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Type")
	FLinearColor LabelColor = FLinearColor::White;
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
