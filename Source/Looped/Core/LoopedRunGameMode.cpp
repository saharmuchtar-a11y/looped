#include "LoopedRunGameMode.h"
#include "Looped.h"
#include "Enemies/EnemyBase.h"
#include "Enemies/BossBase.h"
#include "Core/PortalActor.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Engine/TargetPoint.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "Components/BrushComponent.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Character.h"
#include "Blueprint/UserWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/CanvasPanelSlot.h"
#include "TimerManager.h"
#include "Core/LoopedGameInstance.h"
#include "Core/TreasureChest.h"
#include "Player/LoopedCharacter.h"
#include "Data/RoomRouting.h"
#include "Data/TreasureTypes.h"

ALoopedRunGameMode::ALoopedRunGameMode()
{
	// DefaultPawnClass set via Blueprint or World Settings — not hardcoded
	// This allows BP_LoopedCharacter (with input actions) to be used

	static ConstructorHelpers::FClassFinder<UUserWidget> BossHUDClassFinder(TEXT("/Game/UI/WBP_BossHUD"));
	if (BossHUDClassFinder.Succeeded())
	{
		BossHUDClass = BossHUDClassFinder.Class;
	}

	static ConstructorHelpers::FClassFinder<UUserWidget> RoomHUDClassFinder(TEXT("/Game/UI/WBP_RoomHUD"));
	if (RoomHUDClassFinder.Succeeded())
	{
		RoomHUDClass = RoomHUDClassFinder.Class;
	}
}

void ALoopedRunGameMode::BeginPlay()
{
	Super::BeginPlay();
	bRunActive = true;

	// Fresh save arriving at the Hub → tutorial first (Orin gives the monitor there).
	// Skip once bTutorialCompleted (or Orin owned on legacy saves). Contains() also matches
	// PIE's UEDPIE_ map prefix; the tutorial level itself never redirects.
	if (!TutorialGateArtifact.IsNone() && !TutorialLevelName.IsNone() &&
		GetWorld()->GetMapName().Contains(TEXT("L_Hub")))
	{
		if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
		{
			const bool bDone = GI->HasCompletedTutorial() || GI->HasArtifact(TutorialGateArtifact);
			if (!bDone)
			{
				UE_LOG(LogLoopedRun, Display, TEXT("[Tutorial] Fresh save — routing Hub -> %s."), *TutorialLevelName.ToString());
				UGameplayStatics::OpenLevel(this, TutorialLevelName);
				return;
			}
		}
	}

	// Run progress is read from the GameInstance's generated path (data-driven; no MapName
	// string matching). This GameMode is recreated every level load, so it re-reads the node
	// the player is currently in. Drives the "Room X / N" HUD via GetCurrentRoom/GetTotalRooms.
	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		bool bShowPlayerHUD = false;
		if (!GI->IsInRunRoom())
		{
			// Hub / pre-run — no run room context.
			GI->CurrentRunRoom = 0;
			CurrentRoomIndex = 0;
			TotalRoomsInFloor = GI->CurrentRunPath.Num();
			// Loaded a level DIRECTLY (testing, no active run): still show the HP bar on any non-Hub
			// arena so the player can see/lose health (e.g. testing the boss via direct load).
			bShowPlayerHUD = !GetWorld()->GetMapName().Contains(TEXT("L_Hub"));
		}
		else
		{
			const FRoomNode Node = GI->GetCurrentRoomNode();
			CurrentRoomIndex = Node.RoomIndex;                  // 1-based room number (forks grow this)
			TotalRoomsInFloor = GI->GetFloorRunLength() + 1;    // this floor's length incl. its boss
			GI->CurrentRunRoom = Node.RoomIndex;                // keep the legacy counter in sync
			// HP is ALWAYS visible during a run — merchant/treasure/event rooms included (Sahar:
			// "hp must always be shown"; curses/statuses can drain you anywhere).
			bShowPlayerHUD = true;

			// Curse "Toll": every run-room door demands payment. Deferred a beat so the player
			// pawn exists for the HP-fallback path.
			if (GI->HasCurse(TEXT("Toll")))
			{
				FTimerHandle TollTimer;
				GetWorldTimerManager().SetTimer(TollTimer, FTimerDelegate::CreateWeakLambda(GI, [GI]()
				{
					GI->ApplyRoomEntryToll();
				}), 0.6f, false);
			}

			// Curse "Swarm": the loop sends extra bodies into every combat room.
			if (Node.Type == ERoomType::Combat && GI->HasCurse(TEXT("Swarm")))
			{
				GetWorldTimerManager().SetTimer(SwarmSpawnTimerHandle, this,
					&ALoopedRunGameMode::SpawnSwarmEnemies, 0.6f, false);
			}

			// Fresh treasure room — reset the "N of X" pick budget so pedestals are pickable.
			if (Node.Type == ERoomType::Treasure)
			{
				GI->ResetTreasurePicks();
			}

			// Non-combat run rooms (Treasure / Merchant) have no card draft to fire the exit fork,
			// so reveal it on entry — the player grabs their relic / shops, then picks where to go.
			if (Node.Type == ERoomType::Merchant || Node.Type == ERoomType::Treasure)
			{
				GI->ActivateRoomExitPortals();
			}
		}

		if (bShowPlayerHUD)
		{
			CreateRoomHUD();
		}
	}
	else
	{
		CurrentRoomIndex = 1;
	}

	EnsureNavMeshExists();
	SpawnBossIfBossLevel();

	UE_LOG(LogLoopedRun, Display, TEXT("Run started. Floor 1, Room %d/%d"), CurrentRoomIndex, TotalRoomsInFloor);
}

