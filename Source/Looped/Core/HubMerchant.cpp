#include "HubMerchant.h"
#include "Looped.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Core/LoopedGameInstance.h"
#include "Core/CompanionCage.h"
#include "EngineUtils.h"
#include "Player/LoopedCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

AHubMerchant::AHubMerchant()
{
	PrimaryActorTick.bCanEverTick = true; // Vorr turns to face the player

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(SceneRoot);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Placeholder visual — a cylinder "stall". Swap for proper art later.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> MeshFinder(TEXT("/Engine/BasicShapes/Cylinder"));
	if (MeshFinder.Succeeded())
	{
		Mesh->SetStaticMesh(MeshFinder.Object);
		Mesh->SetRelativeScale3D(FVector(1.2f, 1.2f, 2.0f));
	}

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(SceneRoot);
	Trigger->SetSphereRadius(TriggerRadius);
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AHubMerchant::OnTriggerBegin);
	Trigger->OnComponentEndOverlap.AddDynamic(this, &AHubMerchant::OnTriggerEnd);

	// Floating name tag above Vorr's head — screen-space so it always faces the camera (same as
	// the treasure sign). WBP_NameTag already exists, so a ctor FClassFinder is safe here.
	FloatingNameComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("FloatingNameComp"));
	FloatingNameComp->SetupAttachment(SceneRoot);
	FloatingNameComp->SetRelativeLocation(FVector(0.0f, 0.0f, 240.0f)); // above the tall cylinder stall
	FloatingNameComp->SetWidgetSpace(EWidgetSpace::Screen);
	FloatingNameComp->SetDrawSize(FVector2D(300.0f, 80.0f));
	FloatingNameComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FClassFinder<UUserWidget> NameTagClass(TEXT("/Game/UI/WBP_NameTag"));
	if (NameTagClass.Succeeded())
	{
		FloatingNameComp->SetWidgetClass(NameTagClass.Class);
	}

	static ConstructorHelpers::FObjectFinder<USoundBase> BtnSnd(TEXT("/Game/Audio/button.button"));
	if (BtnSnd.Succeeded()) ButtonSound = BtnSnd.Object;

	// NOTE: the shop widget class is loaded at runtime in OpenShop() via LoadClass — NOT here.
	// A constructor FClassFinder caches once at startup; if WBP_Merchant didn't exist yet it
	// would cache null forever. Runtime LoadClass is robust to asset-creation order.
}

void AHubMerchant::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bFacePlayer || !Mesh) return;

	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player) return;

	FVector ToPlayer = Player->GetActorLocation() - Mesh->GetComponentLocation();
	ToPlayer.Z = 0.0f; // yaw only — keep Vorr upright
	if (ToPlayer.IsNearlyZero()) return;

	const float Yaw = ToPlayer.Rotation().Yaw + FaceYawOffset;
	Mesh->SetWorldRotation(FRotator(0.0f, Yaw, 0.0f));
}

void AHubMerchant::BeginPlay()
{
	Super::BeginPlay();

	Trigger->SetSphereRadius(TriggerRadius);

	// WARES — every GATED card in the table, sold as a next-run boon, PRICED BY RARITY.
	// Data-driven: a new DT row with bRequiresUnlock shows up here automatically (and stays
	// hidden until unlocked — the refresh filter). Sorted cheap→dear so tiers read at a glance.
	// NOTE: only the first NumSlots(5) unlocked items fit until the shop-UI pass adds room.
	CardCatalogue.Reset();
	if (const ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance()))
	{
		for (const FName& CardId : GI->GetAllCardIds())
		{
			if (!GI->IsCardGated(CardId)) continue; // ungated Commons are free pickups in-run

			const ECardRarity Rarity = GI->GetPerkRarity(CardId);
			if (Rarity == ECardRarity::Cursed) continue; // never sold

			FShopItem It;
			It.Type = EShopGoodType::NextRunCard;
			It.Id = CardId;
			It.Cost = (Rarity == ECardRarity::Epic) ? CardCostEpic
			        : (Rarity == ECardRarity::Rare) ? CardCostRare
			                                        : CardCostCommon;
			It.Display = FString::Printf(TEXT("%s — next-run boon  [%s]"),
				*GI->GetCardDisplayName(CardId).ToString(), GetRarityWord(Rarity));
			CardCatalogue.Add(It);
		}
		CardCatalogue.Sort([](const FShopItem& A, const FShopItem& B)
		{
			return A.Cost != B.Cost ? A.Cost < B.Cost : A.Id.LexicalLess(B.Id);
		});
	}

	// VAULT — permanent goods (progression-gated section).
	PermanentCatalogue.Reset();
	PermanentCatalogue.Add({ EShopGoodType::PermanentArtifact, TEXT("GoldBar"),       TEXT("Gold Bar — +10% Echoes forever"),            500, 0 });
	PermanentCatalogue.Add({ EShopGoodType::PermanentUpgrade,  TEXT("CardChoice"),    TEXT("Foresight — +1 card choice"),                600, 0 });
	PermanentCatalogue.Add({ EShopGoodType::PermanentUpgrade,  TEXT("MaxHP"),         TEXT("Vitality — +25 max HP forever"),             450, 25 });
	PermanentCatalogue.Add({ EShopGoodType::PermanentUpgrade,  TEXT("StartShards"),   TEXT("Deep Pockets — start runs with +60 Shards"), 550, 60 });
	PermanentCatalogue.Add({ EShopGoodType::PermanentUpgrade,  TEXT("StartBlessing"), TEXT("Keepsake — start runs with a Blessing"),     700, 0 });
	PermanentCatalogue.Add({ EShopGoodType::PermanentArtifact, TEXT("BrokersSeal"),   TEXT("Broker's Seal — the cache stocks +1 card"),  650, 0 });
}

