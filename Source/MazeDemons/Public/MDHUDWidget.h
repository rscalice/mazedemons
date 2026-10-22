#pragma once

#include "CoreMinimal.h"
#include "MDInventoryTypes.h"
#include "MDQuestTypes.h"
#include "MDUIWidgets.h"
#include "MDHUDWidget.generated.h"

class AMDLevelMap;
class AMDMapZone;
class AMDPlayerController;
class UMDMapComponent;
class UMDQuestComponent;
class UMDAbilitySystemComponent;
class UMDPerkComponent;
class UMDStaminaComponent;

// Native base of the in-game HUD (W_MDHUD). Binds once to the local player's PlayerState (ASC + stamina) and keeps
// plain cached values; the Blueprint child only lays out widgets and reacts to the events below. No game logic and no
// raw attribute reads live in Blueprint, so a missing/late PlayerState or a zero max never divides by zero.
UCLASS(Abstract)
class MAZEDEMONS_API UMDHUDWidget : public UMDUserWidget
{
	GENERATED_BODY()

public:
	// Value / Max in [0,1]; returns 0 when Max <= 0 (never divides by zero).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|UI")
	static float SafePercent(float Value, float Max);

	UFUNCTION(BlueprintPure, Category = "Maze Demons|HUD")
	float GetHealthPercent() const { return SafePercent(Health, MaxHealth); }

	UFUNCTION(BlueprintPure, Category = "Maze Demons|HUD")
	float GetStaminaPercent() const { return SafePercent(Stamina, MaxStamina); }

	UFUNCTION(BlueprintPure, Category = "Maze Demons|HUD")
	bool IsBound() const { return bBound; }

	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|HUD")
	float Health = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|HUD")
	float MaxHealth = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|HUD")
	float Stamina = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|HUD")
	float MaxStamina = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|HUD")
	bool bExhausted = false;

	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|HUD")
	int32 Gold = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|HUD")
	bool bDead = false;

	// Unspent perk points (earned minus spent) for the "+1" badge next to the health bar.
	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|HUD")
	int32 PerkPointsAvailable = 0;

	// Seconds until the server respawns this player (0 when alive). Derived from the replicated respawn time.
	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|HUD")
	float RespawnSecondsRemaining = 0.0f;

	// ---- Quests (U4). Seeded when the game state is found, then kept current from UMDQuestComponent::OnQuestsChanged. ----

	// Zone number (1..4) of the Active quest; 0 when every quest is done or none exist yet.
	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|HUD")
	int32 QuestZone = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|HUD")
	FText QuestTitle;

	// "Slay ghouls 3/6" for the Active quest.
	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|HUD")
	FText ObjectiveText;

	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|HUD")
	int32 ObjectiveCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|HUD")
	int32 ObjectiveRequired = 1;

	// Every zone quest is Completed (show "All zones cleansed").
	UPROPERTY(BlueprintReadOnly, Category = "Maze Demons|HUD")
	bool bAllQuestsDone = false;

	// ProHUD ETaskState index for the Active quest (see UMDQuestRules::ToProHUDTaskState).
	UFUNCTION(BlueprintPure, Category = "Maze Demons|HUD")
	int32 GetObjectiveTaskState() const { return QuestZone > 0 ? UMDQuestRules::ToProHUDTaskState(EMDQuestState::Active) : UMDQuestRules::ToProHUDTaskState(EMDQuestState::Completed); }

	// The local map component (zone, fog texture, minimap options), or null before the controller exists.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|HUD")
	UMDMapComponent* GetMapComponent() const { return BoundMap.Get(); }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Fired every time the widget (re)binds to a PlayerState, after the cached values are seeded.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|HUD")
	void OnBound();

	// Delta < 0 is damage (drives the lag bar), Delta > 0 is healing (drives the heal flash).
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|HUD")
	void OnHealthUpdated(float Percent, float Delta);

	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|HUD")
	void OnStaminaUpdated(float Percent, bool bNowExhausted);

	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|HUD")
	void OnGoldUpdated(int32 NewGold, int32 Delta);

	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|HUD")
	void OnDeathStateChanged(bool bNowDead);

	// Unspent perk points changed (also fired once on bind). Show the badge while Available > 0.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|HUD")
	void OnPerkPointsChanged(int32 Available);

	// Server confirmed a pickup for this player (shown as a toast).
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|HUD")
	void OnPickupToast(FName ItemRowName, int32 Quantity);

	// The server refused a consumable use; Message is the localized reason (shown as a text toast).
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|HUD")
	void OnUseRejectedToast(const FText& Message);

	// Generic server toast (gold, repair / upgrade results, station rejections). Tone picks the accent colour.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|HUD")
	void OnToast(const FText& Message, EMDColor Tone);

	// A quest changed or the quests were first found. bJustCompleted: a zone quest finished with this change (the tracker
	// checks the task and swaps in the next zone). Also fired once when the quests are first bound (bJustCompleted false).
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|HUD")
	void OnQuestUpdated(bool bJustCompleted);

	// The local pawn moved into another map zone (null = outside every zone). Tracker, labels and guidance only; never the map.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|HUD")
	void OnMapZoneChanged(AMDMapZone* Zone);

	// The level map is ready (first capture after binding) or was recaptured: feed the map component's GetMapTexture to the
	// minimap once and place the calibration corners at the level map's bounds. Fog updates change that texture in place.
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze Demons|HUD")
	void OnLevelMapReady(AMDLevelMap* LevelMap);

private:
	void TryBind();
	void Unbind();
	void TryBindQuests();
	void RefreshQuest(bool bAllowJustCompleted);

	UFUNCTION()
	void HandleHealthChanged(float OldValue, float NewValue);
	UFUNCTION()
	void HandleMaxHealthChanged(float OldValue, float NewValue);
	UFUNCTION()
	void HandleStaminaChanged(float OldValue, float NewValue);
	UFUNCTION()
	void HandleMaxStaminaChanged(float OldValue, float NewValue);
	UFUNCTION()
	void HandleExhaustedChanged(bool bNowExhausted);
	UFUNCTION()
	void HandlePickupNotified(FName ItemRowName, int32 Quantity);
	UFUNCTION()
	void HandleUseRejected(EMDUseRejectReason Reason, float CooldownRemaining);
	UFUNCTION()
	void HandleToast(FText Message, EMDColor Tone);
	UFUNCTION()
	void HandlePerksChanged();
	UFUNCTION()
	void HandleQuestsChanged();
	UFUNCTION()
	void HandleMapZoneChanged(AMDMapZone* Zone);
	UFUNCTION()
	void HandleLevelMapChanged(AMDLevelMap* LevelMap);

	TWeakObjectPtr<UMDAbilitySystemComponent> BoundASC;
	TWeakObjectPtr<UMDStaminaComponent> BoundStamina;
	TWeakObjectPtr<AMDPlayerController> BoundController;
	TWeakObjectPtr<UMDPerkComponent> BoundPerks;
	TWeakObjectPtr<UMDQuestComponent> BoundQuests;
	TWeakObjectPtr<UMDMapComponent> BoundMap;
	int32 LastCompletedZones = -1;
	bool bBound = false;
};