void ALoopedRunGameMode::CreateRoomHUD()
{
	if (!RoomHUDClass || RoomHUDWidget) return; // nothing to do / already built
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		// Direct level load: the PlayerController can lag the GameMode a frame — retry instead of
		// silently skipping the player HP bar (which made the boss feel "invincible").
		GetWorldTimerManager().SetTimer(PlayerHUDTimerHandle, this, &ALoopedRunGameMode::CreateRoomHUD, 0.15f, false);
		return;
	}

	RoomHUDWidget = CreateWidget<UUserWidget>(PC, RoomHUDClass);
	if (!RoomHUDWidget) return;
	RoomHUDWidget->AddToViewport(50);

	if (UTextBlock* RoomText = Cast<UTextBlock>(RoomHUDWidget->GetWidgetFromName(TEXT("RoomText"))))
	{
		RoomText->SetText(FText::FromString(FString::Printf(TEXT("Room %d / %d"), CurrentRoomIndex, TotalRoomsInFloor)));
	}

	// Player HP bar + numeric readout — anchored bottom-left, numbers layered ON TOP of the bar.
	// Canvas slots set here in C++ (MCP can't reliably write canvas slot fields; same reason the
	// Boss HUD positions itself at runtime). HealthBar z=0 (behind), HealthNumbers z=1 (front).
	if (UWidget* BarW = RoomHUDWidget->GetWidgetFromName(TEXT("HealthBar")))
	{
		if (UCanvasPanelSlot* CPS = Cast<UCanvasPanelSlot>(BarW->Slot))
		{
			CPS->SetAnchors(FAnchors(0.0f, 1.0f, 0.0f, 1.0f));
			CPS->SetAlignment(FVector2D(0.0f, 1.0f));
			CPS->SetAutoSize(false);
			CPS->SetPosition(FVector2D(40.0f, -44.0f));
			CPS->SetSize(FVector2D(380.0f, 44.0f));
			CPS->SetZOrder(0);
		}
	}
	if (UWidget* HealthW = RoomHUDWidget->GetWidgetFromName(TEXT("HealthNumbers")))
	{
		if (UCanvasPanelSlot* CPS = Cast<UCanvasPanelSlot>(HealthW->Slot))
		{
			CPS->SetAnchors(FAnchors(0.0f, 1.0f, 0.0f, 1.0f));
			CPS->SetAlignment(FVector2D(0.0f, 1.0f));
			CPS->SetAutoSize(true);
			CPS->SetPosition(FVector2D(58.0f, -50.0f));
			CPS->SetZOrder(1);
		}
	}

	// Drive the HP text from the player's live health on a light timer (mirrors UpdateBossHUD).
	UpdatePlayerHUD();
	GetWorldTimerManager().SetTimer(PlayerHUDTimerHandle, this, &ALoopedRunGameMode::UpdatePlayerHUD, 0.1f, true);

	UE_LOG(LogLoopedRun, Display, TEXT("Room HUD: Room %d / %d"), CurrentRoomIndex, TotalRoomsInFloor);
}

