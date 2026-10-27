#include "MD_ItemAssetLibrary.h"

#include "DataTableEditorUtils.h"
#include "Engine/DataTable.h"
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace MDItemAssetLocal
{
	template <typename T>
	T* LoadByPath(const FString& AssetPath)
	{
		return LoadObject<T>(nullptr, *FString::Printf(TEXT("%s.%s"), *AssetPath, *FPackageName::GetShortName(AssetPath)));
	}

	bool SaveAsset(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, Asset, *FileName, SaveArgs);
	}

	bool PropertyMatches(const FProperty* Prop, const FString& Key)
	{
		const FString Name = Prop->GetName();
		return Name.Equals(Key, ESearchCase::IgnoreCase) || Name.StartsWith(Key + TEXT("_"), ESearchCase::IgnoreCase);
	}

	const UEnum* EnumOf(const FProperty* Prop)
	{
		if (const FByteProperty* Byte = CastField<FByteProperty>(Prop))
		{
			return Byte->Enum;
		}
		if (const FEnumProperty* Enum = CastField<FEnumProperty>(Prop))
		{
			return Enum->GetEnum();
		}
		return nullptr;
	}

	bool ApplyOverride(const FProperty* Prop, uint8* Row, const FString& Value, FString& OutError)
	{
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Row);
		if (const UEnum* Enum = EnumOf(Prop))
		{
			int64 Ordinal = INDEX_NONE;
			if (Value.IsNumeric())
			{
				Ordinal = FCString::Atoi64(*Value);
			}
			else
			{
				for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
				{
					if (Enum->GetDisplayNameTextByIndex(Index).ToString() == Value || Enum->GetNameStringByIndex(Index) == Value)
					{
						Ordinal = Enum->GetValueByIndex(Index);
						break;
					}
				}
			}
			if (Ordinal == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("%s: enumerator '%s' not found"), *Prop->GetName(), *Value);
				return false;
			}
			if (const FByteProperty* Byte = CastField<FByteProperty>(Prop))
			{
				Byte->SetPropertyValue(ValuePtr, static_cast<uint8>(Ordinal));
			}
			else if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
			{
				EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, Ordinal);
			}
			return true;
		}
		const TCHAR* End = Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);
		if (!End)
		{
			OutError = FString::Printf(TEXT("%s: cannot import '%s'"), *Prop->GetName(), *Value);
			return false;
		}
		return true;
	}
}

int32 UMD_ItemAssetLibrary::AppendEnumerator(const FString& EnumAssetPath, const FString& DisplayName)
{
	using namespace MDItemAssetLocal;
	UUserDefinedEnum* Enum = LoadByPath<UUserDefinedEnum>(EnumAssetPath);
	if (!Enum)
	{
		return -1;
	}
	for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
	{
		if (Enum->GetDisplayNameTextByIndex(Index).ToString() == DisplayName)
		{
			return Index;
		}
	}
	FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
	const int32 NewIndex = Enum->NumEnums() - 2;
	FEnumEditorUtils::SetEnumeratorDisplayName(Enum, NewIndex, FText::FromString(DisplayName));
	return SaveAsset(Enum) ? NewIndex : -1;
}

TArray<FString> UMD_ItemAssetLibrary::ListEnumerators(const FString& EnumAssetPath)
{
	TArray<FString> Names;
	if (const UEnum* Enum = MDItemAssetLocal::LoadByPath<UEnum>(EnumAssetPath))
	{
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
			Names.Add(Enum->GetDisplayNameTextByIndex(Index).ToString());
		}
	}
	return Names;
}

bool UMD_ItemAssetLibrary::SetRowFromTemplate(const FString& TableAssetPath, FName RowName, FName TemplateRow, const TMap<FString, FString>& Overrides, TArray<FString>& OutLog)
{
	using namespace MDItemAssetLocal;
	UDataTable* Table = LoadByPath<UDataTable>(TableAssetPath);
	if (!Table || !Table->GetRowStruct())
	{
		OutLog.Add(FString::Printf(TEXT("Error: %s does not load as a data table"), *TableAssetPath));
		return false;
	}
	const UScriptStruct* Struct = Table->GetRowStruct();
	const uint8* Template = TemplateRow.IsNone() ? nullptr : Table->FindRowUnchecked(TemplateRow);
	if (!TemplateRow.IsNone() && !Template)
	{
		OutLog.Add(FString::Printf(TEXT("Error: template row %s not found"), *TemplateRow.ToString()));
		return false;
	}
	// Copy the template first: adding a row can reallocate the table's row map.
	TArray<uint8> Scratch;
	Scratch.SetNumZeroed(Struct->GetStructureSize());
	Struct->InitializeStruct(Scratch.GetData());
	if (Template)
	{
		Struct->CopyScriptStruct(Scratch.GetData(), Template);
	}

	bool bOk = true;
	for (const TPair<FString, FString>& Pair : Overrides)
	{
		const FProperty* Match = nullptr;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (PropertyMatches(*It, Pair.Key))
			{
				Match = *It;
				break;
			}
		}
		FString Error;
		if (!Match)
		{
			OutLog.Add(FString::Printf(TEXT("Error: no property matches '%s'"), *Pair.Key));
			bOk = false;
		}
		else if (!ApplyOverride(Match, Scratch.GetData(), Pair.Value, Error))
		{
			OutLog.Add(TEXT("Error: ") + Error);
			bOk = false;
		}
		else
		{
			OutLog.Add(FString::Printf(TEXT("%s = %s"), *Match->GetName(), *Pair.Value));
		}
	}
	if (!bOk)
	{
		Struct->DestroyStruct(Scratch.GetData());
		return false;
	}

	uint8* Dest = Table->FindRowUnchecked(RowName);
	if (!Dest)
	{
		Dest = FDataTableEditorUtils::AddRow(Table, RowName);
	}
	if (!Dest)
	{
		Struct->DestroyStruct(Scratch.GetData());
		OutLog.Add(TEXT("Error: could not add the row"));
		return false;
	}
	Struct->CopyScriptStruct(Dest, Scratch.GetData());
	Struct->DestroyStruct(Scratch.GetData());
	FDataTableEditorUtils::BroadcastPostChange(Table, FDataTableEditorUtils::EDataTableChangeInfo::RowList);
	const bool bSaved = SaveAsset(Table);
	OutLog.Add(FString::Printf(TEXT("%s row %s in %s"), bSaved ? TEXT("Saved") : TEXT("Error: save failed for"), *RowName.ToString(), *TableAssetPath));
	return bSaved;
}

TArray<FString> UMD_ItemAssetLibrary::DumpTable(const FString& TableAssetPath, int32 MaxRows)
{
	TArray<FString> Lines;
	const UDataTable* Table = MDItemAssetLocal::LoadByPath<UDataTable>(TableAssetPath);
	if (!Table || !Table->GetRowStruct())
	{
		Lines.Add(TEXT("Error: table does not load"));
		return Lines;
	}
	int32 Count = 0;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		if (MaxRows > 0 && Count++ >= MaxRows)
		{
			break;
		}
		FString Line = Pair.Key.ToString();
		for (TFieldIterator<FProperty> It(Table->GetRowStruct()); It; ++It)
		{
			FString Value;
			It->ExportText_InContainer(0, Value, Pair.Value, Pair.Value, nullptr, PPF_None);
			Line += FString::Printf(TEXT(" | %s=%s"), *It->GetName(), *Value);
		}
		Lines.Add(Line);
	}
	return Lines;
}
