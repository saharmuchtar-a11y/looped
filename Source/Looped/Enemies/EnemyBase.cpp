#include "EnemyBase.h"
#include "GAS/LoopedAbilitySystemComponent.h"
#include "Looped.h"
#include "Core/LoopedRunGameMode.h"
#include "Core/LoopedGameInstance.h"
#include "Data/LoopedSaveData.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "Enemies/EnemyProjectile.h"
#include "Enemies/BossBase.h"
#include "Data/EnemyVisualData.h"
#include "Data/PassiveCardData.h"
#include "GameplayTagContainer.h"
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
#include "Core/ElementalHazard.h"

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

	// Branch stick for the Floor-3 hero-copy boss (attached to hand_r when SetupAsHeroCopy runs).
	HeldWeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeldWeaponMesh"));
	HeldWeaponMesh->SetupAttachment(GetMesh(), HeldWeaponSocket);
	HeldWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeldWeaponMesh->SetVisibility(false);
	HeldWeaponMesh->SetCastShadow(false);

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
		// Deeper floors widen the pool: rows with MinFloor 2/3 only start appearing down there.
		int32 CurrentFloor = 1;
		if (const ULoopedGameInstance* FloorGI = GetGameInstance<ULoopedGameInstance>())
		{
			CurrentFloor = FloorGI->GetCurrentFloor();
		}
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

	// Melee role cadence (Swarm/Regular/Tank/Flanker) — bosses keep their own wider ranges.
	MeleeRole = ResolveMeleeRole(RowName, *Row);
	if (!IsA<ABossBase>())
	{
		ApplyMeleeRoleDefaults(MeleeRole);
	}
	// Optional per-row overrides (0 = keep role preset).
	if (Row->LungeTriggerRange > 0.0f)   { LungeTriggerRange = Row->LungeTriggerRange; }
	if (Row->RecoverDuration > 0.0f)     { RecoverDuration = Row->RecoverDuration; }
	if (Row->MeleeHitCooldown > 0.0f)     { MeleeHitCooldown = Row->MeleeHitCooldown; }
	if (Row->LungeSpeedMultiplier > 0.0f){ LungeSpeedMultiplier = Row->LungeSpeedMultiplier; }
	if (Row->MeleeContactRange > 0.0f)   { MeleeContactRange = Row->MeleeContactRange; }

	// Floors 2-3: deeper floors hit harder and last longer (per-floor knobs on the GameInstance).
	// Bosses are EXEMPT — their DT rows are absolute per-floor tuning, not floor-multiplied.
	if (!IsA<ABossBase>())
	{
		if (const ULoopedGameInstance* FloorGI = GetGameInstance<ULoopedGameInstance>())
		{
			POCHealth    *= FloorGI->GetFloorHealthMult();
			MeleeDamage  *= FloorGI->GetFloorDamageMult();
			RangedDamage *= FloorGI->GetFloorDamageMult();
		}
	}

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

	UE_LOG(LogLoopedAI, Display, TEXT("%s: applied enemy type '%s' (HP=%.0f %s%s role=%d)"), *GetName(), *RowName.ToString(), POCHealth, bIsRanged ? TEXT("RANGED") : TEXT("MELEE"), bHybridMelee ? TEXT("+HYBRID") : TEXT(""), (int32)MeleeRole);

	// HeroCopy row: finish melee-mirror setup (deck snapshot + Branch). Safe if GameMode also calls it.
	if (RowName == FName(TEXT("HeroCopy")))
	{
		SetupAsHeroCopy();
	}
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

	// Meshy visual kit — the FLOOR owns the look (per-floor melee/ranged/boss characters);
	// the archetype row above keeps owning the stats. Missing rows keep the legacy visuals.
	ApplyEnemyVisual(VisualRowOverride.IsNone() ? AutoVisualRow() : VisualRowOverride);

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
	// EXCEPTION: Floor-3 HeroCopy is a melee-only mirror (Branch stick, no projectiles).
	if (bIsBoss && !bHeroCopy && EnemyTypeRow != FName(TEXT("HeroCopy")))
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
	else if (bIsBoss && (bHeroCopy || EnemyTypeRow == FName(TEXT("HeroCopy"))))
	{
		// Melee mirror: Approach AI, never kite/shoot. SetupAsHeroCopy (called after ApplyEnemyType)
		// finalizes deck snapshot + Branch attach; BeginPlay may run before that on spawn path.
		bIsRanged = false;
		bHybridMelee = false;
		bIsBoss = true; // keep boss HUD / death flow
		AIState = EEnemyAIState::Approach;
		BaseSpeed = MoveSpeed;
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

	// Curse "ShatteredSight": enemy health bars are hidden from spawn, not just after a hit.
	if (HPBarWidget)
	{
		if (const ULoopedGameInstance* SightGI = GetGameInstance<ULoopedGameInstance>())
		{
			if (SightGI->HasCurse(TEXT("ShatteredSight")))
			{
				HPBarWidget->SetVisibility(false);
			}
		}
	}

	// Guard post = where we spawned. Only used when GuardRadius > 0.
	GuardHome = GetActorLocation();

	// Assign a flank slot deterministically per actor so multiple melee enemies spread out
	const uint32 NameHash = GetTypeHash(GetName());
	FlankSlotIndex = (FlankSlotCount > 0) ? (NameHash % (uint32)FMath::Max(1, FlankSlotCount)) : 0;

	bIsFrenzied = false;
	bIsAlerted = false;

	UE_LOG(LogLoopedAI, Display, TEXT("Enemy spawned: HP=%.0f %s slot=%d"), CurrentHealth, bIsRanged ? TEXT("(RANGED)") : TEXT("(MELEE)"), FlankSlotIndex);
}

FName AEnemyBase::AutoVisualRow() const
{
	if (bHeroCopy || EnemyTypeRow == FName(TEXT("HeroCopy")))
	{
		return FName(TEXT("Hero"));
	}
	int32 Floor = 1;
	if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		Floor = GI->GetCurrentFloor();
	}
	if (bIsBoss)
	{
		return FName(*FString::Printf(TEXT("Boss_F%d"), Floor));
	}
	return FName(*FString::Printf(TEXT("F%d_%s"), Floor, bIsRanged ? TEXT("Ranged") : TEXT("Melee")));
}

bool AEnemyBase::ApplyEnemyVisual(FName VisualRow)
{
	if (VisualRow.IsNone() || !GetMesh()) return false;
	if (!VisualTable)
	{
		VisualTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_EnemyVisuals.DT_EnemyVisuals"));
	}
	if (!VisualTable) return false;
	const FEnemyVisualSet* Row = VisualTable->FindRow<FEnemyVisualSet>(VisualRow, TEXT("ApplyEnemyVisual"), false);
	if (!Row) return false;
	USkeletalMesh* NewMesh = Row->Mesh.LoadSynchronous();
	if (!NewMesh) return false;

	GetMesh()->SetSkeletalMesh(NewMesh);

	// Grounded presentation for ALL kits (Sahar 2026-07-09: no one floats above lava).
	// Asset reminder: F3_Ranged mesh is floor2ranged/Hexweaver (F2↔F3 folder swap) — still true,
	// but float/bob/lava-exception presentation was reversed.
	GetMesh()->SetRelativeLocation(Row->MeshRelLocation);
	GetMesh()->SetRelativeRotation(FRotator(0.0f, Row->MeshRelYaw, 0.0f));
	GetMesh()->SetRelativeScale3D(FVector(Row->MeshScale));
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	GetMesh()->EmptyOverrideMaterials(); // Meshy materials live on the mesh asset (the Mira lesson)

	// Meshy rigs have degenerate REF poses (near-zero bone scales), so the pose must always
	// tick (a culled mesh that never refreshes bones would stay collapsed forever). Bounds come
	// from the LIVE bones — correct now that the anims' roots are unlocked. (Fixed-skel-bounds
	// was tried and caused distance flicker: the asset's imported bounds are ref-pose-degenerate.)
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	VisIdle   = Row->Idle.LoadSynchronous();
	VisWalk   = Row->Walk.LoadSynchronous();
	VisRun    = Row->Run.LoadSynchronous();
	VisAttack = Row->Attack.LoadSynchronous();
	VisCast   = Row->Cast.LoadSynchronous();
	VisDeath  = Row->Death.LoadSynchronous();
	VisDodge  = Row->Dodge.LoadSynchronous();
	bVisualDriven = true;
	CurrentVisualRow = VisualRow;
	CurrentVisAnim = nullptr;

	bFloats = false;
	bCanFloatOverHazards = false;
	FloatBobAmplitude = 0.0f;
	FloatBobSpeed = 0.0f;
	FloatBobRestZ = Row->MeshRelLocation.Z;

	PlayVisualAnim(VisIdle ? VisIdle.Get() : VisWalk.Get(), true);
	UE_LOG(LogLoopedAI, Display, TEXT("%s: visual kit '%s' applied."),
		*GetName(), *VisualRow.ToString());
	return true;
}

