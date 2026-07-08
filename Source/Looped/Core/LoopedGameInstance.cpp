#include "LoopedGameInstance.h"
#include "Looped.h"
#include "Data/LoopedSaveData.h"
#include "Kismet/GameplayStatics.h"
#include "Player/LoopedCharacter.h"
#include "Core/PortalActor.h"
#include "EngineUtils.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"

static FName CanonCardId(FName Id); // defined below — folds "VenomStrike" alias onto "Venom"

void ULoopedGameInstance::Init()
{
	Super::Init();
	LoadOrCreateStats();

	// Single source of card data — loaded by path (asset created/populated in refactor Step 2).
	CardTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_PassiveCards.DT_PassiveCards"));
	if (!CardTable)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Deck] Card DataTable not found at /Game/Data/DT_PassiveCards — create + populate it (Step 2)."));
	}

	// Data-driven room routing — pools + run layout (asset DA_RunRouting). Same load pattern.
	RoutingConfig = LoadObject<URunRoutingConfig>(nullptr, TEXT("/Game/Data/DA_RunRouting.DA_RunRouting"));
	if (!RoutingConfig)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Routing] DA_RunRouting not found at /Game/Data/DA_RunRouting — run path will be empty (routing falls back to Hub)."));
	}

	// Data-driven room TYPES for the choice-fork system (Step 2). Same load-by-path pattern.
	RoomTypeTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_RoomTypes.DT_RoomTypes"));
	if (!RoomTypeTable)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Routing] DT_RoomTypes not found at /Game/Data/DT_RoomTypes — fork choices unavailable until it's created + populated."));
	}

	// Artifact/relic definitions — loaded by path, same pattern as the card table.
	ArtifactTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Artifacts.DT_Artifacts"));
	if (!ArtifactTable)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Artifacts] DT_Artifacts not found at /Game/Data/DT_Artifacts — create + populate it (Content Overhaul Step 2)."));
	}

	// Missions & hints for the monitor's guidance panel — same load-by-path pattern.
	MissionTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Missions.DT_Missions"));
	if (!MissionTable)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Missions] DT_Missions not found at /Game/Data/DT_Missions — the monitor guidance panel stays empty until it's created + populated."));
	}

	UE_LOG(LogLoopedCore, Display, TEXT("LoopedGameInstance initialized. Hunter Rank: %d"), HunterRank);
#if !UE_BUILD_SHIPPING
	// [GameInstance] INIT on-screen message removed for a clean screen (UE_LOG above kept for dev).
#endif

	// Re-apply the persisted motion-blur choice (the cvar resets every launch).
	SetMotionBlurEnabled(IsMotionBlurEnabled());
}

void ULoopedGameInstance::SetMotionBlurEnabled(bool bEnabled)
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality")))
	{
		CVar->Set(bEnabled ? 4 : 0, ECVF_SetByGameSetting);
	}
	GConfig->SetBool(TEXT("LoopedSettings"), TEXT("bMotionBlur"), bEnabled, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
	UE_LOG(LogLoopedCore, Display, TEXT("[Settings] Motion blur -> %s"), bEnabled ? TEXT("ON") : TEXT("OFF"));
}

bool ULoopedGameInstance::IsMotionBlurEnabled() const
{
	bool bEnabled = true; // engine default
	GConfig->GetBool(TEXT("LoopedSettings"), TEXT("bMotionBlur"), bEnabled, GGameUserSettingsIni);
	return bEnabled;
}

TArray<FText> ULoopedGameInstance::GetKeyBindLines() const
{
	// Read the live mappings so this panel can never drift from IMC_Default. Keys for the
	// same action collapse onto one line (Move -> W/A/S/D).
	TArray<FText> Out;
	const UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/IMC_Default.IMC_Default"));
	if (!IMC) return Out;

	TArray<FString> Order;                 // first-seen action order (the IMC reads top-down)
	TMap<FString, TArray<FString>> Keys;   // action display name -> key names
	for (const FEnhancedActionKeyMapping& M : IMC->GetMappings())
	{
		if (!M.Action) continue;
		FString Name = M.Action->GetName();
		Name.RemoveFromStart(TEXT("IA_"));
		if (!Keys.Contains(Name)) { Order.Add(Name); }
		Keys.FindOrAdd(Name).Add(M.Key.GetDisplayName(false).ToString());
	}
	for (const FString& Name : Order)
	{
		Out.Add(FText::FromString(FString::Printf(TEXT("%s  -  %s"), *Name, *FString::Join(Keys[Name], TEXT(" / ")))));
	}
	return Out;
}

void ULoopedGameInstance::Shutdown()
{
	SaveStats();
	Super::Shutdown();
}

void ULoopedGameInstance::LoadOrCreateStats()
{
	if (UGameplayStatics::DoesSaveGameExist(ULoopedSaveData::SlotName, ULoopedSaveData::UserIndex))
	{
		USaveGame* Loaded = UGameplayStatics::LoadGameFromSlot(ULoopedSaveData::SlotName, ULoopedSaveData::UserIndex);
		Stats = Cast<ULoopedSaveData>(Loaded);
	}
	if (!Stats)
	{
		// Auto-create fresh — covers missing OR corrupted save (cast returns null on either).
		Stats = Cast<ULoopedSaveData>(UGameplayStatics::CreateSaveGameObject(ULoopedSaveData::StaticClass()));
		UE_LOG(LogLoopedCore, Display, TEXT("[Stats] Created fresh save (no existing save or load failed)."));
		SaveStats();
	}

	// --- Legacy-save migration (tutorial arc): the Arm Monitor became Orin's gift. Any save
	// with real progress predates the tutorial — grant Orin silently so the monitor keeps
	// working and the Hub (which now routes Orin-less saves to L_Tutorial) never bounces them.
	if (!Stats->OwnedArtifacts.Contains(TEXT("Orin")) &&
		(Stats->RunsCompleted > 0 || Stats->BossKills > 0 || Stats->RoomClears > 0 ||
		 Stats->Echoes > 0 || Stats->UnlockedCards.Num() > 0 || Stats->OwnedArtifacts.Num() > 0))
	{
		Stats->OwnedArtifacts.Add(TEXT("Orin"));
		SaveStats();
		UE_LOG(LogLoopedCore, Display, TEXT("[Stats] Legacy save migrated — Orin (Arm Monitor) granted."));
	}

	// Consistency repair: Mira owned but fragments short (a cheat-grant predating the
	// grant-time top-up) — reform her whole so the fragment mission/hint retire.
	if (Stats->OwnedArtifacts.Contains(TEXT("Mira")) && Stats->MiraFragments < MiraFragmentsNeeded)
	{
		Stats->MiraFragments = MiraFragmentsNeeded;
		SaveStats();
		UE_LOG(LogLoopedCore, Display, TEXT("[Stats] Save repaired — Mira owned, fragments topped to %d."), MiraFragmentsNeeded);
	}
	UE_LOG(LogLoopedCore, Display, TEXT("[Stats] Loaded: BossDeaths=%d BossKills=%d Runs=%d Playtime=%.0fs Unlocks=%d"),
		Stats->BossDeaths, Stats->BossKills, Stats->RunsCompleted, Stats->TotalPlaytimeSeconds, Stats->UnlockedCards.Num());
}

void ULoopedGameInstance::SaveStats()
{
	if (!Stats) return;
	UGameplayStatics::SaveGameToSlot(Stats, ULoopedSaveData::SlotName, ULoopedSaveData::UserIndex);
}

void ULoopedGameInstance::AddBossDeath()
{
	if (!Stats) return;
	Stats->BossDeaths++;
	Stats->TotalDeaths++;
	ApplyChronometerOnDeath();
	EvaluateUnlocksAfterStatChange();
	SaveStats();
}

void ULoopedGameInstance::AddBossKill(float KillTimeSeconds)
{
	if (!Stats) return;
	Stats->BossKills++;
	if (Stats->FastestBossKillSeconds <= 0.0f || (KillTimeSeconds > 0.0f && KillTimeSeconds < Stats->FastestBossKillSeconds))
	{
		Stats->FastestBossKillSeconds = KillTimeSeconds;
	}
	EvaluateUnlocksAfterStatChange();
	SaveStats();
}

void ULoopedGameInstance::AddPlayerDeath()
{
	if (!Stats) return;
	Stats->TotalDeaths++;
	ApplyChronometerOnDeath();
	EvaluateUnlocksAfterStatChange(); // Chronometer itself unlocks at 10 deaths
	SaveStats();
}

void ULoopedGameInstance::ApplyChronometerOnDeath()
{
	// Relic "Chronometer": death converts a cut of your run Shards into permanent Echoes —
	// the loop pities the persistent. Consumes the Shards so the conversion can't double-dip.
	if (!HasArtifact(TEXT("Chronometer")) || CurrentRunState.Shards <= 0) return;
	const int32 Converted = FMath::FloorToInt(CurrentRunState.Shards * ChronometerConvertFraction);
	if (Converted > 0)
	{
		AddEchoes(Converted);
		UE_LOG(LogLoopedCore, Display, TEXT("[Relic] Chronometer converted %d Shards -> %d Echoes on death."),
			CurrentRunState.Shards, Converted);
	}
	CurrentRunState.Shards = 0;
}

void ULoopedGameInstance::AddRoomClear()
{
	if (!Stats) return;
	Stats->RoomClears++;
	EvaluateUnlocksAfterStatChange();
	SaveStats();
}

void ULoopedGameInstance::AddEnemyKill()
{
	if (!Stats) return;
	Stats->TotalEnemyKills++;
	EvaluateUnlocksAfterStatChange(); // cheap set-checks; Echo pops mid-fight at 150

	// --- Single on-kill hook: kill-triggered blessings + cards fire here (once per enemy death) ---
	// Blessing "LootersEye": flat Shards per kill (distinct from Greedring's multiplier).
	if (HasRunArtifact(TEXT("LootersEye")))
	{
		AddShards(LootersEyeShardsPerKill);
	}

	ALoopedCharacter* Player = nullptr;
	if (UWorld* World = GetWorld())
	{
		Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(World, 0));
	}
	if (!Player) return;

	// Blessing "LeechFang": a sliver of life per kill.
	if (HasRunArtifact(TEXT("LeechFang")))
	{
		Player->HealPlayer(LeechFangHealPerKill);
	}
	// Card "Reaper": kills feed you (bigger heal than Lifesteal's per-hit trickle, rarer trigger).
	if (const FPassiveCardLevel* ReaperLv = GetEffectiveLevelData(TEXT("Reaper")))
	{
		if (ReaperLv->HealAmount > 0.0f) Player->HealPlayer(ReaperLv->HealAmount);
	}
	// Card "Momentum": kills grant a brief stacking speed burst (the Character owns the timer).
	if (const FPassiveCardLevel* MomLv = GetEffectiveLevelData(TEXT("Momentum")))
	{
		if (MomLv->Fraction > 0.0f) Player->TriggerMomentumBurst(MomLv->Fraction);
	}
}

