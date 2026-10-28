// Debug tooling for the held-gun visuals (non-shipping only): console commands to dump the hand / gun / arm transforms, tune the
// grip offsets live, park a camera next to the player and save screenshots, plus an optional command-file watcher so a script can
// drive a standalone game ("UnrealEditor.exe MazeDemons.uproject <map> -game -MDCmdFile": it executes Saved/ClaudeDump/md_cmd.txt,
// one console command per line, "wait <seconds>" pauses, and appends the output to Saved/ClaudeDump/md_cmd.log).
//
//   md.Ranged.Dump                               hand / gun / arm transforms, support grip distance, IK weights
//   md.Ranged.Tune Lx Ly Lz Pitch Yaw Roll Scale weapon mesh relative to the hand socket (live)
//   md.Ranged.TuneSupport X Y Z                  support (left hand) grip in weapon-mesh space (live)
//   md.Ranged.Cam RelYaw Pitch Dist [body|gun|hands] [Fov] | off     camera parked relative to the character (RelYaw 0 = behind, -90 = right side, 180 = front)
//   md.Ranged.Shot Name [ui]                     Saved/Screenshots/guns_v2_<Name>.png
//   md.Ranged.Walk Seconds                       walk forward for that long
//   md.Ranged.Swap / md.Ranged.Aim 0|1           ranged-mode swap (same as Q) and aim flag (authority)

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraActor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "UObject/StrongObjectPtr.h"
#include "Camera/CameraComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Modules/ModuleManager.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MDInventoryTypes.h"
#include "Ranged/MDRangedComponent.h"
#include "Ranged/MDRangedWeaponData.h"
#include "UnrealClient.h"

namespace MDRangedDebug
{
	struct FState
	{
		TWeakObjectPtr<ASceneCapture2D> Cam;
		TStrongObjectPtr<UTextureRenderTarget2D> Target;
		float WalkSecondsLeft = 0.0f;
		TArray<FString> Queue;
		float WaitLeft = 0.0f;
		float PollAccum = 0.0f;
	};

	FState& State()
	{
		static FState S;
		return S;
	}

