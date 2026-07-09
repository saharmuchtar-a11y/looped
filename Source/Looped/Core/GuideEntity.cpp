#include "GuideEntity.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Blueprint/UserWidget.h"
#include "Engine/StaticMesh.h"
#include "Engine/DataTable.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Core/LoopedGameInstance.h"
#include "Data/PassiveCardData.h"
#include "Data/EnemyData.h"
#include "Data/ArtifactData.h"
#include "Looped.h"

AGuideEntity::AGuideEntity()
{
	PrimaryActorTick.bCanEverTick = true; // face-player yaw tracking
	EntityName = FText::FromString(TEXT("The First Hunter"));

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(SceneRoot);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Overlap);

	// Placeholder body: the podium model with a warm glow (swap when Sahar models the Hunter).
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(SceneRoot);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Podium(TEXT("/Game/new_assets/podium/StaticMeshes/podium.podium"));
	if (Podium.Succeeded())
	{
		BodyMesh->SetStaticMesh(Podium.Object);
	}

	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
	GlowLight->SetupAttachment(SceneRoot);
	GlowLight->SetRelativeLocation(FVector(0.0f, 0.0f, 160.0f));
	GlowLight->SetIntensityUnits(ELightUnits::Lumens);
	GlowLight->SetIntensity(1400.0f);
	GlowLight->SetAttenuationRadius(700.0f);
	GlowLight->SetLightColor(FLinearColor(1.0f, 0.85f, 0.5f, 1.0f)); // warm — knowledge, not danger
	GlowLight->SetCastShadows(false);

	// Floating name tag (screen-space, always faces the camera) — same pattern as Vorr's.
	NameTagComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("NameTag"));
	NameTagComp->SetupAttachment(SceneRoot);
	NameTagComp->SetRelativeLocation(FVector(0.0f, 0.0f, 235.0f));
	NameTagComp->SetWidgetSpace(EWidgetSpace::Screen);
	NameTagComp->SetDrawSize(FVector2D(280.0f, 70.0f));
	static ConstructorHelpers::FClassFinder<UUserWidget> TagWidget(TEXT("/Game/UI/WBP_HunterTag"));
	if (TagWidget.Succeeded())
	{
		NameTagComp->SetWidgetClass(TagWidget.Class);
	}
}

void AGuideEntity::BeginPlay()
{
	Super::BeginPlay();
	Trigger->SetSphereRadius(TriggerRadius);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AGuideEntity::OnOverlap);

	// The Hunter only shows himself to those who persist (same gate as Vorr's vault).
	if (bRequireVaultUnlock)
	{
		const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
		if (!GI || !GI->IsPermanentVaultUnlocked())
		{
			SetActorHiddenInGame(true);
			SetActorEnableCollision(false);
		}
	}
}

void AGuideEntity::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Watch the player — yaw only, same pattern as Vorr (HubMerchant::Tick).
	if (!bFacePlayer || !BodyMesh) return;
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player) return;
	FVector ToPlayer = Player->GetActorLocation() - BodyMesh->GetComponentLocation();
	ToPlayer.Z = 0.0f;
	if (ToPlayer.IsNearlyZero()) return;
	BodyMesh->SetWorldRotation(FRotator(0.0f, ToPlayer.Rotation().Yaw + FaceYawOffset, 0.0f));
}

void AGuideEntity::OnOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*Sweep*/)
{
	// Walk-in no longer opens the book — reading is a deliberate press-E (Interact).
}

void AGuideEntity::Interact(ALoopedCharacter* Player)
{
	if (bOpen) return;
	OpenGuide();
}

