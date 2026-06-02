#include "HubMerchant.h"
#include "Looped.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Core/LoopedGameInstance.h"
#include "Player/LoopedCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

AHubMerchant::AHubMerchant()
{
	PrimaryActorTick.bCanEverTick = false;

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

	// NOTE: the shop widget class is loaded at runtime in OpenShop() via LoadClass — NOT here.
	// A constructor FClassFinder caches once at startup; if WBP_Merchant didn't exist yet it
	// would cache null forever. Runtime LoadClass is robust to asset-creation order.
}

void AHubMerchant::BeginPlay()
{
	Super::BeginPlay();

	Trigger->SetSphereRadius(TriggerRadius);

	// WARES — already-unlocked gated cards sold as next-run boons (filtered to unlocked at refresh).
	CardCatalogue.Reset();
	CardCatalogue.Add({ EShopGoodType::NextRunCard, TEXT("MaxHP"),      TEXT("MaxHP — start with +health"),         100 });
	CardCatalogue.Add({ EShopGoodType::NextRunCard, TEXT("Lifesteal"),  TEXT("Lifesteal — start with heal-on-hit"), 120 });
	CardCatalogue.Add({ EShopGoodType::NextRunCard, TEXT("ChainSpark"), TEXT("ChainSpark — start with chain dmg"),  140 });
	CardCatalogue.Add({ EShopGoodType::NextRunCard, TEXT("Speed"),      TEXT("Speed — start faster"),               180 });
	CardCatalogue.Add({ EShopGoodType::NextRunCard, TEXT("Gravity"),    TEXT("Gravity — start lighter"),            180 });

	// VAULT — permanent goods (progression-gated section).
	PermanentCatalogue.Reset();
	PermanentCatalogue.Add({ EShopGoodType::PermanentArtifact, TEXT("GoldBar"),    TEXT("Gold Bar — +10% Echoes forever"), 500, 0 });
	PermanentCatalogue.Add({ EShopGoodType::PermanentUpgrade,  TEXT("CardChoice"), TEXT("Foresight — +1 card choice"),     600, 0 });
	PermanentCatalogue.Add({ EShopGoodType::PermanentUpgrade,  TEXT("MaxHP"),      TEXT("Vitality — +25 max HP forever"),  400, 25 });
}

void AHubMerchant::OnTriggerBegin(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*Sweep*/)
{
	if (bShopOpen) return;
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled()) return;
	if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
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
	bShowingVault = false; // always open on the WARES section
	if (!ShopWidget)
	{
		ShopWidget = CreateWidget<UUserWidget>(PC, ShopWidgetClass);
		if (!ShopWidget) return;

		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("Item0Buy"))))      B->OnClicked.AddDynamic(this, &AHubMerchant::OnBuy0);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("Item1Buy"))))      B->OnClicked.AddDynamic(this, &AHubMerchant::OnBuy1);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("Item2Buy"))))      B->OnClicked.AddDynamic(this, &AHubMerchant::OnBuy2);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("Item3Buy"))))      B->OnClicked.AddDynamic(this, &AHubMerchant::OnBuy3);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("Item4Buy"))))      B->OnClicked.AddDynamic(this, &AHubMerchant::OnBuy4);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("CloseButton"))))   B->OnClicked.AddDynamic(this, &AHubMerchant::OnCloseClicked);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("ResetButton"))))   B->OnClicked.AddDynamic(this, &AHubMerchant::OnResetClicked);
		if (UButton* B = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("SectionButton")))) B->OnClicked.AddDynamic(this, &AHubMerchant::OnToggleSection);
	}

	ShopWidget->AddToViewport(150);
	bShopOpen = true;

	PC->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(InputMode);

	// Fresh cache stock + reset rising-price counters each time the player opens the cache.
	if (bInRunShop)
	{
		RerollCount = 0;
		HealBuyCount = 0;
		RollInRunStock();
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
		if (Item.Id == TEXT("CardChoice")) return GI->HasPermanentCardChoice();
		if (Item.Id == TEXT("MaxHP"))      return GI->HasPermanentMaxHP();
		return false;
	default: return false;
	}
}

void AHubMerchant::RollInRunStock()
{
	InRunStock.Reset();
	ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance());
	if (!GI) return;
	TArray<FName> Pool;
	for (const FShopItem& Card : CardCatalogue)
	{
		if (GI->IsCardUnlocked(Card.Id) && !GI->IsPerkAtMax(Card.Id)) Pool.Add(Card.Id);
	}
	for (int32 i = Pool.Num() - 1; i > 0; --i) { const int32 j = FMath::RandRange(0, i); Pool.Swap(i, j); }
	const int32 N = FMath::Min(InRunStockSize, Pool.Num());
	for (int32 i = 0; i < N; ++i) InRunStock.Add(Pool[i]);
}

