#include "EnemyBase.h"
#include "GAS/LoopedAbilitySystemComponent.h"
#include "Looped.h"
#include "Core/LoopedRunGameMode.h"
#include "Core/LoopedGameInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "Enemies/EnemyProjectile.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "Player/LoopedCharacter.h"
#include "Components/WidgetComponent.h"
#include "Components/ProgressBar.h"
#include "Blueprint/UserWidget.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "AIController.h"

AEnemyBase::AEnemyBase()
{
	PrimaryActorTick.bCanEverTick = true;

	AbilitySystemComponent = CreateDefaultSubobject<ULoopedAbilitySystemComponent>(TEXT("AbilitySystem"));

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(GetRootComponent());
	VisualMesh->SetRelativeScale3D(FVector(0.8f, 0.8f, 1.8f));
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Invisible by design: the Manny skeletal mesh is the visible body; all color/flash tells render via
	// the additive overlay on GetMesh(). This cube survives only as BossBase's melee-hit collision proxy.
	VisualMesh->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
	if (CubeMesh.Succeeded())
	{
		VisualMesh->SetStaticMesh(CubeMesh.Object);
	}

	// Cache the enemy materials so BeginPlay can force-assign one (by bIsRanged). Both expose the
	// BaseColor/EmissiveColor params RefreshColor drives — assigning in C++ means the color system
	// no longer depends on a (loseable) Blueprint material slot.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MeleeMat(TEXT("/Game/Materials/M_EnemyMelee.M_EnemyMelee"));
	if (MeleeMat.Succeeded()) MeleeMaterial = MeleeMat.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RangedMat(TEXT("/Game/Materials/M_EnemyRanged.M_EnemyRanged"));
	if (RangedMat.Succeeded()) RangedMaterial = RangedMat.Object;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> OverlayMat(TEXT("/Game/Materials/M_EnemyOverlay.M_EnemyOverlay"));
	if (OverlayMat.Succeeded()) OverlayBaseMaterial = OverlayMat.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> HurtSnd(TEXT("/Game/Audio/enemy_hurt.enemy_hurt"));
	if (HurtSnd.Succeeded()) EnemyHurtSound = HurtSnd.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> ShotSnd(TEXT("/Game/Audio/ranged_shot.ranged_shot"));
	if (ShotSnd.Succeeded()) RangedShotSound = ShotSnd.Object;

	// Default attack anims (Manny unarmed) — swappable per-enemy in the editor.
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Atk1(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01.MM_Attack_01"));
	if (Atk1.Succeeded()) AttackAnims.Add(Atk1.Object);
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Atk2(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_02.MM_Attack_02"));
	if (Atk2.Succeeded()) AttackAnims.Add(Atk2.Object);
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Atk3(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_03.MM_Attack_03"));
	if (Atk3.Succeeded()) AttackAnims.Add(Atk3.Object);

	// (Rifle mesh removed — ranged enemies read clearly from posture; no held weapon needed.)

	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = AAIController::StaticClass();

	GetCapsuleComponent()->SetCapsuleHalfHeight(96.0f);
	GetCapsuleComponent()->SetCapsuleRadius(42.0f);

	HPBarWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("HPBar"));
	HPBarWidget->SetupAttachment(GetRootComponent());
	HPBarWidget->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	HPBarWidget->SetWidgetSpace(EWidgetSpace::Screen);
	HPBarWidget->SetDrawSize(FVector2D(80.0f, 8.0f));

	static ConstructorHelpers::FClassFinder<UUserWidget> HPWidgetClass(TEXT("/Game/UI/WBP_EnemyHP"));
	if (HPWidgetClass.Succeeded())
	{
		HPBarWidget->SetWidgetClass(HPWidgetClass.Class);
	}
}

bool AEnemyBase::ApplyEnemyType(FName RowName)
{
	if (RowName.IsNone())
	{
		return false;
	}

	// Lazy-load the shared table so levels/BPs don't each need an assignment and the
	// constructor has no hard dependency on the asset existing yet.
	if (!EnemyTypeTable)
	{
		EnemyTypeTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Enemies.DT_Enemies"));
	}
	if (!EnemyTypeTable)
	{
		UE_LOG(LogLoopedAI, Warning, TEXT("%s: EnemyTypeRow '%s' set but DT_Enemies not found — keeping instance values."), *GetName(), *RowName.ToString());
		return false;
	}

	// "Random" = weighted roll over the table (SpawnWeight, MinFloor-gated) — different mix every
	// run. Set placed filler enemies to Random and keep each room's signature enemy a fixed row.
	if (RowName == TEXT("Random"))
	{
		const int32 CurrentFloor = 1; // single floor today; reads the run state once Floors 2-3 exist
		float TotalWeight = 0.0f;
		TArray<TPair<FName, float>> Candidates;
		for (const auto& Pair : EnemyTypeTable->GetRowMap())
		{
			const FEnemyTypeData* Candidate = reinterpret_cast<const FEnemyTypeData*>(Pair.Value);
			if (Candidate && Candidate->SpawnWeight > 0.0f && Candidate->MinFloor <= CurrentFloor)
			{
				TotalWeight += Candidate->SpawnWeight;
				Candidates.Emplace(Pair.Key, Candidate->SpawnWeight);
			}
		}
		if (Candidates.Num() == 0)
		{
			UE_LOG(LogLoopedAI, Warning, TEXT("%s: Random enemy type requested but no eligible DT_Enemies rows."), *GetName());
			return false;
		}
		float Roll = FMath::FRandRange(0.0f, TotalWeight);
		RowName = Candidates.Last().Key;
		for (const auto& Candidate : Candidates)
		{
			Roll -= Candidate.Value;
			if (Roll <= 0.0f)
			{
				RowName = Candidate.Key;
				break;
			}
		}
	}

	const FEnemyTypeData* Row = EnemyTypeTable->FindRow<FEnemyTypeData>(RowName, TEXT("ApplyEnemyType"));
	if (!Row)
	{
		UE_LOG(LogLoopedAI, Warning, TEXT("%s: EnemyTypeRow '%s' not in DT_Enemies — keeping instance values."), *GetName(), *RowName.ToString());
		return false;
	}

	EnemyTypeRow = RowName;

	POCHealth = Row->MaxHealth;
	MoveSpeed = Row->MoveSpeed;
	EnemyColor = Row->EnemyColor;
	bIsRanged = Row->bIsRanged;
	bHybridMelee = Row->bHybridMelee;
	MeleeDamage = Row->MeleeDamage;
	RangedDamage = Row->RangedDamage;
	RangedFireRate = Row->RangedFireRate;
	WindupDuration = Row->WindupDuration;
	ProjectileElement = Row->ProjectileElement;
	SetActorScale3D(FVector(Row->Scale));

	// Runtime application (spawner path): BeginPlay already ran, so redo the bits it derived.
	if (HasActorBegunPlay())
	{
		CurrentHealth = POCHealth;
		MaxHealthCached = POCHealth;
		BaseSpeed = MoveSpeed;
		AIState = bIsRanged ? EEnemyAIState::Kite : EEnemyAIState::Approach;

		// bIsRanged may have flipped — reassign the matching base material + dynamic instance.
		if (VisualMesh)
		{
			if (UMaterialInterface* BaseMat = bIsRanged ? RangedMaterial : MeleeMaterial)
			{
				VisualMesh->SetMaterial(0, BaseMat);
			}
			DynMaterial = VisualMesh->CreateDynamicMaterialInstance(0);
		}
		RefreshSpeed();
		RefreshColor();
	}

	// Discovery: meeting an archetype unlocks its codex entry (first time only; saved to disk).
	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->RecordEnemySeen(RowName);
	}

	UE_LOG(LogLoopedAI, Display, TEXT("%s: applied enemy type '%s' (HP=%.0f %s%s)"), *GetName(), *RowName.ToString(), POCHealth, bIsRanged ? TEXT("RANGED") : TEXT("MELEE"), bHybridMelee ? TEXT("+HYBRID") : TEXT(""));
	return true;
}

void AEnemyBase::BeginPlay()
{
	Super::BeginPlay();

	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	AbilitySystemComponent->InitializeAttributes();

	// Archetype row (if set) overrides the per-instance POC values BEFORE anything is derived
	// from them. Enemies without a row behave exactly as before.
	const bool bTypeApplied = ApplyEnemyType(EnemyTypeRow);

	CurrentHealth = POCHealth;
	MaxHealthCached = POCHealth;

	if (VisualMesh)
	{
		// Force the correct base material (by ranged/melee) so the dynamic instance is GUARANTEED to
		// have the BaseColor/EmissiveColor params — this is the fix for the broken color system.
		if (UMaterialInterface* BaseMat = bIsRanged ? RangedMaterial : MeleeMaterial)
		{
			VisualMesh->SetMaterial(0, BaseMat);
		}

		DynMaterial = VisualMesh->CreateDynamicMaterialInstance(0);
		if (DynMaterial)
		{
			DynMaterial->SetVectorParameterValue(TEXT("Color"), EnemyColor);
			DynMaterial->SetVectorParameterValue(TEXT("EmissiveColor"), EnemyColor);
			DynMaterial->SetVectorParameterValue(TEXT("BaseColor"), EnemyColor);
		}
	}

	// Color feedback now renders via an additive OVERLAY on the visible skeletal mesh (the cube
	// VisualMesh is invisible). Black overlay = pure Manny; tells + hit-flash glow over it.
	if (USkeletalMeshComponent* SkMesh = GetMesh())
	{
		if (OverlayBaseMaterial)
		{
			OverlayMID = UMaterialInstanceDynamic::Create(OverlayBaseMaterial, this);
			SkMesh->SetOverlayMaterial(OverlayMID);
		}
		// Remember the rig-pose transform so Respawn() can restore it after a ragdoll death.
		MeshDefaultRelativeTransform = SkMesh->GetRelativeTransform();
	}

	BaseSpeed = MoveSpeed;
	GetCharacterMovement()->JumpZVelocity = 500.0f;
	GetCharacterMovement()->AirControl = 0.3f;
	GetCharacterMovement()->bUseRVOAvoidance = true;
	GetCharacterMovement()->AvoidanceConsiderationRadius = 200.0f;

	// Bosses start ranged regardless of bIsRanged on the BP instance — Phase 2 unlocks melee/teleport.
	if (bIsBoss)
	{
		bIsRanged = true;
		AIState = EEnemyAIState::Kite;
		// Guarantee bosses can keep up with the player — if BP value is too low, force a floor.
		if (MoveSpeed < 300.0f)
		{
			MoveSpeed = 400.0f;
		}
		BaseSpeed = MoveSpeed;
		// Don't override boss cosmetics — BP scale/color stays.
	}
	else if (bIsRanged)
	{
		// Legacy ranged cosmetics — only when no archetype row drove them (the table owns
		// scale/color/speed for typed enemies; stomping them here would undo the row).
		if (!bTypeApplied)
		{
			SetActorScale3D(FVector(0.7f, 0.7f, 1.4f));
			EnemyColor = FLinearColor(0.2f, 0.3f, 0.8f, 1.0f);
			MoveSpeed = 260.0f;
			BaseSpeed = MoveSpeed;
		}
		AIState = EEnemyAIState::Kite;
	}
	else
	{
		AIState = EEnemyAIState::Approach;
	}

	RefreshSpeed();
	RefreshColor();

	// Guard post = where we spawned. Only used when GuardRadius > 0.
	GuardHome = GetActorLocation();

	// Assign a flank slot deterministically per actor so multiple melee enemies spread out
	const uint32 NameHash = GetTypeHash(GetName());
	FlankSlotIndex = (FlankSlotCount > 0) ? (NameHash % (uint32)FMath::Max(1, FlankSlotCount)) : 0;

	bIsFrenzied = false;
	bIsAlerted = false;

	UE_LOG(LogLoopedAI, Display, TEXT("Enemy spawned: HP=%.0f %s slot=%d"), CurrentHealth, bIsRanged ? TEXT("(RANGED)") : TEXT("(MELEE)"), FlankSlotIndex);
}

void AEnemyBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!IsAlive()) return;

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;
	APawn* Player = PC->GetPawn();
	if (!Player) return;

	// Jump if player is significantly above us — but only if close enough horizontally
	// and only after a cooldown, so we don't bunny-hop endlessly under ledges.
	const FVector PlayerLoc = Player->GetActorLocation();
	const FVector MyLoc = GetActorLocation();
	const float HeightDiff = PlayerLoc.Z - MyLoc.Z;
	const float HorizDist = FVector::Dist2D(PlayerLoc, MyLoc);
	JumpCooldownTimer = FMath::Max(0.0f, JumpCooldownTimer - DeltaTime);
	if (HeightDiff > 80.0f &&
		HorizDist <= JumpMaxHorizontalDist &&
		JumpCooldownTimer <= 0.0f &&
		GetCharacterMovement()->IsMovingOnGround())
	{
		LaunchCharacter(FVector(0.0f, 0.0f, 500.0f), false, true);
		JumpCooldownTimer = JumpCooldown;
	}

	// Stuck detector: if we haven't moved much over StuckWindow, rotate flank slot
	// so the next path attempt picks a different approach angle.
	StuckTimer += DeltaTime;
	if (StuckTimer >= StuckWindow)
	{
		const float Progressed = FVector::Dist2D(MyLoc, LastStuckCheckLocation);
		if (LastStuckCheckLocation != FVector::ZeroVector && Progressed < StuckThreshold && !bIsRanged)
		{
			FlankSlotIndex = (FlankSlotIndex + 1) % FMath::Max(1, FlankSlotCount);
			bHasNavTarget = false; // force fresh path next tick
			PathRefreshTimer = 0.0f;
			UE_LOG(LogLoopedAI, Verbose, TEXT("Enemy stuck — rotating to flank slot %d"), FlankSlotIndex);
		}
		LastStuckCheckLocation = MyLoc;
		StuckTimer = 0.0f;
	}

	// Face player on yaw
	const FVector FlatDir = (PlayerLoc - MyLoc).GetSafeNormal2D();
	if (!FlatDir.IsNearlyZero())
	{
		const FRotator LookRot = FlatDir.Rotation();
		SetActorRotation(FRotator(0.0f, LookRot.Yaw, 0.0f));
	}

	PathRefreshTimer -= DeltaTime;

	// Boss special-attack states preempt normal kite/melee logic
	if (AIState == EEnemyAIState::SpecialWindup ||
		AIState == EEnemyAIState::SpecialBurst ||
		AIState == EEnemyAIState::TeleportWindup)
	{
		TickBossSpecial(DeltaTime, Player);
		return;
	}

	if (bIsRanged)
	{
		// Hybrid (e.g. boss): melee when the player is close, otherwise kite + shoot. Enter melee
		// under MeleeEngageRange; only leave once a swing has finished (AIState back to Approach) AND
		// the player has backed well out — hysteresis so it doesn't flip-flop on the boundary.
		if (bHybridMelee)
		{
			const float DistToPlayer = FVector::Dist2D(Player->GetActorLocation(), GetActorLocation());
			if (!bMeleeEngaged && DistToPlayer < MeleeEngageRange)
			{
				bMeleeEngaged = true;
				EnterState(EEnemyAIState::Approach);
			}
			else if (bMeleeEngaged && AIState == EEnemyAIState::Approach && DistToPlayer > MeleeEngageRange * 1.6f)
			{
				bMeleeEngaged = false;
				EnterState(EEnemyAIState::Kite);
			}
			if (bMeleeEngaged)
			{
				TickMelee(DeltaTime, Player);
				return;
			}
		}
		TickRanged(DeltaTime, Player);
	}
	else
	{
		TickMelee(DeltaTime, Player);
	}
}