void AEnemyBase::PlayVisualAnim(UAnimSequence* Anim, bool bLoop)
{
	if (!Anim || !GetMesh() || CurrentVisAnim == Anim) return;
	CurrentVisAnim = Anim;
	GetMesh()->PlayAnimation(Anim, bLoop);
}

void AEnemyBase::UpdateVisualAnim()
{
	// Jump-back dodge holds the Back_Jump one-shot while airborne/recovering.
	if (AIState == EEnemyAIState::Dodge)
	{
		if (VisDodge)
		{
			PlayVisualAnim(VisDodge.Get(), false);
		}
		else if (VisRun)
		{
			PlayVisualAnim(VisRun.Get(), true); // movement-only fallback if kit has no Dodge anim
		}
		return;
	}
	// Attack-ish states hold a one-shot swing/cast; everything else follows velocity.
	if (AIState == EEnemyAIState::Windup || AIState == EEnemyAIState::Lunge ||
		AIState == EEnemyAIState::SpecialWindup || AIState == EEnemyAIState::SpecialBurst)
	{
		const bool bCasting = bIsRanged ||
			AIState == EEnemyAIState::SpecialWindup || AIState == EEnemyAIState::SpecialBurst;
		UAnimSequence* Swing = bCasting ? (VisCast ? VisCast.Get() : VisAttack.Get())
		                                : (VisAttack ? VisAttack.Get() : VisCast.Get());
		PlayVisualAnim(Swing, false);
		return;
	}
	const float Speed = GetVelocity().Size2D();
	if (Speed > 260.0f)     PlayVisualAnim(VisRun ? VisRun.Get() : VisWalk.Get(), true);
	else if (Speed > 30.0f) PlayVisualAnim(VisWalk ? VisWalk.Get() : VisRun.Get(), true);
	else                    PlayVisualAnim(VisIdle ? VisIdle.Get() : VisWalk.Get(), true);
}

void AEnemyBase::UpdateFloatBob(float DeltaTime)
{
	if (!bFloats || !GetMesh() || bIsDying) return;
	FVector Rel = GetMesh()->GetRelativeLocation();
	const double T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Rel.Z = FloatBobRestZ + FMath::Sin(T * FloatBobSpeed) * FloatBobAmplitude;
	GetMesh()->SetRelativeLocation(Rel);
}

void AEnemyBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!IsAlive()) return;

	// Frostbite: frozen solid — no AI, no movement, no attacks until the thaw timer fires.
	if (bFrozen) return;

	// Visual-kit pawns pick their single-node anim from the AI state + velocity each tick.
	if (bVisualDriven) UpdateVisualAnim();
	UpdateFloatBob(DeltaTime);

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;
	APawn* Player = PC->GetPawn();
	if (!Player) return;

	DodgeCooldownTimer = FMath::Max(0.0f, DodgeCooldownTimer - DeltaTime);

	// Active jump-back — exclusive until the dodge window ends.
	if (AIState == EEnemyAIState::Dodge)
	{
		TickDodge(DeltaTime, Player);
		return;
	}

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
		// Pressure dodge: player in face during approach → jump back (not only chase).
		if (Dist < DodgeTriggerRange && TryBeginDodge(Player, /*bFromPlayerHit*/ false))
		{
			break;
		}

		// Flankers circle to the player's BACK; others use ring slots for pack spread.
		const FVector FlankPoint = (MeleeRole == EMeleeRole::Flanker)
			? ComputeBehindFlankPoint(Player)
			: ComputeFlankPoint(Player);
		if (PathRefreshTimer <= 0.0f || !bHasNavTarget)
		{
			const float DistToFlank = FVector::Dist2D(FlankPoint, MyLoc);
			// Flankers stay on the back-orbit longer before committing to a frontal lunge.
			const float CommitMul = (MeleeRole == EMeleeRole::Flanker) ? 1.05f : 1.5f;
			const bool bFinalApproach = (Dist < LungeTriggerRange * CommitMul);
			const bool bBehindPlayer = (FVector::DotProduct(
				(MyLoc - PlayerLoc).GetSafeNormal2D(),
				(-Player->GetActorForwardVector()).GetSafeNormal2D()) > 0.35f);
			const bool bFlankerReady = (MeleeRole != EMeleeRole::Flanker) || bBehindPlayer || Dist < LungeTriggerRange * 0.85f;
			const FVector Destination = (bFinalApproach && bFlankerReady) || DistToFlank < 180.0f
				? PlayerLoc : FlankPoint;
			CurrentNavTarget = ComputeNavTarget(AvoidHazardDestination(Destination));
			bHasNavTarget = true;
			PathRefreshTimer = PathRefreshInterval;
		}
		const FVector MoveDir = (CurrentNavTarget - MyLoc).GetSafeNormal2D();
		const bool bStandingInHazard = IsPointInActiveHazard(MyLoc);
		const bool bNextStepBurns = !MoveDir.IsNearlyZero()
			&& IsPointInActiveHazard(MyLoc + MoveDir * 90.0f);
		const bool bAllowStep = !MoveDir.IsNearlyZero()
			&& (bStandingInHazard || !bNextStepBurns);
		if (bAllowStep)
		{
			AddMovementInput(MoveDir, 1.0f);
		}
		if (bStandingInHazard)
		{
			bHasNavTarget = false;
			break;
		}
		if (IsPointInActiveHazard(PlayerLoc) || bNextStepBurns)
		{
			break;
		}
		if (Dist < LungeTriggerRange)
		{
			// Flanker: slight delay before commit when already behind (readable backstab feel).
			if (MeleeRole == EMeleeRole::Flanker)
			{
				const bool bBehind = FVector::DotProduct(
					(MyLoc - PlayerLoc).GetSafeNormal2D(),
					(-Player->GetActorForwardVector()).GetSafeNormal2D()) > 0.25f;
				if (!bBehind && Dist > LungeTriggerRange * 0.7f)
				{
					break; // keep circling; don't frontal-spam
				}
			}
			EnterState(EEnemyAIState::Windup);
			PlayAttackAnim();
			float WindupMul = 1.0f;
			if (bIsFrenzied)
			{
				WindupMul = (MeleeRole == EMeleeRole::Tank) ? TankFrenzyWindupMultiplier : FrenzyWindupMultiplier;
			}
			StateTimer = WindupDuration * WindupMul * GetTellDurationMult();
			RefreshColor();
			UE_LOG(LogLoopedAI, Verbose, TEXT("Enemy WINDUP role=%d frenzied=%d dur=%.2f"),
				(int32)MeleeRole, bIsFrenzied ? 1 : 0, StateTimer);
		}
		break;
	}
	case EEnemyAIState::Windup:
	{
		StateTimer -= DeltaTime;
		if (StateTimer <= 0.0f)
		{
			EnterState(EEnemyAIState::Lunge);
			StateTimer = LungeMaxDuration;
			RefreshSpeed();
			RefreshColor();
		}
		break;
	}
	case EEnemyAIState::Lunge:
	{
		const FVector LungeDir = (PlayerLoc - MyLoc).GetSafeNormal2D();
		const bool bLungeIntoHazard = IsPointInActiveHazard(PlayerLoc)
			|| (!LungeDir.IsNearlyZero() && IsPointInActiveHazard(MyLoc + LungeDir * 120.0f));
		if (bLungeIntoHazard && !IsPointInActiveHazard(MyLoc))
		{
			EnterState(EEnemyAIState::Approach);
			bHasNavTarget = false;
			break;
		}
		if (!LungeDir.IsNearlyZero() && !bLungeIntoHazard)
		{
			AddMovementInput(LungeDir, 1.0f);
		}
		StateTimer -= DeltaTime;

		bool bHitThisLunge = false;
		if (Dist < MeleeContactRange && bCanMeleeHit)
		{
			if (ALoopedCharacter* LC = Cast<ALoopedCharacter>(Player))
			{
				const float Dmg = MeleeDamage * GetHeroCopyOutgoingDamageMult();
				LC->TakeDamageFromEnemy(Dmg);
				ApplyHeroCopyDeckOnHit(LC);
				bCanMeleeHit = false;
				bHitThisLunge = true;
				GetWorldTimerManager().SetTimer(MeleeTimerHandle, this, &AEnemyBase::MeleeCooldownReset, MeleeHitCooldown, false);
			}
		}

		if (bHitThisLunge || StateTimer <= 0.0f)
		{
			bLungeConnected = bHitThisLunge;
			EnterState(EEnemyAIState::Recover);
			// Whiff punish: Regular/Tank stay open longer after a miss.
			const float RecoverMul = bHitThisLunge ? 1.0f : WhiffRecoverMultiplier;
			StateTimer = RecoverDuration * RecoverMul;
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
			const FVector AwayDir = (MyLoc - PlayerLoc).GetSafeNormal2D();
			if (!AwayDir.IsNearlyZero() && !IsPointInActiveHazard(MyLoc + AwayDir * 90.0f))
			{
				AddMovementInput(AwayDir, 0.3f);
			}
		}
		else
		{
			// Whiff: Swarm still presses in; Regular/Tank hold / soft backpedal so player can punish.
			if (MeleeRole == EMeleeRole::Swarm)
			{
				const FVector TowardDir = (PlayerLoc - MyLoc).GetSafeNormal2D();
				const bool bPressBurns = IsPointInActiveHazard(PlayerLoc)
					|| (!TowardDir.IsNearlyZero() && IsPointInActiveHazard(MyLoc + TowardDir * 90.0f));
				if (!TowardDir.IsNearlyZero() && !bPressBurns)
				{
					AddMovementInput(TowardDir, 0.6f);
				}
			}
			else
			{
				const FVector AwayDir = (MyLoc - PlayerLoc).GetSafeNormal2D();
				if (!AwayDir.IsNearlyZero() && !IsPointInActiveHazard(MyLoc + AwayDir * 90.0f))
				{
					AddMovementInput(AwayDir, 0.25f);
				}
			}
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
			const float Dmg = MeleeDamage * GetHeroCopyOutgoingDamageMult();
			LC->TakeDamageFromEnemy(Dmg);
			ApplyHeroCopyDeckOnHit(LC);
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
	const FVector ToPlayer = (PlayerLoc - MyLoc).GetSafeNormal2D();

	// Under melee pressure: jump-back dodge (shared system), then hard kite away.
	if (Dist < DodgeTriggerRange && TryBeginDodge(Player, /*bFromPlayerHit*/ false))
	{
		return;
	}

	// Kite movement: prefer clear LOS hold points; flee hard when chased.
	FVector DesiredDest = MyLoc;
	const bool bHasLOSNow = HasLineOfSightTo(Player);

	if (Dist < KiteMinDist)
	{
		// Player chasing — RUN AWAY (stronger than soft backpedal).
		DesiredDest = MyLoc - ToPlayer * FMath::Max(KiteBackpedalDistance, 520.0f);
		bHasNavTarget = false; // repath every refresh while fleeing
	}
	else if (Dist > KiteMaxDist)
	{
		DesiredDest = PlayerLoc;
	}
	else if (!bHasLOSNow)
	{
		// In band but blocked — reposition to a spot with clear line to the player.
		DesiredDest = FindLOSHoldPoint(Player);
	}
	else
	{
		// Clear LOS in band — soft strafe while holding the angle.
		KiteStrafeTimer -= DeltaTime;
		if (KiteStrafeTimer <= 0.0f)
		{
			KiteStrafeSign = FMath::RandBool() ? 1.0f : -1.0f;
			KiteStrafeTimer = KiteStrafeChangeInterval + FMath::FRandRange(-0.3f, 0.5f);
		}
		const FVector SideDir = FVector::CrossProduct(FVector::UpVector, ToPlayer).GetSafeNormal2D();
		DesiredDest = MyLoc + SideDir * KiteStrafeSign * (KiteStrafeDistance * 0.65f);
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
		const FVector Dest = AvoidHazardDestination(DesiredDest);
		CurrentNavTarget = ComputeNavTarget(Dest);
		bHasNavTarget = true;
		// Flee / seek-LOS refreshes faster so they don't stand and eat melee.
		PathRefreshTimer = (Dist < KiteMinDist || !bHasLOSNow)
			? FMath::Min(PathRefreshInterval, 0.18f)
			: PathRefreshInterval;
	}

	const FVector MoveDir = (CurrentNavTarget - MyLoc).GetSafeNormal2D();
	const bool bStandingInHazard = IsPointInActiveHazard(MyLoc);
	const bool bNextStepBurns = !MoveDir.IsNearlyZero()
		&& IsPointInActiveHazard(MyLoc + MoveDir * 90.0f);
	if (bStandingInHazard)
	{
		bHasNavTarget = false;
		if (!MoveDir.IsNearlyZero())
		{
			AddMovementInput(MoveDir, 1.0f);
		}
	}
	else if (!MoveDir.IsNearlyZero() && !bNextStepBurns)
	{
		// Flee harder when the player is close.
		const float MoveScale = (Dist < KiteMinDist) ? 1.0f : 0.85f;
		AddMovementInput(MoveDir, MoveScale);
	}

	// Only shoot from a clear LOS position (and not while face-hugged).
	const bool bInBand = (Dist < RangedRange && Dist > 220.0f);
	const bool bHasLOS = bInBand && HasLineOfSightTo(Player);

	if (bIsBoss && !bHeroCopy && bIsPhase2 &&
		Dist > TeleportTriggerDistance &&
		GetWorld()->GetTimeSeconds() >= NextTeleportReadyTime)
	{
		BeginTeleport();
		return;
	}

	if (bIsBoss && !bHeroCopy && bHasLOS && GetWorld()->GetTimeSeconds() >= NextSpecialReadyTime)
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
			// Refuse to step into an active hazard along the path (melee + ranged).
			if (IsPointInActiveHazard(Pt))
			{
				// Return the previous safe point (or stay put).
				return (i > 1) ? Path->PathPoints[i - 1] : MyLoc;
			}
			if (FVector::Dist2D(Pt, MyLoc) > 50.0f)
			{
				return Pt;
			}
		}
		return Path->PathPoints.Last();
	}

	// No usable path (often a Null nav hole over lava) — stop short of the burn.
	return AvoidHazardDestination(Destination);
}

