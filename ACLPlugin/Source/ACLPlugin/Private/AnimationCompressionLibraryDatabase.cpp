// Copyright 2020 Nicholas Frechette. All Rights Reserved.

#include "AnimationCompressionLibraryDatabase.h"
#include "AnimBoneCompressionCodec_ACLDatabase.h"

#include "UE4DatabaseStreamer.h"

#include "LatentActions.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"

#include "ACLImpl.h"

#if WITH_EDITORONLY_DATA
#include "Animation/AnimBoneCompressionSettings.h"
#include "PlatformInfo.h"
#include "Interfaces/ITargetPlatform.h"
#include "UObject/UObjectIterator.h"

#include "UE4DatabasePreviewStreamer.h"

#include <acl/compression/compress.h>
#endif	// WITH_EDITORONLY_DATA

const TCHAR* VisualFidelityToString(ACLVisualFidelity Fidelity)
{
	switch (Fidelity)
	{
	case ACLVisualFidelity::Highest:
		return TEXT("Highest");
	case ACLVisualFidelity::Medium:
		return TEXT("Medium");
	case ACLVisualFidelity::Lowest:
		return TEXT("Lowest");
	default:
		return TEXT("Unknown visual fidelity");
	}
}

UAnimationCompressionLibraryDatabase::UAnimationCompressionLibraryDatabase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, CurrentVisualFidelity(ACLVisualFidelity::Lowest)
	, NextFidelityChangeRequestID(0)
#if WITH_EDITORONLY_DATA
	, HighestImportanceProportion(0.5f)	// The rest remains in the anim sequences
	, MediumImportanceProportion(0.0f)	// No medium quality tier by default
	, LowestImportanceProportion(0.5f)	// By default we move 50% of the key frames to the database
	, StripLowestImportanceTier(false)	// By default we don't strip the lowest tier
#endif
	, MaxStreamRequestSizeKB(1024)		// By default we stream 1 MB (1 chunk) at a time
#if WITH_EDITORONLY_DATA
	// By default, in the editor we preview the full quality.
	// Our database context won't be used until we need to build the database for preview if we change this value.
	, PreviewVisualFidelity(ACLVisualFidelity::Highest)
#endif
{
}

#if WITH_EDITORONLY_DATA
void UAnimationCompressionLibraryDatabase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property == nullptr)
	{
		return;	// Safety check
	}

	const FName ChangedPropertyName = PropertyChangedEvent.Property->GetFName();

	bool bBuildDatabaseForPreview = false;
	bool bUpdateStreaming = false;

	if (ChangedPropertyName == GET_MEMBER_NAME_CHECKED(UAnimationCompressionLibraryDatabase, LowestImportanceProportion))
	{
		// Clamp our medium importance propertion
		MediumImportanceProportion = FMath::Clamp(MediumImportanceProportion, 0.0f, FMath::Clamp(1.0f - LowestImportanceProportion, 0.0f, 1.0f));

		// Update our high importance proportion to reflect what remains
		HighestImportanceProportion = FMath::Clamp(1.0f - LowestImportanceProportion - MediumImportanceProportion, 0.0f, 1.0f);

		// Only update our preview state if we are setting the final value to avoid slowing down the UI when using the slider
		if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
		{
			bBuildDatabaseForPreview = PreviewDatabaseStreamer != nullptr;	// If we already had a preview database, we need to rebuild it
		}
	}
	else if (ChangedPropertyName == GET_MEMBER_NAME_CHECKED(UAnimationCompressionLibraryDatabase, MediumImportanceProportion))
	{
		// Clamp our medium importance propertion
		LowestImportanceProportion = FMath::Clamp(LowestImportanceProportion, 0.0f, FMath::Clamp(1.0f - MediumImportanceProportion, 0.0f, 1.0f));

		// Update our high importance proportion to reflect what remains
		HighestImportanceProportion = FMath::Clamp(1.0f - LowestImportanceProportion - MediumImportanceProportion, 0.0f, 1.0f);

		// Only update our preview state if we are setting the final value to avoid slowing down the UI when using the slider
		if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
		{
			bBuildDatabaseForPreview = PreviewDatabaseStreamer != nullptr;	// If we already had a preview database, we need to rebuild it
		}
	}
	else if (ChangedPropertyName == GET_MEMBER_NAME_CHECKED(UAnimationCompressionLibraryDatabase, PreviewVisualFidelity))
	{
		// Our preview state changed, check if we need to generate our database
		bBuildDatabaseForPreview = PreviewDatabaseStreamer == nullptr;	// We didn't have a preview database, create one now
		bUpdateStreaming = true;
	}

	if (bBuildDatabaseForPreview || bUpdateStreaming)
	{
		UpdatePreviewState(bBuildDatabaseForPreview);
	}
}

