#include "LoopedGameInstance.h"
#include "Looped.h"
#include "Data/LoopedSaveData.h"
#include "Kismet/GameplayStatics.h"
#include "Player/LoopedCharacter.h"

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

	// Artifact/relic definitions — loaded by path, same pattern as the card table.
	ArtifactTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Artifacts.DT_Artifacts"));
	if (!ArtifactTable)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Artifacts] DT_Artifacts not found at /Game/Data/DT_Artifacts — create + populate it (Content Overhaul Step 2)."));
	}

	UE_LOG(LogLoopedCore, Display, TEXT("LoopedGameInstance initialized. Hunter Rank: %d"), HunterRank);
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.0f, FColor::Magenta,
			TEXT("[GameInstance] INIT — all perk levels start at 0"));
	}
#endif
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
	SaveStats();
}

void ULoopedGameInstance::AddRoomClear()
{
	if (!Stats) return;
	Stats->RoomClears++;
	EvaluateUnlocksAfterStatChange();
	SaveStats();
}

void ULoopedGameInstance::AddRunCompleted()
{
	if (!Stats) return;
	Stats->RunsCompleted++;
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
	SaveStats();
	UE_LOG(LogLoopedCore, Display, TEXT("[Artifact] Granted: %s (owned: %d)"), *ArtifactName.ToString(), Stats->OwnedArtifacts.Num());
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Cyan,
			FString::Printf(TEXT("=== ARTIFACT ACQUIRED: %s ==="), *ArtifactName.ToString()), true, FVector2D(2.0f, 2.0f));
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
	bool bFirst = true;
	for (const FName& A : Stats->OwnedArtifacts)
	{
		if (!bFirst) Out += TEXT("\n");
		Out += A.ToString();
		bFirst = false;
	}
	return FText::FromString(Out);
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

void ULoopedGameInstance::ClearPendingNextRunCards()
{
	if (Stats && Stats->PendingNextRunCards.Num() > 0)
	{
		Stats->PendingNextRunCards.Empty();
		SaveStats();
	}
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
	CurrentRunState.ActiveCurses.Add(Curse);
	UE_LOG(LogLoopedCore, Display, TEXT("[Curse] +%s (active: %d)"), *Curse.ToString(), CurrentRunState.ActiveCurses.Num());
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

	// Rare cards
	TryUnlock(TEXT("MaxHP"),      Stats->BossDeaths >= 1);   // first boss death (earliest — safety net)
	TryUnlock(TEXT("Lifesteal"),  Stats->RoomClears >= 10);  // ~a couple full runs in
	TryUnlock(TEXT("ChainSpark"), Stats->BossKills  >= 5);   // 5 boss kills

	// Epic cards.
	TryUnlock(TEXT("Speed"),      Stats->RoomClears >= 30);
	// Gravity is NOT unlocked here — it's gated behind completing the Hub parkour
	// (ACardUnlockTrigger at the summit calls UnlockCard("Gravity")).

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
	UE_LOG(LogLoopedCore, Display, TEXT("[GI] Card '%s' -> Lv %d / %d"), *Id.ToString(), NewLevel, Row->MaxLevel);
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Magenta,
			FString::Printf(TEXT("[GI] %s -> Lv %d"), *Id.ToString(), NewLevel));
	}
#endif

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
	TArray<FName> Eligible = GetEligibleCards(InPool);

	// Fisher–Yates shuffle so the N we take are distinct and randomized.
	for (int32 i = Eligible.Num() - 1; i > 0; --i)
	{
		const int32 j = FMath::RandRange(0, i);
		Eligible.Swap(i, j);
	}

	const int32 N = FMath::Min(GetCardChoiceCount(), Eligible.Num());
	TArray<FName> Out;
	Out.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		Out.Add(Eligible[i]);
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

	FString Result;
	if (Current <= 0)        Result = FString::Printf(TEXT("%s — NEW (Lv 1/%d)"), *Name, Cap);
	else if (Current >= Cap) Result = FString::Printf(TEXT("%s — MAX (Lv %d)"), *Name, Cap);
	else                     Result = FString::Printf(TEXT("%s — Lv %d → %d / %d"), *Name, Current, Current + 1, Cap);
	return FText::FromString(Result);
}

ECardRarity ULoopedGameInstance::GetPerkRarity(FName PerkName) const
{
	const FPassiveCardData* Row = FindCardRow(PerkName);
	return Row ? Row->Rarity : ECardRarity::Common;
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
	if (CurrentRunPath.Num() == 0)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Routing] BeginRunPath with empty path — returning L_Hub."));
		CurrentPathIndex = -1;
		return FName(TEXT("L_Hub"));
	}
	CurrentPathIndex = 0;
	return CurrentRunPath[0].LevelName;
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

void ULoopedGameInstance::EndRunPath()
{
	CurrentPathIndex = -1;
	UE_LOG(LogLoopedCore, Display, TEXT("[Routing] EndRunPath — path index cleared (heading to Hub)."));
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

void ULoopedGameInstance::ResetTreasurePicks()
{
	CurrentRoomPicks = 0;
}

void ULoopedGameInstance::RegisterTreasurePick()
{
	++CurrentRoomPicks;
	UE_LOG(LogLoopedCore, Display, TEXT("[Treasure] Pick registered (%d/%d)."), CurrentRoomPicks, MaxAllowedPicks);
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
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.0f, FColor::Red,
			TEXT("[GameInstance] PERKS WIPED — ResetPerks() called"));
	}
#endif
}