bool AEnemyBase::IsPointInActiveHazard(const FVector& Point) const
{
	UWorld* World = GetWorld();
	if (!World) return false;
	// Inflate XY so AI stop SHORT of the burn (don't toe the lava edge / clip the volume).
	// Z: only the floor-strip band — elevated platforms (Crossing z80+, LavaField overlook
	// z300) must stay walkable even when their XY sits over a hazard footprint.
	static constexpr float HazardPadXY = 120.0f;
	static constexpr float HazardPadZ = 40.0f;
	for (TActorIterator<AElementalHazard> It(World); It; ++It)
	{
		const AElementalHazard* H = *It;
		if (!H || !H->IsHazardActive()) continue;
		const FBox Box = H->GetHazardBounds();
		const float MinX = Box.Min.X - HazardPadXY;
		const float MaxX = Box.Max.X + HazardPadXY;
		const float MinY = Box.Min.Y - HazardPadXY;
		const float MaxY = Box.Max.Y + HazardPadXY;
		const float MinZ = Box.Min.Z - HazardPadZ;
		const float MaxZ = Box.Max.Z + HazardPadZ;
		if (Point.X >= MinX && Point.X <= MaxX
			&& Point.Y >= MinY && Point.Y <= MaxY
			&& Point.Z >= MinZ && Point.Z <= MaxZ)
		{
			return true;
		}
	}
	return false;
}

FVector AEnemyBase::AvoidHazardDestination(const FVector& Destination) const
{
	const FVector MyLoc = GetActorLocation();
	if (IsPointInActiveHazard(MyLoc))
	{
		// Scramble OUT — prefer a safe Destination; else step away from hazard centers.
		if (!IsPointInActiveHazard(Destination)) return Destination;
		FVector Away = FVector::ZeroVector;
		int32 Count = 0;
		for (TActorIterator<AElementalHazard> It(GetWorld()); It; ++It)
		{
			const AElementalHazard* H = *It;
			if (!H || !H->IsHazardActive()) continue;
			const FBox Box = H->GetHazardBounds();
			const float MinX = Box.Min.X - 120.0f;
			const float MaxX = Box.Max.X + 120.0f;
			const float MinY = Box.Min.Y - 120.0f;
			const float MaxY = Box.Max.Y + 120.0f;
			const float MinZ = Box.Min.Z - 40.0f;
			const float MaxZ = Box.Max.Z + 40.0f;
			if (!(MyLoc.X >= MinX && MyLoc.X <= MaxX
				&& MyLoc.Y >= MinY && MyLoc.Y <= MaxY
				&& MyLoc.Z >= MinZ && MyLoc.Z <= MaxZ))
			{
				continue;
			}
			Away += (MyLoc - H->GetActorLocation()).GetSafeNormal2D();
			++Count;
		}
		if (Count > 0)
		{
			return MyLoc + Away.GetSafeNormal2D() * 500.0f;
		}
		return MyLoc;
	}

	auto StopBeforeHazard = [this, &MyLoc](const FVector& Dest) -> FVector
	{
		const FVector Delta = Dest - MyLoc;
		const float Len = Delta.Size2D();
		if (Len < 1.0f) return MyLoc;
		const FVector Dir = Delta.GetSafeNormal2D();
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(Len / 60.0f), 1, 50);
		for (int32 s = 1; s <= Steps; ++s)
		{
			const FVector Sample = MyLoc + Dir * (Len * (float(s) / float(Steps)));
			if (IsPointInActiveHazard(Sample))
			{
				// Stop well short of the padded edge so they never toe into the burn.
				const float SafeDist = FMath::Max(0.0f, Len * (float(s - 1) / float(Steps)) - 80.0f);
				return MyLoc + Dir * SafeDist;
			}
		}
		return Dest;
	};

	if (!IsPointInActiveHazard(Destination))
	{
		return StopBeforeHazard(Destination);
	}

	// Destination is inside a hazard (player on the far bank / platform) — hold this bank.
	return StopBeforeHazard(Destination);
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
	// Softened 2026-07-07 (Sahar: "color flash needs to be more subtle") — reads, doesn't scream.
	if (bHitFlashing)                                   Glow = FLinearColor(1.2f, 0.02f, 0.02f, 1.0f); // red hit tick
	else if (bFrozen)                                   Glow = FLinearColor(0.15f, 0.9f, 2.2f, 1.0f);  // frozen solid — icy blue
	else if (AIState == EEnemyAIState::TeleportWindup)  Glow = TeleportWindupColor;
	else if (AIState == EEnemyAIState::SpecialWindup)   Glow = SpecialWindupColor;
	else if (AIState == EEnemyAIState::Windup)          Glow = WindupColor;
	else if (bTelegraphInFlight)                        Glow = TelegraphColor;
	else if (BurnTicksRemaining > 0)                    Glow = FLinearColor(0.7f, 0.2f, 0.0f, 1.0f);
	else if (ChillStacks > 0)                           Glow = FLinearColor(0.05f, 0.35f, 0.9f, 1.0f); // chill building toward freeze
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
	// Frostbite: frozen solid = zero speed. Recomputed normally on thaw (EndFreeze calls again).
	if (bFrozen)
	{
		GetCharacterMovement()->MaxWalkSpeed = 0.0f;
		return;
	}

	float S = BaseSpeed;
	if (bIsFrenzied)
	{
		S *= (MeleeRole == EMeleeRole::Tank) ? TankFrenzySpeedMultiplier : FrenzySpeedMultiplier;
	}
	else if (bIsAlerted)                          S *= AlertedSpeedMultiplier;
	if (VenomTicksRemaining > 0)                  S *= VenomSlowMultiplier;
	if (AIState == EEnemyAIState::Lunge)          S *= LungeSpeedMultiplier;

	// Curse "Marked": the player's run is cursed — enemies hunt faster (Iron Will softens it).
	if (const UWorld* World = GetWorld())
	{
		if (const ULoopedGameInstance* GI = World->GetGameInstance<ULoopedGameInstance>())
		{
			if (GI->HasCurse(TEXT("Marked"))) S *= GI->ScaleCurseMult(GI->CurseMarkedEnemySpeedMult);
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
	// player-dealt damage (base hit + ChainSpark + DoT ticks).
	if (const ULoopedGameInstance* PlayerGI = GetGameInstance<ULoopedGameInstance>())
	{
		Damage *= PlayerGI->GetArtifactDamageMult();

		// Curse "Weakness": the player's outgoing damage is dulled this run (Iron Will softens it).
		if (PlayerGI->HasCurse(TEXT("Weakness")))
		{
			Damage *= PlayerGI->ScaleCurseMult(PlayerGI->CurseWeaknessDamageMult);
		}

		const ALoopedCharacter* LC = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));

		// Run relic "BerserkerFetish": hit harder while near death (bespoke hook, like Wing/GoldBar).
		if (LC && PlayerGI->HasRunArtifact(TEXT("BerserkerFetish")) &&
			LC->GetPOCHealthPercent() <= PlayerGI->BerserkerHPThreshold)
		{
			Damage *= PlayerGI->BerserkerDamageMult;
		}

		// Card "Overcharge": the mirror of Berserker — hit harder while UNTOUCHED (full HP).
		if (LC && LC->GetPOCHealthPercent() >= 0.999f)
		{
			if (const FPassiveCardLevel* OverLv = PlayerGI->GetEffectiveLevelData(TEXT("Overcharge")))
			{
				Damage *= 1.0f + OverLv->Fraction;
			}
		}

		// Card "GlassCannon": more damage dealt (the taken side lives in TakeDamageFromEnemy).
		if (const FPassiveCardLevel* GlassLv = PlayerGI->GetEffectiveLevelData(TEXT("GlassCannon")))
		{
			Damage *= 1.0f + GlassLv->Fraction;
		}

		// Card "Rupture": afflicted enemies (burning / venomed / chilled / frozen) tear open.
		if (const FPassiveCardLevel* RuptLv = PlayerGI->GetEffectiveLevelData(TEXT("Rupture")))
		{
			const bool bAfflicted = BurnTicksRemaining > 0 || VenomTicksRemaining > 0 || ChillStacks > 0 || bFrozen;
			if (bAfflicted)
			{
				Damage *= 1.0f + RuptLv->Fraction;
			}
		}

		// Card "Executioner": enemies below the HP threshold take heavily amplified damage.
		if (const FPassiveCardLevel* ExecLv = PlayerGI->GetEffectiveLevelData(TEXT("Executioner")))
		{
			if (ExecLv->Threshold > 0.0f && GetHealthPercent() <= ExecLv->Threshold)
			{
				Damage *= 1.0f + FMath::Max(0.0f, ExecLv->Fraction);
			}
		}

		// Card "HuntersMark": a marked enemy takes bonus damage from EVERYTHING. The first hit
		// applies the mark (no bonus yet); every later source benefits.
		if (const FPassiveCardLevel* MarkLv = PlayerGI->GetEffectiveLevelData(TEXT("HuntersMark")))
		{
			if (bMarkedByHunter)
			{
				Damage *= 1.0f + MarkLv->Fraction;
			}
			bMarkedByHunter = true;
		}

		// Relic "TrophyFang": bonus damage vs frenzied enemies (permanent, achievement-gated).
		if (bIsFrenzied && PlayerGI->HasArtifact(TEXT("TrophyFang")))
		{
			Damage *= PlayerGI->TrophyFangFrenzyMult;
		}

		// Relic "ScarLedger": lifetime damage compounds into power — +1% per 25k dealt, capped.
		if (PlayerGI->HasArtifact(TEXT("ScarLedger")) && PlayerGI->Stats)
		{
			const float Steps = FMath::FloorToFloat(PlayerGI->Stats->TotalDamageDealt / FMath::Max(1.0f, PlayerGI->ScarLedgerDamageStep));
			const float Bonus = FMath::Min(PlayerGI->ScarLedgerMaxBonus, Steps * PlayerGI->ScarLedgerStepBonus);
			Damage *= 1.0f + Bonus;
		}
	}

	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Max(0.0f, CurrentHealth - Damage);

	// Visible + audible "I connected" feedback — red flash + hurt sound on every player hit.
	FlashHit();
	if (EnemyHurtSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, EnemyHurtSound, GetActorLocation());
	}

	// Jump-back under player attack pressure (melee + ranged share this).
	if (APawn* AttackerPawn = Cast<APawn>(DamageSource))
	{
		TryBeginDodge(AttackerPawn, /*bFromPlayerHit*/ true);
	}
	else if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (APawn* P = PC->GetPawn())
		{
			TryBeginDodge(P, /*bFromPlayerHit*/ true);
		}
	}

	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->AddDamageDealt(OldHealth - CurrentHealth);
	}

	if (HPBarWidget)
	{
		// Curse "ShatteredSight": enemy health is hidden — the bar stays dark all run.
		const ULoopedGameInstance* SightGI = GetGameInstance<ULoopedGameInstance>();
		if (SightGI && SightGI->HasCurse(TEXT("ShatteredSight")))
		{
			HPBarWidget->SetVisibility(false);
		}
		else if (UUserWidget* Widget = HPBarWidget->GetWidget())
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

	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		// Curse "Bounty": every kill rings out — the WHOLE room comes alerted.
		if (GI->HasCurse(TEXT("Bounty")))
		{
			AlertNearbyEnemies(this, /*bRoomWide*/ true);
			UE_LOG(LogLoopedAI, Display, TEXT("[Curse] Bounty — kill alerted the whole room."));
		}

		// Card "Detonate": a burning corpse goes up — AoE damage to everything near it. Inlined
		// (ApplyChainSparkEffect early-outs on dead enemies, and this one is mid-death). Damage
		// routes through TakeDamageFromPlayer, so multipliers apply and chained kills re-enter
		// Die safely via the bIsDying guard.
		if (BurnTicksRemaining > 0)
		{
			if (const FPassiveCardLevel* DetLv = GI->GetEffectiveLevelData(TEXT("Detonate")))
			{
				if (DetLv->Damage > 0.0f && DetLv->Radius > 0.0f)
				{
					const FVector BlastLoc = GetActorLocation();
					for (TActorIterator<AEnemyBase> It(GetWorld()); It; ++It)
					{
						AEnemyBase* Other = *It;
						if (!Other || Other == this || !Other->IsAlive()) continue;
						if (FVector::Dist(BlastLoc, Other->GetActorLocation()) <= DetLv->Radius)
						{
							Other->TakeDamageFromPlayer(DetLv->Damage, this);
						}
					}
					UE_LOG(LogLoopedAI, Display, TEXT("[Card] Detonate — burning corpse exploded (%.0f dmg, %.0f uu)."),
						DetLv->Damage, DetLv->Radius);
				}
			}
		}

		// Curse "Haunted": one enemy per room refuses to stay dead. Claim the room's single
		// rise token from the GameMode, then get back up after a short delay.
		if (!bHauntedRespawnUsed && !bIsBoss && GI->HasCurse(TEXT("Haunted")))
		{
			if (ALoopedRunGameMode* HauntGM = Cast<ALoopedRunGameMode>(UGameplayStatics::GetGameMode(this)))
			{
				if (HauntGM->TryConsumeHauntedToken())
				{
					bHauntedRespawnUsed = true;
					GetWorldTimerManager().SetTimer(HauntedRiseTimerHandle, this, &AEnemyBase::HauntedRise, 2.5f, false);
				}
			}
		}
	}

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
	GetWorldTimerManager().ClearTimer(FreezeTimerHandle);
	GetWorldTimerManager().ClearTimer(ChillDecayTimerHandle);
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
	// Visual-kit pawns die in their own skin: single-node Death sequence, last pose held.
	if (bVisualDriven && VisDeath && GetMesh())
	{
		CurrentVisAnim = VisDeath;
		GetMesh()->PlayAnimation(VisDeath, false);
		return VisDeath->GetPlayLength();
	}
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
	GetWorldTimerManager().ClearTimer(FreezeTimerHandle);
	GetWorldTimerManager().ClearTimer(ChillDecayTimerHandle);
	BurnTicksRemaining = 0;
	VenomTicksRemaining = 0;
	ChillStacks = 0;
	bFrozen = false;
	bMarkedByHunter = false; // Hunter's Mark does not survive a respawn
	SpecialShotsRemaining = 0;
	NextSpecialReadyTime = 0.0f;
	NextTeleportReadyTime = 0.0f;
	bIsPhase2 = false;
	CurrentHealth = POCHealth;
	MaxHealthCached = POCHealth;
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	if (HPBarWidget)
	{
		// Curse "ShatteredSight": bars stay hidden even through a respawn.
		const ULoopedGameInstance* SightGI = GetGameInstance<ULoopedGameInstance>();
		HPBarWidget->SetVisibility(!(SightGI && SightGI->HasCurse(TEXT("ShatteredSight"))));
	}
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

