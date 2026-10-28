// MazeDemons - editor-only Chooser Table helpers (see header). Mirrors the engine's Chooser plugin API (UE 5.8):
// UChooserTable::ResultsStructs / DisabledRows / ColumnsStructs, FChooserColumnBase::InsertRows / CopyRow / SetNumRows,
// FEnumColumn / FMultiEnumColumn with FEnumContextProperty (FChooserEnumPropertyBinding).
// Struct types are resolved by path (FindObject) and rows/columns are built through FInstancedStruct::InitializeAs(UScriptStruct*),
// so we never reference un-exported FXxx::StaticStruct() symbols; members are accessed through typed pointers into the struct memory.

#include "MD_ChooserLibrary.h"

#include "Chooser.h"
#include "EnumColumn.h"
#include "MultiEnumColumn.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "UObject/Class.h"

DEFINE_LOG_CATEGORY_STATIC(LogMDChooser, Log, All);

namespace MD_ChooserLib
{
	static const UScriptStruct* FindStruct(const TCHAR* Path)
	{
		return FindObject<UScriptStruct>(nullptr, Path);
	}

	/** Root chooser + every UChooserTable living inside the root asset (nested choosers are sub-objects of the root). */
	static void GatherTables(UChooserTable* Any, TArray<UChooserTable*>& Out)
	{
		if (!Any) { return; }
		UChooserTable* Root = Any->GetRootChooser();
		Out.AddUnique(Root);
		TArray<UObject*> Objs;
		GetObjectsWithOuter(Root, Objs, true);
		for (UObject* O : Objs)
		{
			if (UChooserTable* T = Cast<UChooserTable>(O))
			{
				Out.AddUnique(T);
			}
		}
	}

	static void Touch(UChooserTable* Table)
	{
		Table->Modify();
		Table->MarkPackageDirty();
	}

	/** Column object living inside ColumnsStructs[Index]; null if the index or struct type is wrong. */
	static FChooserColumnBase* ColumnAt(UChooserTable* Table, int32 Index)
	{
		if (!Table || !Table->ColumnsStructs.IsValidIndex(Index)) { return nullptr; }
		FInstancedStruct& Inst = Table->ColumnsStructs[Index];
		const UScriptStruct* Base = FindStruct(TEXT("/Script/Chooser.ChooserColumnBase"));
		if (!Inst.IsValid() || !Base || !Inst.GetScriptStruct()->IsChildOf(Base)) { return nullptr; }
		return reinterpret_cast<FChooserColumnBase*>(Inst.GetMutableMemory());
	}

	static FName ColumnStructName(UChooserTable* Table, int32 Index)
	{
		return (Table && Table->ColumnsStructs.IsValidIndex(Index) && Table->ColumnsStructs[Index].IsValid())
			? Table->ColumnsStructs[Index].GetScriptStruct()->GetFName() : NAME_None;
	}

	static bool IsMultiEnumColumn(UChooserTable* Table, int32 Index) { return ColumnStructName(Table, Index) == FName(TEXT("MultiEnumColumn")); }
	static bool IsEnumColumn(UChooserTable* Table, int32 Index) { return ColumnStructName(Table, Index) == FName(TEXT("EnumColumn")); }

	/** The property binding of a column's input parameter (all Chooser property-bound parameters expose a "Binding" struct deriving FChooserPropertyBinding). */
	static FChooserPropertyBinding* BindingOf(FChooserColumnBase* Col)
	{
		FInstancedStruct* In = Col ? Col->GetInputValuePtr() : nullptr;
		if (!In || !In->IsValid()) { return nullptr; }
		const FStructProperty* P = FindFProperty<FStructProperty>(In->GetScriptStruct(), TEXT("Binding"));
		const UScriptStruct* BindingBase = FindStruct(TEXT("/Script/Chooser.ChooserPropertyBinding"));
		if (!P || !BindingBase || !P->Struct || !P->Struct->IsChildOf(BindingBase)) { return nullptr; }
		return P->ContainerPtrToValuePtr<FChooserPropertyBinding>(In->GetMutableMemory());
	}

	static FString BindingText(FChooserColumnBase* Col)
	{
		if (const FChooserPropertyBinding* B = BindingOf(Col))
		{
			TArray<FString> Parts;
			for (const FName& N : B->PropertyBindingChain) { Parts.Add(N.ToString()); }
			return FString::Join(Parts, TEXT("."));
		}
		return FString();
	}

