#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ProjectileTypes.generated.h"

// One row in DT_ProjectileElements — a projectile "class/element" (Sahar: color/texture by class).
// An enemy sets its ProjectileElement (RowName) and its shots take this element's look + scaling +
// status. Adding a new element (or tuning fire/ice/venom/void) = editing a row, no code.
USTRUCT(BlueprintType)
struct FProjectileElementData : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element")
	FText DisplayName;

	// Orb glow color (drives the projectile's additive material). >1 channels = brighter glow.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element")
	FLinearColor OrbColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);

	// Multiplies the firing enemy's RangedDamage — lets harder elements scale up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element")
	float DamageMultiplier = 1.0f;

	// Optional on-hit status applied to the player (e.g. "Burn", "Venom", "Slow"). Empty = none.
	// Carried as data now; the player-side application is wired when harder enemies / player status ship.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element|Status (future)")
	FName StatusEffect = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element|Status (future)")
	float StatusMagnitude = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element|Status (future)")
	float StatusDuration = 0.0f;
};