void UAnimationCompressionLibraryDatabase::PreSave(const ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);

	// Clear any stale cooked data we might have
	CookedCompressedBytes.Empty(0);
	CookedAnimSequenceMappings.Empty(0);
	CookedBulkData.RemoveBulkData();

	if (TargetPlatform != nullptr && TargetPlatform->RequiresCookedData())
	{
		const bool bStripLowestTier = StripLowestImportanceTier.GetValueForPlatformIdentifiers(
			TargetPlatform->GetPlatformInfo().PlatformGroupName,
			TargetPlatform->GetPlatformInfo().VanillaPlatformName);

		TArray<uint8> BulkData;
		BuildDatabase(CookedCompressedBytes, CookedAnimSequenceMappings, BulkData, bStripLowestTier);

		CookedBulkData.Lock(LOCK_READ_WRITE);
		{
			const uint32 BulkDataSize = BulkData.Num();
			void* BulkDataToSave = CookedBulkData.Realloc(BulkDataSize);
			FMemory::Memcpy(BulkDataToSave, BulkData.GetData(), BulkDataSize);
		}
		CookedBulkData.Unlock();
	}
}

void UAnimationCompressionLibraryDatabase::BuildDatabase(TArray<uint8>& OutCompressedBytes, TArray<uint64>& OutAnimSequenceMappings, TArray<uint8>& OutBulkData, bool bStripLowestTier) const
{
	// Clear any stale data we might have
	OutCompressedBytes.Empty(0);
	OutAnimSequenceMappings.Empty(0);
	OutBulkData.Empty(0);

	// We are cooking or previewing, iterate over every animation sequence that references this database
	// and merge them together into our final database instance. Note that the mapping could
	// be stale and we must double check.

	// Gather the sequences we need to merge, these are already sorted by FName by construction
	TArray<UAnimSequence*> CookedSequences;
	CookedSequences.Empty(AnimSequences.Num());

	// Because we use a hash at runtime in a cooked build to retrieve our clip data, we must ensure that the hash value is unique
	TSet<uint32> SequenceHashes;
	SequenceHashes.Empty(AnimSequences.Num());

	for (UAnimSequence* AnimSeq : AnimSequences)
	{
		UAnimBoneCompressionCodec_ACLDatabase* DatabaseCodec = Cast<UAnimBoneCompressionCodec_ACLDatabase>(AnimSeq->CompressedData.BoneCompressionCodec);
		if (DatabaseCodec == nullptr || DatabaseCodec->DatabaseAsset != this)
		{
			UE_LOG(LogAnimationCompression, Warning, TEXT("ACL Database mapping is stale. [%s] no longer references it."), *AnimSeq->GetPathName());
			continue;
		}

		// Update our compressed data to match the current settings
		// We might need to revert to non-frame stripped data or the codec might have changed
		// forcing us to recompress
		// In practice we'll load directly from the DDC and it should be fast
		const bool bAsyncCompression = false;
		const bool bAllowAlternateCompressor = false;
		const bool bOutput = false;

		FRequestAnimCompressionParams Params(bAsyncCompression, bAllowAlternateCompressor, bOutput);
		Params.bPerformFrameStripping = false;

		AnimSeq->RequestAnimCompression(Params);

		const FACLDatabaseCompressedAnimData& AnimData = static_cast<const FACLDatabaseCompressedAnimData&>(*AnimSeq->CompressedData.CompressedDataStructure);
		if (!AnimData.IsValid())
		{
			UE_LOG(LogAnimationCompression, Warning, TEXT("Cannot include an invalid sequence in the ACL database: [%s]"), *AnimSeq->GetPathName());
			continue;
		}

		bool bIsAlreadyInSet = false;
		SequenceHashes.Add(AnimData.SequenceNameHash, &bIsAlreadyInSet);
		if (bIsAlreadyInSet)
		{
			// Our anim sequence has a hash that we've seen already which means an anim sequence has been duplicated
			// and lives in a separate asset with identical data.
			// We'll skip it since we'll be able to re-use the same data at runtime.
			continue;
		}

		CookedSequences.Add(AnimSeq);
	}

	if (CookedSequences.Num() == 0)
	{
		return;	// Nothing to cook
	}

	TArray<const acl::compressed_tracks*> ACLCompressedTracks;
	for (const UAnimSequence* AnimSeq : CookedSequences)
	{
		const FACLDatabaseCompressedAnimData& AnimData = static_cast<const FACLDatabaseCompressedAnimData&>(*AnimSeq->CompressedData.CompressedDataStructure);
		ACLCompressedTracks.Add(AnimData.GetCompressedTracks());
	}

	const int32 NumSequences = ACLCompressedTracks.Num();

	acl::compression_database_settings Settings;	// Use defaults
	Settings.low_importance_tier_proportion = LowestImportanceProportion;
	Settings.medium_importance_tier_proportion = MediumImportanceProportion;

	TArray<acl::compressed_tracks*> ACLDBCompressedTracks;
	ACLDBCompressedTracks.AddZeroed(NumSequences);

	acl::compressed_database* MergedDB = nullptr;
	acl::error_result MergeResult = acl::build_database(ACLAllocatorImpl, Settings, ACLCompressedTracks.GetData(), NumSequences, ACLDBCompressedTracks.GetData(), MergedDB);

	if (MergeResult.any())
	{
		// Free our duplicate compressed clips
		UE_LOG(LogAnimationCompression, Error, TEXT("ACL failed to merge databases: %s"), ANSI_TO_TCHAR(MergeResult.c_str()));
		return;
	}

#if DO_GUARD_SLOW
	// Sanity check that the database is properly constructed
	{
		checkSlow(MergedDB->is_valid(true).empty());

		acl::database_context<UE4DefaultDatabaseSettings> DebugDatabaseContext;
		const bool ContextInitResult = DebugDatabaseContext.initialize(ACLAllocatorImpl, *MergedDB);
		checkf(ContextInitResult, TEXT("ACL failed to initialize the database context"));

		for (const acl::compressed_tracks* CompressedTracks : ACLDBCompressedTracks)
		{
			checkSlow(CompressedTracks->is_valid(true).empty());
			checkSlow(MergedDB->contains(*CompressedTracks));
			checkSlow(DebugDatabaseContext.contains(*CompressedTracks));
		}
}
#endif

	// Split our database to serialize the bulk data separately
	acl::compressed_database* SplitDB = nullptr;
	uint8* SplitDBBulkDataMedium = nullptr;
	uint8* SplitDBBulkDataLow = nullptr;
	const acl::error_result SplitResult = acl::split_database_bulk_data(ACLAllocatorImpl, *MergedDB, SplitDB, SplitDBBulkDataMedium, SplitDBBulkDataLow);

	// Free the merged instance we no longer need
	ACLAllocatorImpl.deallocate(MergedDB, MergedDB->get_size());
	MergedDB = nullptr;

	if (SplitResult.any())
	{
		// Free our clip copies
		for (acl::compressed_tracks* CompressedTracks : ACLDBCompressedTracks)
		{
			ACLAllocatorImpl.deallocate(CompressedTracks, CompressedTracks->get_size());
		}

		UE_LOG(LogAnimationCompression, Error, TEXT("ACL failed to split database: %s"), ANSI_TO_TCHAR(SplitResult.c_str()));
		return;
	}

	checkSlow(SplitDB->is_valid(true).empty());

	const uint32 BulkDataSizeMedium = SplitDB->get_bulk_data_size(acl::quality_tier::medium_importance);
	const uint32 BulkDataSizeLow = SplitDB->get_bulk_data_size(acl::quality_tier::lowest_importance);
	uint32 BulkDataEffectiveSizeLow = BulkDataSizeLow;

	// Strip our lowest tier if requested and if it contains data
	if (bStripLowestTier && SplitDB->has_bulk_data(acl::quality_tier::lowest_importance))
	{
		acl::compressed_database* StrippedDB = nullptr;
		const acl::error_result StripResult = acl::strip_database_quality_tier(ACLAllocatorImpl, *SplitDB, acl::quality_tier::lowest_importance, StrippedDB);

		if (StripResult.any())
		{
			// We failed to strip our tier but the split database is still usable, don't fail anything
			UE_LOG(LogAnimationCompression, Warning, TEXT("ACL failed to strip lowest database tier: %s"), ANSI_TO_TCHAR(StripResult.c_str()));
		}
		else
		{
			// Medium tier shouldn't have changed
			check(BulkDataSizeMedium == StrippedDB->get_bulk_data_size(acl::quality_tier::medium_importance));

			// Free the split instance we no longer need
			ACLAllocatorImpl.deallocate(SplitDB, SplitDB->get_size());

			// Use the stripped database instead
			SplitDB = StrippedDB;

			// We no longer have the lowest tier
			BulkDataEffectiveSizeLow = 0;
		}
	}

	const uint32 CompressedDatabaseSize = SplitDB->get_size();

	// Our compressed sequences follow the database in memory, aligned to 16 bytes
	uint32 CompressedSequenceOffset = acl::align_to(CompressedDatabaseSize, 16);

	// Write our our cooked offset mappings
	// We use an array for simplicity. UE4 doesn't support serializing a TMap or TSortedMap and so instead
	// we store an array of sorted FNames hashes and offsets. We'll use binary search to find our
	// index at runtime in O(logN) in the sorted names array, and read the offset we need in the other.
	// TODO: Use perfect hashing to bring it to O(1)

	SIZE_T TotalSizeSeqOld = 0;
	SIZE_T TotalSizeSeqNew = 0;

	OutAnimSequenceMappings.Empty(NumSequences);
	for (int32 MappingIndex = 0; MappingIndex < NumSequences; ++MappingIndex)
	{
		UAnimSequence* AnimSeq = CookedSequences[MappingIndex];
		const acl::compressed_tracks* CompressedTracks = ACLDBCompressedTracks[MappingIndex];
		FACLDatabaseCompressedAnimData& AnimData = static_cast<FACLDatabaseCompressedAnimData&>(*AnimSeq->CompressedData.CompressedDataStructure);

		// Align our sequence to 16 bytes
		CompressedSequenceOffset = acl::align_to(CompressedSequenceOffset, 16);

		// Add our mapping
		OutAnimSequenceMappings.Add((uint64(AnimData.SequenceNameHash) << 32) | uint64(CompressedSequenceOffset));

		// Increment our offset but don't align since we don't want to add unnecesary padding at the end of the last sequence
		CompressedSequenceOffset += CompressedTracks->get_size();

		TotalSizeSeqOld += ACLCompressedTracks[MappingIndex]->get_size();
		TotalSizeSeqNew += CompressedTracks->get_size();
	}

	auto BytesToMB = [](SIZE_T NumBytes) { return (double)NumBytes / (1024.0 * 1024.0); };

	UE_LOG(LogAnimationCompression, Log, TEXT("ACL DB [%s] Sequences (%u) went from %.2f MB -> %.2f MB. DB is %.2f MB"),
		*GetPathName(), NumSequences, BytesToMB(TotalSizeSeqOld), BytesToMB(TotalSizeSeqNew), BytesToMB(SplitDB->get_total_size()));
	UE_LOG(LogAnimationCompression, Log, TEXT("    DB metadata is %.2f MB"), BytesToMB(SplitDB->get_size()));
	UE_LOG(LogAnimationCompression, Log, TEXT("    DB medium tier is %.2f MB"), BytesToMB(BulkDataSizeMedium));
	UE_LOG(LogAnimationCompression, Log, TEXT("    DB lowest tier is %.2f MB%s"), BytesToMB(BulkDataSizeLow), bStripLowestTier ? TEXT(" (stripped)") : TEXT(""));

	// Make sure to sort our array, it'll be sorted by hash first since it lives in the top bits
	OutAnimSequenceMappings.Sort();

	// Our full buffer size is our resulting offset
	const uint32 CompressedBytesSize = CompressedSequenceOffset;

	// Copy our database
	OutCompressedBytes.Empty(CompressedBytesSize);
	OutCompressedBytes.AddZeroed(CompressedBytesSize);
	FMemory::Memcpy(OutCompressedBytes.GetData(), SplitDB, CompressedDatabaseSize);

	// Copy our compressed clips
	CompressedSequenceOffset = acl::align_to(CompressedDatabaseSize, 16);	// Reset

	for (const acl::compressed_tracks* CompressedTracks : ACLDBCompressedTracks)
	{
		// Align our sequence to 16 bytes
		CompressedSequenceOffset = acl::align_to(CompressedSequenceOffset, 16);

		// Copy our data
		FMemory::Memcpy(OutCompressedBytes.GetData() + CompressedSequenceOffset, CompressedTracks, CompressedTracks->get_size());

		// Increment our offset but don't align since we don't want to add unnecesary padding at the end of the last sequence
		CompressedSequenceOffset += CompressedTracks->get_size();
	}

	// Copy our bulk data
	uint32 BulkDataSize = BulkDataSizeMedium;
	if (!bStripLowestTier && BulkDataEffectiveSizeLow != 0)
	{
		BulkDataSize = acl::align_to(BulkDataSize, acl::k_database_bulk_data_alignment) + BulkDataEffectiveSizeLow;
	}

	OutBulkData.Empty(BulkDataSize);
	OutBulkData.AddZeroed(BulkDataSize);
	FMemory::Memcpy(OutBulkData.GetData(), SplitDBBulkDataMedium, BulkDataSizeMedium);

	if (!bStripLowestTier && BulkDataEffectiveSizeLow != 0)
	{
		FMemory::Memcpy(acl::align_to(OutBulkData.GetData() + BulkDataSizeMedium, acl::k_database_bulk_data_alignment), SplitDBBulkDataLow, BulkDataEffectiveSizeLow);
	}

	// Free the split instance we no longer need
	ACLAllocatorImpl.deallocate(SplitDB, CompressedDatabaseSize);
	ACLAllocatorImpl.deallocate(SplitDBBulkDataMedium, BulkDataSizeMedium);
	ACLAllocatorImpl.deallocate(SplitDBBulkDataLow, BulkDataSizeLow);

	// Free our clip copies
	for (acl::compressed_tracks* CompressedTracks : ACLDBCompressedTracks)
	{
		ACLAllocatorImpl.deallocate(CompressedTracks, CompressedTracks->get_size());
	}
}