void AGuideEntity::OpenGuide()
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return;

	if (!GuideWidget)
	{
		TSubclassOf<UUserWidget> Cls = LoadClass<UUserWidget>(nullptr, TEXT("/Game/UI/WBP_Guide.WBP_Guide_C"));
		if (!Cls)
		{
			UE_LOG(LogLoopedRun, Warning, TEXT("[Guide] WBP_Guide missing — cannot open."));
			return;
		}
		GuideWidget = CreateWidget<UUserWidget>(PC, Cls);
		if (!GuideWidget) return;

		if (UButton* B = Cast<UButton>(GuideWidget->GetWidgetFromName(TEXT("BtnCards"))))     B->OnClicked.AddDynamic(this, &AGuideEntity::OnPageCards);
		if (UButton* B = Cast<UButton>(GuideWidget->GetWidgetFromName(TEXT("BtnEnemies"))))   B->OnClicked.AddDynamic(this, &AGuideEntity::OnPageEnemies);
		if (UButton* B = Cast<UButton>(GuideWidget->GetWidgetFromName(TEXT("BtnCurses"))))    B->OnClicked.AddDynamic(this, &AGuideEntity::OnPageCurses);
		if (UButton* B = Cast<UButton>(GuideWidget->GetWidgetFromName(TEXT("BtnBlessings")))) B->OnClicked.AddDynamic(this, &AGuideEntity::OnPageBlessings);
		if (UButton* B = Cast<UButton>(GuideWidget->GetWidgetFromName(TEXT("BtnRelics"))))    B->OnClicked.AddDynamic(this, &AGuideEntity::OnPageRelics);
		if (UButton* B = Cast<UButton>(GuideWidget->GetWidgetFromName(TEXT("BtnClose"))))     B->OnClicked.AddDynamic(this, &AGuideEntity::CloseGuide);
		// Dev: full save wipe lives in the logbook now (moved out of the shop).
		if (UButton* B = Cast<UButton>(GuideWidget->GetWidgetFromName(TEXT("BtnReset"))))     B->OnClicked.AddDynamic(this, &AGuideEntity::OnDevWipeSave);

		if (UTextBlock* Author = Cast<UTextBlock>(GuideWidget->GetWidgetFromName(TEXT("AuthorText"))))
		{
			Author->SetText(FText::FromString(TEXT("— the loop remembers —")));
		}
	}

	if (!GuideWidget->IsInViewport())
	{
		GuideWidget->AddToViewport(310);
	}
	PC->bShowMouseCursor = true;
	FInputModeGameAndUI Mode;
	PC->SetInputMode(Mode);
	if (APawn* P = PC->GetPawn()) P->DisableInput(PC);

	bOpen = true;
	ShowPage(0);
}

void AGuideEntity::CloseGuide()
{
	bOpen = false;
	if (GuideWidget && GuideWidget->IsInViewport())
	{
		GuideWidget->RemoveFromParent();
	}
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		PC->bShowMouseCursor = false;
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		if (APawn* P = PC->GetPawn()) P->EnableInput(PC);
	}
}

void AGuideEntity::OnDevWipeSave()
{
	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->WipeSave();
	}
	ShowPage(0); // codex re-renders all-locked — instant visual confirmation of the wipe
}

void AGuideEntity::OnPageCards()     { ShowPage(0); }
void AGuideEntity::OnPageEnemies()   { ShowPage(1); }
void AGuideEntity::OnPageCurses()    { ShowPage(2); }
void AGuideEntity::OnPageBlessings() { ShowPage(3); }
void AGuideEntity::OnPageRelics()    { ShowPage(4); }

void AGuideEntity::ShowPage(int32 PageIndex)
{
	if (!GuideWidget) return;
	static const TCHAR* Titles[] = { TEXT("CARDS — Void Frequencies"), TEXT("ENEMIES"), TEXT("CURSES"),
	                                 TEXT("BLESSINGS — run boons"), TEXT("RELICS — permanent") };
	if (UTextBlock* Title = Cast<UTextBlock>(GuideWidget->GetWidgetFromName(TEXT("TitleText"))))
	{
		Title->SetText(FText::FromString(Titles[FMath::Clamp(PageIndex, 0, 4)]));
	}
	if (UTextBlock* Content = Cast<UTextBlock>(GuideWidget->GetWidgetFromName(TEXT("ContentText"))))
	{
		Content->SetText(FText::FromString(BuildPage(PageIndex)));
	}
}

// The locked-entry line: the codex shows THAT something exists, never WHAT, until you've met it.
static const TCHAR* GLockedLine = TEXT("--------  [ locked ]\n\n");