void AEnemyBase::ApplyCryoEffect(float FreezeDuration)
{
	if (!IsAlive()) return;
	if (bFrozen) return; // no freeze-lock chaining — chill only counts again after the thaw

	// Blessing "FrostSigil": the freeze needs one fewer stack (min 1) this run.
	int32 StacksNeeded = ChillStacksToFreeze;
	if (const ULoopedGameInstance* SigilGI = GetGameInstance<ULoopedGameInstance>())
	{
		if (SigilGI->HasRunArtifact(TEXT("FrostSigil")))
		{
			StacksNeeded = FMath::Max(1, StacksNeeded - 1);
		}
	}

	ChillStacks++;
	// Stacks decay if not refreshed: chill pressure must be sustained to reach the freeze.
	GetWorldTimerManager().SetTimer(ChillDecayTimerHandle, this, &AEnemyBase::DecayChillStacks, FMath::Max(0.1f, ChillStackDecaySeconds), false);

	if (ChillStacks < StacksNeeded)
	{
		RefreshColor();
		UE_LOG(LogLoopedAI, Display, TEXT("Chill stack %d/%d"), ChillStacks, StacksNeeded);
		return;
	}

	// Full stacks: frozen solid. Tick early-outs while bFrozen, so all AI/attacks stop.
	ChillStacks = 0;
	bFrozen = true;
	GetWorldTimerManager().ClearTimer(ChillDecayTimerHandle);
	GetCharacterMovement()->StopMovementImmediately();
	RefreshSpeed();
	RefreshColor();
	GetWorldTimerManager().SetTimer(FreezeTimerHandle, this, &AEnemyBase::EndFreeze, FMath::Max(0.1f, FreezeDuration), false);
	UE_LOG(LogLoopedAI, Display, TEXT("Enemy FROZEN for %.1fs"), FreezeDuration);
}

