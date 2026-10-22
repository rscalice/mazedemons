#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MDMapPlan.h"
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
	TestEqual(TEXT("huge bounds cap the width at 128"), Huge.Width, 128);
	TestEqual(TEXT("huge bounds cap the height at 128"), Huge.Height, 128);
	TestTrue(TEXT("the cell size was raised to fit"), Huge.CellSize >= 1.0e9f / 128.0f * 0.99f);
	TestEqual(TEXT("128x128 cells are 2048 bytes"), Huge.Bits.Num(), 2048);

	FMDFogGrid Large;
	Large.Init(FBox2D(FVector2D(0.0, 0.0), FVector2D(100000.0, 100000.0)), 400.0f);
	TestTrue(TEXT("250 cells per axis are capped to 128"), Large.Width == 128 && Large.Height == 128);

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

// ---- Whole-level map (u4-design §9) ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDMapLevelFogGridTest, "MazeDemons.Map.LevelFogGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDMapLevelFogGridTest::RunTest(const FString& Parameters)
{
	// L_MazeDemonsPrototype: x -100..14500, y +-800, squared around its centre like AMDLevelMap::GetMapBounds.
	const FBox2D Level(FVector2D(-100.0, -800.0), FVector2D(14500.0, 800.0));
	const FBox2D Square = FMDMapPlanRules::MakeSquare(Level);
	FMDFogGrid Grid;
	Grid.Init(Square, 200.0f);
	TestEqual(TEXT("14600 / 200 = 73 cells wide, not capped"), Grid.Width, 73);
	TestEqual(TEXT("square grid"), Grid.Height, 73);
	TestEqual(TEXT("cell size kept"), Grid.CellSize, 200.0f);
	TestTrue(TEXT("at most 2 KB of bits"), Grid.Bits.Num() <= 2048);

	// A tunnel between rooms (no zone there) is inside the level grid and reveals.
	const FVector2D Tunnel(2400.0, 0.0);
	TestTrue(TEXT("tunnel is a cell of the level grid"), Grid.WorldToCell(Tunnel) >= 0);
	TestTrue(TEXT("revealing in a tunnel changes bits"), Grid.RevealRadius(Tunnel, 1200.0f));
	TestTrue(TEXT("the tunnel cell is revealed"), Grid.IsRevealed(Grid.WorldToCell(Tunnel)));
	TestFalse(TEXT("the far room is still hidden"), Grid.IsRevealed(Grid.WorldToCell(FVector2D(13600.0, 0.0))));

	// A longer generated level raises the cell size instead of exceeding 128 cells.
	FMDFogGrid Long;
	Long.Init(FMDMapPlanRules::MakeSquare(FBox2D(FVector2D(0.0, 0.0), FVector2D(60000.0, 2000.0))), 200.0f);
	TestEqual(TEXT("a 60k level is capped at 128 cells"), Long.Width, 128);
	TestTrue(TEXT("its cells grew to fit"), Long.CellSize >= 60000.0f / 128.0f - 0.01f);

	// Layout comparison decides whether replicated bits still line up.
	FMDFogGrid Same;
	Same.Init(Square, 200.0f);
	TestTrue(TEXT("identical init has the same layout"), Grid.HasSameLayout(Same));
	FMDFogGrid Moved;
	Moved.Init(Square.ShiftBy(FVector2D(500.0, 0.0)), 200.0f);
	TestFalse(TEXT("moved bounds are a different layout"), Grid.HasSameLayout(Moved));
	FMDFogGrid Coarser;
	Coarser.Init(Square, 400.0f);
	TestFalse(TEXT("another cell size is a different layout"), Grid.HasSameLayout(Coarser));
	TestFalse(TEXT("an invalid grid never matches"), FMDFogGrid().HasSameLayout(Grid));

	// Replicated layout round trip.
	FMDFogLevelBits Bits;
	TestFalse(TEXT("empty replicated fog has no layout"), Bits.HasLayout());
	Bits.BoundsMin = Grid.Bounds.Min;
	Bits.BoundsMax = Grid.Bounds.Max;
	Bits.CellSize = Grid.CellSize;
	Bits.Bits = Grid.Bits;
	TestTrue(TEXT("filled replicated fog has a layout"), Bits.HasLayout());
	FMDFogGrid Client;
	Client.Init(FBox2D(Bits.BoundsMin, Bits.BoundsMax), Bits.CellSize);
	Client.MergeBits(Bits.Bits);
	TestTrue(TEXT("client grid matches the server layout"), Client.HasSameLayout(Grid));
	TestEqual(TEXT("client sees the same reveal"), Client.GetRevealedFraction(), Grid.GetRevealedFraction());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDMapLevelBoundsFitTest, "MazeDemons.Map.LevelBoundsFit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDMapLevelBoundsFitTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("no boxes give invalid bounds"), FMDMapPlanRules::FitBounds({}, 400.0f).bIsValid);

	// The four zones of L_MazeDemonsPrototype.
	TArray<FBox2D> Zones;
	Zones.Add(FBox2D(FVector2D(0.0, -800.0), FVector2D(1600.0, 800.0)));
	Zones.Add(FBox2D(FVector2D(3200.0, -800.0), FVector2D(6400.0, 800.0)));
	Zones.Add(FBox2D(FVector2D(8000.0, -800.0), FVector2D(11200.0, 800.0)));
	Zones.Add(FBox2D(FVector2D(12800.0, -800.0), FVector2D(14400.0, 800.0)));
	Zones.Add(FBox2D(ForceInit)); // invalid boxes are ignored
	const FBox2D Fit = FMDMapPlanRules::FitBounds(Zones, 400.0f);
	TestTrue(TEXT("fit is valid"), Fit.bIsValid);
	TestTrue(TEXT("fit min is the union min minus padding"), Fit.Min.Equals(FVector2D(-400.0, -1200.0)));
	TestTrue(TEXT("fit max is the union max plus padding"), Fit.Max.Equals(FVector2D(14800.0, 1200.0)));
	TestTrue(TEXT("negative padding is treated as 0"), FMDMapPlanRules::FitBounds(Zones, -50.0f).Min.Equals(FVector2D(0.0, -800.0)));

	const FBox2D Square = FMDMapPlanRules::MakeSquare(Fit);
	TestEqual(TEXT("square side is the longer axis (x)"), Square.Max.X - Square.Min.X, 15200.0);
	TestEqual(TEXT("square is square"), Square.Max.Y - Square.Min.Y, 15200.0);
	TestTrue(TEXT("square keeps the centre"), ((Square.Min + Square.Max) * 0.5).Equals((Fit.Min + Fit.Max) * 0.5));
	const FBox2D Tall = FMDMapPlanRules::MakeSquare(FBox2D(FVector2D(0.0, 0.0), FVector2D(100.0, 900.0)));
	TestEqual(TEXT("a tall box squares on y"), Tall.Max.X - Tall.Min.X, 900.0);
	const FBox2D Degenerate = FMDMapPlanRules::MakeSquare(FBox2D(FVector2D(5.0, 5.0), FVector2D(5.0, 5.0)));
	TestTrue(TEXT("a zero box becomes at least 1 unit"), Degenerate.Max.X - Degenerate.Min.X >= 1.0);
	TestFalse(TEXT("invalid in, invalid out"), FMDMapPlanRules::MakeSquare(FBox2D(ForceInit)).bIsValid);

	// UV across the whole level: both room ends and a tunnel map inside 0..1 and in order.
	const FVector2D Room1 = UMDQuestRules::WorldToMapUV(FVector2D(800.0, 0.0), Square);
	const FVector2D TunnelUV = UMDQuestRules::WorldToMapUV(FVector2D(2400.0, 0.0), Square);
	const FVector2D Room4 = UMDQuestRules::WorldToMapUV(FVector2D(13600.0, 0.0), Square);
	TestTrue(TEXT("room 1 < tunnel < room 4 along U"), Room1.X < TunnelUV.X && TunnelUV.X < Room4.X);
	TestTrue(TEXT("centre line is V 0.5"), FMath::IsNearlyEqual(TunnelUV.Y, 0.5, 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDMapPlanRulesTest, "MazeDemons.Map.PlanRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDMapPlanRulesTest::RunTest(const FString& Parameters)
{
	const FMDMapPlanStyle Style; // tolerance 30, step 120, edge 2
	const float Floor = 30.0f;
	const float MinValid = -100.0f;
	TestTrue(TEXT("floor height is Floor"), FMDMapPlanRules::Classify(35.0f, Floor, MinValid, Style) == EMDMapCell::Floor);
	TestTrue(TEXT("water channel is Low"), FMDMapPlanRules::Classify(-20.0f, Floor, MinValid, Style) == EMDMapCell::Low);
	TestTrue(TEXT("a crate is Obstacle"), FMDMapPlanRules::Classify(110.0f, Floor, MinValid, Style) == EMDMapCell::Obstacle);
	TestTrue(TEXT("a wall top is Solid"), FMDMapPlanRules::Classify(700.0f, Floor, MinValid, Style) == EMDMapCell::Solid);
	TestTrue(TEXT("below the box is Void"), FMDMapPlanRules::Classify(-500.0f, Floor, MinValid, Style) == EMDMapCell::Void);
	TestTrue(TEXT("NaN is Void"), FMDMapPlanRules::Classify(std::numeric_limits<float>::quiet_NaN(), Floor, MinValid, Style) == EMDMapCell::Void);

	// Floor estimate: the most common height wins over walls and voids.
	TArray<float> Heights;
	for (int32 I = 0; I < 50; ++I) { Heights.Add(31.0f); }
	for (int32 I = 0; I < 20; ++I) { Heights.Add(700.0f); }
	for (int32 I = 0; I < 40; ++I) { Heights.Add(-1000.0f); }
	TestTrue(TEXT("floor estimate ignores voids and walls"), FMath::IsNearlyEqual(FMDMapPlanRules::EstimateFloorHeight(Heights, MinValid), 31.0f, 0.01f));
	TestEqual(TEXT("nothing valid falls back to the minimum"), FMDMapPlanRules::EstimateFloorHeight({ -1000.0f }, MinValid), MinValid);

	// 8x8: walls on the left two columns, void on the right column, floor between, one doorway gap in the wall.
	const int32 Size = 8;
	TArray<float> Image;
	Image.Init(Floor, Size * Size);
	for (int32 Y = 0; Y < Size; ++Y)
	{
		Image[Y * Size + 0] = 700.0f;
		Image[Y * Size + 1] = (Y == 4) ? Floor : 700.0f; // doorway at row 4
		Image[Y * Size + 7] = -1000.0f;
	}
	TArray<FColor> Plan;
	TArray<int32> Counts;
	TestFalse(TEXT("wrong pixel count is refused"), FMDMapPlanRules::BuildPlan(Heights, Size, Floor, MinValid, Style, Plan));
	TestTrue(TEXT("plan builds"), FMDMapPlanRules::BuildPlan(Image, Size, Floor, MinValid, Style, Plan, &Counts));
	TestEqual(TEXT("one colour per pixel"), Plan.Num(), Size * Size);
	TestEqual(TEXT("wall is the solid colour"), Plan[0 * Size + 0], Style.SolidColor);
	TestEqual(TEXT("void is the void colour"), Plan[0 * Size + 7], Style.VoidColor);
	TestEqual(TEXT("floor next to the wall is outlined"), Plan[0 * Size + 2], Style.EdgeColor);
	TestEqual(TEXT("doorway floor is outlined too"), Plan[4 * Size + 1], Style.EdgeColor);
	TestEqual(TEXT("floor 2 px from the void is outlined"), Plan[0 * Size + 5], Style.EdgeColor);
	TestEqual(TEXT("open floor stays light"), Plan[0 * Size + 4], Style.FloorColor);
	TestTrue(TEXT("floor is lighter than walls"), Style.FloorColor.R > Style.SolidColor.R + 100);
	TestEqual(TEXT("solid count"), Counts[static_cast<int32>(EMDMapCell::Solid)], 15);
	TestEqual(TEXT("void count"), Counts[static_cast<int32>(EMDMapCell::Void)], 8);
	TestEqual(TEXT("floor count (edges included)"), Counts[static_cast<int32>(EMDMapCell::Floor)], 41);
	TestTrue(TEXT("every pixel is opaque"), Plan[10].A == 255 && Plan[63].A == 255);

	FMDMapPlanStyle NoEdge = Style;
	NoEdge.EdgeWidth = 0;
	FMDMapPlanRules::BuildPlan(Image, Size, Floor, MinValid, NoEdge, Plan);
	TestEqual(TEXT("no outline when EdgeWidth is 0"), Plan[0 * Size + 2], Style.FloorColor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDMapComposeFogTest, "MazeDemons.Map.ComposeFog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMDMapComposeFogTest::RunTest(const FString& Parameters)
{
	const int32 Size = 16;
	TArray<FColor> Plan;
	Plan.Init(FColor(200, 190, 170, 255), Size * Size);
	const FColor Fog(10, 12, 14, 255);
	TArray<FColor> Out;

	TestFalse(TEXT("wrong plan size is refused"), FMDMapPlanRules::ComposeFog(Plan, Size + 1, FMDFogGrid(), Fog, Out));
	TestTrue(TEXT("an invalid grid composes"), FMDMapPlanRules::ComposeFog(Plan, Size, FMDFogGrid(), Fog, Out));
	TestEqual(TEXT("no fog layout yet = all hidden"), Out[0], Fog);
	TestEqual(TEXT("invalid grid samples 0"), FMDMapPlanRules::SampleFog(FMDFogGrid(), 0.5, 0.5), 0.0f);

	FMDFogGrid Grid;
	Grid.Init(FBox2D(FVector2D(0.0, 0.0), FVector2D(1600.0, 1600.0)), 200.0f); // 8x8 cells, 2 px each
	FMDMapPlanRules::ComposeFog(Plan, Size, Grid, Fog, Out);
	TestEqual(TEXT("hidden grid = fog colour"), Out[5 * Size + 5], Fog);

	Grid.RevealAll();
	FMDMapPlanRules::ComposeFog(Plan, Size, Grid, Fog, Out);
	TestEqual(TEXT("revealed grid = plan"), Out[5 * Size + 5], Plan[5 * Size + 5]);
	TestEqual(TEXT("revealed corners = plan"), Out[Size * Size - 1], Plan[Size * Size - 1]);

	// Only the left half revealed: far left is plan, far right is fog, the seam is in between.
	Grid.Clear();
	for (int32 Y = 0; Y < 8; ++Y)
	{
		for (int32 X = 0; X < 4; ++X)
		{
			Grid.RevealRadius(FVector2D(X * 200.0 + 100.0, Y * 200.0 + 100.0), 1.0f);
		}
	}
	FMDMapPlanRules::ComposeFog(Plan, Size, Grid, Fog, Out);
	TestEqual(TEXT("left edge revealed"), Out[8 * Size + 0], Plan[0]);
	TestEqual(TEXT("right edge hidden"), Out[8 * Size + Size - 1], Fog);
	const FColor Seam = Out[8 * Size + 8];
	TestTrue(TEXT("the seam is a soft blend"), Seam.R > Fog.R && Seam.R < Plan[0].R);
	TestEqual(TEXT("sample at the left is 1"), FMDMapPlanRules::SampleFog(Grid, 0.05, 0.5), 1.0f);
	TestEqual(TEXT("sample at the right is 0"), FMDMapPlanRules::SampleFog(Grid, 0.95, 0.5), 0.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
