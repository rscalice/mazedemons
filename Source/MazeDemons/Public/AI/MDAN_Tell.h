#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "MDAN_Tell.generated.h"

// The attack tell (v1-design 1.3, V1 WP4): placed at the start of the wind-up of an enemy attack montage. It plays on every
// machine that plays the montage (the server multicasts it), and calls AMDEnemyCharacter::PlayTellCue there: the archetype's
// short growl at low volume plus a brief flash on the enemy. How long the wind-up lasts is not decided here: the attack
// component slows the montage so the first trace window starts TellTime after the swing began (tier scaled, floor 0.35 s).
UCLASS(meta = (DisplayName = "MD Tell"))
class MAZEDEMONS_API UMDAN_Tell : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("MD Tell"); }
};