void ALoopedRunGameMode::UpdatePlayerHUD()
{
	if (!RoomHUDWidget) return;
	ALoopedCharacter* Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
	if (!Player) return;

	if (UTextBlock* Nums = Cast<UTextBlock>(RoomHUDWidget->GetWidgetFromName(TEXT("HealthNumbers"))))
	{
		Nums->SetText(Player->GetHealthText());
	}
	if (UProgressBar* Bar = Cast<UProgressBar>(RoomHUDWidget->GetWidgetFromName(TEXT("HealthBar"))))
	{
		const float Pct = FMath::Clamp(Player->GetPOCHealthPercent(), 0.0f, 1.0f);
		Bar->SetPercent(Pct);
		// Dynamic tint: green > 50%, amber 25–50%, red < 25%.
		const FLinearColor Fill = (Pct > 0.5f)  ? FLinearColor(0.10f, 0.85f, 0.20f, 1.0f)
		                        : (Pct > 0.25f) ? FLinearColor(1.00f, 0.75f, 0.10f, 1.0f)
		                                        : FLinearColor(0.90f, 0.12f, 0.12f, 1.0f);
		Bar->SetFillColorAndOpacity(Fill);
	}
}

void ALoopedRunGameMode::SpawnBossIfBossLevel()
{
	UWorld* World = GetWorld();
	if (!World) return;

	// FAILSAFE 1: never spawn a boss in the Hub, no matter what the run state says. The Hub is
	// never a combat/boss level, so this is an absolute short-circuit against any stale state.
	if (World->GetMapName().Contains(TEXT("L_Hub")))
	{
		UE_LOG(LogLoopedRun, Verbose, TEXT("[Boss] SpawnBossIfBossLevel short-circuited in Hub."));
		return;
	}

	// Spawn the boss when EITHER the data-driven run node is a Boss room (normal full-run path) OR
	// the level itself is the boss arena by name (so loading L_FinalBoss DIRECTLY for testing — with
	// no active run state — still spawns the boss). The Hub failsafe above still blocks Hub spawns.
	const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	const bool bBossByNode = GI && GI->IsInRunRoom() && GI->GetCurrentRoomNode().Type == ERoomType::Boss;
	const bool bBossByName = World->GetMapName().Contains(TEXT("FinalBoss"));
	if (!bBossByNode && !bBossByName)
	{
		return;
	}

	// Default spawn — matches the original boss placement from earlier sessions.
	FVector SpawnLocation(800.0f, 0.0f, 200.0f);
	FRotator SpawnRotation = FRotator::ZeroRotator;
	bool bUsedTargetPoint = false;

	// Prefer an ATargetPoint tagged "BossSpawn" if the level designer placed one.
	for (TActorIterator<ATargetPoint> It(World); It; ++It)
	{
		ATargetPoint* TP = *It;
		if (TP && TP->ActorHasTag(FName(TEXT("BossSpawn"))))
		{
			SpawnLocation = TP->GetActorLocation();
			SpawnRotation = TP->GetActorRotation();
			bUsedTargetPoint = true;
			break;
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ABossBase* Boss = World->SpawnActor<ABossBase>(ABossBase::StaticClass(), SpawnLocation, SpawnRotation, Params);
	if (Boss)
	{
		// Slight scale bump to match the "chunky boss" silhouette from earlier sessions.
		Boss->SetActorScale3D(FVector(1.6f, 1.6f, 1.6f));

		// FLOORS: each floor names its boss (DT_Enemies row — Gatekeeper / Unbound / The Looped).
		// The row overrides stats/color/scale wholesale; rows carry ABSOLUTE per-floor tuning.
		if (ULoopedGameInstance* FloorGI = GetGameInstance<ULoopedGameInstance>())
		{
			const FName BossRow = FloorGI->GetFloorBossRow();
			if (!BossRow.IsNone() && Boss->ApplyEnemyType(BossRow))
			{
				UE_LOG(LogLoopedRun, Display, TEXT("Boss row '%s' applied (floor %d)."),
					*BossRow.ToString(), FloorGI->GetCurrentFloor());
			}
			// Hero-copy mirror (any floor — HeroCopyBossFloor decides): hero mesh + Branch + deck.
			if (BossRow == FloorGI->HeroCopyBossRow || BossRow == FName(TEXT("HeroCopy")))
			{
				Boss->SetupAsHeroCopy();
			}
		}
		BossSpawnedAtSeconds = World->GetTimeSeconds();
		UE_LOG(LogLoopedRun, Display, TEXT("Boss spawned at %s (%s)"),
			*SpawnLocation.ToCompactString(),
			bUsedTargetPoint ? TEXT("from BossSpawn TargetPoint") : TEXT("default location"));

		TrackedBoss = Boss;
		// Bind death FIRST (independent of the HUD) so the hub portal always spawns on boss death,
		// even if the HUD creation has to retry.
		Boss->OnEnemyDied.AddDynamic(this, &ALoopedRunGameMode::HandleBossDied);
		// Create the Souls bar (retries internally if the PlayerController isn't ready yet).
		SetupBossHUD();
	}
	else
	{
		UE_LOG(LogLoopedRun, Warning, TEXT("Boss spawn FAILED at %s"), *SpawnLocation.ToCompactString());
	}
}

void ALoopedRunGameMode::SetupBossHUD()
{
	if (!TrackedBoss || !BossHUDClass || BossHUDWidget)
	{
		return; // no boss, no class, or HUD already built
	}

	// On a direct level load the PlayerController can lag a frame or two behind the GameMode — without
	// a PC there's no viewport to add to, so retry shortly instead of silently skipping the bar.
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		GetWorldTimerManager().SetTimer(BossHUDTimerHandle, this, &ALoopedRunGameMode::SetupBossHUD, 0.15f, false);
		return;
	}

	BossHUDWidget = CreateWidget<UUserWidget>(PC, BossHUDClass);
	if (!BossHUDWidget)
	{
		return;
	}
	BossHUDWidget->AddToViewport(100);

	// Souls-style positioning: anchor bottom-center, ~130px above screen bottom.
	if (UWidget* BarBox = BossHUDWidget->GetWidgetFromName(TEXT("BarBox")))
	{
		if (USizeBox* Box = Cast<USizeBox>(BarBox))
		{
			Box->SetWidthOverride(820.0f);
			Box->SetHeightOverride(70.0f);
		}
		if (UCanvasPanelSlot* CPS = Cast<UCanvasPanelSlot>(BarBox->Slot))
		{
			CPS->SetAnchors(FAnchors(0.5f, 1.0f, 0.5f, 1.0f));
			CPS->SetAlignment(FVector2D(0.5f, 1.0f));
			CPS->SetOffsets(FMargin(0.0f, -130.0f, 820.0f, 70.0f));
			CPS->SetSize(FVector2D(820.0f, 70.0f));
		}
	}

	GetWorldTimerManager().SetTimer(BossHUDTimerHandle, this, &ALoopedRunGameMode::UpdateBossHUD, 0.05f, true);
}

void ALoopedRunGameMode::UpdateBossHUD()
{
	if (!BossHUDWidget || !TrackedBoss || !TrackedBoss->IsAlive())
	{
		return;
	}
	// Data-driven: read the boss's real max HP so the numbers track whatever the boss is set to
	// (per-boss POCHealth in BP), not a hardcoded 300.
	const float MaxHP = FMath::Max(1.0f, TrackedBoss->GetMaxHealth());
	const float Cur = FMath::Max(0.0f, TrackedBoss->GetHealthPercent() * MaxHP);
	if (UProgressBar* Bar = Cast<UProgressBar>(BossHUDWidget->GetWidgetFromName(TEXT("HPBar"))))
	{
		Bar->SetPercent(TrackedBoss->GetHealthPercent());
	}
	if (UTextBlock* Nums = Cast<UTextBlock>(BossHUDWidget->GetWidgetFromName(TEXT("HPNumbers"))))
	{
		const int32 CurInt = FMath::RoundToInt(Cur);
		const int32 MaxInt = FMath::RoundToInt(MaxHP);
		Nums->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), CurInt, MaxInt)));
	}
}