void AEnemyBase::TickMelee(float DeltaTime, APawn* Player)
{
	// Guard post: a melee guard only engages targets inside its territory; otherwise it returns to
	// (and idles at) home instead of chasing across the map. Makes platform/chokepoint defenders real.
	if (GuardRadius > 0.0f && Player &&
		FVector::Dist2D(Player->GetActorLocation(), GuardHome) > GuardRadius * 1.25f)
	{
		if (FVector::Dist2D(GetActorLocation(), GuardHome) > 150.0f)
		{
			const FVector Back = ComputeNavTarget(GuardHome);
			AddMovementInput((Back - GetActorLocation()).GetSafeNormal2D(), 1.0f);
		}
		return;
	}

	const FVector PlayerLoc = Player->GetActorLocation();
	const FVector MyLoc = GetActorLocation();
	const float Dist = FVector::Dist2D(PlayerLoc, MyLoc);

	switch (AIState)
	{
	case EEnemyAIState::Approach:
	{
		// Approach via a flank point — multiple enemies spread out instead of bee-lining
		const FVector FlankPoint = ComputeFlankPoint(Player);
		if (PathRefreshTimer <= 0.0f || !bHasNavTarget)
		{
			// Once we're in the final approach band, beeline straight at the PLAYER instead of
			// orbiting a flank point — otherwise a stationary target never gets closed on.
			const float DistToFlank = FVector::Dist2D(FlankPoint, MyLoc);
			const bool bFinalApproach = (Dist < LungeTriggerRange * 1.5f);
			const FVector Destination = (bFinalApproach || DistToFlank < 180.0f) ? PlayerLoc : FlankPoint;
			CurrentNavTarget = ComputeNavTarget(Destination);
			bHasNavTarget = true;
			PathRefreshTimer = PathRefreshInterval;
		}
		const FVector MoveDir = (CurrentNavTarget - MyLoc).GetSafeNormal2D();
		if (!MoveDir.IsNearlyZero())
		{
			AddMovementInput(MoveDir, 1.0f);
		}
		// Enter windup once close — frenzied enemies windup faster
		if (Dist < LungeTriggerRange)
		{
			EnterState(EEnemyAIState::Windup);
			PlayAttackAnim(); // visible melee wind-up swing
			const float WindupMul = bIsFrenzied ? FrenzyWindupMultiplier : 1.0f;
			StateTimer = WindupDuration * WindupMul;
			RefreshColor();
			UE_LOG(LogLoopedAI, Verbose, TEXT("Enemy WINDUP (frenzied=%d, dur=%.2f)"), bIsFrenzied ? 1 : 0, StateTimer);
		}
		break;
	}
	case EEnemyAIState::Windup:
	{
		// Hold still during windup; player gets a tell
		StateTimer -= DeltaTime;
		if (StateTimer <= 0.0f)
		{
			EnterState(EEnemyAIState::Lunge);
			StateTimer = LungeMaxDuration; // drive until contact, capped by this window
			RefreshSpeed();
			RefreshColor();
		}
		break;
	}
	case EEnemyAIState::Lunge:
	{
		// Drive STRAIGHT at the player for the whole lunge window
		const FVector LungeDir = (PlayerLoc - MyLoc).GetSafeNormal2D();
		AddMovementInput(LungeDir, 1.0f);
		StateTimer -= DeltaTime;

		// Apply contact damage if we make contact during the lunge
		bool bHitThisLunge = false;
		if (Dist < MeleeContactRange && bCanMeleeHit)
		{
			if (ALoopedCharacter* LC = Cast<ALoopedCharacter>(Player))
			{
				LC->TakeDamageFromEnemy(MeleeDamage);
				bCanMeleeHit = false;
				bHitThisLunge = true;
				GetWorldTimerManager().SetTimer(MeleeTimerHandle, this, &AEnemyBase::MeleeCooldownReset, MeleeHitCooldown, false);
			}
		}

		// End the lunge the instant we connect, or when the window closes — whichever comes
		// first. Record the outcome so Recover knows whether to back off or press back in.
		if (bHitThisLunge || StateTimer <= 0.0f)
		{
			bLungeConnected = bHitThisLunge;
			EnterState(EEnemyAIState::Recover);
			StateTimer = RecoverDuration;
			RefreshSpeed();
			RefreshColor();
		}
		break;
	}
	case EEnemyAIState::Recover:
	{
		StateTimer -= DeltaTime;
		if (bLungeConnected)
		{
			// Landed the hit — slight backpedal so it feels less zombie-like.
			const FVector AwayDir = (MyLoc - PlayerLoc).GetSafeNormal2D();
			AddMovementInput(AwayDir, 0.3f);
		}
		else
		{
			// Whiffed — press back IN instead of backpedaling, so we don't lose ground
			// against a stationary player and air-punch forever.
			const FVector TowardDir = (PlayerLoc - MyLoc).GetSafeNormal2D();
			AddMovementInput(TowardDir, 0.6f);
		}
		if (StateTimer <= 0.0f)
		{
			EnterState(EEnemyAIState::Approach);
		}
		break;
	}
	default:
		EnterState(EEnemyAIState::Approach);
		break;
	}

	// Failsafe: if somehow already touching player while approaching/recovering, apply contact damage
	if ((AIState == EEnemyAIState::Approach || AIState == EEnemyAIState::Recover) &&
		Dist < MeleeContactRange * 0.75f && bCanMeleeHit)
	{
		if (ALoopedCharacter* LC = Cast<ALoopedCharacter>(Player))
		{
			LC->TakeDamageFromEnemy(MeleeDamage);
			bCanMeleeHit = false;
			GetWorldTimerManager().SetTimer(MeleeTimerHandle, this, &AEnemyBase::MeleeCooldownReset, MeleeHitCooldown, false);
		}
	}
}