void ULoopedGameInstance::AddRunCompleted()
{
	if (!Stats) return;
	Stats->RunsCompleted++;

	// Time the full run (run start -> now). RunStartRealTime is 0 if we somehow finished without a
	// tracked start (e.g. direct-load testing) — skip timing in that case.
	if (RunStartRealTime > 0.0)
	{
		const float RunSeconds = (float)(FPlatformTime::Seconds() - RunStartRealTime);
		if (RunSeconds > 0.0f && (Stats->FastestRunSeconds <= 0.0f || RunSeconds < Stats->FastestRunSeconds))
		{
			Stats->FastestRunSeconds = RunSeconds;
		}
		// Speed-run reward: finish under the threshold -> unlock the Speed card.
		if (RunSeconds > 0.0f && RunSeconds <= FastRunUnlockSeconds)
		{
			UE_LOG(LogLoopedCore, Display, TEXT("[Stats] Fast run %.1fs <= %.0fs -> unlocking Speed."), RunSeconds, FastRunUnlockSeconds);
			UnlockCard(FName(TEXT("Speed")));
		}
		RunStartRealTime = 0.0; // consumed
	}

	EvaluateUnlocksAfterStatChange();
	SaveStats();
}

void ULoopedGameInstance::AddPlaytime(float Seconds)
{
	if (!Stats || Seconds <= 0.0f) return;
	Stats->TotalPlaytimeSeconds += Seconds;
	// No SaveStats here — playtime save happens via SaveStats called on other events.
}

void ULoopedGameInstance::AddDamageDealt(float Amount)
{
	if (!Stats || Amount <= 0.0f) return;
	Stats->TotalDamageDealt += Amount;
}

void ULoopedGameInstance::AddDamageTaken(float Amount)
{
	if (!Stats || Amount <= 0.0f) return;
	Stats->TotalDamageTaken += Amount;
}

void ULoopedGameInstance::RecordPerkPicked(FName PerkName, int32 NewLevel)
{
	if (!Stats || PerkName.IsNone()) return;
	int32& Count = Stats->PerksPickedByName.FindOrAdd(PerkName);
	Count++;
	int32& Highest = Stats->HighestPerkLevel.FindOrAdd(PerkName);
	Highest = FMath::Max(Highest, NewLevel);
	SaveStats();
}

void ULoopedGameInstance::UnlockCard(FName CardName)
{
	if (!Stats || CardName.IsNone()) return;
	if (Stats->UnlockedCards.Contains(CardName)) return;
	Stats->UnlockedCards.Add(CardName);
	SaveStats();
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		const FString Msg = FString::Printf(TEXT("=== NEW CARD UNLOCKED: %s ==="), *CardName.ToString());
		// Big, long-lived, unique-key so it stands out from the per-frame debug spam.
		GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Green, Msg, true, FVector2D(2.0f, 2.0f));
	}
#endif
	UE_LOG(LogLoopedCore, Display, TEXT("[Stats] Unlocked card: %s"), *CardName.ToString());
}

bool ULoopedGameInstance::IsCardUnlocked(FName CardName) const
{
	if (!Stats) return false;
	return Stats->UnlockedCards.Contains(CardName);
}

bool ULoopedGameInstance::HasPerkEverMaxed(FName PerkName) const
{
	if (!Stats) return false;
	const FName Canon = (PerkName == TEXT("VenomStrike")) ? FName(TEXT("Venom")) : PerkName;
	return Stats->PerksEverMaxed.Contains(Canon);
}

int32 ULoopedGameInstance::GetMaxedPerkCount() const
{
	return Stats ? Stats->PerksEverMaxed.Num() : 0;
}

void ULoopedGameInstance::GrantArtifact(FName ArtifactName)
{
	if (!Stats || ArtifactName.IsNone()) return;
	if (Stats->OwnedArtifacts.Contains(ArtifactName)) return;
	Stats->OwnedArtifacts.Add(ArtifactName);
	// Mira reforms WHOLE: however she arrives (5th fragment or a cheat/dev grant), the fragment
	// counter reads complete — else the fragment mission + hint sit stuck at N/5 forever.
	if (ArtifactName == TEXT("Mira") && Stats->MiraFragments < MiraFragmentsNeeded)
	{
		Stats->MiraFragments = MiraFragmentsNeeded;
	}
	EvaluateUnlocksAfterStatChange(); // companions gate cards (Lysa -> Frostbite) — fire NOW
	SaveStats();
	UE_LOG(LogLoopedCore, Display, TEXT("[Artifact] Granted: %s (owned: %d)"), *ArtifactName.ToString(), Stats->OwnedArtifacts.Num());
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		// Player-facing naming: permanent items are RELICS (run items are Blessings).
		const FArtifactData* Row = FindArtifactRow(ArtifactName);
		const FString Desc = Row ? Row->Description.ToString() : FString();
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Cyan,
			FString::Printf(TEXT("=== RELIC ACQUIRED: %s ===%s%s"), *ArtifactName.ToString(),
				Desc.IsEmpty() ? TEXT("") : TEXT("\n"), *Desc), true, FVector2D(2.0f, 2.0f));
	}
#endif

	// Refresh the on-screen artifacts list + re-apply movement mods (Wing changes gravity live).
	if (UWorld* World = GetWorld())
	{
		if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(World, 0)))
		{
			Player->UpdateArtifactHUD();
			Player->ApplyMovementMods();
		}
	}
}

bool ULoopedGameInstance::HasArtifact(FName ArtifactName) const
{
	return Stats ? Stats->OwnedArtifacts.Contains(ArtifactName) : false;
}

FText ULoopedGameInstance::GetOwnedArtifactsLabel() const
{
	if (!Stats || Stats->OwnedArtifacts.Num() == 0) return FText::GetEmpty();
	FString Out;
	int32 Listed = 0;
	const int32 MaxListed = 8; // the corner is a glance — long collections collapse (Sahar 2026-07-07)
	int32 Skipped = 0;
	for (const FName& A : Stats->OwnedArtifacts)
	{
		// Companions aren't relics — they stand in the Hub in person (Sahar 2026-07-07).
		static const FName Companions[] = {
			FName("Orin"), FName("Lysa"), FName("Brann"), FName("Serin"), FName("Mira") };
		bool bCompanion = false;
		for (const FName& C : Companions) { if (A == C) { bCompanion = true; break; } }
		if (bCompanion) { ++Skipped; continue; }

		if (Listed >= MaxListed) break;
		if (Listed > 0) Out += TEXT("\n");
		// NAMES ONLY — full descriptions overflowed the monitor corner (Sahar 2026-07-03); the
		// First Hunter's codex RELICS page carries the complete "what it does" text.
		const FArtifactData* Row = FindArtifactRow(A);
		Out += (Row && !Row->DisplayName.IsEmpty()) ? Row->DisplayName.ToString() : A.ToString();
		++Listed;
	}
	const int32 Overflow = Stats->OwnedArtifacts.Num() - Skipped - Listed;
	if (Overflow > 0)
	{
		Out += FString::Printf(TEXT("\n+%d more"), Overflow);
	}
	return FText::FromString(Out);
}

TArray<FName> ULoopedGameInstance::GetOwnedArtifactNames() const
{
	return Stats ? Stats->OwnedArtifacts.Array() : TArray<FName>();
}

bool ULoopedGameInstance::SellPermanentArtifact(FName ArtifactName)
{
	if (!Stats || ArtifactName.IsNone()) return false;
	if (Stats->OwnedArtifacts.Remove(ArtifactName) == 0) return false;
	// Blacklist so milestone auto-grants (EvaluateUnlocksAfterStatChange / the Iron Will gate)
	// can't hand the sold relic straight back — the sale must MEAN something.
	Stats->SoldRelics.AddUnique(ArtifactName);
	SaveStats();
	UE_LOG(LogLoopedCore, Display, TEXT("[Artifact] SOLD to Vorr: %s"), *ArtifactName.ToString());
	return true;
}

FText ULoopedGameInstance::GetCurseDescription(FName Curse) const
{
	// One-liners matching the live magnitudes — shown on pickup and in the monitor so a curse is
	// never a mystery word. Keep in sync with the Curse* tunables above.
	if (Curse == TEXT("Tithe"))     return FText::FromString(TEXT("currency gains are halved"));
	if (Curse == TEXT("Frailty"))   return FText::FromString(TEXT("you take 50% more damage"));
	if (Curse == TEXT("Bloodless")) return FText::FromString(TEXT("you cannot heal"));
	if (Curse == TEXT("Leaden"))    return FText::FromString(TEXT("slower and heavier"));
	if (Curse == TEXT("Decay"))     return FText::FromString(TEXT("your HP drains over time"));
	if (Curse == TEXT("Marked"))    return FText::FromString(TEXT("enemies move much faster"));
	if (Curse == TEXT("Brittle"))   return FText::FromString(TEXT("your cards act one level weaker"));
	if (Curse == TEXT("Scarcity"))  return FText::FromString(TEXT("one fewer card choice"));
	if (Curse == TEXT("Dimmed"))    return FText::FromString(TEXT("the world goes dark around you"));
	if (Curse == TEXT("Weakness"))  return FText::FromString(TEXT("you deal 25% less damage"));
	if (Curse == TEXT("Volatile"))  return FText::FromString(TEXT("dying enemies detonate against you too"));
	if (Curse == TEXT("Static"))    return FText::FromString(TEXT("your cards only fire every other hit"));
	if (Curse == TEXT("Toll"))          return FText::FromString(TEXT("every door demands payment"));
	if (Curse == TEXT("DullBlade"))     return FText::FromString(TEXT("your strikes cannot crit"));
	if (Curse == TEXT("ShatteredSight"))return FText::FromString(TEXT("enemy health is hidden from you"));
	if (Curse == TEXT("Bounty"))        return FText::FromString(TEXT("every kill alerts the whole room"));
	if (Curse == TEXT("Cowardice"))     return FText::FromString(TEXT("enemies frenzy at half health"));
	if (Curse == TEXT("Feverdream"))    return FText::FromString(TEXT("enemy attacks come faster"));
	if (Curse == TEXT("Extortion"))     return FText::FromString(TEXT("Vorr's prices climb steeply"));
	if (Curse == TEXT("Amnesia"))       return FText::FromString(TEXT("the cards refuse to explain themselves"));
	if (Curse == TEXT("Swarm"))         return FText::FromString(TEXT("the loop sends one more"));
	if (Curse == TEXT("Haunted"))       return FText::FromString(TEXT("one foe per room refuses to stay dead"));
	if (Curse == TEXT("Fogbound"))      return FText::FromString(TEXT("the portals hide where they lead"));
	return FText::GetEmpty();
}

float ULoopedGameInstance::GetCurseSeverityMult() const
{
	return HasArtifact(TEXT("IronWill")) ? IronWillSeverityMult : 1.0f;
}

void ULoopedGameInstance::ApplyRoomEntryToll()
{
	if (!HasCurse(TEXT("Toll"))) return;

	if (SpendShards(CurseTollShards))
	{
		UE_LOG(LogLoopedCore, Display, TEXT("[Curse] Toll paid: %d Shards."), CurseTollShards);
		return;
	}
	// Can't afford the toll — it takes flesh instead (Iron Will softens it).
	ResetShards();
	if (UWorld* World = GetWorld())
	{
		if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(World, 0)))
		{
			const float HPCost = CurseTollHPFallback * GetCurseSeverityMult();
			Player->TakeDamageFromEnemy(HPCost);
			Player->ShowCenterMessage(FText::FromString(TEXT("the Toll takes flesh instead")), 2.0f);
			UE_LOG(LogLoopedCore, Display, TEXT("[Curse] Toll unaffordable — took %.0f HP."), HPCost);
		}
	}
}

void ULoopedGameInstance::RecordEnemySeen(FName EnemyTypeRow)
{
	if (!Stats || EnemyTypeRow.IsNone() || EnemyTypeRow == TEXT("Random")) return;
	if (Stats->SeenEnemyTypes.Contains(EnemyTypeRow)) return; // only the FIRST meeting writes
	Stats->SeenEnemyTypes.Add(EnemyTypeRow);
	SaveStats();
	UE_LOG(LogLoopedCore, Display, TEXT("[Discovery] Enemy seen: %s"), *EnemyTypeRow.ToString());
}