void AHubMerchant::OnTriggerBegin(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*Sweep*/)
{
	// Walk-in no longer opens the shop — trading is a deliberate press-E (Interact). The end-
	// overlap below keeps auto-closing so backing away still dismisses the shop.
}

void AHubMerchant::Interact(ALoopedCharacter* Player)
{
	if (bShopOpen || !Player) return;
	if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
	{
		OpenShop(PC);
	}
}

void AHubMerchant::OnTriggerEnd(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled()) return;
	CloseShop();
}

void AHubMerchant::OpenShop(APlayerController* PC)
{
	if (!ShopWidgetClass)
	{
		ShopWidgetClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/UI/WBP_Merchant.WBP_Merchant_C"));
	}
	if (!PC || !ShopWidgetClass) return;

	CurrentPC = PC;
	CurrentSection = EShopSection::Wares; // always open on the first page
	if (!ShopWidget)
	{
		ShopWidget = CreateWidget<UUserWidget>(PC, ShopWidgetClass);
		if (!ShopWidget) return;

		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("Item0Buy"))))      B->OnClicked.AddDynamic(this, &AHubMerchant::OnBuy0);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("Item1Buy"))))      B->OnClicked.AddDynamic(this, &AHubMerchant::OnBuy1);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("Item2Buy"))))      B->OnClicked.AddDynamic(this, &AHubMerchant::OnBuy2);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("Item3Buy"))))      B->OnClicked.AddDynamic(this, &AHubMerchant::OnBuy3);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("Item4Buy"))))      B->OnClicked.AddDynamic(this, &AHubMerchant::OnBuy4);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("Item5Buy"))))      B->OnClicked.AddDynamic(this, &AHubMerchant::OnBuy5);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("Item6Buy"))))      B->OnClicked.AddDynamic(this, &AHubMerchant::OnBuy6);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("Item7Buy"))))      B->OnClicked.AddDynamic(this, &AHubMerchant::OnBuy7);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("Item8Buy"))))      B->OnClicked.AddDynamic(this, &AHubMerchant::OnBuy8);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("CloseButton"))))   B->OnClicked.AddDynamic(this, &AHubMerchant::OnCloseClicked);
		// (Dev save-wipe button moved to the First Hunter's logbook — hide it in the shop.)
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("ResetButton"))))   B->SetVisibility(ESlateVisibility::Collapsed);
		// Book-style page tabs (the old toggle button is gone from the widget).
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("TabWares"))))      B->OnClicked.AddDynamic(this, &AHubMerchant::OnTabWares);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("TabVault"))))      B->OnClicked.AddDynamic(this, &AHubMerchant::OnTabVault);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("TabSell"))))       B->OnClicked.AddDynamic(this, &AHubMerchant::OnTabSell);
	}

	ShopWidget->AddToViewport(150);
	bShopOpen = true;

	PC->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(InputMode);

	// Fresh stock each open: cache rolls its level-up cards (+ rising-price counters reset);
	// the hub rolls its rotating WARES + this visit's deal.
	if (bInRunShop)
	{
		RerollCount = 0;
		HealBuyCount = 0;
		RollInRunStock();
	}
	else
	{
		RollHubWaresStock();
	}

	RefreshShop();
	UE_LOG(LogLoopedCore, Display, TEXT("[Merchant] Shop opened."));
}

