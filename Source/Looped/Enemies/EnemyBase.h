#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Data/EnemyData.h"
#include "Data/PassiveCardData.h"
#include "EnemyBase.generated.h"

class ULoopedAbilitySystemComponent;
class UStaticMeshComponent;
class UWidgetComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyDied, AEnemyBase*, Enemy);

UENUM(BlueprintType)
enum class EEnemyAIState : uint8
{
	Approach        UMETA(DisplayName = "Approach"),
	Windup          UMETA(DisplayName = "Windup"),
	Lunge           UMETA(DisplayName = "Lunge"),
	Recover         UMETA(DisplayName = "Recover"),
	Dodge           UMETA(DisplayName = "Dodge"),
	Kite            UMETA(DisplayName = "Kite"),
	SpecialWindup   UMETA(DisplayName = "SpecialWindup"),
	SpecialBurst    UMETA(DisplayName = "SpecialBurst"),
	TeleportWindup  UMETA(DisplayName = "TeleportWindup"),
};

UCLASS(Blueprintable)
class LOOPED_API AEnemyBase : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AEnemyBase();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UPROPERTY(BlueprintAssignable, Category = "LOOPED|Events")
	FOnEnemyDied OnEnemyDied;

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Enemy")
	void TakeDamageFromPlayer(float Damage, AActor* DamageSource);

	UFUNCTION(BlueprintPure, Category = "LOOPED|Enemy")
	float GetHealthPercent() const;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Enemy")
	bool IsAlive() const;

	// Cached max health (= POCHealth at spawn). Lets HUD/UI show the real max instead of a literal.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Enemy")
	float GetMaxHealth() const { return MaxHealthCached; }

	UFUNCTION(BlueprintPure, Category = "LOOPED|Enemy")
	bool IsBoss() const { return bIsBoss; }

	UFUNCTION(BlueprintImplementableEvent, Category = "LOOPED|Enemy")
	void BP_OnDied();

	// Applies a DT_Enemies archetype row over this enemy's defaults (HP/speed/scale/color/
	// behavior/damage/element). Called automatically in BeginPlay when EnemyTypeRow is set;
	// also callable right after a runtime spawn (before the enemy has taken damage).
	// Returns false (and changes nothing) if the table/row can't be resolved.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Enemy")
	bool ApplyEnemyType(FName RowName);

	// --- Meshy visual kit (DT_EnemyVisuals rows = FEnemyVisualSet; floor owns the LOOK) ---
	// Swap this pawn's mesh + single-node anim set to a visual row. STATS untouched — safe
	// mid-fight (the boss phase-2 transformation calls it live). Missing row/mesh = no-op false.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Visual")
	bool ApplyEnemyVisual(FName VisualRow);

	// None (default) = auto-pick: "F<floor>_<Melee|Ranged>" for mooks, "Boss_F<floor>" for bosses.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Visual")
	FName VisualRowOverride = NAME_None;

	// Floor-3 hero-copy boss: melee-only mirror. Snapshots the player's RunDeck at fight start
	// (no relics / blessings), forces Hero visual + Branch stick in hand_r, never shoots.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|HeroCopy")
	void SetupAsHeroCopy();

	UFUNCTION(BlueprintPure, Category = "LOOPED|HeroCopy")
	bool IsHeroCopy() const { return bHeroCopy; }

	// Player HEAVY melee connect: shove back + interrupt an in-progress windup/lunge. Bosses and
	// frozen enemies ignore it; the shove is cancelled if it would land the enemy in a hazard.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Enemy")
	void ApplyHeavyImpact(const FVector& FromDirection, float KnockbackSpeed);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Enemy")
	void Respawn();

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Enemy")
	void RespawnAt(FVector Location);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Enemy")
	void ApplyBurnEffect(float DamagePerTick = 10.0f, int32 NumTicks = 3);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Enemy")
	void ApplyVenomEffect(float DamagePerTick = 5.0f, int32 NumTicks = 5, float SlowMultiplier = 0.4f);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Enemy")
	void ApplyChainSparkEffect(float ChainDamage = 10.0f, float ChainRadius = 500.0f);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Enemy")
	void ApplyLifestealEffect(float HealAmount = 5.0f);

	// Frostbite card: each hit adds a chill stack; at ChillStacksToFreeze the enemy freezes solid
	// (no movement, no AI, no shots) for FreezeDuration seconds. Stacks decay if not refreshed.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Enemy")
	void ApplyCryoEffect(float FreezeDuration);

	UFUNCTION(BlueprintPure, Category = "LOOPED|Enemy")
	bool IsFrozen() const { return bFrozen; }

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<ULoopedAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	// Branch stick held by the Floor-3 hero-copy boss (world-visible hand attach).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> HeldWeaponMesh;

	// --- Archetype (DT_Enemies) ---
	// Row to apply from the enemy-type table. NAME_None = use the values below as-is (legacy
	// per-instance tuning keeps working untouched). Set per placed enemy, or by the spawner.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Type")
	FName EnemyTypeRow = NAME_None;

	// The archetype table. Left null it lazy-loads /Game/Data/DT_Enemies at BeginPlay, so levels
	// don't need a manual assignment and there's no constructor dependency on the asset existing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Type")
	TObjectPtr<class UDataTable> EnemyTypeTable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	float POCHealth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	FLinearColor EnemyColor = FLinearColor::Red;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	float MoveSpeed = 350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	float MeleeDamage = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	float MeleeHitCooldown = 1.0f;

	// Resolved melee job (from DT row / Scale). Drives cadence + dodge rhythm.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	EMeleeRole MeleeRole = EMeleeRole::Regular;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	bool bIsRanged = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	float RangedDamage = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	float RangedFireRate = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	float RangedRange = 1500.0f;

	// --- Melee tuning (telegraphed lunge) ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Melee")
	float LungeTriggerRange = 280.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Melee")
	float WindupDuration = 0.6f;

	// Mesh swells by this factor during Windup so the tell reads in peripheral vision.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Melee")
	float WindupScaleMultiplier = 1.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Melee")
	float LungeDuration = 0.20f;

	// Hard cap on the lunge: the lunge drives STRAIGHT at the player until it makes
	// contact OR this window closes, so a stationary target always gets reached
	// instead of the enemy whiffing past it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Melee")
	float LungeMaxDuration = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Melee")
	float LungeSpeedMultiplier = 2.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Melee")
	float RecoverDuration = 0.35f;

	// Extra recover after a WHIFF (miss) — Regular/Tank get longer punish windows.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Melee")
	float WhiffRecoverMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Melee")
	float MeleeContactRange = 140.0f;

	// --- Jump-back dodge (Meshy Back_Jump / VisDodge) ---
	// Enemies create space under player pressure instead of only chasing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Dodge")
	float DodgeCooldown = 3.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Dodge")
	float DodgeDuration = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Dodge")
	float DodgeDistance = 280.0f;

	// Chance to jump back when conditions are met (role presets overwrite).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Dodge", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DodgeChance = 0.35f;

	// Player must be within this range to trigger a pressure dodge.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Dodge")
	float DodgeTriggerRange = 220.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Dodge")
	float DodgeLaunchSpeed = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Dodge")
	float DodgeLaunchZ = 220.0f;

	// Hybrid combat: a RANGED enemy/boss that ALSO melees (windup→lunge + attack anim) when the
	// player gets close, instead of only kiting + shooting. Set true on bosses.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Melee")
	bool bHybridMelee = false;

	// Player distance (uu) at/under which a hybrid enemy commits to melee. It returns to ranged
	// once the player backs beyond MeleeEngageRange * 1.6 (hysteresis stops twitchy flip-flop).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Melee")
	float MeleeEngageRange = 260.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Melee")
	FLinearColor WindupColor = FLinearColor(4.0f, 3.5f, 0.0f, 1.0f);  // super-bright yellow (HDR for emissive)

	// --- Ranged tuning (kite) ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Ranged")
	float KiteMinDist = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Ranged")
	float KiteMaxDist = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Ranged")
	float KiteStrafeChangeInterval = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Ranged")
	float KiteBackpedalDistance = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Ranged")
	float KiteStrafeDistance = 350.0f;

	// DEPRECATED (Sahar 2026-07-09): no enemy floats over lava. Kept for BP/DT compat; always forced false.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Float")
	bool bCanFloatOverHazards = false;

	// --- Guard post ---
	// > 0 = this enemy DEFENDS its spawn spot instead of chasing across the map: kiting/strafing is
	// clamped inside this radius of home, melee only engages targets near the post, and a strayed
	// guard walks back. Set on placed enemies holding ground (e.g. The Overlook's platform crew).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Guard")
	float GuardRadius = 0.0f;

	// --- Navigation ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Nav")
	float PathRefreshInterval = 0.3f;

	// Min seconds between jumps when player is above us. Stops bunny-hopping.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Nav")
	float JumpCooldown = 2.0f;

	// Only jump if player is within this horizontal range. Far-away high ledges shouldn't trigger jumps.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Nav")
	float JumpMaxHorizontalDist = 450.0f;

	// If we haven't moved more than this distance over StuckWindow seconds, rotate flank slot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Nav")
	float StuckThreshold = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Nav")
	float StuckWindow = 2.0f;

	// --- Flanking ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Flank")
	float FlankRadius = 350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Flank")
	int32 FlankSlotCount = 4;

	// --- Low-HP frenzy ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Frenzy")
	float FrenzyHPThreshold = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Frenzy")
	float FrenzySpeedMultiplier = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Frenzy")
	float FrenzyWindupMultiplier = 0.55f;

	// Tanks keep a milder frenzy so they don't suddenly become swarm.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Frenzy")
	float TankFrenzyWindupMultiplier = 0.80f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Frenzy")
	float TankFrenzySpeedMultiplier = 1.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Frenzy")
	FLinearColor FrenzyColor = FLinearColor(1.0f, 0.05f, 0.05f, 1.0f);

	// --- Telegraph (ranged) ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Telegraph")
	float TelegraphDuration = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Telegraph")
	FLinearColor TelegraphColor = FLinearColor(4.5f, 0.5f, 0.0f, 1.0f);  // saturated red-orange (HDR — stays orange under bloom, doesn't shift to yellow)

	// Mesh swells before each ranged shot so the tell reads as a deliberate windup, not the shot itself.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Telegraph")
	float TelegraphScaleMultiplier = 1.22f;

	// --- Alert (group awareness) ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Alert")
	float AlertRadius = 2200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Alert")
	float AlertDuration = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Alert")
	float AlertedSpeedMultiplier = 1.15f;

	// --- Death pop (AoE on death) ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|DeathPop")
	float DeathPopRadius = 420.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|DeathPop")
	float DeathPopDamage = 12.0f;

	// --- Boss (Skyfall Volley) — opt-in via bIsBoss. Requires bIsRanged=true to fire. ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss")
	bool bIsBoss = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss")
	float SpecialCooldown = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss")
	float SpecialWindupDuration = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss")
	int32 SpecialBurstShots = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss")
	float SpecialBurstInterval = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss")
	float SpecialDamageMultiplier = 1.0f;  // each burst shot uses RangedDamage * this

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss")
	FLinearColor SpecialWindupColor = FLinearColor(4.0f, 0.2f, 2.5f, 1.0f); // super-bright magenta (HDR for emissive)

	// Boss mesh swells during the 1.5s windup so the player sees the boss CHARGING something dangerous.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss")
	float SpecialWindupScaleMultiplier = 1.35f;

	// --- Boss Phase 2 (HP <= threshold) — ranged becomes ranged+melee+teleport ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss|Phase2")
	float Phase2HPThreshold = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss|Phase2")
	float Phase2DamageMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss|Phase2")
	float Phase2SpecialCooldown = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss|Phase2")
	int32 Phase2SpecialBurstShots = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss|Phase2")
	float Phase2FireRateMultiplier = 0.6f; // RangedFireRate * this — lower = faster

	// Phase 2 color shift signals the boss has transformed. Stays in EnemyColor slot until next phase tell.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss|Phase2")
	FLinearColor Phase2BaseColor = FLinearColor(2.5f, 0.0f, 0.0f, 1.0f); // deep blood-red, HDR-boosted

	// Teleport: triggers when player is farther than TriggerDistance and cooldown is elapsed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss|Phase2")
	float TeleportCooldown = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss|Phase2")
	float TeleportTriggerDistance = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss|Phase2")
	float TeleportArrivalDistance = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss|Phase2")
	float TeleportWindupDuration = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss|Phase2")
	float TeleportContactDamage = 4.0f;

	// After teleport, boss enters real melee mode (bIsRanged temporarily false)
	// so the player gets a readable Windup→Lunge sequence instead of being shot point-blank.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss|Phase2")
	float PostTeleportMeleeDuration = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss|Phase2")
	float TeleportWindupScaleMultiplier = 0.65f; // mesh CRUSHES inward before snapping — distinct from outward swell

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Boss|Phase2")
	FLinearColor TeleportWindupColor = FLinearColor(5.0f, 0.0f, 0.0f, 1.0f); // pure HDR red — different tell from magenta SpecialWindup

	UPROPERTY(BlueprintReadOnly, Category = "State")
	float CurrentHealth = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "State")
	float MaxHealthCached = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "State")
	EEnemyAIState AIState = EEnemyAIState::Approach;

	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bIsFrenzied = false;

	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bIsAlerted = false;

	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bIsPhase2 = false;

	UPROPERTY(BlueprintReadOnly, Category = "State")
	int32 FlankSlotIndex = 0;

	// True only when the most recent lunge actually landed a hit. Recover backpedals
	// after a connect (feels less zombie-like) but presses back IN after a whiff so
	// a stationary player can't farm air-punches.
	bool bLungeConnected = false;

	float DodgeCooldownTimer = 0.0f;
	FVector DodgeMoveDir = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWidgetComponent> HPBarWidget;

