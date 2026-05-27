#include "SloMoManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Looped.h"

void USloMoManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogLoopedSloMo, Display, TEXT("SloMoManager initialized (target dilation: %.2f, ramp: %.2fs)"),
		TargetTimeDilation, TransitionDuration);
}

void USloMoManager::RequestSloMo(ESloMoTrigger Trigger, float Duration)
{
	ActiveTriggers.Add(Trigger);

	UE_LOG(LogLoopedSloMo, Display, TEXT("SloMo requested: %s (duration: %.1f, active triggers: %d)"),
		*UEnum::GetValueAsString(Trigger), Duration, ActiveTriggers.Num());

	if (!bIsSlowMo)
	{
		EnterSloMo();
	}

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

	if (ActiveTriggers.Num() == 0 && bIsSlowMo)
	{
		ExitSloMo();
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
		UpdateTimeDilation(FMath::Lerp(TransitionStartDilation, TargetTimeDilation, Alpha));
		if (RawAlpha >= 1.0f)
		{
			UpdateTimeDilation(TargetTimeDilation);
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

	// Exempt player pawn so camera and input remain at full speed
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->CustomTimeDilation = 1.0f / SafeDilation;
		}
	}
}