void UAnimationCompressionLibraryDatabase::UpdatePreviewState(bool bBuildDatabase)
{
	// Check if we need to build/rebuild our preview database
	if (bBuildDatabase)
	{
		// Create a temporary database now so we can preview our animations at the desired quality
		PreviewDatabaseStreamer.Reset();
		DatabaseContext.reset();

		BuildDatabase(PreviewCompressedBytes, PreviewAnimSequenceMappings, PreviewBulkData);

		if (PreviewCompressedBytes.Num() != 0)
		{
			// Our database was built, initialize what we need to be able to use it
			const acl::compressed_database* CompressedDatabase = acl::make_compressed_database(PreviewCompressedBytes.GetData());
			check(CompressedDatabase != nullptr && CompressedDatabase->is_valid(false).empty());

			PreviewDatabaseStreamer = MakeUnique<UE4DatabasePreviewStreamer>(*CompressedDatabase, PreviewBulkData);

			const bool ContextInitResult = DatabaseContext.initialize(ACLAllocatorImpl, *CompressedDatabase, *PreviewDatabaseStreamer, *PreviewDatabaseStreamer);
			checkf(ContextInitResult, TEXT("ACL failed to initialize the database context"));

			// New fidelity is lowest
			CurrentVisualFidelity = ACLVisualFidelity::Lowest;
		}
	}

	// Perform any streaming request as a result of our new preview/database state
	if (DatabaseContext.is_initialized())
	{
		SetVisualFidelity(PreviewVisualFidelity);
	}
}