void AEnemyBase::TickRanged(float DeltaTime, APawn* Player)
{
	const FVector PlayerLoc = Player->GetActorLocation();
	const FVector MyLoc = GetActorLocation();
	const float Dist = FVector::Dist2D(PlayerLoc, MyLoc);

	// Kite movement: pick a desired destination based on distance band
	FVector DesiredDest = MyLoc;
	const FVector ToPlayer = (PlayerLoc - MyLoc).GetSafeNormal2D();

	if (Dist < KiteMinDist)
	{
		// Back away
		DesiredDest = MyLoc - ToPlayer * KiteBackpedalDistance;
	}
	else if (Dist > KiteMaxDist)
	{
		// Close in (path-aware)
		DesiredDest = PlayerLoc;
	}
	else
	{
		// In band — strafe sideways
		KiteStrafeTimer -= DeltaTime;
		if (KiteStrafeTimer <= 0.0f)
		{
			KiteStrafeSign = FMath::RandBool() ? 1.0f : -1.0f;
			KiteStrafeTimer = KiteStrafeChangeInterval + FMath::FRandRange(-0.3f, 0.5f);
		}
		const FVector SideDir = FVector::CrossProduct(FVector::UpVector, ToPlayer).GetSafeNormal2D();
		DesiredDest = MyLoc + SideDir * KiteStrafeSign * KiteStrafeDistance;
	}

	// Guard post: defend home instead of chasing. Every kite decision is clamped inside the guard
	// zone (a strayed guard walks back). Firing below is untouched — guards still telegraph + shoot
	// anything in range with LOS, which is the whole point of holding the high ground.
	if (GuardRadius > 0.0f)
	{
		if (FVector::Dist2D(MyLoc, GuardHome) > GuardRadius)
		{
			DesiredDest = GuardHome;
		}
		else if (FVector::Dist2D(DesiredDest, GuardHome) > GuardRadius)
		{
			DesiredDest = GuardHome + (DesiredDest - GuardHome).GetSafeNormal2D() * (GuardRadius * 0.8f);
		}
	}

	if (PathRefreshTimer <= 0.0f || !bHasNavTarget)
	{
		CurrentNavTarget = ComputeNavTarget(DesiredDest);
		bHasNavTarget = true;
		PathRefreshTimer = PathRefreshInterval;
	}

	const FVector MoveDir = (CurrentNavTarget - MyLoc).GetSafeNormal2D();
	if (!MoveDir.IsNearlyZero())
	{
		AddMovementInput(MoveDir, 1.0f);
	}

	// Ranged: telegraph then shoot, with line of sight, on a fire-rate cadence
	const bool bInBand = (Dist < RangedRange && Dist > 200.0f);
	const bool bHasLOS = bInBand && HasLineOfSightTo(Player);

	// Phase 2 teleport — boss snaps to player when they run away. Highest priority so it
	// preempts Skyfall when both are ready (mobility punish beats damage punish).
	if (bIsBoss && bIsPhase2 &&
		Dist > TeleportTriggerDistance &&
		GetWorld()->GetTimeSeconds() >= NextTeleportReadyTime)
	{
		BeginTeleport();
		return;
	}

	// Boss: if the special-attack cooldown elapsed and we have LOS, pivot into windup.
	if (bIsBoss && bHasLOS && GetWorld()->GetTimeSeconds() >= NextSpecialReadyTime)
	{
		BeginSpecialAttack();
		return;
	}

	if (bHasLOS)
	{
		if (!GetWorldTimerManager().IsTimerActive(RangedTimerHandle) && !bTelegraphInFlight)
		{
			BeginTelegraph();
			GetWorldTimerManager().SetTimer(RangedTimerHandle, this, &AEnemyBase::BeginTelegraph, RangedFireRate, true);
		}
	}
	else
	{
		if (GetWorldTimerManager().IsTimerActive(RangedTimerHandle))
		{
			GetWorldTimerManager().ClearTimer(RangedTimerHandle);
		}
		if (bTelegraphInFlight)
		{
			GetWorldTimerManager().ClearTimer(TelegraphTimerHandle);
			bTelegraphInFlight = false;
			RefreshColor();
		}
	}
}

