// Copyright 2021 Nicholas Frechette. All Rights Reserved.

#include "ACLDatabaseBuildCommandlet.h"

#include "AnimationCompressionLibraryDatabase.h"

#include "AnimationCompression.h"
#include "AnimationUtils.h"
#include "AssetRegistryModule.h"
#include "FileHelpers.h"
#include "ISourceControlModule.h"
#include "SourceControlOperations.h"


//////////////////////////////////////////////////////////////////////////
// Commandlet example inspired by: https://github.com/ue4plugins/CommandletPlugin
// To run the commandlet, add to the commandline: "$(SolutionDir)$(ProjectName).uproject" -run=/Script/ACLPluginEditor.ACLDatabaseBuild

//////////////////////////////////////////////////////////////////////////

UACLDatabaseBuildCommandlet::UACLDatabaseBuildCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UACLDatabaseBuildCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamsMap;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	TArray<FAssetData> DatabaseAssets;
	{
		UE_LOG(LogAnimationCompression, Log, TEXT("Retrieving all ACL databases from current project ..."));

		FARFilter DatabaseFilter;
		DatabaseFilter.ClassNames.Add(UAnimationCompressionLibraryDatabase::StaticClass()->GetFName());
		AssetRegistryModule.Get().GetAssets(DatabaseFilter, DatabaseAssets);
	}

	if (DatabaseAssets.Num() == 0)
	{
		UE_LOG(LogAnimationCompression, Log, TEXT("Failed to find any ACL databases, done"));
		return 0;
	}

	TArray<FAssetData> AnimSequenceAssets;
	{
		UE_LOG(LogAnimationCompression, Log, TEXT("Retrieving all animation sequences from current project ..."));

		FARFilter AnimSequenceFilter;
		AnimSequenceFilter.ClassNames.Add(UAnimSequence::StaticClass()->GetFName());
		AssetRegistryModule.Get().GetAssets(AnimSequenceFilter, AnimSequenceAssets);
	}

	if (AnimSequenceAssets.Num() == 0)
	{
		UE_LOG(LogAnimationCompression, Log, TEXT("Failed to find any animation sequences, done"));
		return 0;
	}

	{
		UE_LOG(LogAnimationCompression, Log, TEXT("Loading %u animation sequences ..."), AnimSequenceAssets.Num());
		for (const FAssetData& Asset : AnimSequenceAssets)
		{
			UAnimSequence* AnimSeq = Cast<UAnimSequence>(Asset.GetAsset());
			if (AnimSeq == nullptr)
			{
				UE_LOG(LogAnimationCompression, Log, TEXT("Failed to load animation sequence: %s"), *Asset.PackagePath.ToString());
				continue;
			}

			// Make sure all our required dependencies are loaded
			FAnimationUtils::EnsureAnimSequenceLoaded(*AnimSeq);
		}
	}

	{
		UE_LOG(LogAnimationCompression, Log, TEXT("Loading %u ACL databases ..."), DatabaseAssets.Num());
		for (const FAssetData& Asset : DatabaseAssets)
		{
			UAnimationCompressionLibraryDatabase* Database = Cast<UAnimationCompressionLibraryDatabase>(Asset.GetAsset());
			if (Database == nullptr)
			{
				UE_LOG(LogAnimationCompression, Log, TEXT("Failed to load ACL database: %s"), *Asset.PackagePath.ToString());
				continue;
			}
		}
	}

	TArray<UPackage*> DirtyDatabasePackages;
	{
		for (const FAssetData& Asset : DatabaseAssets)
		{
			UE_LOG(LogAnimationCompression, Log, TEXT("Building mapping for ACL database: %s ..."), *Asset.PackagePath.ToString());

			UAnimationCompressionLibraryDatabase* Database = Cast<UAnimationCompressionLibraryDatabase>(Asset.GetAsset());
			if (Database == nullptr)
			{
				continue;
			}

			const bool bIsDirty = Database->UpdateReferencingAnimSequenceList();
			if (bIsDirty)
			{
				UE_LOG(LogAnimationCompression, Log, TEXT("    Mapping updated!"));
				DirtyDatabasePackages.Add(Asset.GetPackage());
			}
		}
	}

	bool bFailedToSave = false;
	{
		if (ISourceControlModule::Get().IsEnabled())
		{
			ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

			TArray<UPackage*> PackagesToSave;
			for (UPackage* Package : DirtyDatabasePackages)
			{
				FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(Package, EStateCacheUsage::Use);
				if (SourceControlState->IsCheckedOutOther())
				{
					UE_LOG(LogAnimationCompression, Warning, TEXT("Package %s is already checked out by someone, will not check out"), *SourceControlState->GetFilename());
				}
				else if (!SourceControlState->IsCurrent())
				{
					UE_LOG(LogAnimationCompression, Warning, TEXT("Package %s is not at head, will not check out"), *SourceControlState->GetFilename());
				}
				else if (SourceControlState->CanCheckout())
				{
					const ECommandResult::Type StatusResult = SourceControlProvider.Execute(ISourceControlOperation::Create<FCheckOut>(), Package);
					if (StatusResult != ECommandResult::Succeeded)
					{
						UE_LOG(LogAnimationCompression, Log, TEXT("Package %s failed to check out"), *SourceControlState->GetFilename());
						bFailedToSave = true;
					}
					else
					{
						PackagesToSave.Add(Package);
					}
				}
				else if (!SourceControlState->IsSourceControlled() || SourceControlState->CanEdit())
				{
					PackagesToSave.Add(Package);
				}
			}

			UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
			ISourceControlModule::Get().QueueStatusUpdate(PackagesToSave);
		}
		else
		{
			// No source control, just try to save what we have
			UEditorLoadingAndSavingUtils::SavePackages(DirtyDatabasePackages, true);
		}
	}

	return bFailedToSave ? 1 : 0;
}
