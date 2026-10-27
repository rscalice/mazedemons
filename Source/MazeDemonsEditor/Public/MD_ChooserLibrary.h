// MazeDemons - editor-only helpers so scripts (Python) can edit Chooser Tables without hand-editing them in the Chooser editor.
// Built for the GASP motion-matching choosers (CHT_PoseSearchDatabases_Sparse and its nested tables), but table-agnostic.
// Python usage (see Saved/ClaudeDump/stancechoosers.py):
//   root  = unreal.load_asset("/Game/.../CHT_PoseSearchDatabases_Sparse")
//   idles = unreal.MD_ChooserLibrary.find_table(root, "Stand Idles")
//   col   = unreal.MD_ChooserLibrary.add_enum_column(idles, "WeaponStances", stance_enum, True, "Weapon Stance")
//   row   = unreal.MD_ChooserLibrary.insert_asset_row(idles, 0, some_psd, template_row)   # copies every column cell from template_row
//   unreal.MD_ChooserLibrary.set_enum_cell_mask(idles, col, row, 1 << greatsword_value)   # mask 0 = match any
// Everything marks the owning package dirty; nothing saves. Struct types are looked up by reflection (no direct StaticStruct links).

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MD_ChooserLibrary.generated.h"

class UChooserTable;
class UEnum;

UCLASS()
class MAZEDEMONSEDITOR_API UMD_ChooserLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Find a table by object name ("Stand Idles"): the root chooser itself, or any chooser nested inside the root's asset. Case-insensitive. Null if not found. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static UChooserTable* FindTable(UChooserTable* AnyTableInAsset, const FString& TableName);

	/** Object names of the root chooser and every nested chooser in the same asset. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static TArray<FString> ListTables(UChooserTable* AnyTableInAsset);

	/** Human-readable dump: columns (struct type + bound property chain) and every row (result + each column's cell text). */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static TArray<FString> DescribeTable(UChooserTable* Table);

	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static int32 GetRowCount(UChooserTable* Table);

	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static int32 GetColumnCount(UChooserTable* Table);

	/** Object name of the row's result (the asset for Asset rows, the table for Nested/Evaluate rows). Empty if none. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static FString GetRowAssetName(UChooserTable* Table, int32 RowIndex);

	/** First row whose result object has this object name (e.g. "PSD_WalkLoops_DualSword"), or -1. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static int32 FindRowByAssetName(UChooserTable* Table, const FString& AssetName);

	/** Index of the (multi-)enum column whose property binding ends in BoundPropertyName (e.g. "WeaponStances"), or -1. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static int32 FindColumnByBinding(UChooserTable* Table, FName BoundPropertyName);

	/** Value of an enum entry by short or full name ("Greatsword"); -1 if missing. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static int32 GetEnumValueByName(UEnum* Enum, const FString& EntryName);

	/**
	 * Add an enum column bound to a property on the table's context object (context index 0, e.g. the AnimBP variable "WeaponStances").
	 * bMulti = "Enum (Or)" column (cells are bitmasks, bit N = enum value N, 0 = any); otherwise a plain Enum column.
	 * If a column bound to the same property already exists its index is returned (idempotent). New cells are "any".
	 * Returns the column index, or -1 on failure. Call CompileTable afterwards.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static int32 AddEnumColumn(UChooserTable* Table, FName BoundPropertyName, UEnum* Enum, bool bMulti, const FString& DisplayName);

	/**
	 * Insert a row whose result is the given asset. Index < 0 or past the end appends. New cells in every column take that column's default,
	 * unless TemplateRow >= 0 (index in the table BEFORE the insert): then every column's cell is copied from that row.
	 * Returns the new row's index, or -1.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static int32 InsertAssetRow(UChooserTable* Table, int32 Index, UObject* ResultAsset, int32 TemplateRow = -1);

	/** Set one cell of an enum column. Mask 0 = match any. Multi-enum: Mask is stored as-is. Plain enum: Mask must be a single bit (1 << value). */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static bool SetEnumCellMask(UChooserTable* Table, int32 ColumnIndex, int32 RowIndex, int32 Mask);

	/** R0: the result object of a row (the database for Asset rows), or null. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static UObject* GetRowObject(UChooserTable* Table, int32 RowIndex);

	/** R0: remove one row from every column and the result list. Returns false when the index is invalid. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static bool RemoveRow(UChooserTable* Table, int32 RowIndex);

	/** R0: stance mask of one cell of an enum column (multi-enum: stored mask; plain enum: 1 << value, 0 = any). -1 on failure. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static int32 GetEnumCellMask(UChooserTable* Table, int32 ColumnIndex, int32 RowIndex);

	/** Convenience: SetEnumCellMask on the column bound to BoundPropertyName (the stance column). */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static bool SetRowStance(UChooserTable* Table, int32 RowIndex, FName BoundPropertyName, int32 StanceMask);

	/** Recompile the root chooser and all nested choosers (re-resolves property bindings) and mark the package dirty. True when no binding errors remain. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static bool CompileTable(UChooserTable* Table);

	/** "Table | column | message" for every column input with a binding/compile error (after CompileTable). Empty = all bindings resolved. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor|Chooser")
	static TArray<FString> GetCompileErrors(UChooserTable* Table);
};