FVector AEnemyBase::ComputeNavTarget(const FVector& Destination)
{
	const FVector MyLoc = GetActorLocation();

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSys)
	{
		// No nav system — fall back to direct path so the enemy still does *something*.
		return Destination;
	}

	// Try to project the destination onto the navmesh. If projection succeeds, use it.
	// If not, fall back to the raw destination — the path query below will still try.
	FNavLocation Projected;
	const bool bProjected = NavSys->ProjectPointToNavigation(Destination, Projected, FVector(500.0f, 500.0f, 800.0f));
	const FVector PathDest = bProjected ? Projected.Location : Destination;

	UNavigationPath* Path = NavSys->FindPathToLocationSynchronously(GetWorld(), MyLoc, PathDest, this);

	// Accept any path with >=2 points, including partial paths — a partial path that
	// gets us closer is still better than freezing. Only fall back to direct if we
	// have nothing at all.
	if (Path && Path->IsValid() && Path->PathPoints.Num() >= 2)
	{
		for (int32 i = 1; i < Path->PathPoints.Num(); ++i)
		{
			const FVector& Pt = Path->PathPoints[i];
			if (FVector::Dist2D(Pt, MyLoc) > 50.0f)
			{
				return Pt;
			}
		}
		return Path->PathPoints.Last();
	}

	// No usable path. Walk toward the destination directly — the jump cooldown and
	// stuck detector will handle the case where we grind on a wall.
	return Destination;
}

bool AEnemyBase::HasLineOfSightTo(AActor* Target) const
{
	if (!Target) return false;
	const FVector Start = GetActorLocation() + FVector(0, 0, 60.0f);
	const FVector End = Target->GetActorLocation();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyLOS), false, this);
	Params.AddIgnoredActor(Target);
	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
	return !bBlocked;
}

void AEnemyBase::EnterState(EEnemyAIState NewState)
{
	AIState = NewState;
	StateTimer = 0.0f;
}

void AEnemyBase::SetColorTemporary(const FLinearColor& /*Color*/)
{
	// Legacy entry point — colors are now resolved by RefreshColor() based on stacked state.
	RefreshColor();
}

void AEnemyBase::RestoreBaseColor()
{
	RefreshColor();
}

void AEnemyBase::RefreshColor()
{
	// Drive the GLOW OVERLAY on the visible skeletal mesh. Additive material: BLACK = no glow (pure
	// Manny), a color = that glow over the body. The old invisible cube (DynMaterial) is ignored.
	if (!OverlayMID) return;

	// Default: no glow. Priority of tells: hit-flash > teleport > special > windup > telegraph >
	// burn > frenzy. Hit-flash RED outranks everything so a player hit always reads.
	FLinearColor Glow = FLinearColor::Black;
	if (bHitFlashing)                                   Glow = FLinearColor(5.0f, 0.0f, 0.0f, 1.0f); // bright red hit
	else if (AIState == EEnemyAIState::TeleportWindup)  Glow = TeleportWindupColor;
	else if (AIState == EEnemyAIState::SpecialWindup)   Glow = SpecialWindupColor;
	else if (AIState == EEnemyAIState::Windup)          Glow = WindupColor;
	else if (bTelegraphInFlight)                        Glow = TelegraphColor;
	else if (BurnTicksRemaining > 0)                    Glow = FLinearColor(1.5f, 0.45f, 0.0f, 1.0f);
	else if (bIsFrenzied)                               Glow = FrenzyColor;

	OverlayMID->SetVectorParameterValue(TEXT("OverlayColor"), Glow);
}