bool ULoopedGameInstance::IsEnemySeen(FName EnemyTypeRow) const
{
	return Stats && Stats->SeenEnemyTypes.Contains(EnemyTypeRow);
}

bool ULoopedGameInstance::IsCurseSeen(FName Curse) const
{
	return Stats && Stats->SeenCurses.Contains(Curse);
}

bool ULoopedGameInstance::IsBlessingSeen(FName ArtifactId) const
{
	return Stats && Stats->SeenBlessings.Contains(ArtifactId);
}

void ULoopedGameInstance::GrantArtifactCheat(FName ArtifactName)
{
	UE_LOG(LogLoopedCore, Display, TEXT("[Artifact] Cheat grant: %s"), *ArtifactName.ToString());
	GrantArtifact(ArtifactName);
}

int32 ULoopedGameInstance::GetEchoes() const
{
	return Stats ? Stats->Echoes : 0;
}

void ULoopedGameInstance::AddEchoes(int32 Amount)
{
	if (!Stats || Amount <= 0) return;
	// Artifact "GoldBar": permanently boosts Echoes income.
	if (Stats->OwnedArtifacts.Contains(TEXT("GoldBar")))
	{
		Amount = FMath::CeilToInt(Amount * GoldBarEchoesMultiplier);
	}
	// Run relics (Artifact.EchoGain) multiply Echo income too.
	Amount = FMath::CeilToInt(Amount * GetArtifactEchoMult());
	// Curse "Tithe": the Broker skims your earnings this run.
	if (HasCurse(TEXT("Tithe")))
	{
		Amount = FMath::FloorToInt(Amount * CurseTitheMultiplier);
		if (Amount <= 0) return;
	}
	Stats->Echoes += Amount;
	SaveStats();
	UE_LOG(LogLoopedCore, Display, TEXT("[Currency] +%d Echoes (total %d)"), Amount, Stats->Echoes);
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor(255, 215, 0),
			FString::Printf(TEXT("+%d Echoes"), Amount));
	}
#endif
}

bool ULoopedGameInstance::SpendEchoes(int32 Amount)
{
	if (!Stats || Amount <= 0) return false;
	if (Stats->Echoes < Amount) return false;
	Stats->Echoes -= Amount;
	SaveStats();
	UE_LOG(LogLoopedCore, Display, TEXT("[Currency] -%d Echoes (remaining %d)"), Amount, Stats->Echoes);
	return true;
}

void ULoopedGameInstance::AddEchoesCheat(int32 Amount)
{
	UE_LOG(LogLoopedCore, Display, TEXT("[Currency] Cheat add: %d Echoes"), Amount);
	AddEchoes(Amount);
}

void ULoopedGameInstance::AddPendingNextRunCard(FName Card)
{
	if (!Stats || Card.IsNone()) return;
	if (Stats->PendingNextRunCards.Contains(Card)) return; // no duplicate boons
	Stats->PendingNextRunCards.Add(Card);
	SaveStats();
	UE_LOG(LogLoopedCore, Display, TEXT("[Merchant] Queued '%s' for next run."), *Card.ToString());
}

bool ULoopedGameInstance::IsCardPendingNextRun(FName Card) const
{
	return Stats && Stats->PendingNextRunCards.Contains(Card);
}

TArray<FName> ULoopedGameInstance::GetPendingNextRunCards() const
{
	return Stats ? Stats->PendingNextRunCards : TArray<FName>();
}

void ULoopedGameInstance::QueueNextRunCurse(FName Curse)
{
	if (!Stats || Curse.IsNone()) return;
	Stats->PendingNextRunCurse = Curse;
	SaveStats();
	UE_LOG(LogLoopedCore, Display, TEXT("[Ransom] Curse '%s' queued for the next run."), *Curse.ToString());
}

FName ULoopedGameInstance::TakePendingNextRunCurse()
{
	if (!Stats || Stats->PendingNextRunCurse.IsNone()) return NAME_None;
	const FName Owed = Stats->PendingNextRunCurse;
	Stats->PendingNextRunCurse = NAME_None;
	SaveStats();
	return Owed;
}

bool ULoopedGameInstance::IsMiraFragmentHuntActive() const
{
	return Stats
		&& Stats->OwnedArtifacts.Contains(TEXT("Serin"))
		&& !Stats->OwnedArtifacts.Contains(TEXT("Mira"))
		&& Stats->MiraFragments < MiraFragmentsNeeded
		&& !CurrentRunState.bMiraFragmentTaken;
}

int32 ULoopedGameInstance::GetMiraFragments() const
{
	return Stats ? Stats->MiraFragments : 0;
}

int32 ULoopedGameInstance::CollectMiraFragment()
{
	if (!Stats) return 0;
	CurrentRunState.bMiraFragmentTaken = true; // one per run — the reset scatters her again
	Stats->MiraFragments = FMath::Min(Stats->MiraFragments + 1, MiraFragmentsNeeded);
	SaveStats();
	UE_LOG(LogLoopedCore, Display, TEXT("[Rescue] Mira fragment %d/%d collected."),
		Stats->MiraFragments, MiraFragmentsNeeded);
	if (Stats->MiraFragments >= MiraFragmentsNeeded)
	{
		GrantArtifact(TEXT("Mira")); // she reforms — hub NPC + the reroll relic go live
	}
	return Stats->MiraFragments;
}

void ULoopedGameInstance::ClearPendingNextRunCards()
{
	if (Stats && Stats->PendingNextRunCards.Num() > 0)
	{
		Stats->PendingNextRunCards.Empty();
		SaveStats();
	}
}

bool ULoopedGameInstance::RemovePendingNextRunCard(FName Card)
{
	if (!Stats || Stats->PendingNextRunCards.Remove(Card) == 0) return false;
	SaveStats();
	UE_LOG(LogLoopedCore, Display, TEXT("[Merchant] Queued boon '%s' bought back."), *Card.ToString());
	return true;
}

int32 ULoopedGameInstance::GetSkillGaugeRanks() const
{
	return Stats ? Stats->SkillGaugeRanks : 0;
}

float ULoopedGameInstance::GetSkillGaugeBonusSeconds() const
{
	return GetSkillGaugeRanks() * SkillGaugePerRankSeconds;
}

int32 ULoopedGameInstance::GrantSkillGaugeRank()
{
	if (!Stats || Stats->SkillGaugeRanks >= MaxSkillGaugeRanks) return 0;
	Stats->SkillGaugeRanks++;
	SaveStats();
	UE_LOG(LogLoopedCore, Display, TEXT("[Skill] Chrono-gauge rank %d/%d bought (+%.1fs each)."),
		Stats->SkillGaugeRanks, MaxSkillGaugeRanks, SkillGaugePerRankSeconds);
	return Stats->SkillGaugeRanks;
}

void ULoopedGameInstance::AddShards(int32 Amount)
{
	if (Amount <= 0) return;
	// Artifact "Greedring" etc.: run relics multiply shard income.
	Amount = FMath::FloorToInt(Amount * GetArtifactShardMult());
	if (Amount <= 0) return;
	// Curse "Tithe" skims per-run earnings too.
	if (HasCurse(TEXT("Tithe")))
	{
		Amount = FMath::FloorToInt(Amount * CurseTitheMultiplier);
		if (Amount <= 0) return;
	}
	CurrentRunState.Shards += Amount;
}

bool ULoopedGameInstance::SpendShards(int32 Amount)
{
	if (Amount <= 0 || CurrentRunState.Shards < Amount) return false;
	CurrentRunState.Shards -= Amount;
	// Lifetime spend tracker — gates the "VorrsMarker" relic. Saved lazily with other events.
	if (Stats)
	{
		Stats->TotalShardsSpent += Amount;
		EvaluateUnlocksAfterStatChange();
	}
	return true;
}

void ULoopedGameInstance::ResetShards()
{
	CurrentRunState.Shards = 0;
}

void ULoopedGameInstance::AddShardsCheat(int32 Amount)
{
	AddShards(Amount);
}

void ULoopedGameInstance::WipeSave()
{
	if (!Stats) return;
	Stats->Echoes = 0;
	Stats->PendingNextRunCards.Empty();
	Stats->bMerchantVaultUnlocked = false;
	Stats->PermanentCardChoiceBonus = 0;
	Stats->PermanentBonusMaxHP = 0;
	Stats->OwnedArtifacts.Empty();
	Stats->UnlockedCards.Empty();
	Stats->PerksEverMaxed.Empty();
	Stats->BossDeaths = 0;
	Stats->BossKills = 0;
	Stats->RunsCompleted = 0;
	Stats->TotalDeaths = 0;
	Stats->RoomClears = 0;
	Stats->TotalPlaytimeSeconds = 0.0f;
	Stats->FastestBossKillSeconds = 0.0f;
	Stats->FastestRunSeconds = 0.0f;
	Stats->TotalDamageDealt = 0.0f;
	Stats->TotalDamageTaken = 0.0f;
	Stats->PerksPickedByName.Empty();
	Stats->HighestPerkLevel.Empty();
	SaveStats();
	ResetPerks();

	UE_LOG(LogLoopedCore, Display, TEXT("[Dev] SAVE WIPED — fresh state."));
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red,
			TEXT("=== SAVE WIPED (DEV) ==="), true, FVector2D(2.0f, 2.0f));
	}
#endif

	// Refresh the live player view (artifacts HUD + movement mods reflect the wipe).
	if (UWorld* World = GetWorld())
	{
		if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(World, 0)))
		{
			Player->UpdateArtifactHUD();
			Player->ApplyMovementMods();
		}
	}
}

void ULoopedGameInstance::WipeSaveCheat()
{
	WipeSave();
}

// --- Curses ---

void ULoopedGameInstance::AddCurse(FName Curse)
{
	if (Curse.IsNone() || CurrentRunState.ActiveCurses.Contains(Curse)) return;

	// Blessing "Hexward": the first curse of the run shatters against the ward instead of landing.
	if (!CurrentRunState.bHexwardUsed && HasRunArtifact(TEXT("Hexward")))
	{
		CurrentRunState.bHexwardUsed = true;
		UE_LOG(LogLoopedCore, Display, TEXT("[Curse] Hexward deflected '%s'."), *Curse.ToString());
		if (UWorld* World = GetWorld())
		{
			if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(World, 0)))
			{
				Player->ShowCenterMessage(FText::FromString(TEXT("the Hexward shatters — the curse never lands")), 2.5f);
			}
		}
		return;
	}

	CurrentRunState.ActiveCurses.Add(Curse);
	UE_LOG(LogLoopedCore, Display, TEXT("[Curse] +%s (active: %d)"), *Curse.ToString(), CurrentRunState.ActiveCurses.Num());

	// Relic gate "IronWill": earned the hard way — by carrying three curses at once.
	if (Stats && !Stats->OwnedArtifacts.Contains(TEXT("IronWill")) &&
		!Stats->SoldRelics.Contains(TEXT("IronWill")) && CurrentRunState.ActiveCurses.Num() >= 3)
	{
		GrantArtifact(TEXT("IronWill"));
	}

	// Discovery: suffering a curse unlocks its codex entry.
	if (Stats && !Stats->SeenCurses.Contains(Curse))
	{
		Stats->SeenCurses.Add(Curse);
		SaveStats();
	}
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Purple,
			FString::Printf(TEXT("=== CURSED: %s ==="), *Curse.ToString()), true, FVector2D(1.6f, 1.6f));
	}