	static const UEnum* EnumOfColumn(FChooserColumnBase* Col)
	{
		FInstancedStruct* In = Col ? Col->GetInputValuePtr() : nullptr;
		if (In && In->IsValid() && In->GetScriptStruct()->GetFName() == FName(TEXT("EnumContextProperty")))
		{
			return reinterpret_cast<FEnumContextProperty*>(In->GetMutableMemory())->Binding.Enum;
		}
		return nullptr;
	}

	/** The result object of a row (Asset / SoftAsset: "Asset", Nested / Evaluate chooser: "Chooser"). */
	static UObject* RowObject(UChooserTable* Table, int32 Row)
	{
		if (!Table || !Table->ResultsStructs.IsValidIndex(Row)) { return nullptr; }
		FInstancedStruct& R = Table->ResultsStructs[Row];
		if (!R.IsValid()) { return nullptr; }
		const UScriptStruct* S = R.GetScriptStruct();
		const FObjectPropertyBase* P = FindFProperty<FObjectPropertyBase>(S, TEXT("Asset"));
		if (!P) { P = FindFProperty<FObjectPropertyBase>(S, TEXT("Chooser")); }
		return P ? P->GetObjectPropertyValue_InContainer(R.GetMutableMemory()) : nullptr;
	}

	static FString CellText(UChooserTable* Table, int32 Col, int32 Row)
	{
		FChooserColumnBase* C = ColumnAt(Table, Col);
		if (!C) { return TEXT("?"); }
		const FName RowValuesName = C->RowValuesPropertyName();
		if (RowValuesName.IsNone()) { return TEXT("-"); }
		FInstancedStruct& Inst = Table->ColumnsStructs[Col];
		const FArrayProperty* AP = FindFProperty<FArrayProperty>(Inst.GetScriptStruct(), RowValuesName);
		if (!AP) { return TEXT("?"); }
		FScriptArrayHelper Helper(AP, AP->ContainerPtrToValuePtr<void>(Inst.GetMutableMemory()));
		if (!Helper.IsValidIndex(Row)) { return TEXT("<missing>"); }
		FString Out;
		AP->Inner->ExportTextItem_Direct(Out, (const void*)Helper.GetRawPtr(Row), nullptr, nullptr, 0);
		return Out;
	}
}

UChooserTable* UMD_ChooserLibrary::FindTable(UChooserTable* AnyTableInAsset, const FString& TableName)
{
	TArray<UChooserTable*> Tables;
	MD_ChooserLib::GatherTables(AnyTableInAsset, Tables);
	for (UChooserTable* T : Tables)
	{
		if (T && T->GetName().Equals(TableName, ESearchCase::IgnoreCase)) { return T; }
	}
	return nullptr;
}

TArray<FString> UMD_ChooserLibrary::ListTables(UChooserTable* AnyTableInAsset)
{
	TArray<UChooserTable*> Tables;
	MD_ChooserLib::GatherTables(AnyTableInAsset, Tables);
	TArray<FString> Out;
	for (UChooserTable* T : Tables) { if (T) { Out.Add(T->GetName()); } }
	return Out;
}

int32 UMD_ChooserLibrary::GetRowCount(UChooserTable* Table)
{
	return Table ? Table->ResultsStructs.Num() : 0;
}

int32 UMD_ChooserLibrary::GetColumnCount(UChooserTable* Table)
{
	return Table ? Table->ColumnsStructs.Num() : 0;
}

FString UMD_ChooserLibrary::GetRowAssetName(UChooserTable* Table, int32 RowIndex)
{
	const UObject* O = MD_ChooserLib::RowObject(Table, RowIndex);
	return O ? O->GetName() : FString();
}

int32 UMD_ChooserLibrary::FindRowByAssetName(UChooserTable* Table, const FString& AssetName)
{
	if (!Table) { return INDEX_NONE; }
	for (int32 Row = 0; Row < Table->ResultsStructs.Num(); ++Row)
	{
		const UObject* O = MD_ChooserLib::RowObject(Table, Row);
		if (O && O->GetName().Equals(AssetName, ESearchCase::IgnoreCase)) { return Row; }
	}
	return INDEX_NONE;
}