void ALoopedRunGameMode::HandleBossDied(AEnemyBase* /*Boss*/)
{
	GetWorldTimerManager().ClearTimer(BossHUDTimerHandle);
	if (BossHUDWidget)
	{
		BossHUDWidget->RemoveFromParent();
		BossHUDWidget = nullptr;
	}
	TrackedBoss = nullptr;

	// Stats: record boss kill + run completed.
	const float KillSeconds = (BossSpawnedAtSeconds > 0.0f && GetWorld())
		? FMath::Max(0.0f, GetWorld()->GetTimeSeconds() - BossSpawnedAtSeconds)
		: 0.0f;
	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->AddBossKill(KillSeconds);
		GI->AddEchoes(EchoesBossBonus); // every floor boss pays Echoes

		// Mid-floor bosses: "?" blessing/cards mark, then descend. Final boss ends the run —
		// no pedestal (game is over); Echoes are the prize.
		if (!GI->IsFinalFloor())
		{
			SpawnBossRewardPedestal();
			const FName NextRoom = GI->BeginNextFloor();
			SpawnDescentPortal(NextRoom);
			if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
			{
				Player->ShowCenterMessage(FText::FromString(TEXT("A mark appears — claim it, then descend.")), 4.0f);
			}
			UE_LOG(LogLoopedRun, Display, TEXT("Floor boss down in %.1fs — descent portal to '%s' (floor %d)."),
				KillSeconds, *NextRoom.ToString(), GI->GetCurrentFloor());
			return;
		}
		GI->AddRunCompleted(); // The Looped / HeroCopy has fallen — the run is truly complete
	}

	// Final boss: hub portal only. Boss is C++-spawned, so ABossRoomExit couldn't bind at load.
	SpawnHubPortal(FName(TEXT("L_Hub")));
	if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
	{
		Player->ShowCenterMessage(
			FText::FromString(FString::Printf(TEXT("The loop breaks. +%d Echoes — press [E] on the portal home."), EchoesBossBonus)),
			5.0f);
	}
	UE_LOG(LogLoopedRun, Display, TEXT("Final boss died in %.1fs — Echoes only, hub portal spawned."), KillSeconds);
}