#endif
	// Re-apply live effects so movement curses (Leaden) take hold immediately.
	if (UWorld* World = GetWorld())
	{
		if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(World, 0)))
		{
			Player->ApplyMovementMods();
		}
	}
}

void ULoopedGameInstance::RemoveCurse(FName Curse)
{
	if (CurrentRunState.ActiveCurses.Remove(Curse) > 0)
	{
		if (UWorld* World = GetWorld())
		{
			if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(World, 0)))
			{
				Player->ApplyMovementMods();
			}
		}
	}
}

bool ULoopedGameInstance::HasCurse(FName Curse) const
{
	return CurrentRunState.ActiveCurses.Contains(Curse);
}

void ULoopedGameInstance::ClearCurses()
{
	if (CurrentRunState.ActiveCurses.Num() == 0) return;
	CurrentRunState.ActiveCurses.Empty();
	UE_LOG(LogLoopedCore, Display, TEXT("[Curse] All curses cleared (new run)."));
}

TArray<FName> ULoopedGameInstance::GetActiveCurses() const
{
	return CurrentRunState.ActiveCurses.Array();
}

int32 ULoopedGameInstance::RollEventCategory()
{
	// Rolling pity odds: fight/treasure shares grow per "?" that doesn't produce them, reset on hit.
	const float FightShare    = FMath::Min(0.5f, EventFightBaseShare + EventFightPityStep * EventRoomsSinceFight);
	float TreasureShare = FMath::Min(0.4f, EventTreasureBaseShare + EventTreasurePityStep * EventRoomsSinceTreasure);
	// Blessing "VoidLens": the lens finds the loot — "?" rooms lean hard toward treasure.
	if (HasRunArtifact(TEXT("VoidLens")))
	{
		TreasureShare = FMath::Max(TreasureShare, VoidLensTreasureShare);
	}
	const float Roll = FMath::FRand();

	int32 Category = 0; // story event
	if (Roll < FightShare)
	{
		Category = 1;
		EventRoomsSinceFight = 0;
		EventRoomsSinceTreasure++;
	}
	else if (Roll < FightShare + TreasureShare)
	{
		Category = 2;
		EventRoomsSinceTreasure = 0;
		EventRoomsSinceFight++;
	}
	else
	{
		EventRoomsSinceFight++;
		EventRoomsSinceTreasure++;
	}
	UE_LOG(LogLoopedCore, Display, TEXT("[Event] Category roll=%.2f fight=%.2f treasure=%.2f -> %s"),
		Roll, FightShare, TreasureShare,
		Category == 1 ? TEXT("FIGHT") : Category == 2 ? TEXT("TREASURE") : TEXT("STORY"));
	return Category;
}

int32 ULoopedGameInstance::GetCardChoiceCount() const
{
	int32 Count = 3;
	if (Stats) Count += Stats->PermanentCardChoiceBonus; // permanent vault upgrade
	if (HasCurse(TEXT("Scarcity"))) Count -= 1;          // curse
	return FMath::Max(1, Count);
}

bool ULoopedGameInstance::IsPermanentVaultUnlocked() const
{
	return Stats && Stats->bMerchantVaultUnlocked;
}

void ULoopedGameInstance::GrantPermanentCardChoice()
{
	if (!Stats || HasPermanentCardChoice()) return;
	Stats->PermanentCardChoiceBonus += 1;
	SaveStats();
	UE_LOG(LogLoopedCore, Display, TEXT("[Vault] Permanent +1 card choice (now +%d)."), Stats->PermanentCardChoiceBonus);
}

bool ULoopedGameInstance::HasPermanentCardChoice() const
{
	return Stats && Stats->PermanentCardChoiceBonus > 0;
}

void ULoopedGameInstance::GrantPermanentMaxHP(int32 Amount)
{
	if (!Stats || Amount <= 0 || HasPermanentMaxHP()) return;
	Stats->PermanentBonusMaxHP += Amount;
	SaveStats();
	UE_LOG(LogLoopedCore, Display, TEXT("[Vault] Permanent +%d max HP (now +%d)."), Amount, Stats->PermanentBonusMaxHP);

	// Apply live so the player's max HP updates immediately.
	if (UWorld* World = GetWorld())
	{
		if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(World, 0)))
		{
			Player->ApplyMaxHPMod();
		}
	}
}

bool ULoopedGameInstance::HasPermanentMaxHP() const
{
	return Stats && Stats->PermanentBonusMaxHP > 0;
}

int32 ULoopedGameInstance::GetPermanentBonusMaxHP() const
{
	return Stats ? Stats->PermanentBonusMaxHP : 0;
}

void ULoopedGameInstance::GrantPermanentStartingShards(int32 Amount)
{
	if (!Stats || Amount <= 0 || HasPermanentStartingShards()) return;
	Stats->PermanentStartingShards = Amount;
	SaveStats();
	UE_LOG(LogLoopedCore, Display, TEXT("[Vault] Deep Pockets — runs now start with +%d Shards."), Amount);
}

bool ULoopedGameInstance::HasPermanentStartingShards() const
{
	return Stats && Stats->PermanentStartingShards > 0;
}

void ULoopedGameInstance::GrantPermanentStartingBlessing()
{
	if (!Stats || Stats->bPermanentStartingBlessing) return;
	Stats->bPermanentStartingBlessing = true;
	SaveStats();
	UE_LOG(LogLoopedCore, Display, TEXT("[Vault] Keepsake — runs now start with a random Blessing."));
}

bool ULoopedGameInstance::HasPermanentStartingBlessing() const
{
	return Stats && Stats->bPermanentStartingBlessing;
}

void ULoopedGameInstance::UnlockVaultCheat()
{
	if (!Stats) return;
	Stats->bMerchantVaultUnlocked = true;
	SaveStats();
	UE_LOG(LogLoopedCore, Display, TEXT("[Vault] Force-unlocked (cheat)."));
}

void ULoopedGameInstance::AddCurseCheat(FName Curse)
{
	AddCurse(Curse);
}

void ULoopedGameInstance::ClearCursesCheat()
{
	ClearCurses();
}

void ULoopedGameInstance::EvaluateUnlocksAfterStatChange()
{
	if (!Stats) return;
	// Hardcoded unlock rules v1 — keep this list small + boring until we hit ~6 unlocks.
	// Then it earns a data table. UnlockCard() is idempotent (no-op if already owned).
	auto TryUnlock = [this](const TCHAR* Card, bool bCondition)
	{
		if (bCondition) { UnlockCard(FName(Card)); }
	};

	// Gated cards
	TryUnlock(TEXT("MaxHP"),      Stats->BossDeaths >= 1);   // first boss death (earliest — safety net)
	TryUnlock(TEXT("Lifesteal"),  Stats->RoomClears >= 10);  // ~a couple full runs in
	TryUnlock(TEXT("Frostbite"),  Stats->OwnedArtifacts.Contains(TEXT("Lysa"))); // Lysa's rescue gift
	TryUnlock(TEXT("Echo"),       Stats->TotalEnemyKills >= 150);                // a hunter's rhythm
	// (ChainSpark retired 2026-07-03 — Echo owns the "hits do more" slot. The BossKills>=5
	// milestone is free for a future unlock; ApplyChainSparkEffect stays: Capacitor reuses it.)

	// Floors-arc batch (gated 2026-07-09 — shipped ungated and leaked into first-run offers;
	// only BurnShot/Venom/Deadeye are from-start, Sahar's call). Ladder approved by Sahar.
	TryUnlock(TEXT("Momentum"),    Stats->RoomClears >= 15);
	TryUnlock(TEXT("Rupture"),     Stats->TotalEnemyKills >= 50);
	TryUnlock(TEXT("HuntersMark"), Stats->BossKills >= 2);
	TryUnlock(TEXT("Overcharge"),  Stats->RoomClears >= 25);
	TryUnlock(TEXT("Executioner"), Stats->TotalEnemyKills >= 300);
	TryUnlock(TEXT("GlassCannon"), Stats->BossKills >= 3);

	// Epic cards.
	// Speed is unlocked ONLY by a fast run (see AddRunCompleted / FastRunUnlockSeconds) — Sahar's call.
	// Gravity is NOT unlocked here — it's gated behind completing the Hub parkour
	// (ACardUnlockTrigger at the summit calls UnlockCard("Gravity")).

	// --- Wave-2 permanent relic gates ---
	// GrantArtifact is idempotent, shows its own "RELIC ACQUIRED" toast, and re-enters this
	// function after adding to the owned set — so nested grants terminate safely.
	auto TryGrantRelic = [this](const TCHAR* Relic, bool bCondition)
	{
		// Sold-to-Vorr relics stay sold — the milestone can't hand them straight back.
		if (bCondition && !Stats->OwnedArtifacts.Contains(Relic) && !Stats->SoldRelics.Contains(Relic))
		{
			GrantArtifact(FName(Relic));
		}
	};
	TryGrantRelic(TEXT("ScarLedger"),     Stats->TotalDamageDealt >= ScarLedgerDamageStep);
	TryGrantRelic(TEXT("TrophyFang"),     Stats->RoomClears >= 50);
	TryGrantRelic(TEXT("Chronometer"),    Stats->TotalDeaths >= 10);
	TryGrantRelic(TEXT("Crown"),          Stats->BossKills >= 3);
	TryGrantRelic(TEXT("FirstFrequency"), GetMaxedPerkCount() >= 3);
	TryGrantRelic(TEXT("VoidCompass"),    Stats->PerksEverMaxed.Contains(TEXT("Gravity")));
	TryGrantRelic(TEXT("VorrsMarker"),    Stats->TotalShardsSpent >= 100);
	// (IronWill is granted in AddCurse — 3 curses at once; BrokersSeal is a vault purchase.)

	// Permanent merchant vault opens on the first boss kill — a milestone reward that
	// keeps players invested (Vorr only deals seriously with proven Hunters).
	if (!Stats->bMerchantVaultUnlocked && Stats->BossKills >= 1)
	{
		Stats->bMerchantVaultUnlocked = true;
		UE_LOG(LogLoopedCore, Display, TEXT("[Vault] Permanent vault UNLOCKED (first boss kill)."));
#if !UE_BUILD_SHIPPING
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Green,
				TEXT("=== VORR'S VAULT UNLOCKED ==="), true, FVector2D(2.0f, 2.0f));
		}
#endif
	}
}

void ULoopedGameInstance::StartDeathScreen()
{
	bDeathScreenActive = true;
	DeathScreenStartedAtRealTime = FPlatformTime::Seconds();
}

void ULoopedGameInstance::EndDeathScreen()
{
	bDeathScreenActive = false;
}

float ULoopedGameInstance::GetDeathScreenRemainingSeconds() const
{
	if (!bDeathScreenActive) return 0.0f;
	const float Elapsed = (float)(FPlatformTime::Seconds() - DeathScreenStartedAtRealTime);
	return FMath::Max(0.0f, DeathScreenDuration - Elapsed);
}

