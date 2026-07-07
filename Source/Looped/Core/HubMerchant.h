#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Styling/SlateColor.h"
#include "Core/LoopedInteractable.h"
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
	RunMaxHP,           // in-run: "Void Vigor" — +max HP for THIS run only
	CompanionRansom,    // hub: buy Serin's freedom — Echoes + a curse stamped on the next run
	SellRunRelic,       // in-run SELL page: pawn a held run relic — Cost = Shards YOU RECEIVE
	SellQueuedCard,     // hub SELL page: refund a queued next-run boon — Cost = Echoes YOU RECEIVE
	SellPermRelic       // hub SELL page: sell a permanent relic — Cost = Echoes YOU RECEIVE
};

// Which book-style tab page the shop is showing (WARES | VAULT | SELL).
enum class EShopSection : uint8 { Wares, Vault, Sell };

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
class LOOPED_API AHubMerchant : public AActor, public ILoopedInteractable
{
	GENERATED_BODY()

public:
	AHubMerchant();

	// Press-E to trade. Walking in range no longer auto-opens the shop; walking OUT still
	// auto-closes it.
	virtual void Interact(class ALoopedCharacter* Player) override;
	virtual float GetInteractRange() const override { return TriggerRadius; }
	virtual FText GetInteractPrompt() const override
	{
		return bShopOpen ? FText::GetEmpty() : FText::FromString(TEXT("trade with Vorr"));
	}

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

	// Serin "Fence" (rescued-companion relic): every price in this shop is multiplied by this
	// while Serin is rescued. Applied once in RefreshShop (TryBuy charges the Displayed cost, so
	// display + charge always agree). The ONLY discount in the game — keep it unique.
	UPROPERTY(EditDefaultsOnly, Category = "Merchant")
	float SerinDiscountMult = 0.85f;

	// WARES next-run boon prices by card rarity — the tier is FELT at the counter.
	UPROPERTY(EditDefaultsOnly, Category = "Merchant|Wares")
	int32 CardCostCommon = 70;

	UPROPERTY(EditDefaultsOnly, Category = "Merchant|Wares")
	int32 CardCostRare = 130;

	UPROPERTY(EditDefaultsOnly, Category = "Merchant|Wares")
	int32 CardCostEpic = 200;

	// Hub WARES stocks a rotating FEW cards per visit (Sahar: not one of each), skipping
	// already-queued ones. Rolled once per shop open so the offer holds steady while browsing.
	UPROPERTY(EditDefaultsOnly, Category = "Merchant|Wares")
	int32 RotatingWaresCount = 3;

	// "Deal of the loop": one random wares row per visit sells at this multiplier, shown as a
	// slashed original + red price (reuses the Fence display path).
	UPROPERTY(EditDefaultsOnly, Category = "Merchant|Wares")
	float DealDiscountMult = 0.7f;

	// SELL page payouts. Pawning a run relic pays flat Shards; buying back a queued boon
	// refunds this fraction of its catalogue price in Echoes (Vorr keeps his cut).
	UPROPERTY(EditDefaultsOnly, Category = "Merchant|Sell")
	int32 SellRelicShards = 45;

	UPROPERTY(EditDefaultsOnly, Category = "Merchant|Sell")
	float QueuedCardRefundMult = 0.6f;

	// --- Serin's ransom (the third rescue): offered in the hub WARES once Brann is rescued ---
	UPROPERTY(EditDefaultsOnly, Category = "Merchant|Ransom")
	int32 SerinRansomCost = 300;

	// The cruelty Vorr stamps on your NEXT run as part of the price (the "never free" rule).
	UPROPERTY(EditDefaultsOnly, Category = "Merchant|Ransom")
	FName RansomCurseId = FName(TEXT("Marked"));

	static constexpr int32 NumSlots = 9;

	// In-run cache state (per shop visit).
	TArray<FName> InRunStock;
	int32 RerollCount = 0;
	int32 HealBuyCount = 0;
	void RollInRunStock();

	// Hub WARES visit state: the rotating card offer + which row is this visit's deal.
	TArray<FName> HubWaresStock;
	FName DealCardId;
	void RollHubWaresStock();

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
	UFUNCTION() void OnBuy5();
	UFUNCTION() void OnBuy6();
	UFUNCTION() void OnBuy7();
	UFUNCTION() void OnBuy8();
	UFUNCTION() void OnCloseClicked();
	UFUNCTION() void OnResetClicked();
	UFUNCTION() void OnTabWares();
	UFUNCTION() void OnTabVault();
	UFUNCTION() void OnTabSell();

	struct FShopItem
	{
		EShopGoodType Type = EShopGoodType::NextRunCard;
		FName Id;          // card name / artifact name / upgrade key
		FString Display;
		int32 Cost = 0;
		int32 Amount = 0;  // payload for upgrades (e.g. +MaxHP amount)
		int32 BaseCost = 0; // pre-Fence price; > Cost means "show the slashed original + red price"
	};

	// Returns true if the player already owns / has queued this item.
	bool IsItemOwned(const FShopItem& Item) const;

	TArray<FShopItem> CardCatalogue;       // WARES section (filtered to unlocked at refresh)
	TArray<FShopItem> PermanentCatalogue;  // VAULT section
	TArray<FShopItem> Displayed;           // slot index -> item, rebuilt each RefreshShop()

	EShopSection CurrentSection = EShopSection::Wares; // which tab page is visible

	// Cost-text color as designed in the widget, captured before the Fence discount ever
	// paints one red — restored on rows that aren't discounted. Name color likewise, so
	// rarity tinting on card rows can restore non-card rows.
	FSlateColor DefaultCostColor;
	bool bCostColorCached = false;
	FSlateColor DefaultNameColor;
	bool bNameColorCached = false;

	// Repaint the WARES/VAULT/SELL tab buttons (active lit, locked vault dimmed, cache hides vault).
	void RefreshTabVisuals();

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