void AEnemyBase::FlashHit()
{
	if (!IsAlive()) return;
	bHitFlashing = true;
	RefreshColor();
	GetWorldTimerManager().SetTimer(HitFlashTimerHandle, this, &AEnemyBase::EndHitFlash, 0.07f, false);
}

void AEnemyBase::EndHitFlash()
{
	bHitFlashing = false;
	RefreshColor();
}

void AEnemyBase::PlayAttackAnim()
{
	if (AttackAnims.Num() == 0 || !GetMesh()) return;
	UAnimInstance* AnimInst = GetMesh()->GetAnimInstance();
	if (!AnimInst) return;
	const int32 Pick = FMath::RandRange(0, AttackAnims.Num() - 1);
	if (UAnimSequence* Seq = AttackAnims[Pick])
	{
		// Plays over locomotion via the AnimBP's DefaultSlot — the visible "winding up to attack" tell.
		AnimInst->PlaySlotAnimationAsDynamicMontage(Seq, FName(TEXT("DefaultSlot")), 0.08f, 0.15f, 1.0f);
	}
}

void AEnemyBase::RefreshSpeed()
{
	float S = BaseSpeed;
	if (bIsFrenzied)                              S *= FrenzySpeedMultiplier;
	else if (bIsAlerted)                          S *= AlertedSpeedMultiplier;
	if (VenomTicksRemaining > 0)                  S *= VenomSlowMultiplier;
	if (AIState == EEnemyAIState::Lunge)          S *= LungeSpeedMultiplier;

	// Curse "Marked": the player's run is cursed — enemies hunt faster.
	if (const UWorld* World = GetWorld())
	{
		if (const ULoopedGameInstance* GI = World->GetGameInstance<ULoopedGameInstance>())
		{
			if (GI->HasCurse(TEXT("Marked"))) S *= GI->CurseMarkedEnemySpeedMult;
		}
	}

	GetCharacterMovement()->MaxWalkSpeed = S;
	MoveSpeed = S;
}

UAbilitySystemComponent* AEnemyBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AEnemyBase::TakeDamageFromPlayer(float Damage, AActor* DamageSource)
{
	if (!IsAlive()) return;

	// Run-relic outgoing damage multiplier (Ember Core 1.40). Single chokepoint for ALL
	// player-dealt damage (base hit + ChainSpark).
	if (const ULoopedGameInstance* PlayerGI = GetGameInstance<ULoopedGameInstance>())
	{
		Damage *= PlayerGI->GetArtifactDamageMult();
	}

	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Max(0.0f, CurrentHealth - Damage);

	// Visible + audible "I connected" feedback — red flash + hurt sound on every player hit.
	FlashHit();
	if (EnemyHurtSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, EnemyHurtSound, GetActorLocation());
	}

	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->AddDamageDealt(OldHealth - CurrentHealth);
	}

	if (HPBarWidget)
	{
		if (UUserWidget* Widget = HPBarWidget->GetWidget())
		{
			if (UProgressBar* Bar = Cast<UProgressBar>(Widget->GetWidgetFromName(TEXT("HPBar"))))
			{
				Bar->SetPercent(GetHealthPercent());
			}
		}
	}

	UE_LOG(LogLoopedCombat, Display, TEXT("Enemy took %.1f damage (HP: %.0f/%.0f)"),
		Damage, CurrentHealth, MaxHealthCached);

	// Pack alert: any enemy within radius becomes alerted
	AlertNearbyEnemies(DamageSource);

	// Boss-only phase transition at 70% HP
	if (bIsBoss)
	{
		CheckEnterPhase2();
	}

	// Low-HP frenzy
	CheckEnterFrenzy();

	if (CurrentHealth <= 0.0f)
	{
		Die();
	}
}

float AEnemyBase::GetHealthPercent() const
{
	return (MaxHealthCached > 0.0f) ? CurrentHealth / MaxHealthCached : 0.0f;
}

bool AEnemyBase::IsAlive() const
{
	return CurrentHealth > 0.0f;
}