void AEnemyBase::EndFreeze()
{
	bFrozen = false;
	RefreshSpeed();
	RefreshColor();
}

void AEnemyBase::HauntedRise()
{
	// Curse "Haunted": the corpse claws back up at half health, already furious.
	if (!bIsDying) return; // respawned/reset by something else in the meantime
	Respawn();
	CurrentHealth = FMath::Max(1.0f, MaxHealthCached * 0.5f);
	bIsAlerted = true;
	RefreshSpeed();
	RefreshColor();
	UE_LOG(LogLoopedAI, Display, TEXT("[Curse] Haunted — enemy ROSE at %.0f HP."), CurrentHealth);
}

void AEnemyBase::DecayChillStacks()
{
	ChillStacks = 0;
	RefreshColor();
}

void AEnemyBase::MeleeCooldownReset()
{
	bCanMeleeHit = true;
}

void AEnemyBase::RangedAttack()
{
	if (!IsAlive() || bHeroCopy) return; // hero-copy: melee/Branch only — never shoot

	APawn* Player = GetWorld()->GetFirstPlayerController()->GetPawn();
	if (!Player) return;

	// Require line of sight to actually land the shot
	if (!HasLineOfSightTo(Player)) return;

	SpawnProjectileAtPlayer(RangedDamage);
}

void AEnemyBase::SpawnProjectileAtPlayer(float Damage)
{
	if (bFrozen || bHeroCopy) return; // hero-copy: no projectiles

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

FVector AEnemyBase::ComputeBehindFlankPoint(const APawn* Player) const
{
	if (!Player) return GetActorLocation();
	const FVector PLoc = Player->GetActorLocation();
	const FVector Back = (-Player->GetActorForwardVector()).GetSafeNormal2D();
	// Slight left/right bias from flank slot so multiple flankers don't stack.
	const float SideBias = (FlankSlotIndex % 2 == 0) ? -35.0f : 35.0f;
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Back).GetSafeNormal2D() * SideBias;
	const float Radius = FMath::Max(FlankRadius, 380.0f);
	return PLoc + Back * Radius + Side;
}

EMeleeRole AEnemyBase::ResolveMeleeRole(FName RowName, const FEnemyTypeData& Row) const
{
	if (Row.MeleeRole != EMeleeRole::Auto)
	{
		return Row.MeleeRole;
	}
	if (Row.bIsRanged && !Row.bHybridMelee)
	{
		return EMeleeRole::None;
	}
	// Known names (DT may still say Auto until Sahar retags rows in editor).
	if (RowName == TEXT("Hound"))
	{
		return EMeleeRole::Flanker;
	}
	if (RowName == TEXT("Rotclaw") || RowName == TEXT("Voidling"))
	{
		return EMeleeRole::Swarm;
	}
	if (RowName == TEXT("Brute") || RowName == TEXT("Frostbrute"))
	{
		return EMeleeRole::Tank;
	}
	if (RowName == TEXT("Grunt"))
	{
		return EMeleeRole::Regular;
	}
	// Hybrids / elites / bosses: keep readable regular melee when they close in.
	if (Row.bHybridMelee || IsA<ABossBase>())
	{
		return EMeleeRole::Regular;
	}
	// Scale heuristic: small = swarm, big = tank, else regular.
	if (Row.Scale <= 0.85f)
	{
		return EMeleeRole::Swarm;
	}
	if (Row.Scale >= 1.25f)
	{
		return EMeleeRole::Tank;
	}
	return EMeleeRole::Regular;
}

void AEnemyBase::ApplyMeleeRoleDefaults(EMeleeRole ResolvedRole)
{
	switch (ResolvedRole)
	{
	case EMeleeRole::Swarm:
		LungeTriggerRange = 260.0f;
		RecoverDuration = 0.28f;
		WhiffRecoverMultiplier = 1.0f;
		MeleeHitCooldown = 0.85f;
		LungeSpeedMultiplier = 2.8f;
		MeleeContactRange = 130.0f;
		DodgeCooldown = 2.6f;
		DodgeChance = 0.28f;
		DodgeTriggerRange = 200.0f;
		DodgeDistance = 240.0f;
		if (WindupDuration > 0.55f) { WindupDuration = 0.40f; }
		break;
	case EMeleeRole::Regular:
		LungeTriggerRange = 320.0f;
		RecoverDuration = 0.70f;
		WhiffRecoverMultiplier = 1.45f;
		MeleeHitCooldown = 1.35f;
		LungeSpeedMultiplier = 2.0f;
		MeleeContactRange = 145.0f;
		DodgeCooldown = 3.0f;
		DodgeChance = 0.38f;
		DodgeTriggerRange = 230.0f;
		DodgeDistance = 280.0f;
		if (WindupDuration < 0.65f) { WindupDuration = 0.85f; }
		break;
	case EMeleeRole::Tank:
		LungeTriggerRange = 400.0f;
		RecoverDuration = 1.00f;
		WhiffRecoverMultiplier = 1.55f;
		MeleeHitCooldown = 1.80f;
		LungeSpeedMultiplier = 1.7f;
		MeleeContactRange = 175.0f;
		DodgeCooldown = 4.2f;
		DodgeChance = 0.18f;
		DodgeTriggerRange = 250.0f;
		DodgeDistance = 220.0f;
		if (WindupDuration < 0.95f) { WindupDuration = 1.20f; }
		break;
	case EMeleeRole::Flanker:
		LungeTriggerRange = 240.0f;
		RecoverDuration = 0.45f;
		WhiffRecoverMultiplier = 1.15f;
		MeleeHitCooldown = 1.10f;
		LungeSpeedMultiplier = 2.4f;
		MeleeContactRange = 135.0f;
		FlankRadius = 420.0f;
		DodgeCooldown = 2.4f;
		DodgeChance = 0.42f;
		DodgeTriggerRange = 240.0f;
		DodgeDistance = 300.0f;
		if (WindupDuration > 0.55f) { WindupDuration = 0.45f; }
		break;
	case EMeleeRole::None:
	default:
		// Ranged / unspecified — still dodge under pressure; no melee cadence change.
		DodgeCooldown = 2.8f;
		DodgeChance = 0.45f;
		DodgeTriggerRange = 280.0f;
		DodgeDistance = 320.0f;
		DodgeLaunchSpeed = 780.0f;
		break;
	}
}

