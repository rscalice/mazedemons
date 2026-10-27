#include "AI/MDEnemyAttributeSet.h"

#include "Net/UnrealNetwork.h"

UMDEnemyAttributeSet::UMDEnemyAttributeSet()
	: Poise(30.0f)
	, MaxPoise(30.0f)
{
}

void UMDEnemyAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UMDEnemyAttributeSet, Poise, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMDEnemyAttributeSet, MaxPoise, COND_None, REPNOTIFY_Always);
}

void UMDEnemyAttributeSet::OnRep_Poise(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDEnemyAttributeSet, Poise, OldValue);
}

void UMDEnemyAttributeSet::OnRep_MaxPoise(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMDEnemyAttributeSet, MaxPoise, OldValue);
}