void ULoopedGameInstance::DumpStatsToScreen()
{
#if !UE_BUILD_SHIPPING
	if (!Stats || !GEngine) return;
	const FString Line1 = FString::Printf(TEXT("[STATS] BossKills=%d BossDeaths=%d Runs=%d RoomClears=%d"),
		Stats->BossKills, Stats->BossDeaths, Stats->RunsCompleted, Stats->RoomClears);
	const FString Line2 = FString::Printf(TEXT("[STATS] DmgDealt=%.0f DmgTaken=%.0f Playtime=%.0fs FastestBoss=%.1fs"),
		Stats->TotalDamageDealt, Stats->TotalDamageTaken, Stats->TotalPlaytimeSeconds, Stats->FastestBossKillSeconds);
	const FString Line3 = FString::Printf(TEXT("[STATS] Unlocked: %d cards | Perks ever maxed: %d"),
		Stats->UnlockedCards.Num(), Stats->PerksEverMaxed.Num());
	GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Cyan, Line1);
	GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Cyan, Line2);
	GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Cyan, Line3);
#endif
}

void ULoopedGameInstance::AddRunXP(int32 XP)
{
	CurrentXP += XP;
	UE_LOG(LogLoopedCore, Display, TEXT("Run XP added: %d (Total: %d)"), XP, CurrentXP);
	// POC: rank progression deferred to post-prototype
}

int32 ULoopedGameInstance::IncrementPerkLevel(FName PerkName)
{
	const FName Id = CanonCardId(PerkName);
	const FPassiveCardData* Row = FindCardRow(Id);
	if (!Row)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[GI] IncrementPerkLevel: no card row '%s' (table unset?)"), *Id.ToString());
		return 0;
	}

	const int32 NewLevel = AddOrLevelCard(Id); // writes RunDeck — single source of truth
	// Level-up kept in the log only — no on-screen print (keeps the player's screen pristine).
	UE_LOG(LogLoopedCore, Display, TEXT("[GI] Card '%s' -> Lv %d / %d"), *Id.ToString(), NewLevel, Row->MaxLevel);

	// First-time-ever MAX tracking (persistent) — now driven by the row's MaxLevel.
	// Generic by id, so any future card is tracked. Gates secret unlocks (e.g. first max Gravity).
	if (Stats && NewLevel >= Row->MaxLevel && !Stats->PerksEverMaxed.Contains(Id))
	{
		Stats->PerksEverMaxed.Add(Id);
		SaveStats();
		UE_LOG(LogLoopedCore, Display, TEXT("[Stats] FIRST MAX: %s (%d perks maxed)"),
			*Id.ToString(), Stats->PerksEverMaxed.Num());
#if !UE_BUILD_SHIPPING
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Yellow,
				FString::Printf(TEXT("=== FIRST MAX: %s ==="), *Id.ToString()), true, FVector2D(2.0f, 2.0f));
		}
#endif
	}
	return NewLevel;
}

int32 ULoopedGameInstance::GetPerkLevel(FName PerkName) const
{
	return GetCardLevel(PerkName); // reads RunDeck
}

bool ULoopedGameInstance::IsPerkAtMax(FName PerkName) const
{
	const FPassiveCardData* Row = FindCardRow(PerkName);
	if (!Row) return false;
	return GetCardLevel(PerkName) >= Row->MaxLevel;
}

TArray<FName> ULoopedGameInstance::GetEligibleCards(const TArray<FName>& /*InAll*/) const
{
	// Now fully data-driven: the offer pool IS the DataTable. Adding a card = add a row,
	// nothing else. (InAll kept for signature compatibility with existing BP callers but
	// ignored — the table is the single source of truth.) A row is eligible when it isn't
	// maxed and either doesn't require an unlock or has been unlocked.
	TArray<FName> Out;
	if (!CardTable) return Out;
	for (const FName& RowName : CardTable->GetRowNames())
	{
		const FPassiveCardData* Row = CardTable->FindRow<FPassiveCardData>(RowName, TEXT("GetEligibleCards"), false);
		if (!Row) continue;
		if (GetCardLevel(RowName) >= Row->MaxLevel) continue;
		if (Row->bRequiresUnlock && !IsCardUnlocked(RowName)) continue;
		Out.Add(RowName);
	}
	return Out;
}

TArray<FName> ULoopedGameInstance::GetCardOffer(const TArray<FName>& InPool) const
{
	const TArray<FName> Eligible = GetEligibleCards(InPool);
	const int32 N = GetCardChoiceCount();
	TArray<FName> Out;
	Out.Reserve(N);
	if (Eligible.Num() == 0) return Out;

	auto WeightOf = [this](const FName& Card) -> int32
	{
		switch (GetPerkRarity(Card))
		{
			case ECardRarity::Common: return OfferWeightCommon;
			case ECardRarity::Rare:   return OfferWeightRare;
			case ECardRarity::Epic:   return OfferWeightEpic;
			default:                  return 0; // Cursed never rolls into a reward
		}
	};

	// Rarity-weighted draw WITHOUT replacement: each pick removes the card from the bag, so
	// the offer stays distinct while the pool allows it.
	TArray<FName> Bag = Eligible;
	while (Out.Num() < N && Bag.Num() > 0)
	{
		int32 Total = 0;
		for (const FName& C : Bag) Total += WeightOf(C);
		if (Total <= 0) break; // only zero-weight cards left

		int32 Roll = FMath::RandRange(1, Total);
		for (int32 i = 0; i < Bag.Num(); ++i)
		{
			Roll -= WeightOf(Bag[i]);
			if (Roll <= 0)
			{
				Out.Add(Bag[i]);
				Bag.RemoveAt(i);
				break;
			}
		}
	}

	// Fill every slot the widget can READ (4) — indices past N are never displayed
	// (ConfigureExtra hides those rows), but the BP reads offer[3] unconditionally and a
	// 3-entry array throws "index 3 of length 3" runtime errors. Repeats also beat "None"
	// cards on screen when the eligible pool runs small.
	// NOTE: copy the repeat BY VALUE before Add — Out.Add(Out[i]) crashes when Add reallocs
	// the buffer out from under the element reference (2026-07-03 mid-fight editor crash).
	for (int32 i = 0; Out.Num() > 0 && Out.Num() < FMath::Max(N, 4); ++i)
	{
		const FName Repeat = Out[i % Out.Num()];
		Out.Add(Repeat);
	}
	return Out;
}

FText ULoopedGameInstance::GetPerkCardLabel(FName PerkName) const
{
	const FPassiveCardData* Row = FindCardRow(PerkName);
	if (!Row) return FText::FromName(PerkName);

	const int32 Current = GetCardLevel(PerkName);
	const int32 Cap = Row->MaxLevel;
	const FString Name = Row->DisplayName.IsEmpty() ? PerkName.ToString() : Row->DisplayName.ToString();

	// Two-row label (no dash): row 1 = NAME [NEW/MAX], row 2 = (Lv x/n) — RARITY.
	FString Top, Bottom;
	if (Current <= 0)        { Top = FString::Printf(TEXT("%s NEW"), *Name); Bottom = FString::Printf(TEXT("(Lv 1/%d)"), Cap); }
	else if (Current >= Cap) { Top = FString::Printf(TEXT("%s MAX"), *Name); Bottom = FString::Printf(TEXT("(Lv %d/%d)"), Cap, Cap); }
	else                     { Top = Name;                                   Bottom = FString::Printf(TEXT("(Lv %d/%d)"), Current + 1, Cap); }
	Bottom += FString::Printf(TEXT(" — %s"), GetRarityWord(Row->Rarity));
	return FText::FromString(Top + TEXT("\n") + Bottom);
}

FText ULoopedGameInstance::GetCardDraftDescription(FName CardId) const
{
	// Curse "Amnesia": the monitor can't decode the frequencies — draft blind.
	if (HasCurse(TEXT("Amnesia")))
	{
		return FText::FromString(TEXT("...the frequency resists analysis..."));
	}
	const FPassiveCardData* Row = FindCardRow(CardId);
	return Row ? Row->Description : FText::GetEmpty();
}

UTexture2D* ULoopedGameInstance::GetCardIcon(FName CardId) const
{
	const FPassiveCardData* Row = FindCardRow(CardId);
	if (!Row || Row->CardIcon.IsNull()) return nullptr;
	return Row->CardIcon.LoadSynchronous();
}

ECardRarity ULoopedGameInstance::GetPerkRarity(FName PerkName) const
{
	const FPassiveCardData* Row = FindCardRow(PerkName);
	return Row ? Row->Rarity : ECardRarity::Common;
}

// Player-facing tier word (shared by the card label + shop lines).
const TCHAR* GetRarityWord(ECardRarity Rarity)
{
	switch (Rarity)
	{
		case ECardRarity::Rare:   return TEXT("RARE");
		case ECardRarity::Epic:   return TEXT("EPIC");
		case ECardRarity::Cursed: return TEXT("CURSED");
		default:                  return TEXT("COMMON");
	}
}

FLinearColor ULoopedGameInstance::GetRarityColor(ECardRarity Rarity)
{
	switch (Rarity)
	{
		case ECardRarity::Rare:   return FLinearColor(0.30f, 0.65f, 1.00f); // azure
		case ECardRarity::Epic:   return FLinearColor(0.80f, 0.40f, 1.00f); // violet
		case ECardRarity::Cursed: return FLinearColor(1.00f, 0.25f, 0.25f); // blood
		default:                  return FLinearColor(0.78f, 0.80f, 0.82f); // Common silver
	}
}

TArray<FName> ULoopedGameInstance::GetAllCardIds() const
{
	return CardTable ? CardTable->GetRowNames() : TArray<FName>();
}

bool ULoopedGameInstance::IsCardGated(FName CardId) const
{
	const FPassiveCardData* Row = FindCardRow(CardId);
	return Row && Row->bRequiresUnlock;
}

FText ULoopedGameInstance::GetCardDisplayName(FName CardId) const
{
	const FPassiveCardData* Row = FindCardRow(CardId);
	return (Row && !Row->DisplayName.IsEmpty()) ? Row->DisplayName : FText::FromName(CardId);
}

FLinearColor ULoopedGameInstance::GetPerkColor(FName PerkName) const
{
	const FPassiveCardData* Row = FindCardRow(PerkName);
	return Row ? Row->CardColor : FLinearColor::White;
}

// Canonical card id — folds the legacy "VenomStrike" alias onto "Venom".
static FName CanonCardId(FName Id)
{
	return (Id == TEXT("VenomStrike")) ? FName(TEXT("Venom")) : Id;
}

const FPassiveCardData* ULoopedGameInstance::FindCardRow(FName CardId) const
{
	if (!CardTable) return nullptr;
	return CardTable->FindRow<FPassiveCardData>(CanonCardId(CardId), TEXT("FindCardRow"), /*bWarnIfMissing*/false);
}

const FPassiveCardLevel* ULoopedGameInstance::GetEffectiveLevelData(FName CardId) const
{
	const int32 Level = GetCardLevel(CardId);
	if (Level <= 0) return nullptr;
	const FPassiveCardData* Row = FindCardRow(CardId);
	if (!Row) return nullptr;
	// Curse "Brittle": every card acts one level weaker (min 1).
	const int32 Eff = HasCurse(TEXT("Brittle")) ? FMath::Max(1, Level - 1) : Level;
	return Row->Levels.IsValidIndex(Eff - 1) ? &Row->Levels[Eff - 1] : nullptr;
}

int32 ULoopedGameInstance::GetCardLevel(FName CardId) const
{
	const FName Id = CanonCardId(CardId);
	for (const FPassiveSlot& S : RunDeck)
	{
		if (S.CardRowName == Id) return S.Level;
	}
	return 0;
}