bool AEnemyBase::TryBeginDodge(APawn* Player, bool bFromPlayerHit)
{
	if (!Player || !IsAlive() || bFrozen) return false;
	if (AIState == EEnemyAIState::Dodge) return false;
	if (AIState == EEnemyAIState::Windup || AIState == EEnemyAIState::Lunge) return false;
	if (AIState == EEnemyAIState::SpecialWindup || AIState == EEnemyAIState::SpecialBurst ||
		AIState == EEnemyAIState::TeleportWindup)
	{
		return false;
	}
	if (DodgeCooldownTimer > 0.0f) return false;
	if (!GetCharacterMovement() || !GetCharacterMovement()->IsMovingOnGround()) return false;

	const float Dist = FVector::Dist2D(Player->GetActorLocation(), GetActorLocation());
	if (!bFromPlayerHit && Dist > DodgeTriggerRange) return false;

	const float Chance = bFromPlayerHit
		? FMath::Clamp(DodgeChance + 0.25f, 0.0f, 0.85f)
		: DodgeChance;
	if (FMath::FRand() > Chance) return false;

	const FVector Away = (GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D();
	if (Away.IsNearlyZero()) return false;

	const FVector Probe = GetActorLocation() + Away * 120.0f;
	if (IsPointInActiveHazard(Probe) || IsPointInActiveHazard(GetActorLocation() + Away * DodgeDistance * 0.5f))
	{
		return false; // never jump-back into lava
	}

	DodgeMoveDir = Away;
	EnterState(EEnemyAIState::Dodge);
	StateTimer = DodgeDuration;
	DodgeCooldownTimer = DodgeCooldown;
	bHasNavTarget = false;

	// Cancel an in-flight ranged telegraph so they don't shoot mid-dodge.
	if (bTelegraphInFlight)
	{
		GetWorldTimerManager().ClearTimer(TelegraphTimerHandle);
		bTelegraphInFlight = false;
		RefreshColor();
	}
	if (GetWorldTimerManager().IsTimerActive(RangedTimerHandle))
	{
		GetWorldTimerManager().ClearTimer(RangedTimerHandle);
	}

	LaunchCharacter(FVector(Away.X * DodgeLaunchSpeed, Away.Y * DodgeLaunchSpeed, DodgeLaunchZ), true, true);
	if (bVisualDriven && VisDodge)
	{
		CurrentVisAnim = nullptr; // force replay
		PlayVisualAnim(VisDodge.Get(), false);
	}
	UE_LOG(LogLoopedAI, Verbose, TEXT("%s DODGE (hit=%d role=%d)"), *GetName(), bFromPlayerHit ? 1 : 0, (int32)MeleeRole);
	return true;
}

void AEnemyBase::TickDodge(float DeltaTime, APawn* Player)
{
	StateTimer -= DeltaTime;
	if (!DodgeMoveDir.IsNearlyZero())
	{
		const FVector Step = GetActorLocation() + DodgeMoveDir * 90.0f;
		if (!IsPointInActiveHazard(Step))
		{
			AddMovementInput(DodgeMoveDir, 1.0f);
		}
	}
	if (StateTimer <= 0.0f)
	{
		EnterState(bIsRanged && !bMeleeEngaged ? EEnemyAIState::Kite : EEnemyAIState::Approach);
		bHasNavTarget = false;
		RefreshSpeed();
	}
}

FVector AEnemyBase::FindLOSHoldPoint(const APawn* Player) const
{
	if (!Player) return GetActorLocation();
	const FVector MyLoc = GetActorLocation();
	const FVector PlayerLoc = Player->GetActorLocation();
	const FVector Away = (MyLoc - PlayerLoc).GetSafeNormal2D();
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Away).GetSafeNormal2D();

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	const float PreferDist = FMath::Clamp(FVector::Dist2D(MyLoc, PlayerLoc), KiteMinDist, KiteMaxDist);

	// Sample ring around preferred kite distance; pick first nav-projected point with LOS.
	static const float Angles[] = { 0.f, 40.f, -40.f, 80.f, -80.f, 120.f, -120.f, 160.f, -160.f, 180.f };
	for (float Ang : Angles)
	{
		const FRotator R(0.f, Ang, 0.f);
		const FVector Dir = R.RotateVector(Away.IsNearlyZero() ? FVector::ForwardVector : Away);
		FVector Candidate = PlayerLoc + Dir * PreferDist + Side * (Ang * 1.5f);
		Candidate.Z = MyLoc.Z;
		if (IsPointInActiveHazard(Candidate)) continue;

		if (NavSys)
		{
			FNavLocation Projected;
			if (NavSys->ProjectPointToNavigation(Candidate, Projected, FVector(400.f, 400.f, 600.f)))
			{
				Candidate = Projected.Location;
			}
		}
		if (IsPointInActiveHazard(Candidate)) continue;

		// Temporary LOS check from candidate height toward player.
		const FVector Start = Candidate + FVector(0, 0, 60.0f);
		const FVector End = PlayerLoc;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyLOSSeek), false, this);
		Params.AddIgnoredActor(Player);
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
		if (!bBlocked)
		{
			return Candidate;
		}
	}
	// Fallback: just back away from the player on safe ground.
	return AvoidHazardDestination(MyLoc + Away * 400.0f);
}

void AEnemyBase::CheckEnterFrenzy()
{
	if (bIsFrenzied) return;
	// Curse "Cowardice": enemies panic early — frenzy triggers at a much higher HP fraction.
	float Threshold = FrenzyHPThreshold;
	if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		if (GI->HasCurse(TEXT("Cowardice")))
		{
			Threshold = FMath::Max(Threshold, GI->CurseCowardiceFrenzyHP);
		}
	}
	if (GetHealthPercent() <= Threshold)
	{
		bIsFrenzied = true;
		RefreshSpeed();
		RefreshColor();
		UE_LOG(LogLoopedAI, Display, TEXT("Enemy entered FRENZY (HP%%=%.2f)"), GetHealthPercent());
	}
}

float AEnemyBase::GetTellDurationMult() const
{
	// Curse "Feverdream": every attack tell (windup / telegraph / special) comes faster.
	if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		if (GI->HasCurse(TEXT("Feverdream")))
		{
			// Iron Will pulls the tell time back toward normal.
			return GI->ScaleCurseMult(GI->CurseFeverdreamTellMult);
		}
	}
	return 1.0f;
}