int32 UMD_ChooserLibrary::FindColumnByBinding(UChooserTable* Table, FName BoundPropertyName)
{
	if (!Table) { return INDEX_NONE; }
	for (int32 i = 0; i < Table->ColumnsStructs.Num(); ++i)
	{
		if (!MD_ChooserLib::IsEnumColumn(Table, i) && !MD_ChooserLib::IsMultiEnumColumn(Table, i)) { continue; }
		const FChooserPropertyBinding* B = MD_ChooserLib::BindingOf(MD_ChooserLib::ColumnAt(Table, i));
		if (B && B->PropertyBindingChain.Num() > 0 && B->PropertyBindingChain.Last() == BoundPropertyName) { return i; }
	}
	return INDEX_NONE;
}

int32 UMD_ChooserLibrary::GetEnumValueByName(UEnum* Enum, const FString& EntryName)
{
	if (!Enum) { return INDEX_NONE; }
	const int64 V = Enum->GetValueByNameString(EntryName);
	return V == INDEX_NONE ? INDEX_NONE : static_cast<int32>(V);
}

int32 UMD_ChooserLibrary::AddEnumColumn(UChooserTable* Table, FName BoundPropertyName, UEnum* Enum, bool bMulti, const FString& DisplayName)
{
	using namespace MD_ChooserLib;
	if (!Table || !Enum || BoundPropertyName.IsNone()) { return INDEX_NONE; }

	const int32 Existing = FindColumnByBinding(Table, BoundPropertyName);
	if (Existing != INDEX_NONE) { return Existing; }

	const UScriptStruct* ColumnStruct = FindStruct(bMulti ? TEXT("/Script/Chooser.MultiEnumColumn") : TEXT("/Script/Chooser.EnumColumn"));
	const UScriptStruct* InputStruct = FindStruct(TEXT("/Script/Chooser.EnumContextProperty"));
	if (!ColumnStruct || !InputStruct)
	{
		UE_LOG(LogMDChooser, Error, TEXT("AddEnumColumn: Chooser column/parameter structs not found (is the Chooser plugin loaded?)"));
		return INDEX_NONE;
	}

	FInstancedStruct NewColumn;
	NewColumn.InitializeAs(ColumnStruct);
	FChooserColumnBase* Col = reinterpret_cast<FChooserColumnBase*>(NewColumn.GetMutableMemory());
	FInstancedStruct* Input = Col->GetInputValuePtr();
	if (!Input) { return INDEX_NONE; }

	// Binding to a property of context object 0 (the table's first ContextData entry, here the AnimBP class).
	Input->InitializeAs(InputStruct);
	FEnumContextProperty* Prop = reinterpret_cast<FEnumContextProperty*>(Input->GetMutableMemory());
	Prop->Binding.PropertyBindingChain.Reset();
	Prop->Binding.PropertyBindingChain.Add(BoundPropertyName);
	Prop->Binding.ContextIndex = 0;
	Prop->Binding.IsBoundToRoot = false;
	Prop->Binding.DisplayName = DisplayName;
	Prop->Binding.Enum = Enum;

	Col->Initialize(Table);
	Col->SetNumRows(Table->ResultsStructs.Num());

	// Random columns must stay last.
	int32 InsertAt = Table->ColumnsStructs.Num();
	for (int32 i = 0; i < Table->ColumnsStructs.Num(); ++i)
	{
		FChooserColumnBase* Other = ColumnAt(Table, i);
		if (Other && Other->IsRandomizeColumn()) { InsertAt = i; break; }
	}

	Touch(Table);
	Table->ColumnsStructs.Insert(MoveTemp(NewColumn), InsertAt);
	return InsertAt;
}