bool UAnimationCompressionLibraryDatabase::UpdateReferencingAnimSequenceList()
{
	// Grab every anim sequence that references us
	TArray<UAnimSequence*> ReferencingAnimSequences;
	for (TObjectIterator<UAnimSequence> It; It; ++It)
	{
		UAnimSequence* AnimSeq = *It;
		if (AnimSeq->GetOutermost() == GetTransientPackage())
		{
			continue;
		}

		UAnimBoneCompressionSettings* Settings = AnimSeq->BoneCompressionSettings;
		if (Settings == nullptr || Settings->Codecs.Num() != 1)
		{
			continue;
		}

		UAnimBoneCompressionCodec_ACLDatabase* DatabaseCodec = Cast<UAnimBoneCompressionCodec_ACLDatabase>(Settings->Codecs[0]);
		if (DatabaseCodec != nullptr && DatabaseCodec->DatabaseAsset == this)
		{
			ReferencingAnimSequences.Add(AnimSeq);
		}
	}

	// Sort our anim sequences by path name to ensure predictable and consistent results
	struct FCompareObjectNames
	{
		FORCEINLINE bool operator()(const UAnimSequence& Lhs, const UAnimSequence& Rhs) const
		{
			return Lhs.GetPathName().Compare(Rhs.GetPathName()) < 0;
		}
	};
	ReferencingAnimSequences.Sort(FCompareObjectNames());

	// Check if the list is any different to avoid marking as dirty if we aren't
	const int32 NumSeqs = ReferencingAnimSequences.Num();
	bool bIsDirty = false;
	if (AnimSequences.Num() == NumSeqs)
	{
		for (int32 SeqIdx = 0; SeqIdx < NumSeqs; ++SeqIdx)
		{
			if (AnimSequences[SeqIdx] != ReferencingAnimSequences[SeqIdx])
			{
				// Sorted arrays do not match, we are dirty
				bIsDirty = true;
				break;
			}
		}
	}
	else
	{
		// Count differs, dirty for sure
		bIsDirty = true;
	}

	if (bIsDirty)
	{
		// Swap our content since we are new
		Swap(AnimSequences, ReferencingAnimSequences);

		MarkPackageDirty();

		// If we were previewing, update our database
		if (PreviewDatabaseStreamer)
		{
			UpdatePreviewState(true);
		}
	}

	return bIsDirty;
}
#endif

