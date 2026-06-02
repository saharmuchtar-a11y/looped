#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Data/EnemyData.h"
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

	UFUNCTION(BlueprintPure, Category = "LOOPED|Enemy")
	bool IsBoss() const { return bIsBoss; }

	UFUNCTION(BlueprintImplementableEvent, Category = "LOOPED|Enemy")
	void BP_OnDied();

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

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<ULoopedAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	// Held rifle, socketed to the skeletal mesh hand. Shown only for ranged enemies (set in BeginPlay).
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Components")
	TObjectPtr<UStaticMeshComponent> RifleMesh;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Melee")
	float LungeSpeedMultiplier = 2.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Melee")
	float RecoverDuration = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Melee")
	float MeleeContactRange = 140.0f;

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWidgetComponent> HPBarWidget;

private:
	void Die();
	void BurnTick();
	void VenomTick();

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynMaterial;

	FTimerHandle BurnTimerHandle;
	float BurnDamagePerTick = 0.0f;
	int32 BurnTicksRemaining = 0;

	FTimerHandle MeleeTimerHandle;
	FTimerHandle RangedTimerHandle;
	bool bCanMeleeHit = true;
	void MeleeCooldownReset();
	void RangedAttack();

	FTimerHandle VenomTimerHandle;
	float VenomDamagePerTick = 0.0f;
	int32 VenomTicksRemaining = 0;
	float VenomSlowMultiplier = 1.0f;
	float BaseSpeed = 0.0f;

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

	// Visual mesh base scale (captured at BeginPlay) so we can swell/shrink during windup.
	FVector BaseMeshScale = FVector::OneVector;

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

	// Line of sight from VisualMesh height to the target's location
	bool HasLineOfSightTo(AActor* Target) const;

	void EnterState(EEnemyAIState NewState);
	void SetColorTemporary(const FLinearColor& Color);
	void RestoreBaseColor();

	// Single source of truth — pick the right color/speed each frame based on stacked states
	void RefreshColor();
	void RefreshSpeed();

	// Creative behaviors
	FVector ComputeFlankPoint(const APawn* Player) const;
	void CheckEnterFrenzy();
	void AlertNearbyEnemies(AActor* Cause);
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
