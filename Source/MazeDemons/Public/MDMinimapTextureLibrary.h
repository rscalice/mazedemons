#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MDMinimapTextureLibrary.generated.h"

class UTexture2D;
class UTextureRenderTarget2D;

// Runtime (packaged-build safe) bridge between our minimap render target and ProHUD.
// ProHUD's WB_Minimap reads its map size by casting the map brush to UTexture2D, so a
// render target or material cannot be fed to it directly. CopyRenderTargetToTexture reads
// the render target back into a transient UTexture2D. Pass the previous result as Reuse so
// the same texture object is updated in place (ProHUD's brush keeps pointing at it).
// The read-back flushes the render thread: call it only when the capture or fog changes.
UCLASS()
class MAZEDEMONS_API UMDMinimapTextureLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Copies RenderTarget into Reuse (or a new transient texture when Reuse is null or a
	// different size). Returns the texture holding the pixels, or Reuse unchanged on failure.
	UFUNCTION(BlueprintCallable, Category = "Maze Demons|Map")
	static UTexture2D* CopyRenderTargetToTexture(UTextureRenderTarget2D* RenderTarget, UTexture2D* Reuse);

	// Pure rule: a new texture is needed when there is none or its size differs from the target.
	static bool NeedsNewTexture(int32 ExistingX, int32 ExistingY, int32 TargetX, int32 TargetY);

	// Pure rule for throttling re-bakes: due when never baked or MinInterval seconds have passed.
	UFUNCTION(BlueprintPure, Category = "Maze Demons|Map")
	static bool IsRebakeDue(double Now, double LastBakeTime, double MinInterval);
};