	FString DumpDir()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ClaudeDump"));
	}

	void AppendLog(const FString& Text)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MDRangedDebug] %s"), *Text);
		FFileHelper::SaveStringToFile(Text + LINE_TERMINATOR, *(DumpDir() / TEXT("md_cmd.log")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_Append);
	}

	UWorld* FindGameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World() && Context.World()->HasBegunPlay())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	APlayerController* GetPC(UWorld* World)
	{
		return World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	}

	UMDRangedComponent* GetRanged(UWorld* World)
	{
		APlayerController* PC = GetPC(World);
		return PC ? UMDRangedComponent::Find(PC->GetPawn() ? static_cast<const AActor*>(PC->GetPawn()) : static_cast<const AActor*>(PC)) : nullptr;
	}

	FString V(const FVector& In) { return FString::Printf(TEXT("(%.2f, %.2f, %.2f)"), In.X, In.Y, In.Z); }
	FString R(const FRotator& In) { return FString::Printf(TEXT("(P%.1f Y%.1f R%.1f)"), In.Pitch, In.Yaw, In.Roll); }

	void DescribeSocket(const USkeletalMeshComponent* Mesh, FName Name, FString& Out)
	{
		if (!Mesh->DoesSocketExist(Name) && Mesh->GetBoneIndex(Name) == INDEX_NONE)
		{
			Out += FString::Printf(TEXT("  %-12s missing\n"), *Name.ToString());
			return;
		}
		const FTransform World = Mesh->GetSocketTransform(Name, RTS_World);
		const FTransform Comp = Mesh->GetSocketTransform(Name, RTS_Component);
		Out += FString::Printf(TEXT("  %-12s world %s  comp %s  axes X%s Y%s Z%s  (%s)\n"), *Name.ToString(), *V(World.GetLocation()), *V(Comp.GetLocation()),
			*V(World.GetUnitAxis(EAxis::X)), *V(World.GetUnitAxis(EAxis::Y)), *V(World.GetUnitAxis(EAxis::Z)),
			Mesh->DoesSocketExist(Name) ? TEXT("socket") : TEXT("bone"));
	}

	void Dump(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		APlayerController* PC = GetPC(World);
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		UMDRangedComponent* Ranged = GetRanged(World);
		FString Out = TEXT("---- md.Ranged.Dump ----\n");
		if (!Pawn || !Ranged)
		{
			Ar.Log(TEXT("md.Ranged.Dump: no pawn / ranged component"));
			return;
		}
		USkeletalMeshComponent* Mesh = UMDRangedComponent::FindVisibleMesh(Pawn);
		Out += FString::Printf(TEXT("pawn %s loc %s yaw %.1f\n"), *Pawn->GetName(), *V(Pawn->GetActorLocation()), Pawn->GetActorRotation().Yaw);
		Out += FString::Printf(TEXT("ranged mode %d aimed %d stance %d mags(ranged slot) %d\n"), Ranged->IsRangedMode(), Ranged->IsAimed(),
			(int32)Ranged->GetRangedStance(), Ranged->GetMagRounds(MDItemType::RangedSlot));
		if (!Mesh)
		{
			Ar.Log(Out + TEXT("no visible mesh"));
			return;
		}
		Out += FString::Printf(TEXT("visible mesh %s anim %s world %s scale %s rel %s\n"), *Mesh->GetName(),
			Mesh->GetAnimClass() ? *Mesh->GetAnimClass()->GetName() : TEXT("none"), *V(Mesh->GetComponentLocation()),
			*V(Mesh->GetComponentScale()), *V(Mesh->GetRelativeLocation()));
		if (const ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			const UCharacterMovementComponent* Move = Character->GetCharacterMovement();
			Out += FString::Printf(TEXT("movement: mode %d velocity %s speed %.1f maxWalk %.1f tickEnabled %d moveIgnored %d inputEnabled %d controller %s\n"), (int32)Move->MovementMode.GetValue(),
				*V(Move->Velocity), Move->Velocity.Size(), Move->MaxWalkSpeed, Move->IsComponentTickEnabled(), PC->IsMoveInputIgnored(), Pawn->InputEnabled(),
				Pawn->GetController() ? *Pawn->GetController()->GetName() : TEXT("none"));
		}
		TArray<USkeletalMeshComponent*> AllMeshes;
		Pawn->GetComponents(AllMeshes);
		for (USkeletalMeshComponent* Other : AllMeshes)
		{
			if (Other != Mesh && Other->GetAnimInstance() && Other->DoesSocketExist(TEXT("hand_r")))
			{
				const UAnimInstance* Anim = Other->GetAnimInstance();
				Out += FString::Printf(TEXT("source mesh %s (%s): hand_r comp %s hand_l comp %s, montage playing %d, active montage %s\n"), *Other->GetName(), *Anim->GetClass()->GetName(),
					*V(Other->GetSocketTransform(TEXT("hand_r"), RTS_Component).GetLocation()), *V(Other->GetSocketTransform(TEXT("hand_l"), RTS_Component).GetLocation()),
					Anim->IsAnyMontagePlaying(), Anim->GetCurrentActiveMontage() ? *Anim->GetCurrentActiveMontage()->GetName() : TEXT("none"));
			}
		}
		if (const UAnimInstance* VisibleAnim = Mesh->GetAnimInstance())
		{
			Out += FString::Printf(TEXT("visible mesh montage playing %d, active montage %s\n"), VisibleAnim->IsAnyMontagePlaying(),
				VisibleAnim->GetCurrentActiveMontage() ? *VisibleAnim->GetCurrentActiveMontage()->GetName() : TEXT("none"));
		}
		for (const TCHAR* Name : { TEXT("MainHand"), TEXT("hand_r"), TEXT("hand_l"), TEXT("lowerarm_l"), TEXT("upperarm_l"), TEXT("lowerarm_r"), TEXT("upperarm_r"), TEXT("middle_01_r") })
		{
			DescribeSocket(Mesh, FName(Name), Out);
		}
		if (const UMeshComponent* Gun = Ranged->GetWeaponMesh())
		{
			const FBoxSphereBounds B = Gun->Bounds;
			Out += FString::Printf(TEXT("gun %s attach %s socket %s\n  world %s rot %s scale %s\n  relative loc %s rot %s scale %s\n  bounds origin %s extent %s (full length along its longest axis %.1f cm)\n"),
				*Gun->GetName(), Gun->GetAttachParent() ? *Gun->GetAttachParent()->GetName() : TEXT("none"), *Gun->GetAttachSocketName().ToString(),
				*V(Gun->GetComponentLocation()), *R(Gun->GetComponentRotation()), *V(Gun->GetComponentScale()),
				*V(Gun->GetRelativeLocation()), *R(Gun->GetRelativeRotation()), *V(Gun->GetRelativeScale3D()),
				*V(B.Origin), *V(B.BoxExtent), B.BoxExtent.GetMax() * 2.0f);
			FTransform Grip;
			if (Ranged->GetSupportGripWorldTransform(Grip))
			{
				const FVector Shoulder = Mesh->GetBoneLocation(TEXT("upperarm_l"), EBoneSpaces::WorldSpace);
				const FVector Elbow = Mesh->GetBoneLocation(TEXT("lowerarm_l"), EBoneSpaces::WorldSpace);
				const FVector Hand = Mesh->GetBoneLocation(TEXT("hand_l"), EBoneSpaces::WorldSpace);
				const float Arm = FVector::Dist(Shoulder, Elbow) + FVector::Dist(Elbow, Hand);
				const float Dist = FVector::Dist(Shoulder, Grip.GetLocation());
				Out += FString::Printf(TEXT("support grip world %s  shoulder->grip %.1f cm  arm length %.1f cm  reach alpha %.2f  support alpha %.2f\n"),
					*V(Grip.GetLocation()), Dist, Arm, FMDRangedRules::SupportHandReachAlpha(Dist, Arm), Ranged->GetSupportHandAlpha(0.0f));
			}
			else
			{
				Out += TEXT("support grip: none (one-hander or not available)\n");
			}
		}
		else
		{
			Out += TEXT("no gun mesh component\n");
		}
		AppendLog(Out);
		Ar.Log(Out);
	}

	bool ParseFloats(const TArray<FString>& Args, int32 Count, TArray<float>& Out)
	{
		if (Args.Num() < Count)
		{
			return false;
		}
		for (int32 i = 0; i < Count; ++i)
		{
			Out.Add(FCString::Atof(*Args[i]));
		}
		return true;
	}

	void Tune(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		TArray<float> F;
		UMDRangedComponent* Ranged = GetRanged(World);
		if (!Ranged || !ParseFloats(Args, 7, F))
		{
			Ar.Log(TEXT("usage: md.Ranged.Tune Lx Ly Lz Pitch Yaw Roll Scale [Socket]"));
			return;
		}
		Ranged->DebugSetHandOffset(FTransform(FRotator(F[3], F[4], F[5]), FVector(F[0], F[1], F[2]), FVector(F[6])), Args.IsValidIndex(7) ? FName(*Args[7]) : NAME_None);
		Ar.Log(TEXT("tuned hand offset"));
	}

	void TuneSupport(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		TArray<float> F;
		UMDRangedComponent* Ranged = GetRanged(World);
		if (!Ranged || !ParseFloats(Args, 3, F))
		{
			Ar.Log(TEXT("usage: md.Ranged.TuneSupport X Y Z"));
			return;
		}
		Ranged->DebugSetSupportOffset(FTransform(FRotator::ZeroRotator, FVector(F[0], F[1], F[2]), FVector::OneVector));
		Ar.Log(TEXT("tuned support grip"));
	}

	struct FCamSettings
	{
		bool bActive = false;
		bool bControl = false;
		float RelYaw = 0.0f;
		float Pitch = 0.0f;
		float Dist = 200.0f;
		FString Focus = TEXT("body");
		float Fov = 35.0f;
	};

	FCamSettings& CamSettings()
	{
		static FCamSettings C;
		return C;
	}

	// Places the camera relative to the pawn; called by md.Ranged.Cam and every tick so it follows a walking pawn.
	void ApplyCam(UWorld* World)
	{
		APlayerController* PC = GetPC(World);
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		const FCamSettings& C = CamSettings();
		if (!PC || !Pawn || !C.bActive)
		{
			return;
		}
		if (C.bControl)
		{
			PC->SetControlRotation(FRotator(C.Pitch, Pawn->GetActorRotation().Yaw + C.RelYaw, 0.0f));
			if (PC->PlayerCameraManager && C.Fov > 0.0f)
			{
				PC->PlayerCameraManager->SetFOV(C.Fov);
			}
			return;
		}
		FState& S = State();
		FVector Target = Pawn->GetActorLocation() + FVector(0, 0, 15);
		const USkeletalMeshComponent* Mesh = UMDRangedComponent::FindVisibleMesh(Pawn);
		const UMDRangedComponent* Ranged = GetRanged(World);
		if (C.Focus.Equals(TEXT("gun"), ESearchCase::IgnoreCase) && Ranged && Ranged->GetWeaponMesh())
		{
			Target = Ranged->GetWeaponMesh()->Bounds.Origin;
		}
		else if (C.Focus.Equals(TEXT("hands"), ESearchCase::IgnoreCase) && Mesh)
		{
			Target = (Mesh->GetBoneLocation(TEXT("hand_r"), EBoneSpaces::WorldSpace) + Mesh->GetBoneLocation(TEXT("hand_l"), EBoneSpaces::WorldSpace)) * 0.5f;
		}
		const FRotator Look(C.Pitch, Pawn->GetActorRotation().Yaw + C.RelYaw, 0.0f);
		const FVector Loc = Target - Look.Vector() * C.Dist;
		if (!S.Cam.IsValid())
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			S.Cam = World->SpawnActor<ASceneCapture2D>(Loc, Look, Params);
			if (ASceneCapture2D* Spawned = S.Cam.Get())
			{
				USceneCaptureComponent2D* Capture = Spawned->GetCaptureComponent2D();
				Capture->bCaptureEveryFrame = false;
				Capture->bCaptureOnMovement = false;
				Capture->CaptureSource = SCS_FinalColorLDR;
				if (!S.Target.IsValid())
				{
					UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>();
					RenderTarget->InitCustomFormat(1280, 720, PF_B8G8R8A8, false);
					RenderTarget->bAutoGenerateMips = false;
					RenderTarget->UpdateResourceImmediate(true);
					S.Target.Reset(RenderTarget);
				}
				Capture->TextureTarget = S.Target.Get();
			}
		}
		if (ASceneCapture2D* Camera = S.Cam.Get())
		{
			Camera->SetActorLocationAndRotation(Loc, Look);
			Camera->GetCaptureComponent2D()->FOVAngle = C.Fov;
		}
	}

	void Cam(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		APlayerController* PC = GetPC(World);
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!PC || !Pawn)
		{
			return;
		}
		FCamSettings& C = CamSettings();
		if (Args.Num() >= 1 && Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			C.bActive = false;
			PC->SetViewTargetWithBlend(Pawn, 0.0f);
			if (State().Cam.IsValid())
			{
				State().Cam->Destroy();
			}
			return;
		}
		TArray<float> F;
		if (!ParseFloats(Args, 3, F))
		{
			Ar.Log(TEXT("usage: md.Ranged.Cam RelYaw Pitch Dist [body|gun|hands] [Fov] | off"));
			return;
		}
		C.bActive = true;
		C.bControl = false;
		C.RelYaw = F[0];
		C.Pitch = F[1];
		C.Dist = F[2];
		C.Focus = Args.IsValidIndex(3) ? Args[3] : TEXT("body");
		C.Fov = Args.IsValidIndex(4) ? FCString::Atof(*Args[4]) : 35.0f;
		ApplyCam(World);
	}

	// Orbits the regular gameplay camera instead of spawning a camera actor: View RelYaw Pitch [Fov] | off.
	void View(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		FCamSettings& C = CamSettings();
		if (Args.Num() >= 1 && Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			C.bActive = false;
			if (APlayerController* PC = GetPC(World); PC && PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->UnlockFOV();
			}
			return;
		}
		TArray<float> F;
		if (!ParseFloats(Args, 2, F))
		{
			Ar.Log(TEXT("usage: md.Ranged.View RelYaw Pitch [Fov] | off   (RelYaw 0 = behind, -90 = from the right, 180 = from the front)"));
			return;
		}
		C.bActive = true;
		C.bControl = true;
		C.RelYaw = F[0];
		C.Pitch = F[1];
		C.Fov = Args.IsValidIndex(2) ? FCString::Atof(*Args[2]) : 0.0f;
		ApplyCam(World);
	}
	// Turns the pawn on the spot (the gameplay camera stays put): a cheap way to get side / front views: Face Yaw.
	void Face(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		APlayerController* PC = GetPC(World);
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (Pawn && Args.Num())
		{
			Pawn->SetActorRotation(FRotator(0.0f, FCString::Atof(*Args[0]), 0.0f));
			Ar.Logf(TEXT("pawn yaw %.1f, control yaw %.1f, view target %s, camera manager %s"), Pawn->GetActorRotation().Yaw, PC->GetControlRotation().Yaw,
				PC->GetViewTarget() ? *PC->GetViewTarget()->GetName() : TEXT("none"), PC->PlayerCameraManager ? *PC->PlayerCameraManager->GetClass()->GetName() : TEXT("none"));
		}
	}
	// Makes every skeletal mesh on the pawn animate even while hidden / off screen (the hidden source mesh feeds the retarget).
	void AnimTick(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		APlayerController* PC = GetPC(World);
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn)
		{
			return;
		}
		TArray<USkeletalMeshComponent*> Meshes;
		Pawn->GetComponents(Meshes);
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			Ar.Logf(TEXT("%s: tick option %d, visible %d, hiddenInGame %d, anim %s"), *Mesh->GetName(), (int32)Mesh->VisibilityBasedAnimTickOption,
				Mesh->IsVisible(), Mesh->bHiddenInGame, Mesh->GetAnimClass() ? *Mesh->GetAnimClass()->GetName() : TEXT("none"));
			if (const UAnimInstance* Anim = Mesh->GetAnimInstance(); Anim && Args.Num() && Args[0] == TEXT("props"))
			{
				for (TFieldIterator<FProperty> It(Anim->GetClass()); It; ++It)
				{
					const FString Name = It->GetName();
					if (Name.Contains(TEXT("tance")) || Name.Contains(TEXT("Aim")) || Name.Contains(TEXT("Ranged")) || Name.Contains(TEXT("Gait")) || Name.Contains(TEXT("Weapon")) || Name.Contains(TEXT("Mode")) || Name.Contains(TEXT("Stance")))
					{
						FString Value;
						It->ExportText_InContainer(0, Value, Anim, Anim, nullptr, PPF_None);
						Ar.Logf(TEXT("    %s.%s = %s"), *Mesh->GetName(), *Name, *Value);
					}
				}
			}
			if (Args.Num() && FCString::Atoi(*Args[0]) != 0)
			{
				Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			}
		}
	}
	void Shot(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (Args.Num() < 1)
		{
			Ar.Log(TEXT("usage: md.Ranged.Shot Name [ui]"));
			return;
		}
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots"));
		IFileManager::Get().MakeDirectory(*Dir, true);
		const FString Path = Dir / (TEXT("guns_v2_") + Args[0] + TEXT(".png"));
		// With a Cam active the shot is rendered by that scene capture (independent of the gameplay camera); otherwise the viewport is grabbed.
		ASceneCapture2D* Camera = State().Cam.Get();
		if (Camera && CamSettings().bActive && !CamSettings().bControl)
		{
			ApplyCam(World);
			Camera->GetCaptureComponent2D()->CaptureScene();
			if (TUniquePtr<FArchive> Writer = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*Path)))
			{
				FImageUtils::ExportRenderTarget2DAsPNG(State().Target.Get(), *Writer);
			}
			Ar.Log(TEXT("scene capture saved: ") + Path);
			return;
		}
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		Ar.Log(TEXT("screenshot requested: ") + Path);
	}

	void Walk(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		State().WalkSecondsLeft = Args.Num() ? FCString::Atof(*Args[0]) : 1.0f;
	}

	void Swap(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (UMDRangedComponent* Ranged = GetRanged(World))
		{
			Ar.Log(Ranged->TrySwapMode() ? TEXT("swapped ranged mode") : TEXT("swap refused"));
		}
	}

	void Aim(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (UMDRangedComponent* Ranged = GetRanged(World))
		{
			Ranged->SetAimed(Args.Num() && FCString::Atoi(*Args[0]) != 0);
			Ar.Logf(TEXT("aimed = %d"), Ranged->IsAimed());
		}
	}

	bool TickWatcher(float DeltaTime)
	{
		FState& S = State();
		UWorld* World = FindGameWorld();
		APlayerController* PC = GetPC(World);
		if (!PC)
		{
			return true;
		}
		ApplyCam(World);
		// Walk holds the W key down through the real input path (so the Enhanced Input mapping, gait and motion matching all run as in play).
		static bool bKeyDown = false;
		if (S.WalkSecondsLeft > 0.0f && PC->GetPawn())
		{
			S.WalkSecondsLeft -= DeltaTime;
			if (!bKeyDown)
			{
				bKeyDown = true;
				PC->InputKey(FInputKeyParams(EKeys::W, EInputEvent::IE_Pressed, 1.0));
			}
			PC->ResetIgnoreMoveInput();
			PC->GetPawn()->AddMovementInput(FRotator(0.0f, PC->GetControlRotation().Yaw, 0.0f).Vector(), 1.0f, true);
		}
		else if (bKeyDown)
		{
			bKeyDown = false;
			PC->InputKey(FInputKeyParams(EKeys::W, EInputEvent::IE_Released, 0.0));
		}
		if (S.WaitLeft > 0.0f)
		{
			S.WaitLeft -= DeltaTime;
			return true;
		}
		if (S.Queue.IsEmpty())
		{
			S.PollAccum += DeltaTime;
			if (S.PollAccum < 0.2f)
			{
				return true;
			}
			S.PollAccum = 0.0f;
			const FString File = DumpDir() / TEXT("md_cmd.txt");
			FString Text;
			if (IFileManager::Get().FileExists(*File) && FFileHelper::LoadFileToString(Text, *File))
			{
				IFileManager::Get().Delete(*File);
				Text.ParseIntoArrayLines(S.Queue);
			}
			return true;
		}
		const FString Line = S.Queue[0].TrimStartAndEnd();
		S.Queue.RemoveAt(0);
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
		{
			return true;
		}
		if (Line.StartsWith(TEXT("wait ")))
		{
			S.WaitLeft = FCString::Atof(*Line.Mid(5));
			return true;
		}
		const FString Result = PC->ConsoleCommand(Line, false);
		AppendLog(FString::Printf(TEXT("> %s\n%s"), *Line, *Result));
		return true;
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdDump(TEXT("md.Ranged.Dump"), TEXT("Dump hand / gun / arm transforms."), FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Dump));
	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdTune(TEXT("md.Ranged.Tune"), TEXT("Lx Ly Lz Pitch Yaw Roll Scale: live weapon offset."), FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Tune));
	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdTuneSupport(TEXT("md.Ranged.TuneSupport"), TEXT("X Y Z: live support grip."), FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&TuneSupport));
	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdCam(TEXT("md.Ranged.Cam"), TEXT("RelYaw Pitch Dist [body|gun|hands] [Fov] | off"), FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Cam));
	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdAnimTick(TEXT("md.Ranged.AnimTick"), TEXT("[1]: list pawn meshes' anim tick options; 1 forces AlwaysTickPoseAndRefreshBones."), FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&AnimTick));
	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdView(TEXT("md.Ranged.View"), TEXT("RelYaw Pitch [Fov] | off: orbit the gameplay camera."), FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&View));
	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdFace(TEXT("md.Ranged.Face"), TEXT("Yaw: turn the pawn on the spot."), FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Face));
	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdShot(TEXT("md.Ranged.Shot"), TEXT("Name [ui]: Saved/Screenshots/guns_v2_<Name>.png"), FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Shot));
	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdWalk(TEXT("md.Ranged.Walk"), TEXT("Seconds: walk forward."), FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Walk));
	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdSwap(TEXT("md.Ranged.Swap"), TEXT("Swap melee / ranged mode (authority)."), FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Swap));
	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdAim(TEXT("md.Ranged.Aim"), TEXT("0|1: aim flag (authority)."), FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Aim));

#if WITH_EDITOR
	// A standalone -game run does not load editor modules, so ABP_GenericRetarget (it holds the editor-side anim graph node of the
	// support-hand IK) fails to load and the character stays in the reference pose. Load the module before the map does.
	FDelayedAutoRegisterHelper EditorModuleRegistration(EDelayedRegisterRunPhase::ObjectSystemReady, []()
	{
		if (!GIsEditor && FParse::Param(FCommandLine::Get(), TEXT("MDCmdFile")))
		{
			FModuleManager::Get().LoadModule(TEXT("MazeDemonsEditor"));
		}
	});
#endif

	// Command-file watcher: only when the game was started with -MDCmdFile.
	FDelayedAutoRegisterHelper WatcherRegistration(EDelayedRegisterRunPhase::EndOfEngineInit, []()
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("MDCmdFile")))
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickWatcher), 0.0f);
			AppendLog(TEXT("MDCmdFile watcher active"));
		}
	});
}

#endif // !UE_BUILD_SHIPPING