void ALoopedRunGameMode::EnsureNavMeshExists()
{
	UWorld* World = GetWorld();
	if (!World) return;

	// If a nav volume already exists in the level, do nothing
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		UE_LOG(LogLoopedRun, Display, TEXT("NavMeshBoundsVolume already in level."));
		return;
	}

	// Find bounds from static meshes in level (skip sky sphere)
	FVector MinV(BIG_NUMBER, BIG_NUMBER, BIG_NUMBER);
	FVector MaxV(-BIG_NUMBER, -BIG_NUMBER, -BIG_NUMBER);
	bool bFound = false;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		AStaticMeshActor* SMA = *It;
		if (!SMA) continue;
		if (SMA->GetStaticMeshComponent() && SMA->GetStaticMeshComponent()->GetStaticMesh())
		{
			if (SMA->GetStaticMeshComponent()->GetStaticMesh()->GetName() == TEXT("SM_SkySphere"))
			{
				continue;
			}
		}
		FVector Origin, BoxExtent;
		SMA->GetActorBounds(false, Origin, BoxExtent);
		MinV.X = FMath::Min(MinV.X, Origin.X - BoxExtent.X);
		MinV.Y = FMath::Min(MinV.Y, Origin.Y - BoxExtent.Y);
		MinV.Z = FMath::Min(MinV.Z, Origin.Z - BoxExtent.Z);
		MaxV.X = FMath::Max(MaxV.X, Origin.X + BoxExtent.X);
		MaxV.Y = FMath::Max(MaxV.Y, Origin.Y + BoxExtent.Y);
		MaxV.Z = FMath::Max(MaxV.Z, Origin.Z + BoxExtent.Z);
		bFound = true;
	}

	FVector Center = bFound ? (MinV + MaxV) * 0.5f : FVector::ZeroVector;
	FVector Extent = bFound ? FVector(
		FMath::Max(500.0f, (MaxV.X - MinV.X) * 0.5f + 800.0f),
		FMath::Max(500.0f, (MaxV.Y - MinV.Y) * 0.5f + 800.0f),
		FMath::Max(500.0f, (MaxV.Z - MinV.Z) * 0.5f + 600.0f))
		: FVector(5000.0f, 5000.0f, 1500.0f);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ANavMeshBoundsVolume* NavVol = World->SpawnActor<ANavMeshBoundsVolume>(ANavMeshBoundsVolume::StaticClass(), Center, FRotator::ZeroRotator, Params);
	if (NavVol)
	{
		// Default brush extent is 200 units cube; scale to cover bounds
		NavVol->SetActorScale3D(FVector(Extent.X / 100.0f, Extent.Y / 100.0f, Extent.Z / 100.0f));
		if (UBrushComponent* Brush = NavVol->GetBrushComponent())
		{
			Brush->MarkRenderStateDirty();
		}
		// Force the nav system to register / rebuild
		if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World))
		{
			NavSys->OnNavigationBoundsUpdated(NavVol);
			NavSys->Build();
		}
		UE_LOG(LogLoopedRun, Display, TEXT("Spawned runtime NavMeshBoundsVolume center=%s extent=%s"), *Center.ToString(), *Extent.ToString());
	}
}