void AEnemyBase::Die()
{
	if (bIsDying) return; // guard: DeathPop / chain damage can re-enter on an already-dying enemy
	bIsDying = true;

	UE_LOG(LogLoopedAI, Display, TEXT("Enemy KILLED!"));

	// Death pop: AoE damage to nearby allies before we go down
	DeathPop();

	// Fire gameplay events IMMEDIATELY so room-clear timing is driven by the kill, not the corpse anim.
	OnEnemyDied.Broadcast(this);
	BP_OnDied();
	if (ALoopedRunGameMode* GM = Cast<ALoopedRunGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		GM->NotifyAllEnemiesDefeated();
	}

	// Stop all combat/AI activity.
	GetWorldTimerManager().ClearTimer(RangedTimerHandle);
	GetWorldTimerManager().ClearTimer(MeleeTimerHandle);
	GetWorldTimerManager().ClearTimer(TelegraphTimerHandle);
	GetWorldTimerManager().ClearTimer(AlertTimerHandle);
	GetWorldTimerManager().ClearTimer(SpecialTimerHandle);
	GetWorldTimerManager().ClearTimer(SpecialBurstTimerHandle);
	GetWorldTimerManager().ClearTimer(TeleportTimerHandle);
	GetWorldTimerManager().ClearTimer(MeleeModeTimerHandle);
	bTelegraphInFlight = false;
	SpecialShotsRemaining = 0;
	GetCharacterMovement()->DisableMovement();
	if (HPBarWidget) HPBarWidget->SetVisibility(false);

	// Stop EVERYTHING from blocking the player once dead, EXCEPT the ragdolling mesh: the capsule, the
	// boss melee-proxy cube, AND any Blueprint-added collision (e.g. BP_TestEnemy's cube that otherwise
	// keeps "standing" where the enemy died and walls the player off). The corpse mesh keeps physics
	// but ignores Pawn (set in the ragdoll branch below) so the player walks through it.
	{
		TArray<UPrimitiveComponent*> Prims;
		GetComponents<UPrimitiveComponent>(Prims);
		for (UPrimitiveComponent* Prim : Prims)
		{
			if (Prim && Prim != GetMesh())
			{
				Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
	}

	// Play a death anim if one is assigned; otherwise ragdoll the body (asset-free fallback).
	float HideDelay = DeathHideFallbackDelay;
	const float AnimLen = PlayDeathAnim();
	if (AnimLen > 0.0f)
	{
		HideDelay = AnimLen;
	}
	else if (USkeletalMeshComponent* SkMesh = GetMesh())
	{
		SkMesh->SetCollisionProfileName(TEXT("Ragdoll"));
		SkMesh->SetSimulatePhysics(true);
		SkMesh->SetAllBodiesSimulatePhysics(true);
		SkMesh->WakeAllRigidBodies();
		// Corpse is visual only — it falls on the floor (WorldStatic) but the player walks straight
		// through it. Ignoring Pawn stops the ragdoll from being a physical wall after a kill.
		SkMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}

	GetWorldTimerManager().SetTimer(DeathHideTimerHandle, this, &AEnemyBase::FinishDeathHide, FMath::Max(0.1f, HideDelay), false);
}

void AEnemyBase::FinishDeathHide()
{
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}

float AEnemyBase::PlayDeathAnim()
{
	if (DeathAnims.Num() == 0 || !GetMesh()) return 0.0f;
	UAnimInstance* AnimInst = GetMesh()->GetAnimInstance();
	if (!AnimInst) return 0.0f;
	const int32 Pick = FMath::RandRange(0, DeathAnims.Num() - 1);
	if (UAnimSequence* Seq = DeathAnims[Pick])
	{
		// BlendOut 0 so the last (dead) pose holds until the corpse is hidden.
		AnimInst->PlaySlotAnimationAsDynamicMontage(Seq, FName(TEXT("DefaultSlot")), 0.1f, 0.0f, 1.0f);
		return Seq->GetPlayLength();
	}
	return 0.0f;
}

void AEnemyBase::Respawn()
{
	GetWorldTimerManager().ClearTimer(BurnTimerHandle);
	GetWorldTimerManager().ClearTimer(VenomTimerHandle);
	GetWorldTimerManager().ClearTimer(TelegraphTimerHandle);
	GetWorldTimerManager().ClearTimer(AlertTimerHandle);
	GetWorldTimerManager().ClearTimer(SpecialTimerHandle);
	GetWorldTimerManager().ClearTimer(SpecialBurstTimerHandle);
	GetWorldTimerManager().ClearTimer(TeleportTimerHandle);
	GetWorldTimerManager().ClearTimer(MeleeModeTimerHandle);
	BurnTicksRemaining = 0;
	VenomTicksRemaining = 0;
	SpecialShotsRemaining = 0;
	NextSpecialReadyTime = 0.0f;
	NextTeleportReadyTime = 0.0f;
	bIsPhase2 = false;
	CurrentHealth = POCHealth;
	MaxHealthCached = POCHealth;
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	if (HPBarWidget) HPBarWidget->SetVisibility(true);
	GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_Walking);

	// Clear the dying flag + pending corpse-hide, and un-ragdoll the mesh back to its rig pose.
	bIsDying = false;
	GetWorldTimerManager().ClearTimer(DeathHideTimerHandle);
	if (USkeletalMeshComponent* SkMesh = GetMesh())
	{
		if (SkMesh->IsSimulatingPhysics())
		{
			SkMesh->SetSimulatePhysics(false);
			SkMesh->SetCollisionProfileName(TEXT("CharacterMesh"));
			SkMesh->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			SkMesh->SetRelativeTransform(MeshDefaultRelativeTransform);
		}
	}
	if (GetCapsuleComponent())
	{
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	// Reset AI state
	AIState = bIsRanged ? EEnemyAIState::Kite : EEnemyAIState::Approach;
	StateTimer = 0.0f;
	PathRefreshTimer = 0.0f;
	bHasNavTarget = false;
	bCanMeleeHit = true;
	bLungeConnected = false;
	bMeleeEngaged = false;
	bIsFrenzied = false;
	bIsAlerted = false;
	bTelegraphInFlight = false;

	// Re-randomize flank slot so respawned waves spread differently
	FlankSlotIndex = FMath::RandRange(0, FMath::Max(1, FlankSlotCount) - 1);

	if (HPBarWidget)
	{
		if (UUserWidget* Widget = HPBarWidget->GetWidget())
		{
			if (UProgressBar* Bar = Cast<UProgressBar>(Widget->GetWidgetFromName(TEXT("HPBar"))))
			{
				Bar->SetPercent(1.0f);
			}
		}
	}

	RefreshSpeed();
	RefreshColor();

	UE_LOG(LogLoopedAI, Display, TEXT("Enemy Respawned: HP=%.0f"), CurrentHealth);
}

void AEnemyBase::RespawnAt(FVector Location)
{
	// Move first, then run the normal respawn reset
	SetActorLocation(Location + FVector(0, 0, 50.0f), false, nullptr, ETeleportType::TeleportPhysics);
	Respawn();
}

void AEnemyBase::ApplyBurnEffect(float DamagePerTick, int32 NumTicks)
{
	if (!IsAlive()) return;

	GetWorldTimerManager().ClearTimer(BurnTimerHandle);
	BurnDamagePerTick = DamagePerTick;
	BurnTicksRemaining = NumTicks;
	RefreshColor();

	GetWorldTimerManager().SetTimer(BurnTimerHandle, this, &AEnemyBase::BurnTick, 1.0f, true);
	UE_LOG(LogLoopedAI, Display, TEXT("Burn applied: %.0f x %d ticks"), DamagePerTick, NumTicks);
}

void AEnemyBase::BurnTick()
{
	if (!IsAlive() || BurnTicksRemaining <= 0)
	{
		GetWorldTimerManager().ClearTimer(BurnTimerHandle);
		RefreshColor();
		return;
	}

	BurnTicksRemaining--;
	TakeDamageFromPlayer(BurnDamagePerTick, this);
	UE_LOG(LogLoopedAI, Display, TEXT("Burn tick: %.0f dmg, %d remaining"), BurnDamagePerTick, BurnTicksRemaining);

	if (BurnTicksRemaining <= 0)
	{
		GetWorldTimerManager().ClearTimer(BurnTimerHandle);
	}
	// Re-assert color in case some other transition stole it between ticks
	RefreshColor();
}

void AEnemyBase::ApplyVenomEffect(float DamagePerTick, int32 NumTicks, float SlowMultiplier)
{
	if (!IsAlive()) return;

	GetWorldTimerManager().ClearTimer(VenomTimerHandle);
	VenomDamagePerTick = DamagePerTick;
	VenomTicksRemaining = NumTicks;
	VenomSlowMultiplier = SlowMultiplier;
	RefreshSpeed();

	GetWorldTimerManager().SetTimer(VenomTimerHandle, this, &AEnemyBase::VenomTick, 1.0f, true);
	UE_LOG(LogLoopedAI, Display, TEXT("Venom applied: %.0f x %d ticks, %.0f%% slow"), DamagePerTick, NumTicks, (1.0f - SlowMultiplier) * 100.0f);
}

void AEnemyBase::VenomTick()
{
	if (!IsAlive() || VenomTicksRemaining <= 0)
	{
		GetWorldTimerManager().ClearTimer(VenomTimerHandle);
		RefreshSpeed();
		return;
	}

	VenomTicksRemaining--;
	TakeDamageFromPlayer(VenomDamagePerTick, this);

	if (VenomTicksRemaining <= 0)
	{
		GetWorldTimerManager().ClearTimer(VenomTimerHandle);
	}
	RefreshSpeed();
}

void AEnemyBase::MeleeCooldownReset()
{
	bCanMeleeHit = true;
}

void AEnemyBase::RangedAttack()
{
	if (!IsAlive()) return;

	APawn* Player = GetWorld()->GetFirstPlayerController()->GetPawn();
	if (!Player) return;

	// Require line of sight to actually land the shot
	if (!HasLineOfSightTo(Player)) return;

	SpawnProjectileAtPlayer(RangedDamage);
}

void AEnemyBase::SpawnProjectileAtPlayer(float Damage)
{
	APawn* Player = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
	if (!Player) return;

	if (RangedShotSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, RangedShotSound, GetActorLocation());
	}

	// Fire a VISIBLE, DODGEABLE projectile aimed at the player. It blocks on walls (cover matters)
	// and only damages the player. Shared by normal ranged fire and the boss Skyfall burst.
	const FVector Muzzle = GetActorLocation() + FVector(0.0f, 0.0f, 40.0f) + GetActorForwardVector() * 60.0f;
	const FRotator AimRot = (Player->GetActorLocation() - Muzzle).Rotation();
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AEnemyProjectile* Proj = GetWorld()->SpawnActor<AEnemyProjectile>(AEnemyProjectile::StaticClass(), Muzzle, AimRot, SpawnParams))
	{
		Proj->Init(Damage, 1800.0f, ProjectileElement);
		UE_LOG(LogLoopedAI, Display, TEXT("Enemy fired %s projectile (%.0f dmg)"), *ProjectileElement.ToString(), Damage);
	}
}

