#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MDCombatTypes.h"
#include "MDPoiseProfile.generated.h"

class UAnimMontage;
class USoundBase;

// One reaction montage for a reaction type and hit direction.
USTRUCT(BlueprintType)
struct MAZEDEMONS_API FMDReactionMontage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction")
	EMDHitReaction Reaction = EMDHitReaction::Flinch;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction")
	EMDHitDirection Direction = EMDHitDirection::Front;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction")
	TObjectPtr<UAnimMontage> Montage = nullptr;
};

// Poise tuning for one kind of target (DA_Poise_Player, DA_Poise_Dummy, enemy profiles). Defaults are the player numbers.
UCLASS(BlueprintType)
class MAZEDEMONS_API UMDPoiseProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise", meta = (ClampMin = "1"))
	float MaxPoise = 50.0f;

	// Seconds without being hit before poise refills.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise", meta = (ClampMin = "0"))
	float RegenDelay = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise", meta = (ClampMin = "0"))
	float RegenPerSec = 25.0f;

	// How long the target cannot act once poise breaks.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise", meta = (ClampMin = "0"))
	float StunDuration = 0.6f;

	// After a stun ends, further hits only flinch for this long (no stun-lock).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise", meta = (ClampMin = "0"))
	float StaggerImmunityAfterStun = 1.5f;

	// Bosses: every hit only flinches except a poise break.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise")
	bool bFlinchOnly = false;

	// 0..1, scales knockback strength down.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise", meta = (ClampMin = "0", ClampMax = "1"))
	float KnockbackResist = 0.0f;

	// Optional hurt sound played at the victim on a Flinch or worse (0.2 s cooldown per feedback component). Empty = none.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reactions")
	TObjectPtr<USoundBase> HurtSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reactions")
	TArray<FMDReactionMontage> ReactionMontages;

	// Exact match, else the same reaction from any direction, else the next weaker reaction (Knockback -> Stagger -> Flinch).
	UAnimMontage* FindMontage(EMDHitReaction Reaction, EMDHitDirection Direction) const;
};