void ALoopedRunGameMode::NotifyAllEnemiesDefeated()
{
	if (!bRunActive) return;

	// Misnomer: this fires once per enemy death. The BP card manager counts them
	// and decides when the room is actually clear. Log accordingly.
	UE_LOG(LogLoopedRun, Display, TEXT("Enemy defeated. (room death-events: %d)"), ++RoomClearCount);
	OnRoomCleared.Broadcast();

	// Per-run Shards drop + the lifetime kill counter (this fires once per enemy death).
	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->AddShards(ShardsPerEnemy);
		GI->AddEnemyKill(); // drives kill-count unlocks (Echo at 150)
	}

	// --- Persistent RoomClears stat (authoritative in C++) ---
	// AddRoomClear() had no caller before, so the save stat was stuck at 0. Count the
	// living non-boss enemies remaining; the just-killed enemy is already at 0 HP
	// (IsAlive()==false) by now, so it's excluded. When none remain, the room is
	// cleared — record it exactly once. The BP card-reward flow is independent of this.
	int32 AliveNonBoss = 0;
	for (TActorIterator<AEnemyBase> It(GetWorld()); It; ++It)
	{
		AEnemyBase* E = *It;
		if (!E || E->IsA(ABossBase::StaticClass())) continue;
		if (E->IsAlive()) ++AliveNonBoss;
	}
	EnemiesAlive = AliveNonBoss;
	if (AliveNonBoss == 0 && !bRoomClearCounted)
	{
		bRoomClearCounted = true;
		if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
		{
			GI->AddRoomClear();
			// Echoes reward: deeper rooms pay more (EchoesPerRoom * room depth, min depth 1).
			GI->AddEchoes(EchoesPerRoom * FMath::Max(1, CurrentRoomIndex));
			// Per-run Shards room-clear bonus.
			GI->AddShards(ShardsRoomClearBonus);

			// Card "SecondSkin": clearing a room knits you back together (heal from the level row).
			if (const FPassiveCardLevel* SkinLv = GI->GetEffectiveLevelData(TEXT("SecondSkin")))
			{
				if (SkinLv->HealAmount > 0.0f)
				{
					if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
					{
						Player->HealPlayer(SkinLv->HealAmount);
						UE_LOG(LogLoopedRun, Display, TEXT("[Card] Second Skin healed %.0f on room clear."), SkinLv->HealAmount);
					}
				}
			}
		}
		UE_LOG(LogLoopedRun, Display, TEXT("Room cleared — RoomClears stat incremented."));
	}
}

bool ALoopedRunGameMode::TryConsumeHauntedToken()
{
	if (bHauntedTokenUsed) return false;
	bHauntedTokenUsed = true;
	return true;
}