void AEnemyBase::CheckEnterPhase2()
{
	if (bIsPhase2 || !bIsBoss || bHeroCopy) return; // hero-copy stays a pure melee mirror
	if (GetHealthPercent() > Phase2HPThreshold) return;

	bIsPhase2 = true;

	// The Warlord sheds its shell: if a "_P2" visual kit exists for the current look, swap the
	// mesh + moveset LIVE — the transformation IS the phase tell. Stats untouched by design.
	if (bVisualDriven && !CurrentVisualRow.IsNone())
	{
		ApplyEnemyVisual(FName(*(CurrentVisualRow.ToString() + TEXT("_P2"))));
	}

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

void AEnemyBase::AlertNearbyEnemies(AActor* Cause, bool bRoomWide)
{
	const FVector MyLoc = GetActorLocation();
	for (TActorIterator<AEnemyBase> It(GetWorld()); It; ++It)
	{
		AEnemyBase* Other = *It;
		if (!Other || Other == this || !Other->IsAlive()) continue;
		const float D = FVector::Dist(Other->GetActorLocation(), MyLoc);
		if ((bRoomWide || D <= AlertRadius) && !Other->bIsAlerted)
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
	const float TellDur = TelegraphDuration * GetTellDurationMult(); // Feverdream shortens tells
	UE_LOG(LogLoopedAI, Verbose, TEXT("Ranged TELEGRAPH begin (dur=%.2f)"), TellDur);
	GetWorldTimerManager().SetTimer(TelegraphTimerHandle, this, &AEnemyBase::FireTelegraphedShot, TellDur, false);
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
	if (!IsAlive() || bHeroCopy) return;

	// Stop normal-ranged firing while in special
	GetWorldTimerManager().ClearTimer(RangedTimerHandle);
	GetWorldTimerManager().ClearTimer(TelegraphTimerHandle);
	bTelegraphInFlight = false;

	AIState = EEnemyAIState::SpecialWindup;
	// Tell is the bright-magenta SpecialWindup glow — boss is CHARGING Skyfall.
	RefreshColor();
	const float SpecialDur = SpecialWindupDuration * GetTellDurationMult(); // Feverdream shortens tells
	UE_LOG(LogLoopedAI, Display, TEXT("Boss: SpecialWindup (will fire %d shots in %.2fs)"), SpecialBurstShots, SpecialDur);

	// After windup, enter the burst phase and fire shots on an interval timer.
	GetWorldTimerManager().SetTimer(SpecialTimerHandle, this, &AEnemyBase::OnSpecialWindupFinished, SpecialDur, false);
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
	// Relic "Crown" (of the Looped): a proven Hunter reads the Skyfall — one fewer volley shot.
	if (const ULoopedGameInstance* CrownGI = GetGameInstance<ULoopedGameInstance>())
	{
		if (CrownGI->HasArtifact(TEXT("Crown")))
		{
			SpecialShotsRemaining = FMath::Max(1, SpecialShotsRemaining - 1);
		}
	}
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

	// Curse "Volatile": the death pop also detonates against the PLAYER — melee kills turn risky.
	if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		if (GI->HasCurse(TEXT("Volatile")))
		{
			if (ALoopedCharacter* LC = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
			{
				if (FVector::Dist(MyLoc, LC->GetActorLocation()) <= DeathPopRadius)
				{
					LC->TakeDamageFromEnemy(GI->CurseVolatileSelfDamage);
					UE_LOG(LogLoopedAI, Display, TEXT("Volatile death pop hit the PLAYER for %.0f"), GI->CurseVolatileSelfDamage);
				}
			}
		}
	}
}

void AEnemyBase::ApplyHeavyImpact(const FVector& FromDirection, float KnockbackSpeed)
{
	if (!IsAlive() || bFrozen) return;
	if (bIsBoss) return; // bosses hold their ground — the heavy still pays via HeavyDamageMult

	const FVector Dir = FromDirection.GetSafeNormal2D();
	if (Dir.IsNearlyZero()) return;

	// Never punt an enemy into lava/venom — a heavy must not hand out free hazard kills.
	if (IsPointInActiveHazard(GetActorLocation() + Dir * 240.0f)) return;

	// Interrupt a wound-up attack: the heavy is the answer to melee pressure.
	if (AIState == EEnemyAIState::Windup || AIState == EEnemyAIState::Lunge)
	{
		bLungeConnected = false;
		EnterState(EEnemyAIState::Recover);
		StateTimer = RecoverDuration * FMath::Max(1.0f, WhiffRecoverMultiplier);
		RefreshSpeed();
		RefreshColor();
	}

	LaunchCharacter(FVector(Dir.X * KnockbackSpeed, Dir.Y * KnockbackSpeed, 160.0f), true, true);
	UE_LOG(LogLoopedAI, Verbose, TEXT("%s knocked back by HEAVY (speed=%.0f)"), *GetName(), KnockbackSpeed);
}

void AEnemyBase::SetupAsHeroCopy()
{
	bHeroCopy = true;
	bIsBoss = true;
	bIsRanged = false;
	bHybridMelee = false;
	bIsPhase2 = false;
	AIState = EEnemyAIState::Approach;

	// Snapshot the player's deck at fight start — cards only (no relics / blessings).
	HeroCopyDeck.Reset();
	if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		HeroCopyDeck = GI->RunDeck;
	}

	VisualRowOverride = FName(TEXT("Hero"));
	ApplyEnemyVisual(FName(TEXT("Hero")));
	AttachHeroCopyBranch();

	BaseSpeed = MoveSpeed;
	RefreshSpeed();
	RefreshColor();

	UE_LOG(LogLoopedAI, Display, TEXT("%s: SetupAsHeroCopy — melee mirror, deck=%d cards, Branch attached."),
		*GetName(), HeroCopyDeck.Num());
}

void AEnemyBase::AttachHeroCopyBranch()
{
	if (!HeldWeaponMesh || !GetMesh()) return;

	UStaticMesh* Branch = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Weapons/Branch/SM_Branch.SM_Branch"));
	if (!Branch)
	{
		UE_LOG(LogLoopedAI, Warning, TEXT("%s: HeroCopy Branch mesh missing at /Game/Weapons/Branch/SM_Branch"), *GetName());
		return;
	}

	HeldWeaponMesh->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, HeldWeaponSocket);
	HeldWeaponMesh->SetStaticMesh(Branch);
	HeldWeaponMesh->SetVisibility(true);
	HeldWeaponMesh->SetCastShadow(false);
	NormalizeHeldWeaponTransform();
	if (GetWorld())
	{
		GetWorldTimerManager().SetTimer(HeldWeaponNormalizeTimerHandle, this,
			&AEnemyBase::NormalizeHeldWeaponTransform, 0.25f, false);
	}
}

void AEnemyBase::NormalizeHeldWeaponTransform()
{
	if (!HeldWeaponMesh || !GetMesh() || !HeldWeaponMesh->GetStaticMesh()) return;
	const FTransform SocketT = GetMesh()->GetSocketTransform(HeldWeaponSocket);
	HeldWeaponMesh->SetWorldScale3D(HeldWeaponWorldScale);
	HeldWeaponMesh->SetWorldLocation(SocketT.GetLocation() + SocketT.TransformVectorNoScale(HeldWeaponGripOffset));
}

float AEnemyBase::GetHeroCopyOutgoingDamageMult() const
{
	if (!bHeroCopy) return 1.0f;
	float Mult = 1.0f;
	for (const FPassiveSlot& Slot : HeroCopyDeck)
	{
		if (Slot.IsEmpty() || Slot.Level <= 0) continue;
		const FPassiveCardData& Card = Slot.CachedData;
		if (!Card.Levels.IsValidIndex(Slot.Level - 1)) continue;
		const FPassiveCardLevel& Lv = Card.Levels[Slot.Level - 1];
		if (Slot.CardRowName == FName(TEXT("GlassCannon")) || Slot.CardRowName == FName(TEXT("Overcharge"))
			|| Slot.CardRowName == FName(TEXT("WoundMemory")))
		{
			// Overcharge: copy is "untouched" at fight start — apply while copy is near full HP.
			if (Slot.CardRowName == FName(TEXT("Overcharge")) && GetHealthPercent() < 0.999f) continue;
			Mult *= 1.0f + Lv.Fraction;
		}
	}
	return Mult;
}

void AEnemyBase::ApplyHeroCopyDeckOnHit(ALoopedCharacter* Player)
{
	if (!bHeroCopy || !Player || !Player->IsAlive()) return;

	const FGameplayTag TagBurn      = FGameplayTag::RequestGameplayTag(FName("Effect.Burn"), false);
	const FGameplayTag TagVenom     = FGameplayTag::RequestGameplayTag(FName("Effect.Venom"), false);
	const FGameplayTag TagLifesteal = FGameplayTag::RequestGameplayTag(FName("Effect.Lifesteal"), false);
	const FGameplayTag TagCryo      = FGameplayTag::RequestGameplayTag(FName("Effect.Cryo"), false);

	for (const FPassiveSlot& Slot : HeroCopyDeck)
	{
		if (Slot.IsEmpty() || Slot.Level <= 0) continue;
		const FPassiveCardData& Card = Slot.CachedData;
		if (!Card.Levels.IsValidIndex(Slot.Level - 1)) continue;
		const FPassiveCardLevel& Lv = Card.Levels[Slot.Level - 1];
		const FGameplayTagContainer& CardTags = Card.EffectTags;

		if (CardTags.HasTagExact(TagBurn))
		{
			Player->ApplyElementalStatus(FName(TEXT("Burn")), Lv.Damage, FMath::Max(1.0f, (float)Lv.Ticks));
		}
		else if (CardTags.HasTagExact(TagVenom))
		{
			Player->ApplyElementalStatus(FName(TEXT("Venom")), Lv.Damage, FMath::Max(1.0f, (float)Lv.Ticks));
			if (Lv.SlowMultiplier < 1.0f)
			{
				Player->ApplyElementalStatus(FName(TEXT("Slow")), Lv.SlowMultiplier, FMath::Max(1.0f, (float)Lv.Ticks));
			}
		}
		else if (CardTags.HasTagExact(TagLifesteal))
		{
			CurrentHealth = FMath::Min(MaxHealthCached, CurrentHealth + Lv.HealAmount);
		}
		else if (CardTags.HasTagExact(TagCryo))
		{
			// Mirror Frostbite as a short chill on the player (no full freeze — keep fight fair).
			Player->ApplyElementalStatus(FName(TEXT("Slow")), 0.55f, FMath::Max(0.5f, Lv.FreezeDuration));
		}
	}
}