FString AGuideEntity::BuildPage(int32 PageIndex) const
{
	const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	FString Out;

	switch (PageIndex)
	{
	case 0: // Cards — unlocked cards show; the rest are locked rows
		if (UDataTable* T = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_PassiveCards.DT_PassiveCards")))
		{
			for (const FName& Row : T->GetRowNames())
			{
				const FPassiveCardData* C = T->FindRow<FPassiveCardData>(Row, TEXT("Guide"), false);
				if (!C) continue;
				if (GI && GI->IsCardUnlocked(Row))
				{
					// Name on its own line, description indented under it — long text wraps inside
					// the page instead of running past the paper edge.
					Out += FString::Printf(TEXT("%s  (max Lv %d)\n    %s\n\n"),
						*C->DisplayName.ToString(), C->MaxLevel, *C->Description.ToString());
				}
				else
				{
					Out += GLockedLine;
				}
			}
		}
		break;
	case 1: // Enemies — only archetypes you have actually faced
		if (UDataTable* T = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Enemies.DT_Enemies")))
		{
			for (const FName& Row : T->GetRowNames())
			{
				const FEnemyTypeData* E = T->FindRow<FEnemyTypeData>(Row, TEXT("Guide"), false);
				if (!E) continue;
				if (GI && GI->IsEnemySeen(Row))
				{
					FString Kind = E->bHybridMelee ? TEXT("ranged + melee") : (E->bIsRanged ? TEXT("ranged") : TEXT("melee"));
					if (E->ProjectileElement != NAME_None && E->ProjectileElement != TEXT("None"))
					{
						Kind += FString::Printf(TEXT(", %s"), *E->ProjectileElement.ToString());
					}
					Out += FString::Printf(TEXT("%s\n    %.0f HP, %s\n\n"), *E->DisplayName.ToString(), E->MaxHealth, *Kind);
				}
				else
				{
					Out += GLockedLine;
				}
			}
		}
		break;
	case 2: // Curses — only ones you have suffered
		if (GI)
		{
			static const TCHAR* CurseIds[] = { TEXT("Tithe"), TEXT("Frailty"), TEXT("Bloodless"), TEXT("Leaden"),
				TEXT("Decay"), TEXT("Marked"), TEXT("Dimmed"), TEXT("Brittle"), TEXT("Scarcity"),
				TEXT("Weakness"), TEXT("Volatile"), TEXT("Static"),
				TEXT("Toll"), TEXT("DullBlade"), TEXT("ShatteredSight"), TEXT("Bounty"), TEXT("Cowardice"),
				TEXT("Feverdream"), TEXT("Extortion"), TEXT("Amnesia"), TEXT("Swarm"), TEXT("Haunted"),
				TEXT("Fogbound") };
			for (const TCHAR* Id : CurseIds)
			{
				if (GI->IsCurseSeen(FName(Id)))
				{
					Out += FString::Printf(TEXT("%s\n    %s\n\n"), Id, *GI->GetCurseDescription(FName(Id)).ToString());
				}
				else
				{
					Out += GLockedLine;
				}
			}
		}
		break;
	case 3: // Blessings — names (+ rarity) only; Sahar: too much text on the list
		if (UDataTable* T = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Artifacts.DT_Artifacts")))
		{
			for (const FName& Row : T->GetRowNames())
			{
				const FArtifactData* A = T->FindRow<FArtifactData>(Row, TEXT("Guide"), false);
				if (!A || A->Scope != EArtifactScope::Run) continue;
				const bool bKnown = GI && GI->IsBlessingSeen(Row);
				if (bKnown)
				{
					const TCHAR* Rarity =
						(A->Rarity == ECardRarity::Epic) ? TEXT("Epic") :
						(A->Rarity == ECardRarity::Rare) ? TEXT("Rare") :
						(A->Rarity == ECardRarity::Cursed) ? TEXT("Cursed") : TEXT("Common");
					Out += FString::Printf(TEXT("%s  [%s]%s\n"), *A->DisplayName.ToString(), Rarity,
						A->bIsCursed ? TEXT("  [CURSED]") : TEXT(""));
				}
				else
				{
					Out += GLockedLine;
				}
			}
		}
		break;
	case 4: // Relics — only ones you own (keep short desc)
		if (UDataTable* T = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Artifacts.DT_Artifacts")))
		{
			for (const FName& Row : T->GetRowNames())
			{
				const FArtifactData* A = T->FindRow<FArtifactData>(Row, TEXT("Guide"), false);
				if (!A || A->Scope != EArtifactScope::Permanent) continue;
				const bool bKnown = GI && GI->HasArtifact(Row);
				if (bKnown)
				{
					Out += FString::Printf(TEXT("%s%s\n    %s\n\n"), *A->DisplayName.ToString(),
						A->bIsCursed ? TEXT("  [CURSED]") : TEXT(""), *A->Description.ToString());
				}
				else
				{
					Out += GLockedLine;
				}
			}
		}
		break;
	default:
		break;
	}

	return Out.IsEmpty() ? FString(TEXT("(nothing recorded yet)")) : Out;
}