void AEnemyBase::ApplyChainSparkEffect(float ChainDamage, float ChainRadius)
{
	if (!IsAlive()) return;

	FVector MyLoc = GetActorLocation();
	for (TActorIterator<AEnemyBase> It(GetWorld()); It; ++It)
	{
		AEnemyBase* Other = *It;
		if (Other == this || !Other->IsAlive()) continue;

		float Dist = FVector::Dist(MyLoc, Other->GetActorLocation());
		if (Dist <= ChainRadius)
		{
			Other->TakeDamageFromPlayer(ChainDamage, this);
			UE_LOG(LogLoopedAI, Display, TEXT("Chain spark hit nearby enemy for %.0f dmg (dist: %.0f)"), ChainDamage, Dist);
		}
	}
}

void AEnemyBase::ApplyLifestealEffect(float HealAmount)
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (ALoopedCharacter* LC = Cast<ALoopedCharacter>(PC->GetPawn()))
		{
			LC->HealPlayer(HealAmount);
			UE_LOG(LogLoopedAI, Display, TEXT("Lifesteal healed player for %.0f"), HealAmount);
		}
	}
}

// --- Creative behaviors ---

FVector AEnemyBase::ComputeFlankPoint(const APawn* Player) const
{
	if (!Player) return GetActorLocation();
	const FVector PLoc = Player->GetActorLocation();
	const FVector Forward = Player->GetActorForwardVector();
	const float SlotAngleDeg = 360.0f / FMath::Max(1, FlankSlotCount);
	const float MyAngleDeg = SlotAngleDeg * FlankSlotIndex;
	const FRotator Rot(0.0f, MyAngleDeg, 0.0f);
	const FVector Offset = Rot.RotateVector(Forward) * FlankRadius;
	return PLoc + Offset;
}

void AEnemyBase::CheckEnterFrenzy()
{
	if (bIsFrenzied) return;
	if (GetHealthPercent() <= FrenzyHPThreshold)
	{
		bIsFrenzied = true;
		RefreshSpeed();
		RefreshColor();
		UE_LOG(LogLoopedAI, Display, TEXT("Enemy entered FRENZY (HP%%=%.2f)"), GetHealthPercent());
	}
}

void AEnemyBase::CheckEnterPhase2()
{
	if (bIsPhase2 || !bIsBoss) return;
	if (GetHealthPercent() > Phase2HPThreshold) return;

	bIsPhase2 = true;

	// Visible transformation: base color shifts to deep red so the player SEES the boss change.
	EnemyColor = Phase2BaseColor;

	// Tighten cooldowns and shot count immediately. Next Skyfall will use the new values.
	SpecialCooldown = Phase2SpecialCooldown;
	SpecialBurstShots = Phase2SpecialBurstShots;
	RangedFireRate = RangedFireRate * Phase2FireRateMultiplier;
	RangedDamage = RangedDamage * Phase2DamageMultiplier;

	// Phase 2 boss does NOT back off — stays close, makes melee feel real.
	KiteMinDist = 200.0f;
	KiteBackpedalDistance = 80.0f;
	KiteStrafeDistance = 220.0f;

	// Reset Skyfall and teleport cooldowns to "ready soon" so phase 2 feels immediately scary.
	NextSpecialReadyTime = GetWorld()->GetTimeSeconds() + 1.0f;
	NextTeleportReadyTime = GetWorld()->GetTimeSeconds() + 2.0f;

	// Restart the ranged fire timer so the new fire rate takes effect immediately.
	if (GetWorldTimerManager().IsTimerActive(RangedTimerHandle))
	{
		GetWorldTimerManager().ClearTimer(RangedTimerHandle);
	}

	RefreshColor();
	RefreshSpeed();

	UE_LOG(LogLoopedAI, Display, TEXT("Boss entered PHASE 2 (HP%%=%.2f) — teleport unlocked, damage x%.1f, cooldowns tightened"),
		GetHealthPercent(), Phase2DamageMultiplier);
	// [BOSS] PHASE 2 on-screen message removed for a clean screen (UE_LOG above kept for dev).
}

void AEnemyBase::BeginTeleport()
{
	if (!IsAlive()) return;

	// Cancel anything in flight so the teleport tell isn't fighting another animation
	GetWorldTimerManager().ClearTimer(RangedTimerHandle);
	GetWorldTimerManager().ClearTimer(TelegraphTimerHandle);
	bTelegraphInFlight = false;

	AIState = EEnemyAIState::TeleportWindup;
	// Tell is the pure-red TeleportWindup glow (distinct from the magenta Skyfall charge).
	RefreshColor();
	UE_LOG(LogLoopedAI, Display, TEXT("Boss: TELEPORT windup (%.2fs)"), TeleportWindupDuration);

	GetWorldTimerManager().SetTimer(TeleportTimerHandle, this, &AEnemyBase::ExecuteTeleport, TeleportWindupDuration, false);
}

void AEnemyBase::ExecuteTeleport()
{
	if (!IsAlive())
	{
		AIState = EEnemyAIState::Kite;
		RefreshColor();
		return;
	}

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	APawn* Player = PC ? PC->GetPawn() : nullptr;
	if (!Player)
	{
		AIState = EEnemyAIState::Kite;
		RefreshColor();
		return;
	}

	// Snap to a point TeleportArrivalDistance behind/around the player on the navmesh.
	const FVector PlayerLoc = Player->GetActorLocation();
	const FVector PlayerForward = Player->GetActorForwardVector();
	// Drop in front of player slightly so the boss is staring them down.
	FVector Desired = PlayerLoc + PlayerForward * TeleportArrivalDistance;

	// Try to project onto navmesh so we don't land inside geometry.
	if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld()))
	{
		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(Desired, Projected, FVector(400.0f, 400.0f, 600.0f)))
		{
			Desired = Projected.Location + FVector(0, 0, 60.0f);
		}
	}

	SetActorLocation(Desired, false, nullptr, ETeleportType::TeleportPhysics);

	// Contact damage on arrival — small positional bump, not an insta-kill chunk.
	const float ArrivalDist = FVector::Dist2D(PlayerLoc, Desired);
	if (ArrivalDist < TeleportArrivalDistance + 100.0f)
	{
		if (ALoopedCharacter* LC = Cast<ALoopedCharacter>(Player))
		{
			LC->TakeDamageFromEnemy(TeleportContactDamage);
			UE_LOG(LogLoopedAI, Display, TEXT("Boss TELEPORT contact hit player for %.0f"), TeleportContactDamage);
		}
	}

	// Switch to melee mode for PostTeleportMeleeDuration seconds — boss now uses
	// the Approach/Windup/Lunge/Recover state machine, giving the player a readable fight.
	bIsRanged = false;
	AIState = EEnemyAIState::Approach;
	bHasNavTarget = false;
	GetWorldTimerManager().ClearTimer(RangedTimerHandle);
	GetWorldTimerManager().SetTimer(MeleeModeTimerHandle, this, &AEnemyBase::EndPostTeleportMeleeMode, PostTeleportMeleeDuration, false);
	NextTeleportReadyTime = GetWorld()->GetTimeSeconds() + TeleportCooldown;
	RefreshColor();
}

