// R4 tests: aim state rules, the aim camera preset blend, camera collision in a corridor, and the aim assist (friction + magnetism).
// MazeDemons.Ranged.Aim.* and MazeDemons.Ranged.Camera.PresetBlendValues / .CorridorCollision.
#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "UObject/CoreNet.h"
#include "GameFramework/Pawn.h"
#include "MDAbilitySystemComponent.h"
#include "MDGameState.h"
#include "MDGameplayStatics.h"
#include "MDPlayerState.h"
#include "Ranged/MDAimRules.h"
#include "Ranged/MDRangedCameraModifier.h"
#include "Ranged/MDRangedComponent.h"
#include "Ranged/MDRangedTypes.h"
#include "Ranged/MDRangedWeaponData.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace MDRangedAimTestsLocal
{
	UMDRangedWeaponData* MakeGun()
	{
		UMDRangedWeaponData* Data = NewObject<UMDRangedWeaponData>(GetTransientPackage());
		Data->FireMode = EMDFireMode::Semi;
		Data->HitModel = EMDRangedHitModel::Projectile;
		Data->RoundsPerSecond = 3.0f;
		Data->SpreadHip = 3.0f;
		Data->SpreadAimed = 1.0f;
		Data->SpreadBloom = 0.0f;
		Data->MagSize = 8;
		return Data;
	}

	// A transient game world with one authority player: pawn, player state with its ASC and ranged component in ranged mode.
	struct FAimFixture
	{
		UWorld* World = nullptr;
		APawn* Pawn = nullptr;
		AMDPlayerState* State = nullptr;
		UMDRangedComponent* Ranged = nullptr;
		UMDAbilitySystemComponent* ASC = nullptr;

		bool Init(FAutomationTestBase& Test, UMDRangedWeaponData* Gun)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDRangedAimTestWorld")));
			if (!World)
			{
				Test.AddError(TEXT("UWorld::CreateWorld failed"));
				return false;
			}
			World->InitializeActorsForPlay(FURL());
			if (AMDGameState* GameState = World->SpawnActor<AMDGameState>())
			{
				World->SetGameState(GameState);
			}
			Pawn = World->SpawnActor<APawn>();
			State = World->SpawnActor<AMDPlayerState>();
			if (!Pawn || !State)
			{
				Test.AddError(TEXT("Could not spawn the pawn or its player state"));
				return false;
			}
			State->SetRole(ROLE_Authority);
			Pawn->SetPlayerState(State);
			ASC = State->GetMDAbilitySystemComponent();
			Ranged = State->RangedComponent;
			if (!ASC || !Ranged)
			{
				Test.AddError(TEXT("Player state has no ASC or ranged component"));
				return false;
			}
			ASC->InitAbilityActorInfo(State, Pawn);
			ASC->RegisterAttributeSet(State->Attributes);
			ASC->InitializeStats(100.0f, 10.0f, 0.0f);
			Ranged->SetPawnForTests(Pawn);
			Ranged->SetWeaponDataForTests(Gun);
			Ranged->SetRangedMode(true);
			return true;
		}

		~FAimFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (World->IsRooted())
				{
					World->RemoveFromRoot();
				}
				World = nullptr;
			}
		}
	};
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAimBlendTimingTest, "MazeDemons.Ranged.Aim.BlendTiming", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAimBlendTimingTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("the aim blend is 0.18 s"), FMDAimRules::AimBlendSeconds, 0.18f);
	TestEqual(TEXT("the camera cvar defaults to it"), FMDRangedCameraRules::ConfiguredAimBlendSeconds(), 0.18f);

	// Half the blend time = half the way (and the smoothstep of 0.5 is 0.5).
	float Alpha = FMDRangedCameraRules::StepAlpha(0.0f, 1.0f, 0.09f, FMDAimRules::AimBlendSeconds);
	TestEqual(TEXT("half way after 0.09 s"), Alpha, 0.5f);
	Alpha = FMDRangedCameraRules::StepAlpha(Alpha, 1.0f, 0.09f, FMDAimRules::AimBlendSeconds);
	TestEqual(TEXT("fully aimed after 0.18 s"), Alpha, 1.0f);
	Alpha = FMDRangedCameraRules::StepAlpha(Alpha, 1.0f, 1.0f, FMDAimRules::AimBlendSeconds);
	TestEqual(TEXT("never overshoots"), Alpha, 1.0f);
	Alpha = FMDRangedCameraRules::StepAlpha(Alpha, 0.0f, 0.09f, FMDAimRules::AimBlendSeconds);
	TestEqual(TEXT("blends out at the same speed"), Alpha, 0.5f);

	// Frame by frame at 60 fps: full in 11 frames (0.183 s), not before 10.
	float A = 0.0f;
	int32 Frames = 0;
	while (A < 1.0f && Frames < 100)
	{
		A = FMDRangedCameraRules::StepAlpha(A, 1.0f, 1.0f / 60.0f, FMDAimRules::AimBlendSeconds);
		++Frames;
	}
	TestTrue(TEXT("full blend takes about 0.18 s of frames"), Frames >= 10 && Frames <= 12);

	// Same blend whatever the frame rate: 0.18 s of 120 fps frames lands on 1.
	float B = 0.0f;
	for (int32 i = 0; i < 22; ++i)
	{
		B = FMDRangedCameraRules::StepAlpha(B, 1.0f, 1.0f / 120.0f, FMDAimRules::AimBlendSeconds);
	}
	TestEqual(TEXT("frame rate independent"), B, 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAimSprintCancelsTest, "MazeDemons.Ranged.Aim.SprintCancelsAim", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAimSprintCancelsTest::RunTest(const FString& Parameters)
{
	// Pure gate.
	TestTrue(TEXT("can aim in ranged mode with a gun"), FMDAimRules::CanAim(true, true, false, false, false));
	TestFalse(TEXT("sprinting cannot aim"), FMDAimRules::CanAim(true, true, false, false, true));
	TestFalse(TEXT("melee mode cannot aim"), FMDAimRules::CanAim(false, true, false, false, false));
	TestFalse(TEXT("no gun cannot aim"), FMDAimRules::CanAim(true, false, false, false, false));
	TestFalse(TEXT("dead cannot aim"), FMDAimRules::CanAim(true, true, true, false, false));
	TestFalse(TEXT("staggered cannot aim"), FMDAimRules::CanAim(true, true, false, true, false));

	// The wish: hold mode resumes after the sprint, toggle mode is cancelled for good, holstering ends both.
	TestTrue(TEXT("hold keeps the wish through a sprint"), FMDAimRules::WantedAfterRules(true, false, false, true));
	TestFalse(TEXT("toggle drops the wish on a sprint"), FMDAimRules::WantedAfterRules(true, true, false, true));
	TestTrue(TEXT("toggle keeps the wish when nothing cancelled it"), FMDAimRules::WantedAfterRules(true, true, true, true));
	TestFalse(TEXT("holstering ends the wish"), FMDAimRules::WantedAfterRules(true, false, true, false));

	// The component on the authority: aimed, then the sprint rule runs.
	using namespace MDRangedAimTestsLocal;
	FAimFixture Fx;
	if (!Fx.Init(*this, MakeGun()))
	{
		return false;
	}
	Fx.Ranged->SetAimed(true);
	TestTrue(TEXT("aimed after SetAimed"), Fx.Ranged->IsAimed());
	TestTrue(TEXT("State.Aiming tag is on"), Fx.ASC->HasMatchingGameplayTag(TAG_State_Aiming));
	Fx.Ranged->EvaluateAimRules(false);
	TestTrue(TEXT("walking keeps the aim"), Fx.Ranged->IsAimed());
	Fx.Ranged->EvaluateAimRules(true);
	TestFalse(TEXT("sprinting leaves aim"), Fx.Ranged->IsAimed());
	TestFalse(TEXT("State.Aiming tag is gone"), Fx.ASC->HasMatchingGameplayTag(TAG_State_Aiming));

	// A refused request stays refused: the server does not trust the client's wish while the rules say no.
	Fx.Ranged->SetRangedMode(false);
	Fx.Ranged->ProcessAimRequest(true);
	TestFalse(TEXT("no aim in melee mode"), Fx.Ranged->IsAimed());
	Fx.Ranged->SetRangedMode(true);
	Fx.Ranged->ProcessAimRequest(true);
	TestTrue(TEXT("aim accepted in ranged mode"), Fx.Ranged->IsAimed());
	Fx.Ranged->SetRangedMode(false);
	TestFalse(TEXT("holstering leaves aim"), Fx.Ranged->IsAimed());
	TestFalse(TEXT("holstering clears the tag"), Fx.ASC->HasMatchingGameplayTag(TAG_State_Aiming));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAimNeverSnapsTest, "MazeDemons.Ranged.Aim.AssistNeverSnaps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAimNeverSnapsTest::RunTest(const FString& Parameters)
{
	const float Weights[] = { FMDAimRules::MagnetismWeight(false, 1.0f), FMDAimRules::MagnetismWeight(true, 1.0f), FMDAimRules::MagnetismWeight(true, 0.5f) };
	const float Deltas[] = { 1.0f / 240.0f, 1.0f / 60.0f, 1.0f / 30.0f, 0.1f, 1.0f, 5.0f }; // a hitch must not turn into a jump
	const float Errors[] = { 0.1f, 0.5f, 2.0f, 5.0f, 9.9f, -0.1f, -2.0f, -9.9f };
	for (const float W : Weights)
	{
		for (const float Dt : Deltas)
		{
			for (const float Err : Errors)
			{
				const float Step = FMDAimRules::MagnetismStep(Err, W, Dt, 0.0f);
				const float Cap = FMDAimRules::MagnetismMaxDegPerSec * FMath::Min(Dt, FMDAimRules::MaxAssistDelta);
				TestTrue(TEXT("a step is never longer than the speed cap allows"), FMath::Abs(Step) <= Cap + 1.0e-4f);
				TestTrue(TEXT("a step never passes the target"), FMath::Abs(Step) <= FMath::Abs(Err) + 1.0e-4f);
				TestTrue(TEXT("a step points at the target"), Step == 0.0f || FMath::Sign(Step) == FMath::Sign(Err));
			}
		}
	}

	// Even the strongest weight takes many frames to close 8 degrees, and the error shrinks monotonically without crossing zero.
	float Error = 8.0f;
	int32 Frames = 0;
	float Previous = Error;
	bool bMonotonic = true;
	bool bCrossed = false;
	while (Error > FMDAimRules::MagnetismDeadZoneDeg && Frames < 2000)
	{
		Error -= FMDAimRules::MagnetismStep(Error, FMDAimRules::MagnetismAimed, 1.0f / 60.0f, 0.0f);
		bMonotonic = bMonotonic && Error <= Previous;
		bCrossed = bCrossed || Error < 0.0f;
		Previous = Error;
		++Frames;
	}
	TestTrue(TEXT("converges monotonically"), bMonotonic);
	TestFalse(TEXT("never overshoots the target"), bCrossed);
	TestTrue(TEXT("takes well over a few frames (no snap): >= 0.25 s"), Frames >= 15);
	TestTrue(TEXT("one frame never covers more than the speed cap"), FMDAimRules::MagnetismStep(8.0f, 1.0f, 1.0f / 60.0f, 0.0f) <= 0.5f + 1.0e-4f);

	// Outside the cone, or inside the dead zone, nothing happens.
	TestEqual(TEXT("far off target: no pull"), FMDAimRules::MagnetismStep(FMDAimRules::MagnetismConeDeg + 1.0f, 0.6f, 1.0f / 60.0f, 0.0f), 0.0f);
	TestEqual(TEXT("on target: no pull"), FMDAimRules::MagnetismStep(0.01f, 0.6f, 1.0f / 60.0f, 0.0f), 0.0f);
	TestEqual(TEXT("no weight: no pull"), FMDAimRules::MagnetismStep(5.0f, 0.0f, 1.0f / 60.0f, 0.0f), 0.0f);
	TestEqual(TEXT("no time: no pull"), FMDAimRules::MagnetismStep(5.0f, 0.6f, 0.0f, 0.0f), 0.0f);

	// The player always wins: turning away from the target switches the pull off on that axis, turning toward it keeps it.
	TestEqual(TEXT("turning away: no pull"), FMDAimRules::MagnetismStep(5.0f, 0.6f, 1.0f / 60.0f, -0.4f), 0.0f);
	TestEqual(TEXT("turning away (negative side): no pull"), FMDAimRules::MagnetismStep(-5.0f, 0.6f, 1.0f / 60.0f, 0.4f), 0.0f);
	TestTrue(TEXT("turning toward it: still gently pulled"), FMDAimRules::MagnetismStep(5.0f, 0.6f, 1.0f / 60.0f, 0.4f) > 0.0f);

	// Friction slows, never stops or reverses the look.
	for (float Strength = 0.0f; Strength <= 1.0f; Strength += 0.25f)
	{
		const float Mult = FMDAimRules::FrictionMultiplier(Strength, true);
		TestTrue(TEXT("friction multiplier stays in (0, 1]"), Mult > 0.0f && Mult <= 1.0f);
		TestTrue(TEXT("friction never drops below the design floor"), Mult >= FMDAimRules::FrictionSensitivity - 1.0e-4f);
	}

	// Hard locks already steer the camera: magnetism steps aside.
	TestFalse(TEXT("no magnetism on a hard lock"), FMDAimRules::MagnetismAllowed(true));
	TestTrue(TEXT("magnetism on a soft lock"), FMDAimRules::MagnetismAllowed(false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAimMouseFrictionTest, "MazeDemons.Ranged.Aim.MouseFrictionOnly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAimMouseFrictionTest::RunTest(const FString& Parameters)
{
	const FMDAimAssistKind Mouse = FMDAimRules::KindFor(false);
	TestTrue(TEXT("mouse gets friction"), Mouse.bFriction);
	TestFalse(TEXT("mouse gets no magnetism"), Mouse.bMagnetism);

	TestEqual(TEXT("full strength on the target: sensitivity x0.55"), FMDAimRules::FrictionMultiplier(1.0f, true), 0.55f);
	TestEqual(TEXT("off the target: untouched"), FMDAimRules::FrictionMultiplier(1.0f, false), 1.0f);
	TestEqual(TEXT("assist off (strength 0): untouched"), FMDAimRules::FrictionMultiplier(0.0f, true), 1.0f);
	TestEqual(TEXT("half strength: half way"), FMDAimRules::FrictionMultiplier(0.5f, true), 0.775f);
	TestEqual(TEXT("strength clamps to 1"), FMDAimRules::FrictionMultiplier(5.0f, true), 0.55f);

	// Reticle overlap: the chest sphere's angular radius shrinks with distance but never below the minimum.
	TestTrue(TEXT("3 deg off at 10 m overlaps a 60 cm sphere"), FMDAimRules::ReticleOverlaps(3.0f, 1000.0f, 60.0f));
	TestFalse(TEXT("4 deg off at 10 m does not"), FMDAimRules::ReticleOverlaps(4.0f, 1000.0f, 60.0f));
	TestTrue(TEXT("the minimum overlap angle keeps far targets assistable"), FMDAimRules::ReticleOverlaps(1.4f, 10000.0f, 60.0f));
	TestFalse(TEXT("but not unbounded"), FMDAimRules::ReticleOverlaps(1.6f, 10000.0f, 60.0f));
	TestTrue(TEXT("the sign of the angle does not matter"), FMDAimRules::ReticleOverlaps(-3.0f, 1000.0f, 60.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAimGamepadMagnetismTest, "MazeDemons.Ranged.Aim.GamepadMagnetismOnly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAimGamepadMagnetismTest::RunTest(const FString& Parameters)
{
	const FMDAimAssistKind Pad = FMDAimRules::KindFor(true);
	TestTrue(TEXT("gamepad gets magnetism"), Pad.bMagnetism);
	TestFalse(TEXT("gamepad gets no friction"), Pad.bFriction);

	TestEqual(TEXT("hip weight at full strength"), FMDAimRules::MagnetismWeight(false, 1.0f), 0.35f);
	TestEqual(TEXT("aimed weight at full strength"), FMDAimRules::MagnetismWeight(true, 1.0f), 0.6f);
	TestEqual(TEXT("the option scales it"), FMDAimRules::MagnetismWeight(true, 0.5f), 0.3f);
	TestEqual(TEXT("strength 0 turns it off"), FMDAimRules::MagnetismWeight(true, 0.0f), 0.0f);
	TestTrue(TEXT("aiming pulls harder than the hip"), FMDAimRules::MagnetismWeight(true, 0.7f) > FMDAimRules::MagnetismWeight(false, 0.7f));

	// A stronger weight pulls faster, inside the same cap.
	const float Hip = FMDAimRules::MagnetismStep(5.0f, FMDAimRules::MagnetismWeight(false, 1.0f), 1.0f / 60.0f, 0.0f);
	const float Aimed = FMDAimRules::MagnetismStep(5.0f, FMDAimRules::MagnetismWeight(true, 1.0f), 1.0f / 60.0f, 0.0f);
	TestTrue(TEXT("hip pulls"), Hip > 0.0f);
	TestTrue(TEXT("aimed pulls faster than hip"), Aimed > Hip);
	TestTrue(TEXT("pull toward a target on the left is negative"), FMDAimRules::MagnetismStep(-5.0f, 0.6f, 1.0f / 60.0f, 0.0f) < 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAimServerFlagTest, "MazeDemons.Ranged.Aim.ServerReplicationFlag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAimServerFlagTest::RunTest(const FString& Parameters)
{
	// bAimed is a replicated property (to everyone: other clients render the aimed pose from it) with an OnRep.
	const FProperty* Prop = FindFProperty<FProperty>(UMDRangedComponent::StaticClass(), TEXT("bAimed"));
	if (!TestNotNull(TEXT("bAimed exists"), Prop))
	{
		return false;
	}
	TestTrue(TEXT("bAimed is a net property"), Prop->HasAnyPropertyFlags(CPF_Net));
	TestTrue(TEXT("bAimed has an OnRep"), Prop->HasAnyPropertyFlags(CPF_RepNotify));

	using namespace MDRangedAimTestsLocal;
	FAimFixture Fx;
	if (!Fx.Init(*this, MakeGun()))
	{
		return false;
	}
	// The server sets it, from a request it validated.
	TestFalse(TEXT("not aimed at the start"), Fx.Ranged->IsAimed());
	Fx.Ranged->ProcessAimRequest(true);
	TestTrue(TEXT("the server accepts a legal aim request"), Fx.Ranged->IsAimed());
	TestTrue(TEXT("the aimed pose input is on for the anim BP"), Fx.Ranged->WantsAimedStrafe());
	TestEqual(TEXT("aim offset layer fully on while aimed"), Fx.Ranged->GetAimOffsetAlpha(), 1.0f);
	TestTrue(TEXT("State.Aiming tag follows the flag"), Fx.ASC->HasMatchingGameplayTag(TAG_State_Aiming));
	Fx.Ranged->ProcessAimRequest(false);
	TestFalse(TEXT("release clears it"), Fx.Ranged->IsAimed());
	TestFalse(TEXT("the tag follows"), Fx.ASC->HasMatchingGameplayTag(TAG_State_Aiming));

	// A client without a gun cannot talk the server into an aimed pose.
	Fx.Ranged->SetWeaponDataForTests(nullptr);
	Fx.Ranged->ProcessAimRequest(true);
	TestFalse(TEXT("no gun: refused"), Fx.Ranged->IsAimed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDAimOffsetLayerTest, "MazeDemons.Ranged.Aim.AimOffsetLayer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDAimOffsetLayerTest::RunTest(const FString& Parameters)
{
	// Yaw +-90, pitch +-60, shortest way round.
	FVector2D Off = FMDAimRules::ComputeAimOffset(FRotator(10.0f, 30.0f, 0.0f), FRotator(0.0f, 0.0f, 0.0f));
	TestEqual(TEXT("yaw offset"), static_cast<float>(Off.X), 30.0f);
	TestEqual(TEXT("pitch offset"), static_cast<float>(Off.Y), 10.0f);
	Off = FMDAimRules::ComputeAimOffset(FRotator(80.0f, 170.0f, 0.0f), FRotator(0.0f, 0.0f, 0.0f));
	TestEqual(TEXT("yaw clamps to 90"), static_cast<float>(Off.X), 90.0f);
	TestEqual(TEXT("pitch clamps to 60"), static_cast<float>(Off.Y), 60.0f);
	Off = FMDAimRules::ComputeAimOffset(FRotator(-5.0f, 350.0f, 0.0f), FRotator(0.0f, 10.0f, 0.0f));
	TestEqual(TEXT("wraps through 360"), static_cast<float>(Off.X), -20.0f);
	TestEqual(TEXT("negative pitch"), static_cast<float>(Off.Y), -5.0f);

	// Alpha: on while aimed, for 1.5 s after a shot, never outside ranged mode.
	TestEqual(TEXT("aimed: on"), FMDAimRules::AimOffsetAlpha(true, true, 100.0f), 1.0f);
	TestEqual(TEXT("just fired from the hip: on"), FMDAimRules::AimOffsetAlpha(false, true, 0.2f), 1.0f);
	TestEqual(TEXT("1.5 s after a shot: still on"), FMDAimRules::AimOffsetAlpha(false, true, 1.5f), 1.0f);
	TestEqual(TEXT("after the linger: off"), FMDAimRules::AimOffsetAlpha(false, true, 1.6f), 0.0f);
	TestEqual(TEXT("melee mode: off"), FMDAimRules::AimOffsetAlpha(true, false, 0.0f), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDCameraPresetBlendTest, "MazeDemons.Ranged.Camera.PresetBlendValues", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDCameraPresetBlendTest::RunTest(const FString& Parameters)
{
	const FMDCameraPreset Hip = FMDAimRules::HipPreset(55.0f, 12.0f);
	const FMDCameraPreset Aimed = FMDAimRules::AimedPreset(65.0f, 12.0f, -18.0f, 180.0f, 60.0f);

	const FMDCameraPreset AtHip = FMDAimRules::Blend(Hip, Aimed, 0.0f);
	TestTrue(TEXT("alpha 0 is the hip preset"), AtHip == Hip);
	TestEqual(TEXT("hip has no arm limit"), AtHip.MaxArmCm, 0.0f);
	const FMDCameraPreset AtAim = FMDAimRules::Blend(Hip, Aimed, 1.0f);
	TestTrue(TEXT("alpha 1 is the aimed preset"), AtAim == Aimed);

	const FMDCameraPreset Mid = FMDAimRules::Blend(Hip, Aimed, 0.5f);
	TestEqual(TEXT("mid shoulder shift"), Mid.RightCm, 60.0f);
	TestEqual(TEXT("mid FOV change"), Mid.FovDelta, -9.0f);
	TestEqual(TEXT("mid pitch clamp"), Mid.PitchLimit, 74.95f, 0.01f);
	TestTrue(TEXT("mid arm is between unlimited and the aimed arm"), Mid.MaxArmCm > 180.0f && Mid.MaxArmCm < 1000.0f);
	TestEqual(TEXT("alpha clamps low"), FMDAimRules::Blend(Hip, Aimed, -1.0f).FovDelta, 0.0f);
	TestEqual(TEXT("alpha clamps high"), FMDAimRules::Blend(Hip, Aimed, 2.0f).FovDelta, -18.0f);

	// The arm only ever shortens as the blend advances.
	float PreviousArm = 100000.0f;
	bool bShortening = true;
	for (float A = 0.05f; A <= 1.0f; A += 0.05f)
	{
		const float Arm = FMDAimRules::Blend(Hip, Aimed, A).MaxArmCm;
		bShortening = bShortening && Arm <= PreviousArm + 1.0e-3f;
		PreviousArm = Arm;
	}
	TestTrue(TEXT("the arm limit shortens monotonically while aiming in"), bShortening);

	// The time curve feeds it: 0.09 s into the blend = eased 0.5 = the midpoint values.
	const float Eased = FMDRangedCameraRules::Ease(FMDRangedCameraRules::StepAlpha(0.0f, 1.0f, 0.09f, FMDAimRules::AimBlendSeconds));
	TestEqual(TEXT("midpoint in time is midpoint in value"), FMDAimRules::Blend(Hip, Aimed, Eased).FovDelta, -9.0f);

	// The configured (cvar) presets are the design numbers: 90 -> 72 FOV, 180 cm arm, 60 degree pitch, a bit further out on the shoulder.
	const FMDCameraPreset ConfHip = FMDRangedCameraRules::ConfiguredHipPreset();
	const FMDCameraPreset ConfAim = FMDRangedCameraRules::ConfiguredAimedPreset();
	TestEqual(TEXT("configured aimed FOV change"), ConfAim.FovDelta, -18.0f);
	TestEqual(TEXT("configured aimed arm"), ConfAim.MaxArmCm, 180.0f);
	TestEqual(TEXT("configured aimed pitch clamp"), ConfAim.PitchLimit, 60.0f);
	TestTrue(TEXT("aimed sits further out on the shoulder than hip"), ConfAim.RightCm > ConfHip.RightCm);
	TestEqual(TEXT("hip keeps the rig FOV"), ConfHip.FovDelta, 0.0f);
	TestTrue(TEXT("a named weapon preset falls back to the default one"), FMDRangedCameraRules::ConfiguredAimedPreset(FName(TEXT("NoSuchPreset"))) == ConfAim);

	// Arm clamp.
	const FVector Pivot(0, 0, 0);
	TestTrue(TEXT("a long arm is cut to the limit"), FMDAimRules::ClampArm(Pivot, FVector(-300, 0, 0), 180.0f).Equals(FVector(-180, 0, 0), 0.01));
	TestTrue(TEXT("a short arm is left alone"), FMDAimRules::ClampArm(Pivot, FVector(-100, 0, 0), 180.0f).Equals(FVector(-100, 0, 0), 0.01));
	TestTrue(TEXT("limit 0 = no limit"), FMDAimRules::ClampArm(Pivot, FVector(-900, 0, 0), 0.0f).Equals(FVector(-900, 0, 0), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDCameraCorridorTest, "MazeDemons.Ranged.Camera.CorridorCollision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDCameraCorridorTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("MDCameraCorridorTestWorld")));
	if (!World)
	{
		AddError(TEXT("UWorld::CreateWorld failed"));
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		if (World->IsRooted())
		{
			World->RemoveFromRoot();
		}
	};

	const auto AddWall = [&](const FVector& Center, const FVector& HalfExtent)
	{
		AActor* Wall = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
		Box->SetBoxExtent(HalfExtent);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Wall->SetRootComponent(Box);
		Box->RegisterComponent();
		Box->SetWorldLocation(Center);
		return Wall;
	};
	APawn* Pawn = World->SpawnActor<APawn>();
	if (!Pawn)
	{
		AddError(TEXT("could not spawn the pawn"));
		return false;
	}

	const FVector Pivot(0, 0, 50);
	const FVector Rig(-200, 0, 50);       // the rig's camera, two metres behind
	const FVector Offset(0, 65, 12);      // the aimed right-shoulder shift

	// Open room: nothing changes.
	bool bPulled = true;
	FVector Final = FMDRangedCameraRules::ResolveCollision(World, Pawn, Pivot, Rig, Offset, &bPulled);
	TestFalse(TEXT("open room: nothing pulled in"), bPulled);
	TestTrue(TEXT("open room: camera at rig + shoulder shift"), Final.Equals(Rig + Offset, 0.5));

	// A corridor wall just right of the character (surface at Y = 60): the shoulder shift shrinks and stays on this side of the wall.
	AddWall(FVector(-100, 80, 50), FVector(400, 20, 200));
	Final = FMDRangedCameraRules::ResolveCollision(World, Pawn, Pivot, Rig, Offset, &bPulled);
	TestTrue(TEXT("corridor: the shoulder camera was pulled in"), bPulled);
	TestTrue(TEXT("corridor: the camera did not go through the right wall"), Final.Y < 60.0);
	TestTrue(TEXT("corridor: it still sits on the shoulder side of the character or at its centre line"), Final.Y > -1.0);
	TestTrue(TEXT("corridor: it kept the rig's distance behind"), Final.X < -150.0);

	// A wall behind the character (surface at X = -120): even the rig's own spot is pulled in front of it.
	AddWall(FVector(-170, 0, 50), FVector(50, 400, 200));
	Final = FMDRangedCameraRules::ResolveCollision(World, Pawn, Pivot, Rig, Offset, &bPulled);
	TestTrue(TEXT("dead end: pulled in"), bPulled);
	TestTrue(TEXT("dead end: the camera stays in front of the back wall"), Final.X > -120.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