void AHubMerchant::CloseShop()
{
	if (!bShopOpen) return;
	bShopOpen = false;

	if (ShopWidget)
	{
		ShopWidget->RemoveFromParent();
	}
	if (CurrentPC)
	{
		CurrentPC->bShowMouseCursor = false;
		CurrentPC->SetInputMode(FInputModeGameOnly());
	}
	UE_LOG(LogLoopedCore, Display, TEXT("[Merchant] Shop closed."));
}

bool AHubMerchant::IsItemOwned(const FShopItem& Item) const
{
	const ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance());
	if (!GI) return false;
	switch (Item.Type)
	{
	case EShopGoodType::NextRunCard:       return GI->IsCardPendingNextRun(Item.Id);
	case EShopGoodType::PermanentArtifact: return GI->HasArtifact(Item.Id);
	case EShopGoodType::PermanentUpgrade:
		if (Item.Id == TEXT("CardChoice"))    return GI->HasPermanentCardChoice();
		if (Item.Id == TEXT("MaxHP"))         return GI->HasPermanentMaxHP();
		if (Item.Id == TEXT("StartShards"))   return GI->HasPermanentStartingShards();
		if (Item.Id == TEXT("StartBlessing")) return GI->HasPermanentStartingBlessing();
		return false;
	default: return false;
	}
}

void AHubMerchant::RollInRunStock()
{
	InRunStock.Reset();
	ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance());
	if (!GI) return;
	// Every card in the table is fair game mid-run (Commons included — you level what you
	// hold), as long as it's unlocked and not already maxed (never a 6/5 on the shelf).
	TArray<FName> Pool;
	for (const FName& CardId : GI->GetAllCardIds())
	{
		if (GI->IsCardUnlocked(CardId) && !GI->IsPerkAtMax(CardId)) Pool.Add(CardId);
	}
	for (int32 i = Pool.Num() - 1; i > 0; --i) { const int32 j = FMath::RandRange(0, i); Pool.Swap(i, j); }
	// Relic "BrokersSeal": Vorr keeps one more card on the cache shelf for sealed customers.
	const int32 StockSize = InRunStockSize + (GI->HasArtifact(TEXT("BrokersSeal")) ? 1 : 0);
	const int32 N = FMath::Min(StockSize, Pool.Num());
	for (int32 i = 0; i < N; ++i) InRunStock.Add(Pool[i]);
}

void AHubMerchant::RollHubWaresStock()
{
	// A rotating FEW per visit (Sahar: never the whole shelf) — unlocked, not already queued.
	// One of them becomes this visit's DEAL. Rolled once per open so browsing doesn't reshuffle.
	HubWaresStock.Reset();
	DealCardId = NAME_None;
	const ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance());
	if (!GI) return;

	TArray<FName> Pool;
	for (const FShopItem& Item : CardCatalogue)
	{
		if (GI->IsCardUnlocked(Item.Id) && !GI->IsCardPendingNextRun(Item.Id)) Pool.Add(Item.Id);
	}
	for (int32 i = Pool.Num() - 1; i > 0; --i) { const int32 j = FMath::RandRange(0, i); Pool.Swap(i, j); }
	const int32 N = FMath::Min(RotatingWaresCount, Pool.Num());
	for (int32 i = 0; i < N; ++i) HubWaresStock.Add(Pool[i]);
	if (HubWaresStock.Num() > 0)
	{
		DealCardId = HubWaresStock[FMath::RandRange(0, HubWaresStock.Num() - 1)];
	}
}