void ALoopedRunGameMode::SpawnSwarmEnemies()
{
	// Curse "Swarm": clone a placed enemy N times at nav points away from the player. Clones use
	// the SAME class as an existing enemy (BP subclasses included) so they match the room's kit.
	ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	if (!GI || !GI->HasCurse(TEXT("Swarm"))) return;

	AEnemyBase* Template = nullptr;
	for (TActorIterator<AEnemyBase> It(GetWorld()); It; ++It)
	{
		AEnemyBase* E = *It;
		if (E && E->IsAlive() && !E->IsA(ABossBase::StaticClass()))
		{
			Template = E;
			break;
		}
	}
	if (!Template)
	{
		UE_LOG(LogLoopedRun, Display, TEXT("[Curse] Swarm: no placed enemy to clone — skipped."));
		return;
	}

	APawn* PlayerPawn = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
	const FVector PlayerLoc = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());

	int32 Spawned = 0;
	for (int32 i = 0; i < FMath::Max(1, GI->CurseSwarmExtraEnemies); ++i)
	{
		FVector SpawnLoc = Template->GetActorLocation();
		if (NavSys)
		{
			for (int32 Tries = 0; Tries < 16; ++Tries)
			{
				FNavLocation NavLoc;
				if (!NavSys->GetRandomReachablePointInRadius(Template->GetActorLocation(), 2000.0f, NavLoc)) continue;
				if (PlayerPawn && FVector::Dist2D(NavLoc.Location, PlayerLoc) < 700.0f) continue;
				SpawnLoc = NavLoc.Location + FVector(0, 0, 100.0f);
				break;
			}
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		if (AEnemyBase* Clone = GetWorld()->SpawnActor<AEnemyBase>(Template->GetClass(), SpawnLoc, FRotator::ZeroRotator, Params))
		{
			++Spawned;
		}
	}
	UE_LOG(LogLoopedRun, Display, TEXT("[Curse] Swarm spawned %d extra enem%s."), Spawned, Spawned == 1 ? TEXT("y") : TEXT("ies"));
}

void ALoopedRunGameMode::RespawnAllEnemies()
{
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	APawn* PlayerPawn = nullptr;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PlayerPawn = PC->GetPawn();
	}
	const FVector PlayerLoc = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;

	// Gather enemies first so we don't recurse during iteration
	TArray<AEnemyBase*> Enemies;
	for (TActorIterator<AEnemyBase> It(GetWorld()); It; ++It)
	{
		Enemies.Add(*It);
	}

	// Determine a sensible "arena center" — average of enemy original positions
	FVector Center = FVector::ZeroVector;
	int32 N = 0;
	for (AEnemyBase* E : Enemies)
	{
		if (E)
		{
			Center += E->GetActorLocation();
			++N;
		}
	}
	if (N > 0) Center /= N;

	const float SearchRadius = 2500.0f;
	const float MinDistFromPlayer = 700.0f;
	const float MinSpacing = 350.0f;

	TArray<FVector> ChosenPoints;
	int32 Count = 0;
	for (AEnemyBase* E : Enemies)
	{
		if (!E) continue;

		FVector Picked = E->GetActorLocation();
		bool bFound = false;

		if (NavSys)
		{
			for (int32 Tries = 0; Tries < 24; ++Tries)
			{
				FNavLocation NavLoc;
				if (!NavSys->GetRandomReachablePointInRadius(Center, SearchRadius, NavLoc))
				{
					continue;
				}
				const FVector Cand = NavLoc.Location;
				// Keep away from player
				if (PlayerPawn && FVector::Dist2D(Cand, PlayerLoc) < MinDistFromPlayer)
				{
					continue;
				}
				// Spacing from already-chosen points
				bool bTooClose = false;
				for (const FVector& P : ChosenPoints)
				{
					if (FVector::Dist2D(Cand, P) < MinSpacing) { bTooClose = true; break; }
				}
				if (bTooClose) continue;

				Picked = Cand;
				bFound = true;
				break;
			}
		}

		ChosenPoints.Add(Picked);
		E->RespawnAt(Picked);
		Count++;
	}

	bRunActive = true;
	bRoomClearCounted = false; // re-fought room can count as a clear again
	UE_LOG(LogLoopedRun, Display, TEXT("Respawned %d enemies at randomized points (nav=%s)"), Count, NavSys ? TEXT("yes") : TEXT("no"));
}

void ALoopedRunGameMode::SetCursorVisibility(bool bVisible)
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->bShowMouseCursor = bVisible;
	}
}

