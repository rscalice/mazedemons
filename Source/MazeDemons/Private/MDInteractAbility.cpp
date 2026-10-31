#include "MDInteractAbility.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"

UMDInteractAbility::UMDInteractAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

bool UMDInteractAbility::IsAttachedToPawn(const AActor* Actor)
{
	for (const AActor* Parent = Actor ? Actor->GetAttachParentActor() : nullptr; Parent; Parent = Parent->GetAttachParentActor())
	{
		if (Parent->IsA<APawn>())
		{
			return true;
		}
	}
	return false;
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

	// Weapons/armor attached to any pawn (ours or others') must never occlude an interactable: the multi-sweep
	// stops at the first blocking hit, so a held weapon clipping into a station would hide it.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDInteract), false, Avatar);
	{
		TArray<AActor*> Attached;
		Avatar->GetAttachedActors(Attached, /*bResetArray*/ true, /*bRecursivelyIncludeAttachedActors*/ true);
		Params.AddIgnoredActors(Attached);
	}

	TArray<FHitResult> Hits;
	constexpr int32 MaxRetries = 8;
	for (int32 Attempt = 0; Attempt < MaxRetries; ++Attempt)
	{
		Hits.Reset();
		World->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, ECC_Visibility,
			FCollisionShape::MakeSphere(Radius), Params);

		bool bRetry = false;
		for (const FHitResult& Hit : Hits)
		{
			AActor* HitActor = Hit.GetActor();
			if (HitActor && IsAttachedToPawn(HitActor))
			{
				Params.AddIgnoredActor(HitActor);
				bRetry = true;
			}
		}
		if (!bRetry)
		{
			break;
		}
	}

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
