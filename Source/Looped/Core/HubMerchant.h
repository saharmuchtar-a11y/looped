#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HubMerchant.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UWidgetComponent;
class UUserWidget;
class APlayerController;

// What a merchant good does when bought.
UENUM()
enum class EShopGoodType : uint8
{
	NextRunCard,        // hub: an already-unlocked card as a one-run starting boon (consumed at run start)
	PermanentArtifact,  // hub vault: grants a permanent artifact
	PermanentUpgrade,   // hub vault: grants a permanent meta-upgrade
	InRunCard,          // in-run: level up an unlocked card RIGHT NOW (this run), paid in Shards
	Heal,               // in-run: restore HP, paid in Shards
	CleanseCurse,       // in-run: remove an active curse, paid in Shards
	ShardExchange,      // in-run: convert Shards -> permanent Echoes (poor rate)
	RunBlessing,        // in-run: a random run Blessing, paid in Shards (the cache's star item)
	RunMaxHP            // in-run: "Void Vigor" — +max HP for THIS run only
};

/**
 * Hub merchant "Vorr, the Hollow Broker". Walk into the trigger -> WBP_Merchant opens.
 *
 * TWO sections sharing the same 5 slots, flipped by a section button:
 *   - WARES (default): your ALREADY-UNLOCKED cards, sold as NEXT-RUN-ONLY boons. Does NOT unlock.
 *   - VAULT (progression-gated, opens on first boss kill): permanent artifacts + meta-upgrades.
 *
 * Buttons bind from C++ by name — same pattern as the Boss/Room HUDs.
 */
UCLASS()
class LOOPED_API AHubMerchant : public AActor
{
	GENERATED_BODY()

public:
	AHubMerchant();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// Vorr rotates (yaw only) to face the player every frame.
	UPROPERTY(EditAnywhere, Category = "Merchant")
	bool bFacePlayer = true;

	// Degrees to add so the model's front lines up with the player (tune per model).
	UPROPERTY(EditAnywhere, Category = "Merchant")
	float FaceYawOffset = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Merchant")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Merchant")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Merchant")
	TObjectPtr<USphereComponent> Trigger;

	// Floating "Vorr" name tag above the merchant (screen-space, always faces camera) — same
	// pattern as the treasure chest's sign.
	UPROPERTY(VisibleAnywhere, Category = "Merchant")
	TObjectPtr<UWidgetComponent> FloatingNameComp;

	UPROPERTY(EditDefaultsOnly, Category = "Merchant")
	float TriggerRadius = 260.0f;

	// When TRUE this is the mid-run "cache" shop (Shards, THIS run): buy cards now, heal,
	// cleanse curses — no vault, no next-run boons. Set on the merchant placed in run rooms.
	UPROPERTY(EditAnywhere, Category = "Merchant")
	bool bInRunShop = false;

	UPROPERTY(EditDefaultsOnly, Category = "Merchant|InRun")
	int32 InRunCardCost = 60;     // Shards to level a card this run (legacy; cards no longer stocked)

	UPROPERTY(EditDefaultsOnly, Category = "Merchant|InRun")
	int32 InRunHealCost = 50;     // Shards to heal

	UPROPERTY(EditDefaultsOnly, Category = "Merchant|InRun")
	float InRunHealAmount = 40.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Merchant|InRun")
	int32 InRunCleanseCost = 90;  // premium — the Sanctum's blood price is the cheap path

	// Rising prices: heal cost climbs by this each purchase (this visit) — no more heal-spam.
	UPROPERTY(EditDefaultsOnly, Category = "Merchant|InRun")
	int32 InRunHealCostStep = 25;

	// Cache star item: a random run Blessing.
	UPROPERTY(EditDefaultsOnly, Category = "Merchant|InRun")
	int32 InRunBlessingCost = 110;

	// "Void Vigor" — +max HP for this run only.
	UPROPERTY(EditDefaultsOnly, Category = "Merchant|InRun")
	int32 InRunVigorCost = 80;

	UPROPERTY(EditDefaultsOnly, Category = "Merchant|InRun")
	int32 InRunVigorAmount = 20;

	// Shard -> Echo exchange (poor rate, a sink for leftover Shards).
	UPROPERTY(EditDefaultsOnly, Category = "Merchant|InRun")
	int32 InRunExchangeShardCost = 50;

	UPROPERTY(EditDefaultsOnly, Category = "Merchant|InRun")
	int32 InRunExchangeEchoYield = 10;

	// Reroll the card stock; cost rises each reroll this visit.
	UPROPERTY(EditDefaultsOnly, Category = "Merchant|InRun")
	int32 InRunRerollCost = 20;

	UPROPERTY(EditDefaultsOnly, Category = "Merchant|InRun")
	int32 InRunStockSize = 2;  // how many random cards the cache offers

	static constexpr int32 NumSlots = 5;

	// In-run cache state (per shop visit).
	TArray<FName> InRunStock;
	int32 RerollCount = 0;
	int32 HealBuyCount = 0;
	void RollInRunStock();

	UFUNCTION()
	void OnTriggerBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UFUNCTION()
	void OnTriggerEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void OpenShop(APlayerController* PC);
	void CloseShop();
	void RefreshShop();
	void TryBuy(int32 SlotIndex);

	UFUNCTION() void OnBuy0();
	UFUNCTION() void OnBuy1();
	UFUNCTION() void OnBuy2();
	UFUNCTION() void OnBuy3();
	UFUNCTION() void OnBuy4();
	UFUNCTION() void OnCloseClicked();
	UFUNCTION() void OnResetClicked();
	UFUNCTION() void OnToggleSection();

	struct FShopItem
	{
		EShopGoodType Type = EShopGoodType::NextRunCard;
		FName Id;          // card name / artifact name / upgrade key
		FString Display;
		int32 Cost = 0;
		int32 Amount = 0;  // payload for upgrades (e.g. +MaxHP amount)
	};

	// Returns true if the player already owns / has queued this item.
	bool IsItemOwned(const FShopItem& Item) const;

	TArray<FShopItem> CardCatalogue;       // WARES section (filtered to unlocked at refresh)
	TArray<FShopItem> PermanentCatalogue;  // VAULT section
	TArray<FShopItem> Displayed;           // slot index -> item, rebuilt each RefreshShop()

	bool bShowingVault = false;            // which section is visible

	TSubclassOf<UUserWidget> ShopWidgetClass;

	UPROPERTY()
	TObjectPtr<UUserWidget> ShopWidget;

	UPROPERTY()
	TObjectPtr<APlayerController> CurrentPC;

	bool bShopOpen = false;

	// Shop button-click SFX (loaded by path in the ctor; played on buy / toggle / close clicks).
	UPROPERTY()
	TObjectPtr<class USoundBase> ButtonSound;

	void PlayButtonSound();
};
