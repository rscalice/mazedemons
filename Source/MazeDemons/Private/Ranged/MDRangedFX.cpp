
#include "Ranged/MDRangedFX.h"

#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

const FName MDRangedFX::OneShotTag(TEXT("MDRangedOneShotFX"));

void MDRangedFX::CapLifetime(UWorld* World, UNiagaraComponent* Comp, float MaxSeconds)
{
	if (!World || !Comp)
	{
		return;
	}
	Comp->ComponentTags.AddUnique(OneShotTag);

	// Backstop for looping systems: deactivate at MaxSeconds, destroy DestroyGraceSeconds later.
	const TWeakObjectPtr<UNiagaraComponent> Weak(Comp);
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Weak, WeakWorld]()
	{
		UNiagaraComponent* C = Weak.Get();
		if (!C)
		{
			return;
		}
		C->Deactivate();
		UWorld* W = WeakWorld.Get();
		if (!W)
		{
			C->DestroyComponent();
			return;
		}
		FTimerHandle DestroyHandle;
		W->GetTimerManager().SetTimer(DestroyHandle, FTimerDelegate::CreateLambda([Weak]()
		{
			if (UNiagaraComponent* C2 = Weak.Get())
			{
				C2->DestroyComponent();
			}
		}), DestroyGraceSeconds, false);
	}), FMath::Max(MaxSeconds, 0.05f), false);
}

UNiagaraComponent* MDRangedFX::SpawnOneShot(UWorld* World, UNiagaraSystem* System, const FVector& Location, const FRotator& Rotation,
	const FVector& Scale, float MaxSeconds)
{
	if (!World || !System)
	{
		return nullptr;
	}
	UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Location, Rotation, Scale,
		/*bAutoDestroy*/ true, /*bAutoActivate*/ true, ENCPoolMethod::None);
	CapLifetime(World, Comp, MaxSeconds);
	return Comp;
}

int32 MDRangedFX::CountActive(const UWorld* World)
{
	int32 Count = 0;
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		const UNiagaraComponent* C = *It;
		if (IsValid(C) && !C->IsBeingDestroyed() && C->GetWorld() == World && C->ComponentTags.Contains(OneShotTag))
		{
			++Count;
		}
	}
	return Count;
}