void UAnimationCompressionLibraryDatabase::BeginDestroy()
{
	Super::BeginDestroy();

	if (DatabaseStreamer)
	{
		// Wait for any pending IO requests
		UE4DatabaseStreamer* Streamer = (UE4DatabaseStreamer*)DatabaseStreamer.Release();
		Streamer->WaitForStreamingToComplete();

		// Reset our context to make sure it no longer references the streamer
		DatabaseContext.reset();

		// Free our streamer, it is no longer needed
		delete Streamer;
	}

#if WITH_EDITORONLY_DATA
	if (PreviewDatabaseStreamer)
	{
		// Reset our context to make sure it no longer references the streamer
		DatabaseContext.reset();

		// Free our streamer, it is no longer needed
		PreviewDatabaseStreamer.Reset();
	}
#endif
}

void UAnimationCompressionLibraryDatabase::PostLoad()
{
	Super::PostLoad();

	if (CookedCompressedBytes.Num() != 0)
	{
		const acl::compressed_database* CompressedDatabase = acl::make_compressed_database(CookedCompressedBytes.GetData());
		check(CompressedDatabase != nullptr && CompressedDatabase->is_valid(false).empty());

		DatabaseStreamer = MakeUnique<UE4DatabaseStreamer>(*CompressedDatabase, CookedBulkData);

		const bool ContextInitResult = DatabaseContext.initialize(ACLAllocatorImpl, *CompressedDatabase, *DatabaseStreamer, *DatabaseStreamer);
		checkf(ContextInitResult, TEXT("ACL failed to initialize the database context"));
	}
}

void UAnimationCompressionLibraryDatabase::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

	if (bCooked)
	{
		CookedBulkData.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
		CookedBulkData.Serialize(Ar, this, INDEX_NONE, false);
	}
}

void UAnimationCompressionLibraryDatabase::SetVisualFidelity(ACLVisualFidelity VisualFidelity)
{
	SetVisualFidelityImpl(VisualFidelity, nullptr);
}

