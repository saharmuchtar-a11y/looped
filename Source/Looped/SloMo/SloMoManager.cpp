#include "SloMoManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Core/LoopedGameInstance.h"
#include "Looped.h"

void USloMoManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogLoopedSloMo, Display, TEXT("SloMoManager initialized (target dilation: %.2f, ramp: %.2fs)"),
		TargetTimeDilation, TransitionDuration);
}

void USloMoManager::RecomputeProfile()
{
	// Strongest slow (lowest dilation) among active triggers picks the profile. Every trigger
	// except the Q skill keeps the classic menu profile: hard slow, player fully exempt.
	CurrentTargetDilation = TargetTimeDilation;
	CurrentExemptFraction = 1.0f;
	if (ActiveTriggers.Num() == 1 && ActiveTriggers.Contains(ESloMoTrigger::SkillDodge))
	{
		CurrentTargetDilation = SkillDilation;
		CurrentExemptFraction = SkillPlayerExemptFraction;
	}

	// Blessing "Hourglass": bullet-time bites deeper this run (world dilation x0.8 by default).
	if (const UWorld* World = GetWorld())
	{
		if (const ULoopedGameInstance* GI = World->GetGameInstance<ULoopedGameInstance>())
		{
			if (GI->HasRunArtifact(TEXT("Hourglass")))
			{
				CurrentTargetDilation = FMath::Max(0.02f, CurrentTargetDilation * GI->HourglassDilationMult);
			}
		}
	}
}

void USloMoManager::RequestSloMo(ESloMoTrigger Trigger, float Duration)
{
	ActiveTriggers.Add(Trigger);
	RecomputeProfile();

	UE_LOG(LogLoopedSloMo, Display, TEXT("SloMo requested: %s (duration: %.1f, active triggers: %d)"),
		*UEnum::GetValueAsString(Trigger), Duration, ActiveTriggers.Num());

	// Always (re)ramp: entering fresh, or re-targeting because the profile changed
	// (e.g. the monitor opened while the Q skill was already slowing time).
	EnterSloMo();

	if (Duration > 0.0f)
	{
		FTimerHandle& Handle = TriggerTimers.FindOrAdd(Trigger);
		GetWorld()->GetTimerManager().SetTimer(Handle, [this, Trigger]()
		{
			ReleaseSloMo(Trigger);
		}, Duration, false);
	}
}

void USloMoManager::ReleaseSloMo(ESloMoTrigger Trigger)
{
	ActiveTriggers.Remove(Trigger);

	if (FTimerHandle* Handle = TriggerTimers.Find(Trigger))
	{
		GetWorld()->GetTimerManager().ClearTimer(*Handle);
		TriggerTimers.Remove(Trigger);
	}

	UE_LOG(LogLoopedSloMo, Display, TEXT("SloMo released: %s (remaining triggers: %d)"),
		*UEnum::GetValueAsString(Trigger), ActiveTriggers.Num());

	RecomputeProfile();
	if (ActiveTriggers.Num() == 0 && bIsSlowMo)
	{
		ExitSloMo();
	}
	else if (bIsSlowMo)
	{
		EnterSloMo(); // remaining triggers may want a different profile — re-ramp to it
	}
}

float USloMoManager::GetCurrentTimeDilation() const
{
	if (UWorld* World = GetWorld())
	{
		AWorldSettings* WS = World->GetWorldSettings();
		return WS ? WS->TimeDilation : 1.0f;
	}
	return 1.0f;
}

void USloMoManager::EnterSloMo()
{
	bIsSlowMo = true;
	bTransitioningToSlow = true;
	TransitionStartRealTime = GetWorld()->GetRealTimeSeconds();
	// Store the dilation we're ramping FROM (may be mid-ramp if rapidly toggled)
	TransitionStartDilation = GetCurrentTimeDilation();

	GetWorld()->GetTimerManager().ClearTimer(TransitionTimerHandle);
	// 0.002s game-time interval — at 0.15x dilation this fires every ~13ms real-time, smooth enough
	GetWorld()->GetTimerManager().SetTimer(TransitionTimerHandle, this,
		&USloMoManager::StepTransition, 0.002f, true);

	OnSloMoStateChanged.Broadcast(true);
	UE_LOG(LogLoopedSloMo, Display, TEXT("ENTERING slo-mo (from %.2f → %.2f over %.2fs)"),
		TransitionStartDilation, TargetTimeDilation, TransitionDuration);
}

void USloMoManager::ExitSloMo()
{
	bIsSlowMo = false;
	bTransitioningToSlow = false;
	TransitionStartRealTime = GetWorld()->GetRealTimeSeconds();
	TransitionStartDilation = GetCurrentTimeDilation();

	GetWorld()->GetTimerManager().ClearTimer(TransitionTimerHandle);
	GetWorld()->GetTimerManager().SetTimer(TransitionTimerHandle, this,
		&USloMoManager::StepTransition, 0.002f, true);

	OnSloMoStateChanged.Broadcast(false);
	UE_LOG(LogLoopedSloMo, Display, TEXT("EXITING slo-mo (from %.2f → 1.0 over %.2fs)"),
		TransitionStartDilation, TransitionDuration);
}

void USloMoManager::StepTransition()
{
	const float RealElapsed = GetWorld()->GetRealTimeSeconds() - TransitionStartRealTime;
	const float RawAlpha = FMath::Clamp(RealElapsed / TransitionDuration, 0.0f, 1.0f);

	// Smooth step curve for a more cinematic feel
	const float Alpha = RawAlpha * RawAlpha * (3.0f - 2.0f * RawAlpha);

	if (bTransitioningToSlow)
	{
		UpdateTimeDilation(FMath::Lerp(TransitionStartDilation, CurrentTargetDilation, Alpha));
		if (RawAlpha >= 1.0f)
		{
			UpdateTimeDilation(CurrentTargetDilation);
			GetWorld()->GetTimerManager().ClearTimer(TransitionTimerHandle);
			UE_LOG(LogLoopedSloMo, Verbose, TEXT("Slo-mo entry ramp complete"));
		}
	}
	else
	{
		UpdateTimeDilation(FMath::Lerp(TransitionStartDilation, 1.0f, Alpha));
		if (RawAlpha >= 1.0f)
		{
			UpdateTimeDilation(1.0f);
			GetWorld()->GetTimerManager().ClearTimer(TransitionTimerHandle);
			UE_LOG(LogLoopedSloMo, Verbose, TEXT("Slo-mo exit ramp complete"));
		}
	}
}

void USloMoManager::UpdateTimeDilation(float Dilation)
{
	const float SafeDilation = FMath::Max(Dilation, SMALL_NUMBER);
	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), SafeDilation);

	// Fractional player exemption: net player speed = Dilation^(1 - Fraction).
	// Fraction 1 (menus) = 1/Dilation = fully exempt, the original behavior; the Q skill's
	// ~0.66 leaves the player slowed too, just less than the world. Smooth through the ramp.
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->CustomTimeDilation = FMath::Pow(1.0f / SafeDilation, CurrentExemptFraction);
		}
	}
}
