#include "MDMinimapTextureLibrary.h"

#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "UObject/Package.h"

bool UMDMinimapTextureLibrary::NeedsNewTexture(int32 ExistingX, int32 ExistingY, int32 TargetX, int32 TargetY)
{
	return ExistingX <= 0 || ExistingY <= 0 || ExistingX != TargetX || ExistingY != TargetY;
}

bool UMDMinimapTextureLibrary::IsRebakeDue(double Now, double LastBakeTime, double MinInterval)
{
	return LastBakeTime < 0.0 || Now - LastBakeTime >= MinInterval || Now < LastBakeTime;
}

UTexture2D* UMDMinimapTextureLibrary::CopyRenderTargetToTexture(UTextureRenderTarget2D* RenderTarget, UTexture2D* Reuse)
{
	if (!RenderTarget)
	{
		return Reuse;
	}
	FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
	const int32 Width = RenderTarget->SizeX;
	const int32 Height = RenderTarget->SizeY;
	if (!Resource || Width <= 0 || Height <= 0)
	{
		return Reuse;
	}

	// Keep the values linear (no gamma conversion) and mark the texture SRGB = false below, so the snapshot
	// shows exactly what the render target holds, for both 8-bit and float targets.
	TArray<FColor> Pixels;
	FReadSurfaceDataFlags Flags(RCM_UNorm);
	Flags.SetLinearToGamma(false);
	if (!Resource->ReadPixels(Pixels, Flags) || Pixels.Num() != Width * Height)
	{
		return Reuse;
	}
	for (FColor& Pixel : Pixels)
	{
		Pixel.A = 255; // the capture clears alpha; the minimap wants an opaque map
	}

	UTexture2D* Texture = Reuse;
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0
		|| NeedsNewTexture(Texture->GetSizeX(), Texture->GetSizeY(), Width, Height))
	{
		Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8,
			MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("MDMinimapSnapshot")));
		if (!Texture)
		{
			return Reuse;
		}
		Texture->SRGB = false;
		Texture->Filter = TF_Bilinear;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
	}

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}
