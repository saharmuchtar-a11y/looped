#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "WeaponData.generated.h"

UENUM(BlueprintType)
enum class EWeaponFamily : uint8
{
	Blade    UMETA(DisplayName = "Blade"),
	Gun      UMETA(DisplayName = "Gun"),
	Void     UMETA(DisplayName = "Void"),
	Launcher UMETA(DisplayName = "Launcher"),
	Energy   UMETA(DisplayName = "Energy")
};

UENUM(BlueprintType)
enum class EHitType : uint8
{
	Melee      UMETA(DisplayName = "Melee"),
	Hitscan    UMETA(DisplayName = "Hitscan"),
	Projectile UMETA(DisplayName = "Projectile")
};

USTRUCT(BlueprintType)
struct FWeaponData : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EWeaponFamily PrimaryFamily = EWeaponFamily::Blade;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	bool bIsHybrid = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (EditCondition = "bIsHybrid"))
	EWeaponFamily SecondaryFamily = EWeaponFamily::Blade;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	EHitType HitType = EHitType::Melee;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float BaseDamage = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float AttackSpeed = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float Range = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	int32 MagazineSize = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float ReloadTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float FireRate = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feel")
	float RecoilMagnitude = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feel")
	float ScreenShakeMagnitude = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FLinearColor FamilyColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<USkeletalMesh> WeaponMesh;
};
