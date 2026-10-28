#include "AI/MDThreatIndicator.h"

#include "AI/MDAITypes.h"
#include "AI/MDEnemyArchetype.h"
#include "AI/MDEnemyCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"
#include "Sound/SoundBase.h"
#include "Styling/CoreStyle.h"

namespace MDThreatLocal
{
	TAutoConsoleVariable<int32> CVarOffscreenThreat(TEXT("md.UI.OffscreenThreat"), 1,
		TEXT("1 (default) = a screen-edge arrow and a sting warn of an off-screen enemy that is about to attack you. 0 = off."), ECVF_Default);

	// Pixels between the arrow centre and the screen edge.
	constexpr float EdgeMargin = 56.0f;

	// Height (cm) above the enemy's origin that is projected, so an enemy standing just below the screen edge still counts.
	constexpr float ChestHeight = 90.0f;

	// Pixels outside the viewport that still count as on-screen.
	constexpr float OnScreenSlack = 0.0f;

	// The chevron outline in arrow space (tip at +X), pixels.
	void ChevronPoints(TArray<FVector2D>& Out, float Scale)
	{
		Out = { FVector2D(22, 0), FVector2D(-12, -17), FVector2D(-4, 0), FVector2D(-12, 17), FVector2D(22, 0) };
		for (FVector2D& P : Out)
		{
			P *= Scale;
		}
	}
}

void MDThreat::ArrowForYaw(float RelYawDeg, const FVector2D& ViewSize, float Margin, FVector2D& OutPosition, float& OutRotationRad)
{
	const float Yaw = FMath::DegreesToRadians(RelYawDeg);
	// Screen direction: ahead (yaw 0) = up, right (+90) = right, behind (180) = down.
	const FVector2D Dir(FMath::Sin(Yaw), -FMath::Cos(Yaw));
	const FVector2D Half = ViewSize * 0.5f;
	const float LimitX = FMath::Max(Half.X - Margin, 1.0f);
	const float LimitY = FMath::Max(Half.Y - Margin, 1.0f);
	const float TX = FMath::Abs(Dir.X) > KINDA_SMALL_NUMBER ? LimitX / FMath::Abs(Dir.X) : TNumericLimits<float>::Max();
	const float TY = FMath::Abs(Dir.Y) > KINDA_SMALL_NUMBER ? LimitY / FMath::Abs(Dir.Y) : TNumericLimits<float>::Max();
	const float T = FMath::Min(TX, TY);
	OutPosition = Half + Dir * T;
	OutRotationRad = FMath::Atan2(Dir.Y, Dir.X);
}

// ---------------------------------------------------------------------------------------------------------------------
// Widget
// ---------------------------------------------------------------------------------------------------------------------

int32 UMDThreatIndicatorWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (Arrows.Num() == 0)
	{
		return LayerId;
	}
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	for (const FMDThreatArrow& Arrow : Arrows)
	{
		FVector2D Position;
		float Rotation;
		MDThreat::ArrowForYaw(Arrow.RelYawDeg, Size, MDThreatLocal::EdgeMargin, Position, Rotation);
		const float Cos = FMath::Cos(Rotation);
		const float Sin = FMath::Sin(Rotation);

		// A dark outline under a bright one keeps the arrow readable on any background.
		const struct { float Scale; float Thickness; FLinearColor Color; } Passes[] = {
			{ 1.0f, 7.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f * Arrow.Alpha) },
			{ 1.0f, 3.5f, FLinearColor(1.0f, 0.22f, 0.12f, Arrow.Alpha) },
			{ 0.45f, 3.0f, FLinearColor(1.0f, 0.65f, 0.35f, Arrow.Alpha) },
		};
		for (const auto& Pass : Passes)
		{
			TArray<FVector2D> Points;
			MDThreatLocal::ChevronPoints(Points, Pass.Scale);
			for (FVector2D& P : Points)
			{
				P = Position + FVector2D(P.X * Cos - P.Y * Sin, P.X * Sin + P.Y * Cos);
			}
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Pass.Color, true, Pass.Thickness);
			++LayerId;
		}
	}
	return LayerId;
}

// ---------------------------------------------------------------------------------------------------------------------
// Subsystem
// ---------------------------------------------------------------------------------------------------------------------

