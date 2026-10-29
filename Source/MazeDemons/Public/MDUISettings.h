#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MDCombatTypes.h"
#include "MDUISettings.generated.h"

class UMDUIStyle;
class UUserWidget;
class UDataTable;
class UInputMappingContext;

// Project Settings > Game > Maze Demons UI. Points at DA_MDUIStyle; empty falls back to the built-in defaults.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Maze Demons UI"))
class MAZEDEMONS_API UMDUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (AllowedClasses = "/Script/MazeDemons.MDUIStyle"))
	TSoftObjectPtr<UMDUIStyle> Style;

	// Widget the local PlayerController adds to the viewport at BeginPlay (survives pawn death).
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD", meta = (MetaClass = "/Script/MazeDemons.MDHUDWidget"))
	TSoftClassPtr<UUserWidget> HUDWidgetClass = TSoftClassPtr<UUserWidget>(FSoftObjectPath(TEXT("/Game/UI/HUD/W_MDHUD.W_MDHUD_C")));

	// DT_ItemDisplay (FMDItemDisplayRow keyed like DT_Items). Empty/unloadable falls back to a split-CamelCase row name.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Items", meta = (RequiredAssetDataTags = "RowStructure=/Script/MazeDemons.MDItemDisplayRow"))
	TSoftObjectPtr<UDataTable> ItemDisplayTable;

	// Pause menu widget created by AMDPlayerController::TogglePauseMenu (never pauses the game).
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD")
	TSoftClassPtr<UUserWidget> PauseWidgetClass;

	// Optional extra mapping context for UI input.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Input")
	TSoftObjectPtr<UInputMappingContext> UIMappingContext;

	// ---- Minimap (U4) ----

	// Multiplier on MinimapRadiusUU (2 = zoomed in, half the radius).
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Minimap", meta = (ClampMin = "0.1", ClampMax = "10"))
	float MinimapZoom = 1.0f;

	// World radius (unreal units) shown by the minimap at zoom 1.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Minimap", meta = (ClampMin = "100"))
	float MinimapRadiusUU = 3000.0f;

	// Default fog cell size (unreal units) for map zones that do not override it.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Minimap", meta = (ClampMin = "50"))
	float FogCellSize = 400.0f;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};

// Per-user local options (saved in GameUserSettings.ini, never replicated). Read and written through
// UMDMapComponent; use GetMutableDefault<UMDLocalSettings>() and SaveConfig() to change them.
UCLASS(Config = GameUserSettings)
class MAZEDEMONS_API UMDLocalSettings : public UObject
{
	GENERATED_BODY()

public:
	// False (default): the minimap rotates with the camera. True: north is up and the player arrow rotates.
	UPROPERTY(Config, BlueprintReadOnly, Category = "Minimap")
	bool bMinimapNorthUp = false;

	// ---- Combat feel (C1, Options > Gameplay). All cosmetic and local. ----
	// Camera shake strength in percent (0 = off).
	UPROPERTY(Config, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0", ClampMax = "100"))
	float ShakeScalePercent = 100.0f;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Combat")
	bool bHitStopEnabled = true;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Combat")
	bool bRumbleEnabled = true;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Combat")
	EMDDamageNumberMode DamageNumberMode = EMDDamageNumberMode::Own;

	// Reduced halves the hit flash and tells impact Blueprints to use the cheap effects.
	UPROPERTY(Config, BlueprintReadWrite, Category = "Combat")
	EMDVFXLevel VFXLevel = EMDVFXLevel::Full;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Combat")
	bool bAfterImagesEnabled = true;

	// ---- Ranged (R7, Options > Gameplay). Local; UMDHUDOptions sets them, saves and applies them. ----
	// Aim assist strength 0..1 (R4 reads it; no effect until then).
	UPROPERTY(Config, BlueprintReadWrite, Category = "Ranged", meta = (ClampMin = "0", ClampMax = "1"))
	float AimAssistStrength = 0.5f;

	// False: hold the aim button. True: it toggles aim (R4 reads it).
	UPROPERTY(Config, BlueprintReadWrite, Category = "Ranged")
	bool bAimToggle = false;

	// Reticle size multiplier (tick length and hit marker; the gap always shows the real spread).
	UPROPERTY(Config, BlueprintReadWrite, Category = "Ranged", meta = (ClampMin = "0.5", ClampMax = "2"))
	float ReticleScale = 1.0f;

	// Reload by itself when the magazine runs dry (R3). Pushed to the server by the ranged component.
	UPROPERTY(Config, BlueprintReadWrite, Category = "Ranged")
	bool bAutoReload = true;

	// Off-screen threat arrows and tell pings (md.UI.OffscreenThreat).
	UPROPERTY(Config, BlueprintReadWrite, Category = "Ranged")
	bool bOffscreenThreat = true;

	// ---- Soft lock-on (V1 WP7, Options > Gameplay). Local; UMDHUDOptions sets them. ----
	// Camera assist strength 0..1 (0 = reticle only). The assist never fights the player's own camera input.
	UPROPERTY(Config, BlueprintReadWrite, Category = "LockOn", meta = (ClampMin = "0", ClampMax = "1"))
	float LockOnAssistStrength = 0.6f;

	// False (default): the lock key toggles the lock. True: the lock holds only while the key is held.
	UPROPERTY(Config, BlueprintReadWrite, Category = "LockOn")
	bool bLockOnHold = false;

	// Reticle size multiplier.
	UPROPERTY(Config, BlueprintReadWrite, Category = "LockOn", meta = (ClampMin = "0.5", ClampMax = "2"))
	float LockOnReticleScale = 1.0f;

	// Lock the best target by itself when attacking with no lock (off by default).
	UPROPERTY(Config, BlueprintReadWrite, Category = "LockOn")
	bool bLockOnAutoAcquire = false;
};