void AHubMerchant::RefreshTabVisuals()
{
	if (!ShopWidget) return;
	const ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance());
	const bool bVaultUnlocked = GI && GI->IsPermanentVaultUnlocked();

	const FLinearColor Active(0.16f, 0.55f, 0.66f);
	const FLinearColor Idle(0.07f, 0.18f, 0.24f);
	const FLinearColor Locked(0.10f, 0.11f, 0.13f);

	const struct { const TCHAR* Btn; EShopSection Sec; } Tabs[] = {
		{ TEXT("TabWares"), EShopSection::Wares },
		{ TEXT("TabVault"), EShopSection::Vault },
		{ TEXT("TabSell"),  EShopSection::Sell  },
	};
	for (const auto& T : Tabs)
	{
		UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(T.Btn));
		if (!B) continue;
		if (T.Sec == EShopSection::Vault && bInRunShop)
		{
			B->SetVisibility(ESlateVisibility::Collapsed); // the cache has no vault
			continue;
		}
		B->SetVisibility(ESlateVisibility::Visible);
		const bool bLockedTab = (T.Sec == EShopSection::Vault && !bVaultUnlocked);
		B->SetBackgroundColor(CurrentSection == T.Sec ? Active : (bLockedTab ? Locked : Idle));
	}
	if (UTextBlock* WT = Cast<UTextBlock>(ShopWidget->GetWidgetFromName(TEXT("TabWaresText"))))
	{
		WT->SetText(FText::FromString(bInRunShop ? TEXT("CACHE") : TEXT("WARES")));
	}
}