int32 UMD_ChooserLibrary::InsertAssetRow(UChooserTable* Table, int32 Index, UObject* ResultAsset, int32 TemplateRow)
{
	using namespace MD_ChooserLib;
	if (!Table || !ResultAsset) { return INDEX_NONE; }

	const UScriptStruct* RowStruct = FindStruct(TEXT("/Script/Chooser.AssetChooser"));
	const FObjectPropertyBase* AssetProp = RowStruct ? FindFProperty<FObjectPropertyBase>(RowStruct, TEXT("Asset")) : nullptr;
	if (!AssetProp)
	{
		UE_LOG(LogMDChooser, Error, TEXT("InsertAssetRow: /Script/Chooser.AssetChooser (Asset) not found"));
		return INDEX_NONE;
	}

	const int32 Num = Table->ResultsStructs.Num();
	if (Index < 0 || Index > Num) { Index = Num; }
	if (TemplateRow >= Num) { TemplateRow = INDEX_NONE; }

	FInstancedStruct NewRow;
	NewRow.InitializeAs(RowStruct);
	AssetProp->SetObjectPropertyValue_InContainer(NewRow.GetMutableMemory(), ResultAsset);

	Touch(Table);
	while (Table->DisabledRows.Num() < Num) { Table->DisabledRows.Add(false); }
	Table->ResultsStructs.Insert(MoveTemp(NewRow), Index);
	Table->DisabledRows.Insert(false, Index);

	const int32 ShiftedTemplate = (TemplateRow >= Index) ? TemplateRow + 1 : TemplateRow;
	for (int32 c = 0; c < Table->ColumnsStructs.Num(); ++c)
	{
		FChooserColumnBase* Col = ColumnAt(Table, c);
		if (!Col) { continue; }
		Col->InsertRows(Index, 1);
		if (TemplateRow != INDEX_NONE) { Col->CopyRow(*Col, ShiftedTemplate, Index); }
	}
	return Index;
}

bool UMD_ChooserLibrary::SetEnumCellMask(UChooserTable* Table, int32 ColumnIndex, int32 RowIndex, int32 Mask)
{
	using namespace MD_ChooserLib;
	FChooserColumnBase* Col = ColumnAt(Table, ColumnIndex);
	if (!Col) { return false; }
	uint8* Memory = Table->ColumnsStructs[ColumnIndex].GetMutableMemory();

	if (IsMultiEnumColumn(Table, ColumnIndex))
	{
		FMultiEnumColumn* M = reinterpret_cast<FMultiEnumColumn*>(Memory);
		if (!M->RowValues.IsValidIndex(RowIndex)) { return false; }
		Touch(Table);
		M->RowValues[RowIndex].Value = static_cast<uint32>(Mask);
		return true;
	}
	if (IsEnumColumn(Table, ColumnIndex))
	{
		FEnumColumn* E = reinterpret_cast<FEnumColumn*>(Memory);
		if (!E->RowValues.IsValidIndex(RowIndex)) { return false; }
		FChooserEnumRowData& Cell = E->RowValues[RowIndex];
		if (Mask == 0)
		{
			Touch(Table);
			Cell.Comparison = EEnumColumnCellValueComparison::MatchAny;
			Cell.Value = 0;
			return true;
		}
		if (Mask > 0 && FMath::IsPowerOfTwo(Mask))
		{
			const int32 Value = FMath::FloorLog2(static_cast<uint32>(Mask));
			Touch(Table);
			Cell.Comparison = EEnumColumnCellValueComparison::MatchEqual;
			Cell.Value = static_cast<uint8>(Value);
			if (const UEnum* Enum = EnumOfColumn(Col)) { Cell.ValueName = Enum->GetNameByValue(Value); }
			return true;
		}
	}
	return false;
}

UObject* UMD_ChooserLibrary::GetRowObject(UChooserTable* Table, int32 RowIndex)
{
	return MD_ChooserLib::RowObject(Table, RowIndex);
}

bool UMD_ChooserLibrary::RemoveRow(UChooserTable* Table, int32 RowIndex)
{
	using namespace MD_ChooserLib;
	if (!Table || !Table->ResultsStructs.IsValidIndex(RowIndex)) { return false; }
	Touch(Table);
	for (int32 c = 0; c < Table->ColumnsStructs.Num(); ++c)
	{
		if (FChooserColumnBase* Col = ColumnAt(Table, c))
		{
			int RowToDelete = RowIndex;
			Col->DeleteRows(TArrayView<int>(&RowToDelete, 1));
		}
	}
	Table->ResultsStructs.RemoveAt(RowIndex);
	if (Table->DisabledRows.IsValidIndex(RowIndex)) { Table->DisabledRows.RemoveAt(RowIndex); }
	return true;
}