void UAnimationCompressionLibraryDatabase::SetVisualFidelityImpl(ACLVisualFidelity VisualFidelity, ACLVisualFidelityChangeResult* OutResult)
{
	// Must execute on the main thread but must do so while animations aren't updating
	check(IsInGameThread());

#if WITH_EDITORONLY_DATA
	if (!DatabaseContext.is_initialized())
	{
		// We are in the editor and we aren't previewing yet which means the highest quality is showing, build the database now
		// so we can properly preview
		UpdatePreviewState(true);
	}
#endif

	check(DatabaseContext.is_initialized());

	const bool bIsFirstRequest = FidelityChangeRequests.Num() == 0;
	const ACLVisualFidelity FinalEffectiveFidelity = bIsFirstRequest ? CurrentVisualFidelity : FidelityChangeRequests.Last().Fidelity;

	// If the desired visual fidelity matches the final effective fidelity once all change requests have completed, we can ignore it
	if (VisualFidelity == FinalEffectiveFidelity)
	{
		if (OutResult != nullptr)
		{
			*OutResult = ACLVisualFidelityChangeResult::Completed;
		}

		return;
	}

	UE_LOG(LogAnimationCompression, Log, TEXT("ACL database is requesting visual fidelity %s [%s]"), VisualFidelityToString(VisualFidelity), *GetPathName());

	// Add our change requests
	// To simplify handling, change requests only transition from one fidelity level to the next closest
	// Large changes are split into simpler steps
	switch (FinalEffectiveFidelity)
	{
	case ACLVisualFidelity::Highest:
		switch (VisualFidelity)
		{
		case ACLVisualFidelity::Medium:
			// From highest to medium we need a single change
			FidelityChangeRequests.Add({ OutResult, NextFidelityChangeRequestID++, ACLVisualFidelity::Medium, false });
			break;
		case ACLVisualFidelity::Lowest:
			// From highest to lowest we need two changes
			FidelityChangeRequests.Add({ nullptr, NextFidelityChangeRequestID++, ACLVisualFidelity::Medium, false });
			FidelityChangeRequests.Add({ OutResult, NextFidelityChangeRequestID++, ACLVisualFidelity::Lowest, false });
			break;
		default:
			checkf(false, TEXT("Unexpected visual fidelity value"));
			break;
		}
		break;
	case ACLVisualFidelity::Medium:
		switch (VisualFidelity)
		{
		case ACLVisualFidelity::Highest:
			// From medium to highest we need a single change
			FidelityChangeRequests.Add({ OutResult, NextFidelityChangeRequestID++, ACLVisualFidelity::Highest, false });
			break;
		case ACLVisualFidelity::Lowest:
			// From medium to lowest we need a single change
			FidelityChangeRequests.Add({ OutResult, NextFidelityChangeRequestID++, ACLVisualFidelity::Lowest, false });
			break;
		default:
			checkf(false, TEXT("Unexpected visual fidelity value"));
			break;
		}
		break;
	case ACLVisualFidelity::Lowest:
		switch (VisualFidelity)
		{
		case ACLVisualFidelity::Highest:
			// From lowest to highest we need two changes
			FidelityChangeRequests.Add({ nullptr, NextFidelityChangeRequestID++, ACLVisualFidelity::Medium, false });
			FidelityChangeRequests.Add({ OutResult, NextFidelityChangeRequestID++, ACLVisualFidelity::Highest, false });
			break;
		case ACLVisualFidelity::Medium:
			// From lowest to medium we need a single change
			FidelityChangeRequests.Add({ OutResult, NextFidelityChangeRequestID++, ACLVisualFidelity::Medium, false });
			break;
		default:
			checkf(false, TEXT("Unexpected visual fidelity value"));
			break;
		}
		break;
	}

	// If this is the first request, queue our ticker so we can start tracking our changes
	if (bIsFirstRequest)
	{
		auto UpdateVisualFidelity = [this](float DeltaTime) { return UpdateVisualFidelityTicker(DeltaTime); };
		FTicker::GetCoreTicker().AddTicker(TEXT("ACLDBStreamOut"), 0.0F, UpdateVisualFidelity);
	}
}

static void LogRequestResult(const UAnimationCompressionLibraryDatabase& Database, acl::database_stream_request_result Result)
{
	switch (Result)
	{
	default:
		UE_LOG(LogAnimationCompression, Log, TEXT("Unknown ACL database stream request result: %u [%s]"), uint32(Result), *Database.GetPathName());
		break;
	case acl::database_stream_request_result::done:
		UE_LOG(LogAnimationCompression, Log, TEXT("ACL database streaming is done [%s]"), *Database.GetPathName());
		break;
	case acl::database_stream_request_result::dispatched:
		break;
	case acl::database_stream_request_result::streaming_in_progress:
		UE_LOG(LogAnimationCompression, Log, TEXT("ACL database streaming is already in progress [%s]"), *Database.GetPathName());
		break;
	case acl::database_stream_request_result::context_not_initialized:
		UE_LOG(LogAnimationCompression, Log, TEXT("ACL database context not initialized [%s]"), *Database.GetPathName());
		break;
	case acl::database_stream_request_result::invalid_database_tier:
		UE_LOG(LogAnimationCompression, Log, TEXT("Specified ACL database tier is invalid [%s]"), *Database.GetPathName());
		break;
	case acl::database_stream_request_result::no_free_streaming_requests:
		UE_LOG(LogAnimationCompression, Log, TEXT("Failed to find a free ACL database streaming request [%s]"), *Database.GetPathName());
		break;
	}
}

static void FailAllRequests(TArray<FFidelityChangeRequest>& Requests)
{
	for (FFidelityChangeRequest& Request : Requests)
	{
		if (Request.Result != nullptr)
		{
			*Request.Result = ACLVisualFidelityChangeResult::Failed;
		}
	}
}

