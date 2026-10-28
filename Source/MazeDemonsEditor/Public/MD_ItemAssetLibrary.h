// R0: editor-only helpers for item data that Python cannot reach: appending to Blueprint enums (E_ItemTypes / E_ItemName) and
// adding rows to Blueprint-struct data tables (DT_Items). Python (commandlet, editor closed):
//   unreal.MD_ItemAssetLibrary.append_enumerator("/Game/.../E_ItemTypes", "Ranged")
//   unreal.MD_ItemAssetLibrary.set_row_from_template("/Game/.../DT_Items", "HollowPistol", "SomeRow", {"itemType": "12"})

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MD_ItemAssetLibrary.generated.h"

UCLASS()
class MAZEDEMONSEDITOR_API UMD_ItemAssetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Appends a display-named enumerator to a Blueprint enum and saves it. Idempotent by display name. Returns the ordinal, -1 on failure. */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor")
	static int32 AppendEnumerator(const FString& EnumAssetPath, const FString& DisplayName);

	/** Display names of a Blueprint enum, in ordinal order (the hidden MAX entry is left out). */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor")
	static TArray<FString> ListEnumerators(const FString& EnumAssetPath);

	/**
	 * Writes RowName into a data table: a copy of TemplateRow (empty = default-initialised row), then Overrides applied.
	 * Override keys match a property by name prefix (Blueprint struct properties are Name_Index_Guid); enum / byte values are an
	 * ordinal or a display name, everything else is Unreal import text. Replaces an existing row of that name. Saves the table.
	 * OutLog gets the applied properties and problems. Returns true when the row was written and saved.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor")
	static bool SetRowFromTemplate(const FString& TableAssetPath, FName RowName, FName TemplateRow, const TMap<FString, FString>& Overrides, TArray<FString>& OutLog);

	/** One line per row: "Row | prop=value | ..." (diagnostics, first MaxRows rows; 0 = all). */
	UFUNCTION(BlueprintCallable, Category = "MazeDemons|Editor")
	static TArray<FString> DumpTable(const FString& TableAssetPath, int32 MaxRows);
};