int32 UMD_ChooserLibrary::GetEnumCellMask(UChooserTable* Table, int32 ColumnIndex, int32 RowIndex)
{
	using namespace MD_ChooserLib;
	if (!ColumnAt(Table, ColumnIndex)) { return INDEX_NONE; }
	uint8* Memory = Table->ColumnsStructs[ColumnIndex].GetMutableMemory();
	if (IsMultiEnumColumn(Table, ColumnIndex))
	{
		FMultiEnumColumn* M = reinterpret_cast<FMultiEnumColumn*>(Memory);
		return M->RowValues.IsValidIndex(RowIndex) ? static_cast<int32>(M->RowValues[RowIndex].Value) : INDEX_NONE;
	}
	if (IsEnumColumn(Table, ColumnIndex))
	{
		FEnumColumn* E = reinterpret_cast<FEnumColumn*>(Memory);
		if (!E->RowValues.IsValidIndex(RowIndex)) { return INDEX_NONE; }
		const FChooserEnumRowData& Cell = E->RowValues[RowIndex];
		return Cell.Comparison == EEnumColumnCellValueComparison::MatchAny ? 0 : (1 << Cell.Value);
	}
	return INDEX_NONE;
}

bool UMD_ChooserLibrary::SetRowStance(UChooserTable* Table, int32 RowIndex, FName BoundPropertyName, int32 StanceMask)
{
	const int32 Col = FindColumnByBinding(Table, BoundPropertyName);
	return Col != INDEX_NONE && SetEnumCellMask(Table, Col, RowIndex, StanceMask);
}

TArray<FString> UMD_ChooserLibrary::GetCompileErrors(UChooserTable* Table)
{
	using namespace MD_ChooserLib;
	TArray<FString> Out;
	TArray<UChooserTable*> Tables;
	GatherTables(Table, Tables);
	for (UChooserTable* T : Tables)
	{
		for (int32 c = 0; c < T->ColumnsStructs.Num(); ++c)
		{
			FChooserColumnBase* Col = ColumnAt(T, c);
			FChooserParameterBase* Param = Col ? Col->GetInputValue() : nullptr;
			FText Message;
			if (Param && Param->HasCompileErrors(Message))
			{
				Out.Add(FString::Printf(TEXT("%s | col %d (%s) | %s"), *T->GetName(), c, *BindingText(Col), *Message.ToString()));
			}
		}
	}
	return Out;
}

bool UMD_ChooserLibrary::CompileTable(UChooserTable* Table)
{
	using namespace MD_ChooserLib;
	if (!Table) { return false; }
	TArray<UChooserTable*> Tables;
	GatherTables(Table, Tables);
	for (UChooserTable* T : Tables)
	{
		T->Compile(true);
	}
	Table->GetRootChooser()->MarkPackageDirty();
	return GetCompileErrors(Table).Num() == 0;
}

TArray<FString> UMD_ChooserLibrary::DescribeTable(UChooserTable* Table)
{
	using namespace MD_ChooserLib;
	TArray<FString> Out;
	if (!Table) { return Out; }
	Out.Add(FString::Printf(TEXT("Table %s: %d rows, %d columns"), *Table->GetName(), Table->ResultsStructs.Num(), Table->ColumnsStructs.Num()));
	for (int32 c = 0; c < Table->ColumnsStructs.Num(); ++c)
	{
		FChooserColumnBase* Col = ColumnAt(Table, c);
		const UEnum* Enum = EnumOfColumn(Col);
		Out.Add(FString::Printf(TEXT("  COL %d: %s bind=%s%s"), c, *ColumnStructName(Table, c).ToString(), *BindingText(Col),
			Enum ? *FString::Printf(TEXT(" enum=%s"), *Enum->GetName()) : TEXT("")));
	}
	for (int32 r = 0; r < Table->ResultsStructs.Num(); ++r)
	{
		const UObject* O = RowObject(Table, r);
		FString Line = FString::Printf(TEXT("  ROW %d: %s"), r, O ? *O->GetName() : TEXT("<none>"));
		for (int32 c = 0; c < Table->ColumnsStructs.Num(); ++c)
		{
			Line += FString::Printf(TEXT(" | c%d=%s"), c, *CellText(Table, c, r));
		}
		Out.Add(Line);
	}
	return Out;
}