void AHubMerchant::RefreshShop()
{
	if (!ShopWidget) return;
	ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance());

	int32 Currency = 0;
	FString BalanceLabel, TitleLabel;
	Displayed.Reset();

	if (bInRunShop)
	{
		// Mid-run CACHE: spend Shards on THIS-run goods. No vault/sections.
		Currency = GI ? GI->GetShards() : 0;
		BalanceLabel = FString::Printf(TEXT("Shards: %d"), Currency);
		TitleLabel = TEXT("VORR'S CACHE");

		FShopItem H; H.Type = EShopGoodType::Heal; H.Id = TEXT("Heal");
		H.Display = FString::Printf(TEXT("Heal  +%.0f HP"), InRunHealAmount);
		H.Cost = InRunHealCost + HealBuyCount * InRunHealCostStep; // rising price
		Displayed.Add(H);

		FShopItem Cl; Cl.Type = EShopGoodType::CleanseCurse; Cl.Id = TEXT("Cleanse");
		Cl.Display = TEXT("Cleanse a curse"); Cl.Cost = InRunCleanseCost;
		Displayed.Add(Cl);

		FShopItem Ex; Ex.Type = EShopGoodType::ShardExchange; Ex.Id = TEXT("Exchange");
		Ex.Display = FString::Printf(TEXT("Exchange -> %d Echoes"), InRunExchangeEchoYield);
		Ex.Cost = InRunExchangeShardCost;
		Displayed.Add(Ex);

		// Random card stock (rerollable via the section button).
		for (const FName& CardId : InRunStock)
		{
			if (Displayed.Num() >= NumSlots) break;
			if (GI && GI->IsCardUnlocked(CardId) && !GI->IsPerkAtMax(CardId))
			{
				FShopItem C; C.Type = EShopGoodType::InRunCard; C.Id = CardId; C.Cost = InRunCardCost;
				C.Display = GI->GetPerkCardLabel(CardId).ToString();
				Displayed.Add(C);
			}
		}

		// Repurpose the section button as REROLL in the cache shop.
		if (UButton* Sec = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("SectionButton"))))
			Sec->SetVisibility(ESlateVisibility::Visible);
		if (UTextBlock* SecT = Cast<UTextBlock>(ShopWidget->GetWidgetFromName(TEXT("SectionButtonText"))))
			SecT->SetText(FText::FromString(FString::Printf(TEXT("Reroll (%d)"), InRunRerollCost * (RerollCount + 1))));
	}
	else
	{
		Currency = GI ? GI->GetEchoes() : 0;
		BalanceLabel = FString::Printf(TEXT("Echoes: %d"), Currency);

		if (bShowingVault && !(GI && GI->IsPermanentVaultUnlocked())) bShowingVault = false;
		if (bShowingVault) Displayed = PermanentCatalogue;
		else for (const FShopItem& Item : CardCatalogue) if (GI && GI->IsCardUnlocked(Item.Id)) Displayed.Add(Item);
		TitleLabel = bShowingVault ? TEXT("VAULT") : TEXT("WARES");

		if (UButton* Sec = Cast<UButton>(ShopWidget->GetWidgetFromName(TEXT("SectionButton"))))
			Sec->SetVisibility(ESlateVisibility::Visible);
		if (UTextBlock* SecT = Cast<UTextBlock>(ShopWidget->GetWidgetFromName(TEXT("SectionButtonText"))))
		{
			const bool bVaultUnlocked = GI && GI->IsPermanentVaultUnlocked();
			SecT->SetText(FText::FromString(
				bShowingVault ? TEXT("Wares") : (bVaultUnlocked ? TEXT("Vault") : TEXT("Vault (locked)"))));
		}
	}

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
		const bool bAfford = bHasItem && Currency >= Cost;

		if (UTextBlock* NameT = Cast<UTextBlock>(ShopWidget->GetWidgetFromName(*FString::Printf(TEXT("Item%dName"), i))))
		{
			NameT->SetText(FText::FromString(NameStr));
		}
		if (UTextBlock* CostT = Cast<UTextBlock>(ShopWidget->GetWidgetFromName(*FString::Printf(TEXT("Item%dCost"), i))))
		{
			CostT->SetText(FText::FromString(bHasItem ? FString::Printf(TEXT("%d"), Cost) : TEXT("")));
		}
		if (UTextBlock* BuyT = Cast<UTextBlock>(ShopWidget->GetWidgetFromName(*FString::Printf(TEXT("Item%dBuyText"), i))))
		{
			const TCHAR* Label = !bHasItem ? TEXT("—")
				: (bOwned ? (Displayed[i].Type == EShopGoodType::NextRunCard ? TEXT("QUEUED") : TEXT("OWNED"))
						  : TEXT("BUY"));
			BuyT->SetText(FText::FromString(Label));
		}
		if (UButton* BuyB = Cast<UButton>(ShopWidget->GetWidgetFromName(*FString::Printf(TEXT("Item%dBuy"), i))))
		{
			BuyB->SetIsEnabled(bHasItem && !bOwned && bAfford);
		}
	}
}

void AHubMerchant::TryBuy(int32 SlotIndex)
{
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
			if (Curses.Num() > 0 && GI->SpendShards(Item.Cost))
			{
				GI->RemoveCurse(Curses[0]);
			}
			break;
		}
		case EShopGoodType::InRunCard:
			if (Player && !GI->IsPerkAtMax(Item.Id) && GI->SpendShards(Item.Cost))
			{
				Player->IncrementPerkLevel(Item.Id); // applies live, this run
			}
			break;
		default: break;
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
		if (Item.Id == TEXT("CardChoice")) GI->GrantPermanentCardChoice();
		else if (Item.Id == TEXT("MaxHP")) GI->GrantPermanentMaxHP(Item.Amount);
		break;
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
void AHubMerchant::OnCloseClicked() { CloseShop(); }

void AHubMerchant::OnToggleSection()
{
	// In the cache shop the section button is the REROLL button instead.
	if (bInRunShop)
	{
		if (ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance()))
		{
			const int32 Cost = InRunRerollCost * (RerollCount + 1);
			if (GI->SpendShards(Cost))
			{
				RerollCount++;
				RollInRunStock();
			}
		}
		RefreshShop();
		return;
	}
	ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance());
	if (!bShowingVault)
	{
		// Trying to enter the vault — gated behind progression.
		if (!(GI && GI->IsPermanentVaultUnlocked()))
		{
#if !UE_BUILD_SHIPPING
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Orange,
					TEXT("Vorr: \"The vault stays shut until you've felled the Looped.\""));
			}
#endif
			return;
		}
		bShowingVault = true;
	}
	else
	{
		bShowingVault = false;
	}
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