void ALoopedRunGameMode::SpawnBossRewardPedestal()
{
	ACharacter* Player = UGameplayStatics::GetPlayerCharacter(this, 0);
	if (!Player || !GetWorld()) return;

	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->ResetTreasurePicks();
	}

	FVector SpawnLoc = Player->GetActorLocation() + Player->GetActorForwardVector() * 220.0f;
	SpawnLoc.Z = Player->GetActorLocation().Z;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ATreasureChest* Pedestal = GetWorld()->SpawnActor<ATreasureChest>(
		ATreasureChest::StaticClass(), SpawnLoc, FRotator::ZeroRotator, Params))
	{
		// Boss "?" is ALWAYS a blessing (Sahar 2026-07-10) — cards come from treasure rooms/shop.
		Pedestal->RewardType = ETreasureRewardType::CleanRelic;
		Pedestal->ApplyQuestionMarkRewardVisual();
		Pedestal->RerollOffer();
		UE_LOG(LogLoopedRun, Display, TEXT("Boss reward pedestal spawned (blessing)."));
	}
}

void ALoopedRunGameMode::SpawnHubPortal(FName Destination)
{
	// The run is ending (boss clear / death / win all route here). Clear the path index NOW so
	// the Hub's GameMode::BeginPlay — which runs before the player's BeginPlay resets state —
	// doesn't read a stale Boss node and spawn a boss in the Hub.
	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->EndRunPath();
	}

	if (ACharacter* Player = UGameplayStatics::GetPlayerCharacter(this, 0))
	{
		FVector SpawnLoc = Player->GetActorLocation() + Player->GetActorForwardVector() * 400.0f;
		SpawnLoc.Z = Player->GetActorLocation().Z;

		FActorSpawnParameters Params;
		APortalActor* Portal = GetWorld()->SpawnActor<APortalActor>(SpawnLoc, FRotator::ZeroRotator, Params);
		if (Portal)
		{
			Portal->TargetLevelName = Destination;
			UE_LOG(LogLoopedRun, Display, TEXT("Portal spawned → %s"), *Destination.ToString());
		}
	}
}

void ALoopedRunGameMode::SpawnDescentPortal(FName Destination)
{
	// Like SpawnHubPortal but the run CONTINUES: no EndRunPath (the path index must survive into
	// the next floor's first room) and the portal advertises the way down.
	if (ACharacter* Player = UGameplayStatics::GetPlayerCharacter(this, 0))
	{
		FVector SpawnLoc = Player->GetActorLocation() + Player->GetActorForwardVector() * 400.0f;
		SpawnLoc.Z = Player->GetActorLocation().Z;

		FActorSpawnParameters Params;
		APortalActor* Portal = GetWorld()->SpawnActor<APortalActor>(SpawnLoc, FRotator::ZeroRotator, Params);
		if (Portal)
		{
			Portal->TargetLevelName = Destination;
			Portal->PortalLabel = FText::FromString(TEXT("DESCEND"));
			// BeginPlay already ran during SpawnActor (before the label was set) — re-enable so
			// the label paints. ActivatePortal is safe to call repeatedly.
			Portal->ActivatePortal();
			UE_LOG(LogLoopedRun, Display, TEXT("Descent portal spawned → %s"), *Destination.ToString());
		}
	}
}

void ALoopedRunGameMode::NotifyPlayerDied()
{
	if (!bRunActive) return;

	UE_LOG(LogLoopedRun, Display, TEXT("Player died in Room %d. Run ended."), CurrentRoomIndex);
	bRunActive = false;
	OnRunEnded.Broadcast(false);
}

void ALoopedRunGameMode::NotifyRunWon()
{
	if (!bRunActive) return;

	// Instant victory hook (e.g. a future secret-sphere auto-win). Records the run as
	// completed, broadcasts victory, and drops the player a portal home to the Hub.
	bRunActive = false;
	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->AddRunCompleted();
	}
	OnRunEnded.Broadcast(true);
	SpawnHubPortal(FName(TEXT("L_Hub")));
	UE_LOG(LogLoopedRun, Display, TEXT("Run WON in Room %d — hub portal spawned."), CurrentRoomIndex);
}