bool UMDThreatIndicatorSubsystem::IsEnabled()
{
	return MDThreatLocal::CVarOffscreenThreat.GetValueOnGameThread() != 0;
}

bool UMDThreatIndicatorSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

bool UMDThreatIndicatorSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_DedicatedServer && !IsTemplate();
}

TStatId UMDThreatIndicatorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMDThreatIndicatorSubsystem, STATGROUP_Tickables);
}

void UMDThreatIndicatorSubsystem::Deinitialize()
{
	if (Widget)
	{
		Widget->RemoveFromParent();
		Widget = nullptr;
	}
	Super::Deinitialize();
}

bool UMDThreatIndicatorSubsystem::IsOnScreen(const APlayerController* PC, const AActor* Enemy, const FVector2D& ViewSize) const
{
	FVector2D Screen;
	const FVector Chest = Enemy->GetActorLocation() + FVector(0.0, 0.0, MDThreatLocal::ChestHeight);
	if (!PC->ProjectWorldLocationToScreen(Chest, Screen, false)) // false = behind the camera
	{
		return false;
	}
	const float Slack = MDThreatLocal::OnScreenSlack;
	return Screen.X >= -Slack && Screen.Y >= -Slack && Screen.X <= ViewSize.X + Slack && Screen.Y <= ViewSize.Y + Slack;
}

void UMDThreatIndicatorSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	LastArrows.Reset();

	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = PC && PC->IsLocalController() ? PC->GetPawn() : nullptr;
	if (!Pawn || !IsEnabled())
	{
		LastIntent.Reset();
		if (Widget)
		{
			Widget->SetArrows(LastArrows);
		}
		return;
	}

	int32 SizeX = 0;
	int32 SizeY = 0;
	PC->GetViewportSize(SizeX, SizeY);
	const FVector2D ViewSize(static_cast<float>(SizeX), static_cast<float>(SizeY));
	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const float Pulse = 0.65f + 0.35f * FMath::Sin(World->GetTimeSeconds() * 9.0f);

	TMap<TWeakObjectPtr<const AMDEnemyCharacter>, uint8> Intents;
	for (const TWeakObjectPtr<AMDEnemyCharacter>& Weak : AMDEnemyCharacter::GetAllEnemies())
	{
		const AMDEnemyCharacter* Enemy = Weak.Get();
		if (!Enemy || Enemy->GetWorld() != World || !Enemy->bThreatening || Enemy->ThreatTarget != Pawn || Enemy->IsEnemyDead())
		{
			continue;
		}
		const bool bOnScreen = ViewSize.X > 1.0f && IsOnScreen(PC, Enemy, ViewSize);
		const uint8 Intent = Enemy->ReplicatedIntent;
		Intents.Add(Enemy, Intent);
		if (bOnScreen)
		{
			continue;
		}

		const FVector ToEnemy = Enemy->GetActorLocation() - CameraLocation;
		const float RelYaw = FRotator::NormalizeAxis(ToEnemy.Rotation().Yaw - CameraRotation.Yaw);
		FMDThreatArrow& Arrow = LastArrows.AddDefaulted_GetRef();
		Arrow.RelYawDeg = RelYaw;
		Arrow.Alpha = Pulse;

		// The sting: this enemy has just started its swing (wind-up) and the player cannot see it.
		const uint8* Previous = LastIntent.Find(Enemy);
		if (Intent == static_cast<uint8>(EMDIntent::Attack) && (!Previous || *Previous != Intent) && Enemy->Archetype && !Enemy->Archetype->ThreatSting.IsNull())
		{
			if (USoundBase* Sting = Enemy->Archetype->ThreatSting.LoadSynchronous())
			{
				UGameplayStatics::PlaySoundAtLocation(this, Sting, Enemy->GetActorLocation(), Enemy->Archetype->ThreatStingVolume);
			}
		}
	}
	LastIntent = MoveTemp(Intents);

	if (!Widget && LastArrows.Num() > 0)
	{
		Widget = CreateWidget<UMDThreatIndicatorWidget>(PC, UMDThreatIndicatorWidget::StaticClass());
		if (Widget)
		{
			Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
			Widget->AddToViewport(40); // above the HUD (0), below the game menu (90) and the pause menu (100)
		}
	}
	if (Widget)
	{
		Widget->SetArrows(LastArrows);
	}
}