private:
	void Die();
	void BurnTick();
	void VenomTick();

	// --- Floor-3 hero-copy (melee mirror) ---
	bool bHeroCopy = false;
	// Snapshot of the player's RunDeck at fight start — deck only (no relics/blessings).
	TArray<FPassiveSlot> HeroCopyDeck;
	void AttachHeroCopyBranch();
	void NormalizeHeldWeaponTransform();
	void ApplyHeroCopyDeckOnHit(class ALoopedCharacter* Player);
	float GetHeroCopyOutgoingDamageMult() const;
	FTimerHandle HeldWeaponNormalizeTimerHandle;

	UPROPERTY(EditAnywhere, Category = "LOOPED|HeroCopy")
	FName HeldWeaponSocket = FName(TEXT("hand_r"));

	UPROPERTY(EditAnywhere, Category = "LOOPED|HeroCopy")
	FVector HeldWeaponWorldScale = FVector(0.3375f, 0.4f, 0.4f);

	UPROPERTY(EditAnywhere, Category = "LOOPED|HeroCopy")
	FVector HeldWeaponGripOffset = FVector(12.0f, 10.6f, 26.6f);

	// Death sequence: Die() fires gameplay events immediately, then plays a death anim or ragdolls;
	// FinishDeathHide() hides the corpse once that finishes. bIsDying guards against double-death.
	bool bIsDying = false;
	FTimerHandle DeathHideTimerHandle;
	void FinishDeathHide();
	// Plays a random DeathAnims entry as a DefaultSlot montage. Returns its play length, or 0 if none.
	// (Visual-kit enemies bypass the montage: their Death sequence plays single-node instead.)
	float PlayDeathAnim();

	// --- Visual-kit runtime (see ApplyEnemyVisual) ---
	FName AutoVisualRow() const;
	void UpdateVisualAnim();
	void PlayVisualAnim(class UAnimSequence* Anim, bool bLoop);
	void UpdateFloatBob(float DeltaTime);

	UPROPERTY() TObjectPtr<class UDataTable> VisualTable;
	UPROPERTY() TObjectPtr<class UAnimSequence> VisIdle;
	UPROPERTY() TObjectPtr<class UAnimSequence> VisWalk;
	UPROPERTY() TObjectPtr<class UAnimSequence> VisRun;
	UPROPERTY() TObjectPtr<class UAnimSequence> VisAttack;
	UPROPERTY() TObjectPtr<class UAnimSequence> VisCast;
	UPROPERTY() TObjectPtr<class UAnimSequence> VisDeath;
	UPROPERTY() TObjectPtr<class UAnimSequence> VisDodge;
	UPROPERTY() TObjectPtr<class UAnimSequence> CurrentVisAnim;
	bool bVisualDriven = false;
	bool bFloats = false;
	float FloatBobAmplitude = 8.0f;
	float FloatBobSpeed = 1.8f;
	float FloatBobRestZ = 0.0f;
	FName CurrentVisualRow = NAME_None;
	// Captured at BeginPlay so Respawn() can un-ragdoll the mesh back to its rig pose.
	FTransform MeshDefaultRelativeTransform;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynMaterial;

	// Base materials force-assigned in BeginPlay (by bIsRanged) so the dynamic instance ALWAYS has
	// the BaseColor/EmissiveColor params RefreshColor drives — the color system can't silently break
	// from a lost Blueprint material assignment again.
	UPROPERTY()
	TObjectPtr<UMaterialInterface> MeleeMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> RangedMaterial;

	// Glow OVERLAY driven on the VISIBLE skeletal mesh (CharacterMesh0). The old VisualMesh cube is
	// invisible, so all color feedback (attack tells + red hit-flash) now renders via this additive
	// overlay on top of the Manny mesh. Black = no glow (pure Manny).
	UPROPERTY()
	TObjectPtr<UMaterialInterface> OverlayBaseMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> OverlayMID;

	// Combat SFX (loaded by path in the ctor; play at the enemy's location).
	UPROPERTY()
	TObjectPtr<class USoundBase> EnemyHurtSound;

	UPROPERTY()
	TObjectPtr<class USoundBase> RangedShotSound;

	// Attack animations played (as a DefaultSlot montage) when the enemy winds up / fires. Editable
	// per-enemy in the editor — point these at a swapped-in model's attack anims (Manny defaults).
	UPROPERTY(EditAnywhere, Category = "LOOPED|Anim")
	TArray<TObjectPtr<class UAnimSequence>> AttackAnims;

	// Death animation(s). If set, one plays on death and the corpse is hidden after it finishes.
	// If EMPTY, the enemy RAGDOLLS instead (asset-free) — drop in a model's death anim later to swap.
	UPROPERTY(EditAnywhere, Category = "LOOPED|Anim")
	TArray<TObjectPtr<class UAnimSequence>> DeathAnims;

	// How long the corpse lingers before being hidden when there's no death anim (ragdoll path).
	UPROPERTY(EditAnywhere, Category = "LOOPED|Anim")
	float DeathHideFallbackDelay = 1.6f;

	// Projectile element/class for this enemy's ranged shots (a DT_ProjectileElements RowName):
	// drives the orb color + damage scaling. "None" = plain grey orb. Set per enemy class in editor.
	UPROPERTY(EditAnywhere, Category = "LOOPED|Combat")
	FName ProjectileElement = FName(TEXT("None"));

	// Brief white "hit flash" when the enemy takes damage — clear "I connected" feedback.
	FTimerHandle HitFlashTimerHandle;
	bool bHitFlashing = false;
	void EndHitFlash();

	FTimerHandle BurnTimerHandle;
	float BurnDamagePerTick = 0.0f;
	int32 BurnTicksRemaining = 0;

	FTimerHandle MeleeTimerHandle;
	FTimerHandle RangedTimerHandle;
	bool bCanMeleeHit = true;
	// Hybrid state: true while a hybrid ranged enemy is currently in its melee (close-range) mode.
	bool bMeleeEngaged = false;
	void MeleeCooldownReset();
	void RangedAttack();

	// Spawns one visible, dodgeable AEnemyProjectile aimed at the player (shared by normal ranged
	// fire AND the boss Skyfall burst). Damage is passed in so the boss can scale it.
	void SpawnProjectileAtPlayer(float Damage);

	FTimerHandle VenomTimerHandle;
	float VenomDamagePerTick = 0.0f;
	int32 VenomTicksRemaining = 0;
	float VenomSlowMultiplier = 1.0f;
	float BaseSpeed = 0.0f;

	// --- Frostbite (chill → freeze) state ---
	// Stacks required to freeze and how long unrefreshed stacks last. Editable per enemy so heavy
	// archetypes can be made freeze-resistant later without code.
	UPROPERTY(EditAnywhere, Category = "LOOPED|Combat")
	int32 ChillStacksToFreeze = 3;

	UPROPERTY(EditAnywhere, Category = "LOOPED|Combat")
	float ChillStackDecaySeconds = 4.0f;

	int32 ChillStacks = 0;
	bool bFrozen = false;
	FTimerHandle FreezeTimerHandle;
	FTimerHandle ChillDecayTimerHandle;
	void EndFreeze();
	void DecayChillStacks();

	// --- Wave-2 state ---
	// Card "HuntersMark": set by the player's first hit; marked enemies take bonus damage from
	// EVERYTHING (chains, DoT ticks, pops) because all of it routes through TakeDamageFromPlayer.
	bool bMarkedByHunter = false;

	// Curse "Haunted": this corpse already rose once — it stays down the second time.
	bool bHauntedRespawnUsed = false;
	FTimerHandle HauntedRiseTimerHandle;
	void HauntedRise();

	// Curse "Feverdream": scales windup/telegraph/special-windup durations (1.0 without it).
	float GetTellDurationMult() const;

	// AI state machine
	float StateTimer = 0.0f;
	float PathRefreshTimer = 0.0f;
	FVector CurrentNavTarget = FVector::ZeroVector;
	bool bHasNavTarget = false;

	// Jump throttle (seconds since last jump-when-above)
	float JumpCooldownTimer = 0.0f;

	// Stuck detection
	FVector LastStuckCheckLocation = FVector::ZeroVector;
	float StuckTimer = 0.0f;

	// Guard post home (spawn location, captured in BeginPlay).
	FVector GuardHome = FVector::ZeroVector;

	// Kite strafe direction (-1 or +1)
	float KiteStrafeSign = 1.0f;
	float KiteStrafeTimer = 0.0f;

	void TickMelee(float DeltaTime, APawn* Player);
	void TickRanged(float DeltaTime, APawn* Player);
	void TickBossSpecial(float DeltaTime, APawn* Player);
	void BeginSpecialAttack();
	void OnSpecialWindupFinished();
	void FireSpecialBurstShot();

	// Returns next path point to move toward (or destination if no path / nav)
	FVector ComputeNavTarget(const FVector& Destination);

	// True if a world point sits inside an ACTIVE ElementalHazard volume (floor-height XY).
	// Elevated platforms above the burn strip are NOT treated as hazard.
	bool IsPointInActiveHazard(const FVector& Point) const;

	// If Destination (or the step toward it) crosses an active hazard, return a stop-short
	// point on this side of the hazard edge. Applies to melee AND ranged (Sahar softlock).
	// Empty hazards → Destination unchanged.
	FVector AvoidHazardDestination(const FVector& Destination) const;

	// Line of sight from VisualMesh height to the target's location
	bool HasLineOfSightTo(AActor* Target) const;

	void EnterState(EEnemyAIState NewState);
	void SetColorTemporary(const FLinearColor& Color);
	void RestoreBaseColor();

	// Single source of truth — pick the right color/speed each frame based on stacked states
	void RefreshColor();
	void RefreshSpeed();

	// Flash the enemy white briefly to register a player hit. Auto-reverts via RefreshColor.
	void FlashHit();

	// Play a random attack animation as a DefaultSlot montage on the skeletal mesh (windup / fire).
	void PlayAttackAnim();

	// Creative behaviors
	FVector ComputeFlankPoint(const APawn* Player) const;
	// Flanker: prefer a point behind the player (not a ring slot around them).
	FVector ComputeBehindFlankPoint(const APawn* Player) const;
	void CheckEnterFrenzy();
	void ApplyMeleeRoleDefaults(EMeleeRole ResolvedRole);
	EMeleeRole ResolveMeleeRole(FName RowName, const FEnemyTypeData& Row) const;
	bool TryBeginDodge(APawn* Player, bool bFromPlayerHit);
	void TickDodge(float DeltaTime, APawn* Player);
	// Ranged: pick a nearby nav point that has clear LOS to the player (and isn't on a hazard).
	FVector FindLOSHoldPoint(const APawn* Player) const;
	// bRoomWide = true alerts EVERY living enemy regardless of radius (curse "Bounty" on kills).
	void AlertNearbyEnemies(AActor* Cause, bool bRoomWide = false);
	void BeginTelegraph();
	void FireTelegraphedShot();
	void DeathPop();
	void EndAlerted();

	FTimerHandle TelegraphTimerHandle;
	FTimerHandle AlertTimerHandle;
	FTimerHandle SpecialTimerHandle;
	FTimerHandle SpecialBurstTimerHandle;
	bool bTelegraphInFlight = false;

	// Boss timing
	float NextSpecialReadyTime = 0.0f;   // world time when special is allowed to trigger again
	int32 SpecialShotsRemaining = 0;

	// Phase 2 teleport
	float NextTeleportReadyTime = 0.0f;
	FTimerHandle TeleportTimerHandle;
	FTimerHandle MeleeModeTimerHandle;
	void CheckEnterPhase2();
	void BeginTeleport();
	void ExecuteTeleport();
	void EndPostTeleportMeleeMode();
};