int32 ULoopedGameInstance::AddOrLevelCard(FName CardId)
{
	const FName Id = CanonCardId(CardId);
	const FPassiveCardData* Row = FindCardRow(Id);
	const int32 MaxLevel = Row ? Row->MaxLevel : 5; // fallback if table/row missing

	for (FPassiveSlot& S : RunDeck)
	{
		if (S.CardRowName == Id)
		{
			S.Level = FMath::Min(S.Level + 1, MaxLevel);
			return S.Level;
		}
	}

	// New card. Cap distinct equipped cards at the deck size (mirrors PassiveStack MAX_PASSIVE_SLOTS=6).
	if (RunDeck.Num() >= 6)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Deck] RunDeck full — '%s' not added"), *Id.ToString());
		return 0;
	}
	FPassiveSlot NewSlot;
	NewSlot.CardRowName = Id;
	NewSlot.Level = 1;
	if (Row) NewSlot.CachedData = *Row;
	RunDeck.Add(NewSlot);
	return 1;
}

void ULoopedGameInstance::ClearRunDeck()
{
	RunDeck.Reset();
}

void ULoopedGameInstance::ResetRunState()
{
	// One call wipes everything run-scoped: the active build (deck) and the run state struct
	// (health flag, shards, curses). Echoes / artifacts / unlocks live in the save and survive.
	ClearRunDeck();
	CurrentRunState = FRunState();
	CurrentRoomPicks = 0; // treasure pick budget is per-room, but clear it on new-run too
	// Roll a fresh room order for the new run (also resets CurrentRunPath + CurrentPathIndex).
	GenerateRunPath();
	UE_LOG(LogLoopedCore, Display, TEXT("[GI] Run state reset (deck + health + shards + curses + path) — new run."));
}

void ULoopedGameInstance::GenerateRunPath()
{
	CurrentRunPath.Reset();
	CurrentPathIndex = -1;

	if (!RoutingConfig || RoutingConfig->RunLayout.Num() == 0)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Routing] No RoutingConfig / empty RunLayout — run path is empty (will fall back to Hub)."));
		return;
	}

	// Resolve the pool a slot draws from (empty when None/unknown).
	auto PoolFor = [this](ERoomPool P) -> const TArray<FName>&
	{
		static const TArray<FName> EmptyPool;
		switch (P)
		{
			case ERoomPool::EarlyCombat: return RoutingConfig->EarlyCombatPool;
			case ERoomPool::LateCombat:  return RoutingConfig->LateCombatPool;
			case ERoomPool::Treasure:    return RoutingConfig->TreasurePool;
			default:                     return EmptyPool;
		}
	};

	// Random draw with the approved no-back-to-back-repeat protection.
	auto PickFromPool = [](const TArray<FName>& Pool, FName PrevLevel) -> FName
	{
		if (Pool.Num() == 0) return NAME_None;
		if (Pool.Num() == 1) return Pool[0];
		FName Chosen = NAME_None;
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			Chosen = Pool[FMath::RandRange(0, Pool.Num() - 1)];
			if (Chosen != PrevLevel) break; // avoid repeating the immediately previous level
		}
		return Chosen;
	};

	FName PrevLevel = NAME_None;
	int32 Index = 1;
	for (const FRunSlotRule& Rule : RoutingConfig->RunLayout)
	{
		const FName Level = (Rule.Pool == ERoomPool::None)
			? Rule.FixedLevel
			: PickFromPool(PoolFor(Rule.Pool), PrevLevel);

		if (Level.IsNone())
		{
			UE_LOG(LogLoopedCore, Warning, TEXT("[Routing] Slot resolved to no level (empty pool / unset FixedLevel) — skipped."));
			continue;
		}

		FRoomNode Node;
		Node.LevelName = Level;
		Node.Type = Rule.Type;
		Node.RoomIndex = Index++;
		CurrentRunPath.Add(Node);
		PrevLevel = Level;
	}

	// Enforce one explicit Treasure room at a fixed path position (config-driven). Overwrites
	// whatever generated there with a Treasure node drawn from the TreasurePool — guarantees a
	// reliable treasure beat every run. Skipped gracefully if disabled / pool empty / index OOB.
	const int32 TIdx = RoutingConfig->TreasureSlotIndex;
	if (CurrentRunPath.IsValidIndex(TIdx))
	{
		if (RoutingConfig->TreasurePool.Num() > 0)
		{
			const FName TreasureLevel = PickFromPool(RoutingConfig->TreasurePool, NAME_None);
			if (!TreasureLevel.IsNone())
			{
				CurrentRunPath[TIdx].LevelName = TreasureLevel;
				CurrentRunPath[TIdx].Type = ERoomType::Treasure;
				UE_LOG(LogLoopedCore, Display, TEXT("[Routing] Treasure slot enforced at index %d -> %s."),
					TIdx, *TreasureLevel.ToString());
			}
		}
		else
		{
			UE_LOG(LogLoopedCore, Warning, TEXT("[Routing] TreasureSlotIndex %d set but TreasurePool is empty — slot left as-is. Populate TreasurePool in DA_RunRouting."), TIdx);
		}
	}

	UE_LOG(LogLoopedCore, Display, TEXT("[Routing] Generated run path: %d room(s)."), CurrentRunPath.Num());
}

FName ULoopedGameInstance::BeginRunPath()
{
	// Fork-based run: start fresh. The first room is always a Fight; every exit after is a 2-way
	// choice (GenerateForkChoices). CurrentRunPath now records rooms as they're VISITED, not a
	// pre-baked plan — so GetCurrentRoomNode()/IsInRunRoom() keep working for the GameMode.
	CurrentRunPath.Reset();
	CurrentPathIndex = -1;
	RunRoomsEntered = 0;
	CurrentFloor = 1; // every run starts the descent from the top

	// Stamp the run-start wall clock so AddRunCompleted can time the whole run (survives OpenLevel).
	RunStartRealTime = FPlatformTime::Seconds();

	// Vault metas kick in at the moment a run begins.
	if (Stats && Stats->PermanentStartingShards > 0)
	{
		AddShards(Stats->PermanentStartingShards);   // Deep Pockets
	}
	if (Stats && Stats->bPermanentStartingBlessing)
	{
		GrantRandomRunArtifact();                    // Keepsake
	}
	// Relic "FirstFrequency": the monitor boots with one frequency already decoded — a random
	// eligible card at level 1, free, every run.
	if (HasArtifact(TEXT("FirstFrequency")))
	{
		TArray<FName> Eligible = GetEligibleCards(TArray<FName>());
		if (Eligible.Num() > 0)
		{
			const FName Freebie = Eligible[FMath::RandRange(0, Eligible.Num() - 1)];
			AddOrLevelCard(Freebie);
			UE_LOG(LogLoopedCore, Display, TEXT("[Relic] First Frequency granted '%s' at run start."), *Freebie.ToString());
		}
	}

	// Fixed opener: if the routing config's first slot pins a level (Pool=None + FixedLevel), the
	// run ALWAYS starts there (L_Room1 = the gentle warm-up; it's in no pool, so it never repeats).
	if (RoutingConfig && RoutingConfig->RunLayout.Num() > 0)
	{
		const FRunSlotRule& Opener = RoutingConfig->RunLayout[0];
		if (Opener.Pool == ERoomPool::None && !Opener.FixedLevel.IsNone())
		{
			++RunRoomsEntered;
			FRoomNode Node;
			Node.LevelName = Opener.FixedLevel;
			Node.Type = Opener.Type;
			Node.RoomIndex = RunRoomsEntered;
			CurrentRunPath.Add(Node);
			CurrentPathIndex = 0;
			UE_LOG(LogLoopedCore, Display, TEXT("[Routing] BeginRunPath: fixed opener -> %s"), *Opener.FixedLevel.ToString());
			return Opener.FixedLevel;
		}
	}

	const FName First = EnterRoomType(FName(TEXT("Combat")));
	if (First.IsNone() || First == FName(TEXT("L_Hub")))
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Routing] BeginRunPath: no 'Combat' room type / empty pool in DT_RoomTypes — falling back to Hub."));
		CurrentPathIndex = -1;
		return FName(TEXT("L_Hub"));
	}
	return First;
}

FName ULoopedGameInstance::AdvanceToNextRoom()
{
	const int32 Next = CurrentPathIndex + 1;
	if (CurrentRunPath.IsValidIndex(Next))
	{
		CurrentPathIndex = Next;
		return CurrentRunPath[Next].LevelName;
	}
	UE_LOG(LogLoopedCore, Warning, TEXT("[Routing] AdvanceToNextRoom past end of path (idx %d / %d) — returning L_Hub."),
		CurrentPathIndex, CurrentRunPath.Num());
	return FName(TEXT("L_Hub"));
}

void ULoopedGameInstance::TravelToNextRoom(const UObject* WorldContextObject)
{
	const FName Next = AdvanceToNextRoom();
	UE_LOG(LogLoopedCore, Display, TEXT("[Routing] TravelToNextRoom -> %s (idx %d / %d)."),
		*Next.ToString(), CurrentPathIndex, CurrentRunPath.Num());
	UGameplayStatics::OpenLevel(WorldContextObject ? WorldContextObject : this, Next);
}

void ULoopedGameInstance::ActivateRoomExitPortals()
{
	UWorld* World = GetWorld();
	if (!World) return;

	// The room's placed fork portals (start disabled; we only reveal the ones we assign a type to).
	TArray<APortalActor*> Portals;
	for (TActorIterator<APortalActor> It(World); It; ++It)
	{
		APortalActor* P = *It;
		if (P && P->Mode == ERoutePortalMode::NextRoom) Portals.Add(P);
	}
	if (Portals.Num() == 0)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Routing] ActivateRoomExitPortals: no fork portals (Mode=NextRoom) in this room."));
		return;
	}

	// Boss gate: once enough rooms are cleared, the only exit is the Boss (single portal).
	if (IsBossNext())
	{
		Portals[0]->SetForkType(BossRoomTypeId); // reveals + labels; other portals stay hidden
		UE_LOG(LogLoopedCore, Display, TEXT("[Routing] Boss gate (%d rooms) — boss portal revealed."), RunRoomsEntered);
		return;
	}

	// Otherwise offer a choice: 2 distinct types (one per portal), each revealed + labelled.
	const TArray<FName> Choices = GenerateForkChoices(Portals.Num());
	for (int32 i = 0; i < Portals.Num(); ++i)
	{
		if (Choices.IsValidIndex(i))
		{
			Portals[i]->SetForkType(Choices[i]);
		}
		// Portals beyond the number of available choices simply stay disabled/hidden.
	}
	UE_LOG(LogLoopedCore, Display, TEXT("[Routing] Fork offered %d type(s) across %d portal(s)."), Choices.Num(), Portals.Num());
}

const FRoomTypeData* ULoopedGameInstance::FindRoomType(FName RoomTypeId) const
{
	if (!RoomTypeTable || RoomTypeId.IsNone()) return nullptr;
	return RoomTypeTable->FindRow<FRoomTypeData>(RoomTypeId, TEXT("FindRoomType"), false);
}

