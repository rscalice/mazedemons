#include "AI/MDAN_Tell.h"

#include "AI/MDEnemyCharacter.h"
#include "Components/SkeletalMeshComponent.h"

void UMDAN_Tell::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	if (AMDEnemyCharacter* Enemy = MeshComp ? Cast<AMDEnemyCharacter>(MeshComp->GetOwner()) : nullptr)
	{
		Enemy->PlayTellCue();
	}
}
