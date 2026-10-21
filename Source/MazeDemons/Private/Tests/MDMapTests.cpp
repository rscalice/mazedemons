#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDMinimapTextureLibrary.h"
#include "MDQuestTypes.h"

#include "Misc/AutomationTest.h"

// Pure fog grid and map UV rules; no world.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDMapFogInitAnyBoundsTest, "MazeDemons.Map.FogInitAnyBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDMapFogInitAnyBoundsTest::RunTest(const FString& Parameters)
{
	FMDFogGrid Uninit;
	TestFalse(TEXT("an uninitialised grid is invalid"), Uninit.IsValid());

	FMDFogGrid Zero;
	Zero.Init(FBox2D(FVector2D(5.0, 5.0), FVector2D(5.0, 5.0)), 400.0f);
	TestTrue(TEXT("zero-size bounds give a valid grid"), Zero.IsValid());
	TestEqual(TEXT("zero-size width"), Zero.Width, 1);
	TestEqual(TEXT("zero-size height"), Zero.Height, 1);

	FMDFogGrid Negative;
	Negative.Init(FBox2D(FVector2D(10.0, 10.0), FVector2D(-10.0, -10.0)), 400.0f);
	TestTrue(TEXT("negative-size bounds give a valid grid"), Negative.IsValid());
	TestEqual(TEXT("negative-size width"), Negative.Width, 1);

	FMDFogGrid Huge;
	Huge.Init(FBox2D(FVector2D(0.0, 0.0), FVector2D(1.0e9, 1.0e9)), 400.0f);
	TestEqual(TEXT("huge bounds cap the width at 64"), Huge.Width, 64);
	TestEqual(TEXT("huge bounds cap the height at 64"), Huge.Height, 64);
	TestTrue(TEXT("the cell size was raised to fit"), Huge.CellSize >= 1.0e9f / 64.0f * 0.99f);
	TestEqual(TEXT("64x64 cells are 512 bytes"), Huge.Bits.Num(), 512);

	FMDFogGrid Large;
	Large.Init(FBox2D(FVector2D(0.0, 0.0), FVector2D(100000.0, 100000.0)), 400.0f);
	TestTrue(TEXT("250 cells per axis are capped to 64"), Large.Width == 64 && Large.Height == 64);

	FMDFogGrid Wide;
	Wide.Init(FBox2D(FVector2D(0.0, 0.0), FVector2D(6400.0, 640.0)), 100.0f);
	TestEqual(TEXT("wide grid width"), Wide.Width, 64);
	TestEqual(TEXT("wide grid height"), Wide.Height, 7);

	FMDFogGrid BadCell;
	BadCell.Init(FBox2D(FVector2D(0.0, 0.0), FVector2D(1000.0, 1000.0)), 0.0f);
	TestTrue(TEXT("a zero cell size is raised, never divides by zero"), BadCell.IsValid() && BadCell.CellSize >= 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDMapWorldToCellClampTest, "MazeDemons.Map.WorldToCellClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDMapWorldToCellClampTest::RunTest(const FString& Parameters)
{
	FMDFogGrid Grid;
	TestEqual(TEXT("uninitialised grid has no cells"), Grid.WorldToCell(FVector2D(0.0, 0.0)), -1);

	Grid.Init(FBox2D(FVector2D(0.0, 0.0), FVector2D(1000.0, 1000.0)), 100.0f);
	TestEqual(TEXT("10x10 cells"), Grid.GetNumCells(), 100);
	TestEqual(TEXT("min corner is cell 0"), Grid.WorldToCell(FVector2D(0.0, 0.0)), 0);
	TestEqual(TEXT("max corner clamps to the last cell"), Grid.WorldToCell(FVector2D(1000.0, 1000.0)), 99);
	TestEqual(TEXT("interior point"), Grid.WorldToCell(FVector2D(550.0, 250.0)), 25);
	TestEqual(TEXT("outside on -X"), Grid.WorldToCell(FVector2D(-1.0, 0.0)), -1);
	TestEqual(TEXT("outside on +X"), Grid.WorldToCell(FVector2D(1001.0, 5.0)), -1);
	TestEqual(TEXT("outside on +Y"), Grid.WorldToCell(FVector2D(5.0, 1001.0)), -1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDMapRevealRadiusTest, "MazeDemons.Map.RevealRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDMapRevealRadiusTest::RunTest(const FString& Parameters)
{
	FMDFogGrid Grid;
	Grid.Init(FBox2D(FVector2D(0.0, 0.0), FVector2D(1000.0, 1000.0)), 100.0f);
	TestEqual(TEXT("nothing revealed at first"), Grid.GetRevealedFraction(), 0.0f);

	TestTrue(TEXT("radius 0 still reveals the cell you stand in"), Grid.RevealRadius(FVector2D(50.0, 50.0), 0.0f));
	TestTrue(TEXT("cell 0 revealed"), Grid.IsRevealed(0));
	TestFalse(TEXT("repeating changes nothing"), Grid.RevealRadius(FVector2D(50.0, 50.0), 0.0f));

	// Radius 150 around the centre of cell (5,5): its 4 neighbours and 4 diagonals (distance 141) are in, (3,5) (200) is out.
	TestTrue(TEXT("a larger radius reveals more"), Grid.RevealRadius(FVector2D(550.0, 550.0), 150.0f));
	TestTrue(TEXT("centre cell"), Grid.IsRevealedAt(5, 5));
	TestTrue(TEXT("orthogonal neighbour"), Grid.IsRevealedAt(4, 5));
	TestTrue(TEXT("diagonal neighbour"), Grid.IsRevealedAt(4, 4));
	TestFalse(TEXT("two cells away stays hidden"), Grid.IsRevealedAt(3, 5));
	TestEqual(TEXT("1 + 9 cells of 100 are revealed"), Grid.GetRevealedFraction(), 0.10f, 0.0001f);

	TestTrue(TEXT("a negative radius is treated as 0 (own cell only)"), Grid.RevealRadius(FVector2D(850.0, 850.0), -50.0f));
	TestTrue(TEXT("own cell revealed"), Grid.IsRevealedAt(8, 8));
	TestFalse(TEXT("its neighbours are not"), Grid.IsRevealedAt(7, 8));
	TestFalse(TEXT("a far-away position changes nothing"), Grid.RevealRadius(FVector2D(100000.0, 100000.0), 200.0f));

	TestTrue(TEXT("reveal all changes"), Grid.RevealAll());
	TestEqual(TEXT("everything revealed"), Grid.GetRevealedFraction(), 1.0f, 0.0001f);
	TestFalse(TEXT("reveal all is idempotent"), Grid.RevealAll());
	TestTrue(TEXT("clear hides"), Grid.Clear());
	TestEqual(TEXT("nothing revealed after clear"), Grid.GetRevealedFraction(), 0.0f);

	FMDFogGrid Empty;
	TestFalse(TEXT("an uninitialised grid ignores reveals"), Empty.RevealRadius(FVector2D(0.0, 0.0), 100.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDMapWorldToMapUVTest, "MazeDemons.Map.WorldToMapUV",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDMapWorldToMapUVTest::RunTest(const FString& Parameters)
{
	const FBox2D Bounds(FVector2D(100.0, 200.0), FVector2D(300.0, 600.0));
	const FVector2D Centre = UMDQuestRules::WorldToMapUV(FVector2D(200.0, 400.0), Bounds);
	TestEqual(TEXT("centre U"), static_cast<float>(Centre.X), 0.5f);
	TestEqual(TEXT("centre V"), static_cast<float>(Centre.Y), 0.5f);

	const FVector2D Below = UMDQuestRules::WorldToMapUV(FVector2D(0.0, 0.0), Bounds);
	TestEqual(TEXT("below min clamps U"), static_cast<float>(Below.X), 0.0f);
	TestEqual(TEXT("below min clamps V"), static_cast<float>(Below.Y), 0.0f);

	const FVector2D Above = UMDQuestRules::WorldToMapUV(FVector2D(1000.0, 1000.0), Bounds);
	TestEqual(TEXT("above max clamps U"), static_cast<float>(Above.X), 1.0f);
	TestEqual(TEXT("above max clamps V"), static_cast<float>(Above.Y), 1.0f);

	const FVector2D Degenerate = UMDQuestRules::WorldToMapUV(FVector2D(5.0, 5.0), FBox2D(FVector2D(1.0, 1.0), FVector2D(1.0, 1.0)));
	TestEqual(TEXT("zero-size bounds give (0,0)"), static_cast<float>(Degenerate.X + Degenerate.Y), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDMapFogMergeSharedTest, "MazeDemons.Map.FogMergeShared",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDMapFogMergeSharedTest::RunTest(const FString& Parameters)
{
	const FBox2D Bounds(FVector2D(0.0, 0.0), FVector2D(1000.0, 1000.0));
	FMDFogGrid PlayerA;
	PlayerA.Init(Bounds, 100.0f);
	FMDFogGrid PlayerB;
	PlayerB.Init(Bounds, 100.0f);
	PlayerA.RevealRadius(FVector2D(50.0, 50.0), 0.0f);
	PlayerB.RevealRadius(FVector2D(950.0, 950.0), 0.0f);

	FMDFogGrid Shared;
	Shared.Init(Bounds, 100.0f);
	TestTrue(TEXT("merging player A changes the shared grid"), Shared.MergeBits(PlayerA.Bits));
	TestTrue(TEXT("merging player B changes the shared grid"), Shared.MergeBits(PlayerB.Bits));
	TestTrue(TEXT("the shared grid holds A's cell"), Shared.IsRevealed(0));
	TestTrue(TEXT("the shared grid holds B's cell (the last cell)"), Shared.IsRevealed(99));
	TestEqual(TEXT("exactly the union"), Shared.GetRevealedFraction(), 0.02f);

	TestFalse(TEXT("merging A again is idempotent"), Shared.MergeBits(PlayerA.Bits));
	TestFalse(TEXT("merging B again is idempotent"), Shared.MergeBits(PlayerB.Bits));

	TestFalse(TEXT("merging an empty array changes nothing"), Shared.MergeBits(TArray<uint8>()));
	TArray<uint8> Zeros;
	Zeros.Init(0, Shared.Bits.Num());
	TestFalse(TEXT("merging all-zero bits never hides a cell"), Shared.MergeBits(Zeros));
	TestEqual(TEXT("still the union"), Shared.GetRevealedFraction(), 0.02f);

	TArray<uint8> Ones;
	Ones.Init(0xFF, Shared.Bits.Num());
	Shared.MergeBits(Ones);
	TestEqual(TEXT("padding bits past the last cell never count"), Shared.GetRevealedFraction(), 1.0f);

	// A shorter foreign array only merges the bytes it has.
	FMDFogGrid Short;
	Short.Init(Bounds, 100.0f);
	TArray<uint8> OneByte;
	OneByte.Add(0x01);
	TestTrue(TEXT("a short array merges its prefix"), Short.MergeBits(OneByte));
	TestTrue(TEXT("cell 0 from the prefix"), Short.IsRevealed(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDMinimapSnapshotRulesTest, "MazeDemons.Map.MinimapSnapshotRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDMinimapSnapshotRulesTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("no texture yet needs one"), UMDMinimapTextureLibrary::NeedsNewTexture(0, 0, 1024, 1024));
	TestTrue(TEXT("a size change needs a new texture"), UMDMinimapTextureLibrary::NeedsNewTexture(512, 512, 1024, 1024));
	TestFalse(TEXT("the same size is reused"), UMDMinimapTextureLibrary::NeedsNewTexture(1024, 1024, 1024, 1024));

	TestTrue(TEXT("never baked is due"), UMDMinimapTextureLibrary::IsRebakeDue(5.0, -1.0, 1.0));
	TestFalse(TEXT("inside the interval is not due"), UMDMinimapTextureLibrary::IsRebakeDue(5.5, 5.0, 1.0));
	TestTrue(TEXT("after the interval is due"), UMDMinimapTextureLibrary::IsRebakeDue(6.0, 5.0, 1.0));
	TestTrue(TEXT("a clock reset is due"), UMDMinimapTextureLibrary::IsRebakeDue(1.0, 5.0, 1.0));

	// A null render target leaves the previous texture untouched.
	TestNull(TEXT("null target and null reuse give null"), UMDMinimapTextureLibrary::CopyRenderTargetToTexture(nullptr, nullptr));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