void AHubMerchant::RefreshShop()
{
	if (!ShopWidget) return;
	ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance());

	int32 Currency = 0;
	FString BalanceLabel, TitleLabel;
	Displayed.Reset();

	Currency = bInRunShop ? (GI ? GI->GetShards() : 0) : (GI ? GI->GetEchoes() : 0);
	if (bInRunShop) BalanceLabel = FString::Printf(TEXT("Shards: %d"), Currency);
	else            BalanceLabel = FString::Printf(TEXT("Echoes: %d"), Currency);

	switch (CurrentSection)
	{
	case EShopSection::Wares:
	{
		if (bInRunShop)
		{
			// Mid-run CACHE: spend Shards on THIS-run goods.
			TitleLabel = TEXT("CACHE — this run only");

			FShopItem H; H.Type = EShopGoodType::Heal; H.Id = TEXT("Heal");
			H.Display = FString::Printf(TEXT("Heal  +%.0f HP"), InRunHealAmount);
			H.Cost = InRunHealCost + HealBuyCount * InRunHealCostStep; // rising price
			Displayed.Add(H);

			// Hide cleanse when the run has no curses — buying it was a silent no-op (Sahar playtest).
			if (GI && GI->GetActiveCurses().Num() > 0)
			{
				FShopItem Cl; Cl.Type = EShopGoodType::CleanseCurse; Cl.Id = TEXT("Cleanse");
				Cl.Display = TEXT("Cleanse a curse"); Cl.Cost = InRunCleanseCost;
				Displayed.Add(Cl);
			}

			// The cache's star item: a random run Blessing.
			FShopItem Bl; Bl.Type = EShopGoodType::RunBlessing; Bl.Id = TEXT("Blessing");
			Bl.Display = TEXT("Mystery Blessing"); Bl.Cost = InRunBlessingCost;
			Displayed.Add(Bl);

			FShopItem Vg; Vg.Type = EShopGoodType::RunMaxHP; Vg.Id = TEXT("Vigor");
			Vg.Display = FString::Printf(TEXT("Void Vigor  +%d max HP (this run)"), InRunVigorAmount);
			Vg.Cost = InRunVigorCost;
			Displayed.Add(Vg);

			FShopItem Ex; Ex.Type = EShopGoodType::ShardExchange; Ex.Id = TEXT("Exchange");
			Ex.Display = FString::Printf(TEXT("Exchange -> %d Echoes"), InRunExchangeEchoYield);
			Ex.Cost = InRunExchangeShardCost;
			Displayed.Add(Ex);

			// LEVEL-UPS: the rolled stock (RollInRunStock already excludes maxed cards — a
			// level can never read 6/5). Pay Shards, the perk levels up live.
			if (GI)
			{
				for (const FName& CardId : InRunStock)
				{
					if (GI->IsPerkAtMax(CardId)) continue; // re-check: it may have maxed since the roll
					FShopItem Lv;
					Lv.Type = EShopGoodType::InRunCard;
					Lv.Id = CardId;
					Lv.Display = FString::Printf(TEXT("LEVEL UP: %s -> Lv %d"),
						*GI->GetCardDisplayName(CardId).ToString(), GI->GetPerkLevel(CardId) + 1);
					Lv.Cost = InRunCardCost;
					Displayed.Add(Lv);
				}
			}
		}
		else
		{
			// Hub WARES: the rotating few (rolled at open), one of them this visit's DEAL.
			TitleLabel = TEXT("WARES — next-run boons");
			for (const FName& CardId : HubWaresStock)
			{
				const FShopItem* Cat = CardCatalogue.FindByPredicate(
					[&CardId](const FShopItem& It) { return It.Id == CardId; });
				if (!Cat) continue;
				FShopItem It = *Cat;
				if (It.Id == DealCardId)
				{
					It.BaseCost = It.Cost;
					It.Cost = FMath::Max(1, FMath::CeilToInt(It.Cost * DealDiscountMult));
					It.Display = TEXT("DEAL — ") + It.Display;
				}
				Displayed.Add(It);
			}

			// Serin's ransom headlines the WARES once Brann is rescued, gone forever once paid.
			if (GI && GI->HasArtifact(TEXT("Brann")) && !GI->HasArtifact(TEXT("Serin")))
			{
				FShopItem Ransom;
				Ransom.Type = EShopGoodType::CompanionRansom;
				Ransom.Id = TEXT("Serin");
				Ransom.Display = FString::Printf(TEXT("RANSOM Serin — she goes free; your next run begins %s"), *RansomCurseId.ToString());
				Ransom.Cost = SerinRansomCost;
				Displayed.Insert(Ransom, 0);
			}
		}
		break;
	}

	case EShopSection::Vault:
	{
		const bool bVaultUnlocked = GI && GI->IsPermanentVaultUnlocked();
		if (bVaultUnlocked)
		{
			TitleLabel = TEXT("VAULT — permanent");
			Displayed = PermanentCatalogue;
		}
		else
		{
			TitleLabel = TEXT("VAULT — locked. Slay the boss first.");
		}
		break;
	}

	case EShopSection::Sell:
	{
		if (bInRunShop)
		{
			// Pawn held run relics for Shards.
			if (GI)
			{
				for (const FName& RelicId : GI->GetRunArtifacts())
				{
					const FArtifactData* Row = GI->FindArtifactRow(RelicId);
					FShopItem It;
					It.Type = EShopGoodType::SellRunRelic;
					It.Id = RelicId;
					It.Display = FString::Printf(TEXT("PAWN: %s"),
						(Row && !Row->DisplayName.IsEmpty()) ? *Row->DisplayName.ToString() : *RelicId.ToString());
					It.Cost = SellRelicShards; // what YOU receive
					Displayed.Add(It);
				}
			}
			TitleLabel = Displayed.Num() > 0 ? TEXT("SELL — pawn your relics") : TEXT("SELL — nothing Vorr wants");
		}
		else
		{
			// Buy back queued next-run boons (Vorr keeps his cut).
			if (GI)
			{
				for (const FName& CardId : GI->GetPendingNextRunCards())
				{
					const FShopItem* Cat = CardCatalogue.FindByPredicate(
						[&CardId](const FShopItem& It) { return It.Id == CardId; });
					FShopItem It;
					It.Type = EShopGoodType::SellQueuedCard;
					It.Id = CardId;
					It.Display = FString::Printf(TEXT("BUYBACK: %s"), *GI->GetCardDisplayName(CardId).ToString());
					It.Cost = FMath::Max(1, FMath::FloorToInt((Cat ? Cat->Cost : CardCostCommon) * QueuedCardRefundMult));
					Displayed.Add(It);
				}

				// Sell PERMANENT relics for Echoes (Sahar 2026-07-07). Companions, the monitor
				// and the Wing are keepsakes — never on the counter. Sold relics land on the
				// SoldRelics blacklist so milestone auto-grants can't hand them straight back.
				static const FName Keepsakes[] = {
					FName("Orin"), FName("Lysa"), FName("Brann"), FName("Serin"), FName("Mira"), FName("Wing") };
				for (const FName& RelicId : GI->GetOwnedArtifactNames())
				{
					bool bKeepsake = false;
					for (const FName& K : Keepsakes) { if (RelicId == K) { bKeepsake = true; break; } }
					if (bKeepsake) continue;
					const FArtifactData* Row = GI->FindArtifactRow(RelicId);
					FShopItem It;
					It.Type = EShopGoodType::SellPermRelic;
					It.Id = RelicId;
					It.Display = FString::Printf(TEXT("SELL: %s"),
						(Row && !Row->DisplayName.IsEmpty()) ? *Row->DisplayName.ToString() : *RelicId.ToString());
					It.Cost = 90; // Echoes YOU receive (flat v1 — rarity pricing later if wanted)
					Displayed.Add(It);
				}
			}
			TitleLabel = Displayed.Num() > 0 ? TEXT("SELL — Vorr buys memories") : TEXT("SELL — nothing Vorr wants");
		}
		break;
	}
	}

	// Serin "Fence" (rescued-companion relic): her cut comes off every price Vorr CHARGES —
	// never off what he pays YOU. BaseCost keeps the pre-cut price for the red "was X -> Y".
	if (GI && GI->HasArtifact(TEXT("Serin")))
	{
		for (FShopItem& Item : Displayed)
		{
			if (Item.Type == EShopGoodType::SellRunRelic || Item.Type == EShopGoodType::SellQueuedCard) continue;
			if (Item.BaseCost <= 0) Item.BaseCost = Item.Cost; // a DEAL row already recorded it
			Item.Cost = FMath::Max(1, FMath::CeilToInt(Item.Cost * SerinDiscountMult));
		}
	}

	// Curse "Extortion": the in-run cache gouges the cursed — prices climb AFTER any discount
	// (never touches what Vorr pays YOU on sell rows; Iron Will softens the gouge).
	if (GI && bInRunShop && GI->HasCurse(TEXT("Extortion")))
	{
		const float Gouge = GI->ScaleCurseMult(GI->CurseExtortionPriceMult);
		for (FShopItem& Item : Displayed)
		{
			if (Item.Type == EShopGoodType::SellRunRelic || Item.Type == EShopGoodType::SellQueuedCard) continue;
			Item.Cost = FMath::CeilToInt(Item.Cost * Gouge);
		}
	}

	RefreshTabVisuals();

	if (UTextBlock* Bal = Cast<UTextBlock>(ShopWidget->GetWidgetFromName(TEXT("EchoesBalance"))))
	{
		Bal->SetText(FText::FromString(BalanceLabel));
	}
	if (UTextBlock* Title = Cast<UTextBlock>(ShopWidget->GetWidgetFromName(TEXT("SectionTitle"))))
	{
		Title->SetText(FText::FromString(TitleLabel));
	}

	for (int32 i = 0; i < NumSlots; ++i)
	{
		const bool bHasItem = Displayed.IsValidIndex(i);
		const FString NameStr = bHasItem ? Displayed[i].Display : TEXT("");
		const int32 Cost = bHasItem ? Displayed[i].Cost : 0;
		const bool bOwned = bHasItem && IsItemOwned(Displayed[i]);
		const bool bSellRow = bHasItem && (Displayed[i].Type == EShopGoodType::SellRunRelic
		                                || Displayed[i].Type == EShopGoodType::SellQueuedCard);
		const bool bAfford = bHasItem && Currency >= Cost;

		// Empty rows vanish instead of showing a dangling "—" button.
		if (UWidget* Row = ShopWidget->GetWidgetFromName(*FString::Printf(TEXT("ItemRow%d"), i)))
		{
			Row->SetVisibility(bHasItem ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}

		if (UTextBlock* NameT = Cast<UTextBlock>(ShopWidget->GetWidgetFromName(*FString::Printf(TEXT("Item%dName"), i))))
		{
			if (!bNameColorCached)
			{
				DefaultNameColor = NameT->GetColorAndOpacity();
				bNameColorCached = true;
			}
			NameT->SetText(FText::FromString(NameStr));
			// Card rows wear their rarity color; everything else keeps the widget's design color.
			const bool bCardRow = bHasItem && GI && (Displayed[i].Type == EShopGoodType::NextRunCard
			                                      || Displayed[i].Type == EShopGoodType::InRunCard
			                                      || Displayed[i].Type == EShopGoodType::SellQueuedCard);
			NameT->SetColorAndOpacity(bCardRow
				? FSlateColor(ULoopedGameInstance::GetRarityColor(GI->GetPerkRarity(Displayed[i].Id)))
				: DefaultNameColor);
		}
		if (UTextBlock* CostT = Cast<UTextBlock>(ShopWidget->GetWidgetFromName(*FString::Printf(TEXT("Item%dCost"), i))))
		{
			if (!bCostColorCached)
			{
				DefaultCostColor = CostT->GetColorAndOpacity();
				bCostColorCached = true;
			}
			// SELL rows show what Vorr PAYS in green. Discounted buys show the slashed original
			// in red. (U+0336 strikethrough looked right on paper but the shop font draws boxes.)
			const bool bDiscounted = !bSellRow && bHasItem && Displayed[i].BaseCost > Cost;
			FString CostStr;
			if (bSellRow)      CostStr = FString::Printf(TEXT("+%d"), Cost);
			else if (bDiscounted) CostStr = FString::Printf(TEXT("was %d -> %d"), Displayed[i].BaseCost, Cost);
			else if (bHasItem) CostStr = FString::Printf(TEXT("%d"), Cost);
			CostT->SetText(FText::FromString(CostStr));
			CostT->SetColorAndOpacity(bSellRow ? FSlateColor(FLinearColor(0.35f, 1.0f, 0.5f))
				: bDiscounted ? FSlateColor(FLinearColor(1.0f, 0.25f, 0.25f)) : DefaultCostColor);
		}
		if (UTextBlock* BuyT = Cast<UTextBlock>(ShopWidget->GetWidgetFromName(*FString::Printf(TEXT("Item%dBuyText"), i))))
		{
			const TCHAR* Label = !bHasItem ? TEXT("—")
				: bSellRow ? TEXT("SELL")
				: (bOwned ? (Displayed[i].Type == EShopGoodType::NextRunCard ? TEXT("QUEUED") : TEXT("OWNED"))
						  : TEXT("BUY"));
			BuyT->SetText(FText::FromString(Label));
		}
		if (UButton* BuyB = Cast<UButton>(ShopWidget->GetWidgetFromName(*FString::Printf(TEXT("Item%dBuy"), i))))
		{
			// Selling is always allowed; buying needs affordability and not-owned.
			BuyB->SetIsEnabled(bHasItem && (bSellRow || (!bOwned && bAfford)));
		}
	}
}