static uint32 CalculateNumChunksToStream(const acl::compressed_database& Database, uint32 MaxStreamRequestSizeKB)
{
	if (MaxStreamRequestSizeKB == 0)
	{
		return ~0U;
	}

	const uint32 MaxStreamRequestSize = MaxStreamRequestSizeKB * 1024;
	const uint32 MaxChunkSize = Database.get_max_chunk_size();
	return FMath::Max<uint32>(MaxStreamRequestSize / MaxChunkSize, 1);	// Must stream at least one chunk
}

/**
* Visual fidelity is updated through this latent ticker. Depending on the current database fidelity,
* we determine whether we need to stream in/out or do nothing.
* If a fidelity change is ongoing and a new value is requested, it will be queued. Subsequent changes
* at the same fidelity level simply update the parameters and do nothing.
* This greatly simplifies the level of control that ACL offers. A user can simply dictate the desired quality level
* and let the plugin determine what to do.
*/
bool UAnimationCompressionLibraryDatabase::UpdateVisualFidelityTicker(float DeltaTime)
{
	check(DatabaseContext.is_initialized());

	const uint32 NumChunksToStream = CalculateNumChunksToStream(*DatabaseContext.get_compressed_database(), MaxStreamRequestSizeKB);

	while (FidelityChangeRequests.Num() != 0)
	{
		FFidelityChangeRequest& Request = FidelityChangeRequests[0];
		check(Request.Fidelity != CurrentVisualFidelity);

		bool bIsRequestCompleted = false;
		acl::database_stream_request_result Result = acl::database_stream_request_result::context_not_initialized;

		switch (CurrentVisualFidelity)
		{
		default:
		case ACLVisualFidelity::Highest:
			// We have highest fidelity
			// The medium/lowest importance tiers are already streamed in

			checkf(Request.Fidelity == ACLVisualFidelity::Medium, TEXT("Unexpected visual fidelity value"));
			if (Request.Fidelity != ACLVisualFidelity::Medium)
			{
				// Something wrong happened, ignore all change requests
				FailAllRequests(FidelityChangeRequests);
				FidelityChangeRequests.Empty();
			}
			else
			{
				// To reach medium quality, we need to stream out our lowest importance tier
				Result = DatabaseContext.stream_out(acl::quality_tier::lowest_importance, NumChunksToStream);
				LogRequestResult(*this, Result);

				switch (Result)
				{
				case acl::database_stream_request_result::done:
					// We are done
					bIsRequestCompleted = true;
					break;
				case acl::database_stream_request_result::dispatched:
					// Streaming request has been dispatched, done for now
					Request.bIsInProgress = true;
					break;
				case acl::database_stream_request_result::streaming_in_progress:
					// Streaming is already in progress, done for now
					checkf(Request.bIsInProgress, TEXT("Expected request to already be in progress"));
					break;
				default:
					// Something wrong happened, ignore all change requests
					checkf(false, TEXT("Something unexpected happened while ACL is streaming"));
					FailAllRequests(FidelityChangeRequests);
					FidelityChangeRequests.Empty();
					break;
				}
			}

			// Done
			break;
		case ACLVisualFidelity::Medium:
			// We have medium fidelity
			// The medium importance tier is already streamed in

			checkf(Request.Fidelity == ACLVisualFidelity::Highest || Request.Fidelity == ACLVisualFidelity::Lowest, TEXT("Unexpected visual fidelity value"));
			if (Request.Fidelity != ACLVisualFidelity::Highest && Request.Fidelity != ACLVisualFidelity::Lowest)
			{
				// Something wrong happened, ignore all change requests
				FailAllRequests(FidelityChangeRequests);
				FidelityChangeRequests.Empty();
			}
			else
			{
				if (Request.Fidelity == ACLVisualFidelity::Highest)
				{
					// To reach highest quality, we need to stream in our lowest importance tier
					Result = DatabaseContext.stream_in(acl::quality_tier::lowest_importance, NumChunksToStream);
				}
				else
				{
					// To reach lowest quality, we need to stream out our medium importance tier
					Result = DatabaseContext.stream_out(acl::quality_tier::medium_importance, NumChunksToStream);
				}

				LogRequestResult(*this, Result);

				switch (Result)
				{
				case acl::database_stream_request_result::done:
					// We are done
					bIsRequestCompleted = true;
					break;
				case acl::database_stream_request_result::dispatched:
					// Streaming request has been dispatched, done for now
					Request.bIsInProgress = true;
					break;
				case acl::database_stream_request_result::streaming_in_progress:
					// Streaming is already in progress, done for now
					checkf(Request.bIsInProgress, TEXT("Expected request to already be in progress"));
					break;
				default:
					// Something wrong happened, ignore all change requests
					checkf(false, TEXT("Something unexpected happened while ACL is streaming"));
					FailAllRequests(FidelityChangeRequests);
					FidelityChangeRequests.Empty();
					break;
				}
			}

			// Done
			break;
		case ACLVisualFidelity::Lowest:
			// We have lowest fidelity
			// Nothing is currently streamed in

			checkf(Request.Fidelity == ACLVisualFidelity::Medium, TEXT("Unexpected visual fidelity value"));
			if (Request.Fidelity != ACLVisualFidelity::Medium)
			{
				// Something wrong happened, ignore all change requests
				FailAllRequests(FidelityChangeRequests);
				FidelityChangeRequests.Empty();
			}
			else
			{
				// To reach medium quality, we need to stream in our medium importance tier
				Result = DatabaseContext.stream_in(acl::quality_tier::medium_importance, NumChunksToStream);
				LogRequestResult(*this, Result);

				switch (Result)
				{
				case acl::database_stream_request_result::done:
					// We are done
					bIsRequestCompleted = true;
					break;
				case acl::database_stream_request_result::dispatched:
					// Streaming request has been dispatched, done for now
					Request.bIsInProgress = true;
					break;
				case acl::database_stream_request_result::streaming_in_progress:
					// Streaming is already in progress, done for now
					checkf(Request.bIsInProgress, TEXT("Expected request to already be in progress"));
					break;
				default:
					// Something wrong happened, ignore all change requests
					checkf(false, TEXT("Something unexpected happened while ACL is streaming"));
					FailAllRequests(FidelityChangeRequests);
					FidelityChangeRequests.Empty();
					break;
				}
			}

			// Done
			break;
		}

		// If we completed this request, consume it and loop again otherwise stop
		if (bIsRequestCompleted)
		{
			UE_LOG(LogAnimationCompression, Log, TEXT("ACL database is changing visual fidelity from %s to %s [%s]"), VisualFidelityToString(CurrentVisualFidelity), VisualFidelityToString(Request.Fidelity), *GetPathName());

#if WITH_EDITORONLY_DATA
			// Make sure our preview fidelity matches what just changed
			PreviewVisualFidelity = Request.Fidelity;
#endif

			CurrentVisualFidelity = Request.Fidelity;
			if (Request.Result != nullptr)
			{
				*Request.Result = ACLVisualFidelityChangeResult::Completed;
			}

			FidelityChangeRequests.RemoveAt(0);
		}
		else
		{
			break;
		}
	}

	return FidelityChangeRequests.Num() != 0;	// We need to fire again if we have more pending requests
}