TArray<FName> ULoopedGameInstance::GenerateForkChoices(int32 Count)
{
	TArray<FName> Result;
	if (!RoomTypeTable) return Result;

	// Build the candidate pool: fork-offerable rows with a usable level pool and positive weight.
	const FName CurrentLevel = GetCurrentRoomNode().LevelName;
	TArray<FName> Pool;
	TArray<float> Weights;
	for (const FName& RowName : RoomTypeTable->GetRowNames())
	{
		const FRoomTypeData* Row = RoomTypeTable->FindRow<FRoomTypeData>(RowName, TEXT("GenerateForkChoices"), false);
		if (!Row || !Row->bOfferableInForks || Row->Weight <= 0.0f || Row->LevelPool.Num() == 0)
		{
			continue;
		}
		// No-back-to-back types (Casino) are never offered from inside one of their own levels.
		if (Row->bNoBackToBack && Row->LevelPool.Contains(CurrentLevel))
		{
			continue;
		}
		Pool.Add(RowName);
		Weights.Add(Row->Weight);
	}

	// Relic "VorrsMarker": once per run, from room 5 on, one fork choice is FORCED to Merchant
	// (if offerable and not already drawn) — the Broker always finds his best customers.
	const FName MerchantType(TEXT("Merchant"));
	const bool bForceMerchant = HasArtifact(TEXT("VorrsMarker"))
		&& !CurrentRunState.bMarkerMerchantOffered
		&& RunRoomsEntered >= 4
		&& Pool.Contains(MerchantType);
	if (bForceMerchant)
	{
		Result.Add(MerchantType);
		const int32 MIdx = Pool.IndexOfByKey(MerchantType);
		Pool.RemoveAt(MIdx);
		Weights.RemoveAt(MIdx);
		CurrentRunState.bMarkerMerchantOffered = true;
		UE_LOG(LogLoopedCore, Display, TEXT("[Relic] Vorr's Marker forced a Merchant fork."));
	}

	// Weighted draw WITHOUT replacement — guarantees the one hard rule: never two of the same type.
	while (Result.Num() < Count && Pool.Num() > 0)
	{
		float Total = 0.0f;
		for (float W : Weights) Total += W;
		float Roll = FMath::FRandRange(0.0f, Total);
		int32 Pick = 0;
		for (; Pick < Pool.Num() - 1; ++Pick)
		{
			Roll -= Weights[Pick];
			if (Roll <= 0.0f) break;
		}
		Result.Add(Pool[Pick]);
		Pool.RemoveAt(Pick);
		Weights.RemoveAt(Pick);
	}
	return Result;
}

FName ULoopedGameInstance::EnterRoomType(FName RoomTypeId)
{
	const FRoomTypeData* Row = FindRoomType(RoomTypeId);
	if (!Row)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Routing] EnterRoomType '%s' — missing row. Returning Hub."), *RoomTypeId.ToString());
		return FName(TEXT("L_Hub"));
	}

	// FLOORS 2/3 draw from their own pools when authored; empty = fall back to the base pool.
	// Floor 1's first rooms use the gentle warm-up pool — a fresh run never opens with an arena.
	const TArray<FName>* Pool = &Row->LevelPool;
	if (CurrentFloor == 1 && RunRoomsEntered <= 2 && Row->LevelPoolFloor1Early.Num() > 0)
	{
		Pool = &Row->LevelPoolFloor1Early;
	}
	else if (CurrentFloor == 2 && Row->LevelPoolFloor2.Num() > 0)      Pool = &Row->LevelPoolFloor2;
	else if (CurrentFloor >= 3 && Row->LevelPoolFloor3.Num() > 0)      Pool = &Row->LevelPoolFloor3;

	if (Pool->Num() == 0)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Routing] EnterRoomType '%s' — empty pool. Returning Hub."), *RoomTypeId.ToString());
		return FName(TEXT("L_Hub"));
	}

	// Pick a level from the pool, avoiding an immediate repeat of the current level.
	const FName Prev = CurrentRunPath.IsValidIndex(CurrentPathIndex) ? CurrentRunPath[CurrentPathIndex].LevelName : NAME_None;
	FName Level = (*Pool)[0];
	if (Pool->Num() > 1)
	{
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			Level = (*Pool)[FMath::RandRange(0, Pool->Num() - 1)];
			if (Level != Prev) break;
		}
	}

	++RunRoomsEntered;
	FRoomNode Node;
	Node.LevelName = Level;
	Node.Type = Row->MappedType;
	Node.RoomIndex = RunRoomsEntered;
	CurrentRunPath.Add(Node);
	CurrentPathIndex = CurrentRunPath.Num() - 1;

	UE_LOG(LogLoopedCore, Display, TEXT("[Routing] EnterRoomType '%s' -> %s (room %d, mappedtype %d)."),
		*RoomTypeId.ToString(), *Level.ToString(), RunRoomsEntered, (int32)Row->MappedType);
	return Level;
}

void ULoopedGameInstance::EndRunPath()
{
	CurrentPathIndex = -1;
	UE_LOG(LogLoopedCore, Display, TEXT("[Routing] EndRunPath — path index cleared (heading to Hub)."));
}

// Shared clamp for the per-floor knob arrays: index 0 = floor 1, short arrays hold their last value.
static float FloorKnob(const TArray<float>& Arr, int32 Floor)
{
	if (Arr.Num() == 0) return 1.0f;
	return Arr[FMath::Clamp(Floor - 1, 0, Arr.Num() - 1)];
}

float ULoopedGameInstance::GetFloorHealthMult() const { return FloorKnob(FloorHealthMult, CurrentFloor); }
float ULoopedGameInstance::GetFloorDamageMult() const { return FloorKnob(FloorDamageMult, CurrentFloor); }

FName ULoopedGameInstance::GetFloorBossRow() const
{
	if (FloorBossRows.Num() == 0) return NAME_None;
	return FloorBossRows[FMath::Clamp(CurrentFloor - 1, 0, FloorBossRows.Num() - 1)];
}

FName ULoopedGameInstance::BeginNextFloor()
{
	CurrentFloor = FMath::Clamp(CurrentFloor + 1, 1, MaxFloors);
	RunRoomsEntered = 0; // the new floor's boss gate re-arms; forks continue as normal
	UE_LOG(LogLoopedCore, Display, TEXT("[Floors] Descending — floor %d begins (%d rooms to its boss)."),
		CurrentFloor, GetFloorRunLength());
	// Land in a fight: draw the floor's opener from the Combat pool. EnterRoomType appends the
	// node and bumps the room counter, so HUD/forks/boss-gate all pick up without special cases.
	return EnterRoomType(FName(TEXT("Combat")));
}

const FArtifactData* ULoopedGameInstance::FindArtifactRow(FName ArtifactId) const
{
	if (!ArtifactTable || ArtifactId.IsNone()) return nullptr;
	return ArtifactTable->FindRow<FArtifactData>(ArtifactId, TEXT("FindArtifactRow"), false);
}

void ULoopedGameInstance::GrantRunArtifact(FName ArtifactId)
{
	if (ArtifactId.IsNone()) return;

	const FArtifactData* Row = FindArtifactRow(ArtifactId);
	if (!Row)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Artifacts] GrantRunArtifact: '%s' not found in DT_Artifacts — ignored."), *ArtifactId.ToString());
		return;
	}

	// Discovery: holding a blessing once unlocks its codex entry forever.
	if (Stats && !Stats->SeenBlessings.Contains(ArtifactId))
	{
		Stats->SeenBlessings.Add(ArtifactId);
		SaveStats();
	}
	if (Row->Scope != EArtifactScope::Run)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Artifacts] GrantRunArtifact: '%s' is Permanent-scope — use the save path, not the run path."), *ArtifactId.ToString());
		return;
	}
	if (CurrentRunState.AcquiredArtifacts.Contains(ArtifactId)) return; // already held this run

	CurrentRunState.AcquiredArtifacts.Add(ArtifactId);
	UE_LOG(LogLoopedCore, Display, TEXT("[Artifacts] Acquired run relic '%s' (held: %d). (Effect application wired in a later step.)"),
		*ArtifactId.ToString(), CurrentRunState.AcquiredArtifacts.Num());

	// Cursed Bargain: the relic's upside comes bundled with a downside. Inject the curse through
	// the existing Phase-2 run-curse system (AddCurse handles dedupe + live movement re-apply).
	if (Row->bIsCursed && !Row->AssociatedCurseId.IsNone())
	{
		AddCurse(Row->AssociatedCurseId);
		UE_LOG(LogLoopedCore, Display, TEXT("[Artifacts] '%s' is a Cursed Bargain — injected curse '%s'."),
			*ArtifactId.ToString(), *Row->AssociatedCurseId.ToString());
	}

	// Apply the relic's passive immediately (mirrors AddCurse's live re-apply): MaxHP/Gravity
	// take effect on pickup instead of waiting for the next room load.
	if (UWorld* World = GetWorld())
	{
		if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(World, 0)))
		{
			Player->ApplyMaxHPMod();
			Player->ApplyMovementMods();
		}
	}
}

// Drop-table weights by rarity for the Treasure altar draw. Cursed tier is authored via the
// bIsCursed bargain flag, not this rarity bucket, so it carries no standalone weight.
static float ArtifactRarityWeight(ECardRarity Rarity)
{
	switch (Rarity)
	{
		case ECardRarity::Common: return 60.0f;
		case ECardRarity::Rare:   return 30.0f;
		case ECardRarity::Epic:   return 10.0f;
		case ECardRarity::Cursed: return 0.0f;
		default:                  return 0.0f;
	}
}

FName ULoopedGameInstance::GrantRandomRunArtifact()
{
	if (!ArtifactTable)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Artifacts] GrantRandomRunArtifact: no DT_Artifacts loaded."));
		return NAME_None;
	}

	// Build the weighted eligible pool: Run-scope, unlocked, not already held this run.
	TArray<TPair<FName, float>> Pool;
	float Total = 0.0f;
	for (const FName& Id : ArtifactTable->GetRowNames())
	{
		const FArtifactData* Row = FindArtifactRow(Id);
		if (!Row) continue;
		if (Row->Scope != EArtifactScope::Run) continue;
		if (Row->bRequiresUnlock) continue;   // no artifact-unlock system yet — gated rows excluded
		if (HasRunArtifact(Id)) continue;      // don't award a duplicate this run
		const float W = ArtifactRarityWeight(Row->Rarity);
		if (W <= 0.0f) continue;
		Pool.Emplace(Id, W);
		Total += W;
	}

	if (Pool.Num() == 0 || Total <= 0.0f)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Artifacts] GrantRandomRunArtifact: no eligible Run artifacts to draw."));
		return NAME_None;
	}

	// Weighted pick.
	float Roll = FMath::FRandRange(0.0f, Total);
	FName Chosen = Pool.Last().Key;
	for (const TPair<FName, float>& Entry : Pool)
	{
		if (Roll < Entry.Value) { Chosen = Entry.Key; break; }
		Roll -= Entry.Value;
	}

	GrantRunArtifact(Chosen); // pushes to FRunState.AcquiredArtifacts + cursed-bargain injection
	UE_LOG(LogLoopedCore, Display, TEXT("[Artifacts] GrantRandomRunArtifact -> %s (from %d eligible)."),
		*Chosen.ToString(), Pool.Num());
	return Chosen;
}

bool ULoopedGameInstance::HasRunArtifact(FName ArtifactId) const
{
	return CurrentRunState.AcquiredArtifacts.Contains(ArtifactId);
}

TArray<FName> ULoopedGameInstance::GetRunArtifacts() const
{
	return CurrentRunState.AcquiredArtifacts;
}

bool ULoopedGameInstance::RemoveRunArtifact(FName ArtifactId)
{
	if (CurrentRunState.AcquiredArtifacts.Remove(ArtifactId) == 0) return false;

	UE_LOG(LogLoopedCore, Display, TEXT("[Artifact] Run relic '%s' pawned/removed."), *ArtifactId.ToString());
	// Its tags stop contributing NOW — re-derive movement/HP and repaint the HUD list.
	if (UWorld* World = GetWorld())
	{
		if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(World, 0)))
		{
			Player->ApplyMovementMods();
			Player->ApplyMaxHPMod();
			Player->UpdateArtifactHUD();
		}
	}
	return true;
}

