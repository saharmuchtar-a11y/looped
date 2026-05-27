#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "WeaponData.h"
#include "PassiveCardData.generated.h"

UENUM(BlueprintType)
enum class ECardRarity : uint8
{
	Common    UMETA(DisplayName = "Common"),
	Rare      UMETA(DisplayName = "Rare"),
	Legendary UMETA(DisplayName = "Legendary"),
	Cursed    UMETA(DisplayName = "Cursed")
};

USTRUCT(BlueprintType)
struct FPassiveCardData : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	ECardRarity Rarity = ECardRarity::Common;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	FGameplayTagContainer EffectTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	float Magnitude = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affinity")
	EWeaponFamily PrimaryAffinity = EWeaponFamily::Blade;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affinity")
	bool bUniversalAffinity = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evolution")
	int32 EvolutionTier1Threshold = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evolution")
	float EvolutionTier1Bonus = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evolution")
	int32 EvolutionTier2Threshold = 25;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evolution")
	float EvolutionTier2Bonus = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FLinearColor CardColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UTexture2D> CardIcon;
};

USTRUCT(BlueprintType)
struct FPassiveSlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName CardRowName = NAME_None;

	UPROPERTY(BlueprintReadOnly)
	FPassiveCardData CachedData;

	UPROPERTY(BlueprintReadOnly)
	int32 TriggerCount = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 EvolutionTier = 0;

	bool IsEmpty() const { return CardRowName == NAME_None; }
};