void AEnemyBase::EndPostTeleportMeleeMode()
{
	if (!IsAlive()) return;
	bIsRanged = true;
	AIState = EEnemyAIState::Kite;
	bHasNavTarget = false;
	RefreshColor();
	UE_LOG(LogLoopedAI, Display, TEXT("Boss melee window ended — back to ranged."));
}

void AEnemyBase::AlertNearbyEnemies(AActor* Cause)
{
	const FVector MyLoc = GetActorLocation();
	for (TActorIterator<AEnemyBase> It(GetWorld()); It; ++It)
	{
		AEnemyBase* Other = *It;
		if (!Other || Other == this || !Other->IsAlive()) continue;
		const float D = FVector::Dist(Other->GetActorLocation(), MyLoc);
		if (D <= AlertRadius && !Other->bIsAlerted)
		{
			Other->bIsAlerted = true;
			Other->RefreshSpeed();
			Other->GetWorldTimerManager().SetTimer(Other->AlertTimerHandle, Other, &AEnemyBase::EndAlerted, Other->AlertDuration, false);
		}
	}
}

void AEnemyBase::EndAlerted()
{
	bIsAlerted = false;
	RefreshSpeed();
}

void AEnemyBase::BeginTelegraph()
{
	if (!IsAlive() || bTelegraphInFlight) return;
	bTelegraphInFlight = true;
	// NOTE: no melee swing here — ranged enemies get a proper fire/cast gesture in Fix #2 (with the
	// projectile). For now the telegraph reads via the windup color tell.
	RefreshColor();
	UE_LOG(LogLoopedAI, Verbose, TEXT("Ranged TELEGRAPH begin (dur=%.2f)"), TelegraphDuration);
	GetWorldTimerManager().SetTimer(TelegraphTimerHandle, this, &AEnemyBase::FireTelegraphedShot, TelegraphDuration, false);
}

void AEnemyBase::FireTelegraphedShot()
{
	bTelegraphInFlight = false;
	if (!IsAlive())
	{
		RefreshColor();
		return;
	}
	RangedAttack();
	RefreshColor();
}

void AEnemyBase::BeginSpecialAttack()
{
	if (!IsAlive()) return;

	// Stop normal-ranged firing while in special
	GetWorldTimerManager().ClearTimer(RangedTimerHandle);
	GetWorldTimerManager().ClearTimer(TelegraphTimerHandle);
	bTelegraphInFlight = false;

	AIState = EEnemyAIState::SpecialWindup;
	// Tell is the bright-magenta SpecialWindup glow — boss is CHARGING Skyfall.
	RefreshColor();
	UE_LOG(LogLoopedAI, Display, TEXT("Boss: SpecialWindup (will fire %d shots in %.2fs)"), SpecialBurstShots, SpecialWindupDuration);

	// After windup, enter the burst phase and fire shots on an interval timer.
	GetWorldTimerManager().SetTimer(SpecialTimerHandle, this, &AEnemyBase::OnSpecialWindupFinished, SpecialWindupDuration, false);
}

void AEnemyBase::OnSpecialWindupFinished()
{
	// Snap mesh back to base scale — windup is done, the burst is about to fire.
	if (!IsAlive())
	{
		AIState = bIsRanged ? EEnemyAIState::Kite : EEnemyAIState::Approach;
		RefreshColor();
		return;
	}
	AIState = EEnemyAIState::SpecialBurst;
	SpecialShotsRemaining = FMath::Max(1, SpecialBurstShots);
	RefreshColor();

	// Fire first shot immediately, then space subsequent shots on an interval timer
	FireSpecialBurstShot();
	if (SpecialBurstShots > 1 && SpecialShotsRemaining > 0)
	{
		GetWorldTimerManager().SetTimer(SpecialBurstTimerHandle, this, &AEnemyBase::FireSpecialBurstShot, SpecialBurstInterval, true);
	}
}

void AEnemyBase::FireSpecialBurstShot()
{
	if (!IsAlive())
	{
		GetWorldTimerManager().ClearTimer(SpecialBurstTimerHandle);
		return;
	}
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	APawn* Player = PC ? PC->GetPawn() : nullptr;
	if (Player && HasLineOfSightTo(Player))
	{
		// Skyfall now fires the SAME visible, dodgeable projectile as normal ranged fire (no more
		// undodgeable hitscan) — scaled by SpecialDamageMultiplier. Cover + movement can beat it.
		const float Dmg = RangedDamage * FMath::Max(1.0f, SpecialDamageMultiplier);
		SpawnProjectileAtPlayer(Dmg);
		UE_LOG(LogLoopedAI, Display, TEXT("Boss Skyfall fired projectile %.0f (shot %d)"), Dmg, (SpecialBurstShots - SpecialShotsRemaining + 1));
	}

	SpecialShotsRemaining = FMath::Max(0, SpecialShotsRemaining - 1);
	if (SpecialShotsRemaining <= 0)
	{
		GetWorldTimerManager().ClearTimer(SpecialBurstTimerHandle);
		// Return to kite; arm cooldown for next special
		AIState = bIsRanged ? EEnemyAIState::Kite : EEnemyAIState::Approach;
		NextSpecialReadyTime = GetWorld()->GetTimeSeconds() + SpecialCooldown;
		RefreshColor();
	}
}

void AEnemyBase::TickBossSpecial(float /*DeltaTime*/, APawn* Player)
{
	// During SpecialWindup the boss freezes in place (and is visually telegraphed).
	// During SpecialBurst the boss also holds position while timers do the firing.
	// The Player param is kept for future targeting logic but currently unused.
	(void)Player;
}

void AEnemyBase::DeathPop()
{
	const FVector MyLoc = GetActorLocation();
	for (TActorIterator<AEnemyBase> It(GetWorld()); It; ++It)
	{
		AEnemyBase* Other = *It;
		if (!Other || Other == this || !Other->IsAlive()) continue;
		const float D = FVector::Dist(MyLoc, Other->GetActorLocation());
		if (D <= DeathPopRadius)
		{
			Other->TakeDamageFromPlayer(DeathPopDamage, this);
			UE_LOG(LogLoopedAI, Display, TEXT("Death pop hit ally for %.0f (dist %.0f)"), DeathPopDamage, D);
		}
	}
}