void ULoopedGameInstance::ResetTreasurePicks()
{
	CurrentRoomPicks = 0;
}

void ULoopedGameInstance::RegisterTreasurePick()
{
	++CurrentRoomPicks;
	UE_LOG(LogLoopedCore, Display, TEXT("[Treasure] Pick registered (%d/%d)."), CurrentRoomPicks, GetEffectiveMaxPicks());
}

int32 ULoopedGameInstance::GetEffectiveMaxPicks() const
{
	// Blessing "TwinPedestal": one extra pedestal pick per treasure room this run.
	return MaxAllowedPicks + (HasRunArtifact(TEXT("TwinPedestal")) ? 1 : 0);
}

// Preview-only weighted roll, filtered by the cursed flag. No grant, no state change (const).
FName ULoopedGameInstance::RollRunArtifact(bool bCursed) const
{
	if (!ArtifactTable) return NAME_None;

	TArray<TPair<FName, float>> Pool;
	float Total = 0.0f;
	for (const FName& Id : ArtifactTable->GetRowNames())
	{
		const FArtifactData* Row = FindArtifactRow(Id);
		if (!Row) continue;
		if (Row->Scope != EArtifactScope::Run) continue;
		if (Row->bIsCursed != bCursed) continue;   // clean vs cursed subset
		if (Row->bRequiresUnlock) continue;
		if (HasRunArtifact(Id)) continue;          // don't offer one already held
		const float W = ArtifactRarityWeight(Row->Rarity);
		if (W <= 0.0f) continue;
		Pool.Emplace(Id, W);
		Total += W;
	}
	if (Pool.Num() == 0 || Total <= 0.0f) return NAME_None;

	float Roll = FMath::FRandRange(0.0f, Total);
	for (const TPair<FName, float>& Entry : Pool)
	{
		if (Roll < Entry.Value) return Entry.Key;
		Roll -= Entry.Value;
	}
	return Pool.Last().Key;
}

void ULoopedGameInstance::GrantRunCardBundle(int32 Count)
{
	if (Count <= 0) return;

	TArray<FName> Eligible = GetEligibleCards(TArray<FName>()); // table-driven; InAll ignored
	// Fisher-Yates shuffle so we draw distinct cards.
	for (int32 i = Eligible.Num() - 1; i > 0; --i)
	{
		const int32 j = FMath::RandRange(0, i);
		Eligible.Swap(i, j);
	}
	const int32 N = FMath::Min(Count, Eligible.Num());
	for (int32 i = 0; i < N; ++i)
	{
		const int32 NewLevel = IncrementPerkLevel(Eligible[i]);
		RecordPerkPicked(Eligible[i], NewLevel);
	}
	UE_LOG(LogLoopedCore, Display, TEXT("[Treasure] Card Bundle granted %d card(s)."), N);
}

float ULoopedGameInstance::SumArtifactMagnitude(FName TagName) const
{
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName, /*ErrorIfNotFound*/false);
	if (!Tag.IsValid()) return 0.0f;
	float Sum = 0.0f;
	for (const FName& Id : CurrentRunState.AcquiredArtifacts)
	{
		const FArtifactData* Row = FindArtifactRow(Id);
		if (Row && Row->EffectTags.HasTagExact(Tag)) Sum += Row->Magnitude;
	}
	return Sum;
}

float ULoopedGameInstance::ProductArtifactMagnitude(FName TagName) const
{
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName, false);
	if (!Tag.IsValid()) return 1.0f;
	float Product = 1.0f;
	for (const FName& Id : CurrentRunState.AcquiredArtifacts)
	{
		const FArtifactData* Row = FindArtifactRow(Id);
		if (Row && Row->EffectTags.HasTagExact(Tag)) Product *= Row->Magnitude;
	}
	return Product;
}

float ULoopedGameInstance::GetArtifactCritChance() const { return SumArtifactMagnitude(TEXT("Artifact.CritChance")); }
float ULoopedGameInstance::GetArtifactFlatMaxHP() const  { return SumArtifactMagnitude(TEXT("Artifact.MaxHP")); }
float ULoopedGameInstance::GetArtifactSpeedBonus() const { return SumArtifactMagnitude(TEXT("Artifact.MoveSpeed")); }
float ULoopedGameInstance::GetArtifactGravityMult() const{ return ProductArtifactMagnitude(TEXT("Artifact.Gravity")); }
float ULoopedGameInstance::GetArtifactShardMult() const  { return ProductArtifactMagnitude(TEXT("Artifact.ShardGain")); }
float ULoopedGameInstance::GetArtifactEchoMult() const   { return ProductArtifactMagnitude(TEXT("Artifact.EchoGain")); }
float ULoopedGameInstance::GetArtifactDamageMult() const { return ProductArtifactMagnitude(TEXT("Artifact.DamageMult")); }

FText ULoopedGameInstance::GetCardDescriptionForLevel(FName CardId, int32 Level) const
{
	const FPassiveCardData* Row = FindCardRow(CardId);
	if (!Row) return FText::GetEmpty();
	const int32 Idx = Level - 1; // LevelDescriptions[0] == Level 1
	if (Row->LevelDescriptions.IsValidIndex(Idx) && !Row->LevelDescriptions[Idx].IsEmpty())
	{
		return Row->LevelDescriptions[Idx];
	}
	return Row->Description; // fallback to the generic description
}

FText ULoopedGameInstance::GetCardUpgradePreviewText(FName CardId, int32 CurrentLevel) const
{
	const FPassiveCardData* Row = FindCardRow(CardId);
	if (!Row) return FText::GetEmpty();

	// Curse "Amnesia": no upgrade math either — the numbers are gone.
	if (HasCurse(TEXT("Amnesia")))
	{
		return FText::FromString(TEXT("??? -> ???"));
	}

	if (CurrentLevel >= Row->MaxLevel)
	{
		return FText::FromString(TEXT("MAX LEVEL"));
	}

	const FText NextText = GetCardDescriptionForLevel(CardId, CurrentLevel + 1);
	if (CurrentLevel <= 0)
	{
		// Not yet owned — just preview what Level 1 grants.
		return FText::FromString(FString::Printf(TEXT("New: %s"), *NextText.ToString()));
	}

	const FText CurText = GetCardDescriptionForLevel(CardId, CurrentLevel);
	return FText::FromString(FString::Printf(TEXT("Current: %s  ->  Upgrade: %s"),
		*CurText.ToString(), *NextText.ToString()));
}

FRoomNode ULoopedGameInstance::GetCurrentRoomNode() const
{
	if (CurrentRunPath.IsValidIndex(CurrentPathIndex))
	{
		return CurrentRunPath[CurrentPathIndex];
	}
	return FRoomNode(); // default — Hub / pre-run
}

int32 ULoopedGameInstance::GetCombatRoomCount() const
{
	int32 Count = 0;
	for (const FRoomNode& Node : CurrentRunPath)
	{
		if (Node.Type == ERoomType::Combat) ++Count;
	}
	return Count;
}

void ULoopedGameInstance::ResetPerks()
{
	ClearRunDeck(); // new run — wipe the active build (single source of truth = RunDeck)
	UE_LOG(LogLoopedCore, Display, TEXT("[GI] Run deck cleared (new run)"));
#if !UE_BUILD_SHIPPING
	// [GameInstance] PERKS WIPED on-screen message removed for a clean screen (UE_LOG above kept for dev).
#endif
}

// --- Missions & hints (monitor State-1 guidance panel) --------------------------------------

int32 ULoopedGameInstance::ResolveMissionCounter(EMissionCondition Type, FName Key) const
{
	switch (Type)
	{
	case EMissionCondition::None:
		return 0;

	case EMissionCondition::HasArtifact:
		return HasArtifact(Key) ? 1 : 0;

	case EMissionCondition::CardUnlocked:
		return IsCardUnlocked(Key) ? 1 : 0;

	case EMissionCondition::CurseActive:
		// Named curse = 0/1; no key = how many curses are active (drives "you carry N curses").
		return Key.IsNone() ? CurrentRunState.ActiveCurses.Num() : (HasCurse(Key) ? 1 : 0);

	case EMissionCondition::StatAtLeast:
	{
		if (!Stats) return 0;
		if (Key == TEXT("BossKills"))       return Stats->BossKills;
		if (Key == TEXT("BossDeaths"))      return Stats->BossDeaths;
		if (Key == TEXT("RoomClears"))      return Stats->RoomClears;
		if (Key == TEXT("TotalEnemyKills")) return Stats->TotalEnemyKills;
		if (Key == TEXT("RunsCompleted"))   return Stats->RunsCompleted;
		if (Key == TEXT("TotalDeaths"))     return Stats->TotalDeaths;
		if (Key == TEXT("Echoes"))          return Stats->Echoes;
		if (Key == TEXT("MiraFragments"))   return Stats->MiraFragments;
		if (Key == TEXT("SkillGaugeRanks")) return Stats->SkillGaugeRanks;
		if (Key == TEXT("UnlockedCards"))   return Stats->UnlockedCards.Num();
		if (Key == TEXT("Companions"))
		{
			// Rescued companions = the five rescue artifacts owned (Orin = the tutorial rescue).
			static const FName Rescued[] = { FName("Orin"), FName("Lysa"), FName("Brann"), FName("Serin"), FName("Mira") };
			int32 N = 0;
			for (const FName& C : Rescued) { if (HasArtifact(C)) ++N; }
			return N;
		}
		UE_LOG(LogLoopedCore, Warning, TEXT("[Missions] Unknown StatAtLeast key '%s' — row reads as 0."), *Key.ToString());
		return 0;
	}
	}
	return 0;
}

TArray<FMissionStatus> ULoopedGameInstance::EvaluateMissions() const
{
	TArray<FMissionStatus> Out;
	if (!MissionTable) return Out;

	for (const TPair<FName, uint8*>& Pair : MissionTable->GetRowMap())
	{
		const FMissionData* Row = reinterpret_cast<const FMissionData*>(Pair.Value);
		if (!Row) continue;

		// Visibility gate — chain-gates spoilers (Mira's hunt only appears after Serin).
		if (Row->ActiveWhenType != EMissionCondition::None &&
			ResolveMissionCounter(Row->ActiveWhenType, Row->ActiveWhenKey) < Row->ActiveWhenValue)
		{
			continue;
		}

		FMissionStatus S;
		S.RowId    = Pair.Key;
		S.Category = Row->Category;
		S.Priority = Row->Priority;
		S.Target   = FMath::Max(1, Row->TargetValue);
		S.Current  = FMath::Clamp(ResolveMissionCounter(Row->ConditionType, Row->ConditionKey), 0, S.Target);
		// ConditionType None never completes — a hint that lives purely on its gate.
		S.bComplete = (Row->ConditionType != EMissionCondition::None) && (S.Current >= S.Target);

		// Hints retire the moment their condition lands (the ransom hint dies with Serin freed).
		if (S.Category == EMissionCategory::Hint && S.bComplete) continue;

		FString Text = Row->Title.ToString();
		Text.ReplaceInline(TEXT("{cur}"), *FString::FromInt(S.Current));
		Text.ReplaceInline(TEXT("{max}"), *FString::FromInt(S.Target));
		S.DisplayText = FText::FromString(Text);

		Out.Add(S);
	}

	// Active work floats, finished missions sink; Priority orders within each half.
	Out.Sort([](const FMissionStatus& A, const FMissionStatus& B)
	{
		if (A.bComplete != B.bComplete) return !A.bComplete;
		return A.Priority < B.Priority;
	});
	return Out;
}
