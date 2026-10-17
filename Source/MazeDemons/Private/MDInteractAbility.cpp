#include "MDInteractAbility.h"

#include "Engine/World.h"

UMDInteractAbility::UMDInteractAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

AActor* UMDInteractAbility::FindInteractTarget() const
{
	return FindInteractTargetFor(GetAvatarActorFromActorInfo(), InteractRange, InteractRadius, RequiredActorTag);
}

AActor* UMDInteractAbility::FindInteractTargetFor(const AActor* Avatar, float Range, float Radius, FName RequiredTag)
{
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	const FVector Start = Avatar->GetActorLocation();
	const FVector End = Start + Avatar->GetActorForwardVector() * Range;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDInteract), false, Avatar);
	TArray<FHitResult> Hits;
	World->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeSphere(Radius), Params);

	AActor* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	for (const FHitResult& Hit : Hits)
	{
		AActor* Candidate = Hit.GetActor();
		if (!Candidate || (RequiredTag != NAME_None && !Candidate->ActorHasTag(RequiredTag)))
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(Start, Candidate->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Candidate;
		}
	}
	return Best;
}

void UMDInteractAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AActor* Target = FindInteractTarget();
	if (!Target)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	OnInteractTarget(Target);
}