void AHubMerchant::TryBuy(int32 SlotIndex)
{
	PlayButtonSound();
	if (!Displayed.IsValidIndex(SlotIndex)) return;
	ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance());
	if (!GI) return;

	const FShopItem Item = Displayed[SlotIndex];

	// --- In-run CACHE shop: pay Shards for THIS-run goods ---
	if (bInRunShop)
	{
		ALoopedCharacter* Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
		switch (Item.Type)
		{
		case EShopGoodType::Heal:
			if (Player && Player->GetPOCHealthPercent() < 1.0f && GI->SpendShards(Item.Cost))
			{
				Player->HealPlayer(InRunHealAmount);
				HealBuyCount++; // next heal costs more this visit
			}
			break;
		case EShopGoodType::ShardExchange:
			if (GI->SpendShards(Item.Cost))
			{
				GI->AddEchoes(InRunExchangeEchoYield);
			}
			break;
		case EShopGoodType::CleanseCurse:
		{
			TArray<FName> Curses = GI->GetActiveCurses();
			if (Curses.Num() == 0)
			{
				if (Player)
				{
					Player->ShowCenterMessage(FText::FromString(TEXT("Nothing to cleanse.")), 2.0f);
				}
				break;
			}
			if (GI->SpendShards(Item.Cost))
			{
				GI->RemoveCurse(Curses[0]);
				if (Player)
				{
					Player->ShowCenterMessage(FText::FromString(
						FString::Printf(TEXT("Cleansed: %s"), *Curses[0].ToString())), 2.5f);
				}
			}
			break;
		}
		case EShopGoodType::InRunCard:
			if (Player && !GI->IsPerkAtMax(Item.Id) && GI->SpendShards(Item.Cost))
			{
				Player->IncrementPerkLevel(Item.Id); // applies live, this run
			}
			break;
		case EShopGoodType::RunBlessing:
			if (GI->SpendShards(Item.Cost))
			{
				const FName Granted = GI->GrantRandomRunArtifact();
				if (Granted.IsNone())
				{
					GI->AddShards(Item.Cost); // pool exhausted — refund, never eat the player's shards
				}
			}
			break;
		case EShopGoodType::RunMaxHP:
			if (Player && GI->SpendShards(Item.Cost))
			{
				GI->AddRunBonusMaxHP(InRunVigorAmount);
				Player->ApplyMaxHPMod(); // re-derive; also heals the delta so it feels good
			}
			break;
		case EShopGoodType::SellRunRelic:
			// SELL page: the relic goes to Vorr, the Shards come to you (Item.Cost = payout).
			if (GI->RemoveRunArtifact(Item.Id))
			{
				GI->AddShards(Item.Cost);
				if (Player)
				{
					Player->ShowCenterMessage(FText::FromString(
						FString::Printf(TEXT("Vorr turns it over once. \"Done.\" +%d Shards."), Item.Cost)), 2.5f);
				}
			}
			break;
		default: break;
		}
		RefreshShop();
		return;
	}

	// --- Hub SELL page: pays OUT — never touches the owned/spend path below ---
	if (Item.Type == EShopGoodType::SellQueuedCard)
	{
		if (GI->RemovePendingNextRunCard(Item.Id))
		{
			GI->AddEchoes(Item.Cost); // Item.Cost = the refund
		}
		RefreshShop();
		return;
	}
	if (Item.Type == EShopGoodType::SellPermRelic)
	{
		if (GI->SellPermanentArtifact(Item.Id))
		{
			GI->AddEchoes(Item.Cost); // Item.Cost = the payout
			if (ALoopedCharacter* Seller = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
			{
				Seller->ShowCenterMessage(FText::FromString(
					FString::Printf(TEXT("Vorr wraps it in cloth. \"A memory for %d Echoes.\""), Item.Cost)), 2.5f);
			}
		}
		RefreshShop();
		return;
	}

	// --- Hub shop: pay Echoes ---
	if (IsItemOwned(Item))
	{
		RefreshShop();
		return;
	}
	if (!GI->SpendEchoes(Item.Cost))
	{
		UE_LOG(LogLoopedCore, Display, TEXT("[Merchant] Can't afford '%s' (%d Echoes)."), *Item.Id.ToString(), Item.Cost);
		RefreshShop();
		return;
	}

	switch (Item.Type)
	{
	case EShopGoodType::NextRunCard:
		GI->AddPendingNextRunCard(Item.Id);
		break;
	case EShopGoodType::PermanentArtifact:
		GI->GrantArtifact(Item.Id);
		break;
	case EShopGoodType::PermanentUpgrade:
		if (Item.Id == TEXT("CardChoice"))         GI->GrantPermanentCardChoice();
		else if (Item.Id == TEXT("MaxHP"))         GI->GrantPermanentMaxHP(Item.Amount);
		else if (Item.Id == TEXT("StartShards"))   GI->GrantPermanentStartingShards(Item.Amount);
		else if (Item.Id == TEXT("StartBlessing")) GI->GrantPermanentStartingBlessing();
		break;
	case EShopGoodType::CompanionRansom:
	{
		// Serin walks free: permanent relic + the debt stamped on the next run. If her cage is
		// placed nearby (hub collateral display), pop it open live for the beat.
		GI->GrantArtifact(Item.Id);
		GI->QueueNextRunCurse(RansomCurseId);
		for (TActorIterator<ACompanionCage> It(GetWorld()); It; ++It)
		{
			if (It->CompanionArtifact == Item.Id)
			{
				It->OpenCage();
				break;
			}
		}
		UE_LOG(LogLoopedCore, Display, TEXT("[Ransom] Serin freed — '%s' owed next run."), *RansomCurseId.ToString());
		break;
	}
	default: break;
	}
	UE_LOG(LogLoopedCore, Display, TEXT("[Merchant] Bought '%s' for %d Echoes."), *Item.Id.ToString(), Item.Cost);
	RefreshShop();
}