class FSetDatabaseVisualFidelityAction final : public FPendingLatentAction
{
public:
	FSetDatabaseVisualFidelityAction(UAnimationCompressionLibraryDatabase* DatabaseAsset_, ACLVisualFidelity VisualFidelity_, ACLVisualFidelityChangeResult& OutResult_, const FLatentActionInfo& LatentInfo_)
		: DatabaseAsset(DatabaseAsset_)
		, VisualFidelity(VisualFidelity_)
		, OutResult(OutResult_)
		, LatentInfo(LatentInfo_)
		, bIsDispatched(false)
	{
	}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		// Must execute on the main thread
		check(IsInGameThread());

		if (DatabaseAsset == nullptr)
		{
			// No valid asset provided
			OutResult = ACLVisualFidelityChangeResult::Failed;
			Response.FinishAndTriggerIf(true, LatentInfo.ExecutionFunction, LatentInfo.Linkage, LatentInfo.CallbackTarget);
			return;
		}

		if (!bIsDispatched)
		{
			// Fire off our change request, will complete right away if no change is requested, otherwise we just dispatch
			bIsDispatched = true;
			OutResult = ACLVisualFidelityChangeResult::Dispatched;
			DatabaseAsset->SetVisualFidelityImpl(VisualFidelity, &OutResult);
		}

		// We are done once our result is set
		const bool bIsDone = OutResult != ACLVisualFidelityChangeResult::Dispatched;
		Response.FinishAndTriggerIf(bIsDone, LatentInfo.ExecutionFunction, LatentInfo.Linkage, LatentInfo.CallbackTarget);
	}

#if WITH_EDITOR
	virtual FString GetDescription() const override
	{
		return TEXT("Updating ACL database visual fidelity");
	}
#endif

private:
	UAnimationCompressionLibraryDatabase* DatabaseAsset;

	ACLVisualFidelity VisualFidelity;
	ACLVisualFidelityChangeResult& OutResult;

	FLatentActionInfo LatentInfo;
	bool bIsDispatched;
};

void UAnimationCompressionLibraryDatabase::SetVisualFidelity(UObject* WorldContextObject, FLatentActionInfo LatentInfo, UAnimationCompressionLibraryDatabase* DatabaseAsset, ACLVisualFidelityChangeResult& Result, ACLVisualFidelity VisualFidelity)
{
	// Must execute on the main thread but must do so while animations aren't updating
	check(IsInGameThread());

	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		FLatentActionManager& LatentManager = World->GetLatentActionManager();
		if (LatentManager.FindExistingAction<FSetDatabaseVisualFidelityAction>(LatentInfo.CallbackTarget, LatentInfo.UUID) == nullptr)
		{
			FSetDatabaseVisualFidelityAction* NewAction = new FSetDatabaseVisualFidelityAction(DatabaseAsset, VisualFidelity, Result, LatentInfo);
			LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, NewAction);
		}
	}
}

ACLVisualFidelity UAnimationCompressionLibraryDatabase::GetVisualFidelity(UAnimationCompressionLibraryDatabase* DatabaseAsset)
{
	checkf(DatabaseAsset != nullptr, TEXT("Cannot query null ACL database asset"));
	return DatabaseAsset != nullptr ? DatabaseAsset->CurrentVisualFidelity : ACLVisualFidelity::Lowest;
}