void AHubMerchant::OnBuy0() { TryBuy(0); }
void AHubMerchant::OnBuy1() { TryBuy(1); }
void AHubMerchant::OnBuy2() { TryBuy(2); }
void AHubMerchant::OnBuy3() { TryBuy(3); }
void AHubMerchant::OnBuy4() { TryBuy(4); }
void AHubMerchant::OnBuy5() { TryBuy(5); }
void AHubMerchant::OnBuy6() { TryBuy(6); }
void AHubMerchant::OnBuy7() { TryBuy(7); }
void AHubMerchant::OnBuy8() { TryBuy(8); }
void AHubMerchant::PlayButtonSound()
{
	if (ButtonSound)
	{
		UGameplayStatics::PlaySound2D(this, ButtonSound);
	}
}

void AHubMerchant::OnCloseClicked() { PlayButtonSound(); CloseShop(); }

void AHubMerchant::OnTabWares()
{
	PlayButtonSound();
	CurrentSection = EShopSection::Wares;
	RefreshShop();
}

void AHubMerchant::OnTabVault()
{
	PlayButtonSound();
	// The vault page opens either way — locked, it shows Vorr's refusal in the page title.
	CurrentSection = EShopSection::Vault;
	RefreshShop();
}

void AHubMerchant::OnTabSell()
{
	PlayButtonSound();
	CurrentSection = EShopSection::Sell;
	RefreshShop();
}

void AHubMerchant::OnResetClicked()
{
	if (ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance()))
	{
		GI->WipeSave();
	}
	RefreshShop();
}
