// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "AnimBoneCompressionCodec_ACLBase.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"

#if WITH_EDITORONLY_DATA
#include "AnimBoneCompressionCodec_ACLSafe.h"
#include "Animation/AnimationSettings.h"
#include "Rendering/SkeletalMeshModel.h"

#include "ACLImpl.h"

#include <acl/compression/compress.h>
#include <acl/compression/transform_error_metrics.h>
#include <acl/compression/track_error.h>
#include <acl/core/bitset.h>
#include <acl/decompression/decompress.h>
#endif	// WITH_EDITORONLY_DATA

#include <acl/core/compressed_tracks.h>

void FACLCompressedAnimData::SerializeCompressedData(class FArchive& Ar)
{
	ICompressedAnimData::SerializeCompressedData(Ar);

#if WITH_EDITORONLY_DATA
	if (!Ar.IsFilterEditorOnly())
	{
		Ar << StrippedBindPose;
	}
#endif

#if WITH_ACL_EXCLUDED_FROM_STRIPPING_CHECKS
	int32 TracksExcludedFromStrippingBitSetCount = TracksExcludedFromStrippingBitSet.Num();
	Ar << TracksExcludedFromStrippingBitSetCount;

	// Checks are enabled, serialize our data (in editor and non-shipping cooked builds)
	if (Ar.IsLoading())
	{
		TArray<uint32> ExcludedBitSetData;
		ExcludedBitSetData.AddUninitialized(TracksExcludedFromStrippingBitSetCount);

		Ar.Serialize(ExcludedBitSetData.GetData(), TracksExcludedFromStrippingBitSetCount * sizeof(int32));

		Swap(TracksExcludedFromStrippingBitSet, ExcludedBitSetData);
	}
	else if (Ar.IsSaving() || Ar.IsCountingMemory())
	{
		Ar.Serialize(TracksExcludedFromStrippingBitSet.GetData(), TracksExcludedFromStrippingBitSetCount * sizeof(int32));
	}
#else
	int32 TracksExcludedFromStrippingBitSetCount = 0;
	Ar << TracksExcludedFromStrippingBitSetCount;

	if (Ar.IsLoading())
	{
		// If checks are disabled, skip the data in the archive since we don't need it
		const int64 CurrentPos = Ar.Tell();
		Ar.Seek(CurrentPos + TracksExcludedFromStrippingBitSetCount * sizeof(int32));
	}
	else if (Ar.IsCountingMemory())
	{
		// Nothing to count since we don't use the data
	}
	else
	{
		// Should never happen since stripping checks should always be enabled in the editor and during cooking where saving happens
		UE_LOG(LogAnimationCompression, Fatal, TEXT("Cannot save ACL excluded from stripping bitset data in this configuration"));
	}
#endif
}

bool FACLCompressedAnimData::IsValid() const
{
	if (CompressedByteStream.Num() == 0)
	{
		return false;
	}

	const acl::compressed_tracks* CompressedClipData = acl::make_compressed_tracks(CompressedByteStream.GetData());
	return CompressedClipData != nullptr && CompressedClipData->is_valid(false).empty();
}

UAnimBoneCompressionCodec_ACLBase::UAnimBoneCompressionCodec_ACLBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	CompressionLevel = ACLCL_Medium;

	// We use a higher virtual vertex distance when bones have a socket attached or are keyed end effectors (IK, hand, camera, etc)
	// We use 100cm instead of 3cm. UE 4 usually uses 50cm (END_EFFECTOR_DUMMY_BONE_LENGTH_SOCKET) but
	// we use a higher value anyway due to the fact that ACL has no error compensation and it is more aggressive.
	DefaultVirtualVertexDistance = 3.0f;	// 3cm, suitable for ordinary characters
	SafeVirtualVertexDistance = 100.0f;		// 100cm

	ErrorThreshold = 0.01f;					// 0.01cm, conservative enough for cinematographic quality

	bStripBindPose = false;					// Disabled by default since it could be destructive depending whether bones are decompressed individually or not
#endif	// WITH_EDITORONLY_DATA
}

#if WITH_EDITORONLY_DATA
static void AppendMaxVertexDistances(USkeletalMesh* OptimizationTarget, TMap<FName, float>& BoneMaxVertexDistanceMap)
{
#if (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27) || ENGINE_MAJOR_VERSION >= 5
	USkeleton* Skeleton = OptimizationTarget != nullptr ? OptimizationTarget->GetSkeleton() : nullptr;
#else
	USkeleton* Skeleton = OptimizationTarget != nullptr ? OptimizationTarget->Skeleton : nullptr;
#endif

	if (Skeleton == nullptr)
	{
		return; // No data to work with
	}

	const FSkeletalMeshModel* MeshModel = OptimizationTarget->GetImportedModel();
	if (MeshModel == nullptr || MeshModel->LODModels.Num() == 0)
	{
		return;	// No data to work with
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	const TArray<FTransform>& RefSkeletonPose = RefSkeleton.GetRefBonePose();
	const uint32 NumBones = RefSkeletonPose.Num();

	TArray<FTransform> RefSkeletonObjectSpacePose;
	RefSkeletonObjectSpacePose.AddUninitialized(NumBones);
	for (uint32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const int32 ParentBoneIndex = RefSkeleton.GetParentIndex(BoneIndex);
		if (ParentBoneIndex != INDEX_NONE)
		{
			RefSkeletonObjectSpacePose[BoneIndex] = RefSkeletonPose[BoneIndex] * RefSkeletonObjectSpacePose[ParentBoneIndex];
		}
		else
		{
			RefSkeletonObjectSpacePose[BoneIndex] = RefSkeletonPose[BoneIndex];
		}
	}

	// Iterate over every vertex and track which one is the most distant for every bone
	TArray<float> MostDistantVertexDistancePerBone;
	MostDistantVertexDistancePerBone.AddZeroed(NumBones);

	const uint32 NumSections = MeshModel->LODModels[0].Sections.Num();
	for (uint32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
	{
		const FSkelMeshSection& Section = MeshModel->LODModels[0].Sections[SectionIndex];
		const uint32 NumVertices = Section.SoftVertices.Num();
		for (uint32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
		{
			const FSoftSkinVertex& VertexInfo = Section.SoftVertices[VertexIndex];
			for (uint32 InfluenceIndex = 0; InfluenceIndex < MAX_TOTAL_INFLUENCES; ++InfluenceIndex)
			{
				if (VertexInfo.InfluenceWeights[InfluenceIndex] != 0)
				{
					const uint32 SectionBoneIndex = VertexInfo.InfluenceBones[InfluenceIndex];
					const uint32 BoneIndex = Section.BoneMap[SectionBoneIndex];

					const FTransform& BoneTransform = RefSkeletonObjectSpacePose[BoneIndex];

					const float VertexDistanceToBone = FVector::Distance(VertexInfo.Position, BoneTransform.GetTranslation());

					float& MostDistantVertexDistance = MostDistantVertexDistancePerBone[BoneIndex];
					MostDistantVertexDistance = FMath::Max(MostDistantVertexDistance, VertexDistanceToBone);
				}
			}
		}
	}

	// Store the results in a map by bone name since the optimizing target might use a different
	// skeleton mapping.
	for (uint32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
		const float MostDistantVertexDistance = MostDistantVertexDistancePerBone[BoneIndex];

		float& BoneMaxVertexDistance = BoneMaxVertexDistanceMap.FindOrAdd(BoneName, 0.0f);
		BoneMaxVertexDistance = FMath::Max(BoneMaxVertexDistance, MostDistantVertexDistance);
	}
}

static void PopulateShellDistanceFromOptimizationTargets(const FCompressibleAnimData& CompressibleAnimData, const TArray<USkeletalMesh*>& OptimizationTargets, acl::track_array_qvvf& ACLTracks)
{
	// For each bone, get the furtest vertex distance
	TMap<FName, float> BoneMaxVertexDistanceMap;
	for (USkeletalMesh* OptimizationTarget : OptimizationTargets)
	{
		AppendMaxVertexDistances(OptimizationTarget, BoneMaxVertexDistanceMap);
	}

	const uint32 NumBones = ACLTracks.get_num_tracks();
	for (uint32 ACLBoneIndex = 0; ACLBoneIndex < NumBones; ++ACLBoneIndex)
	{
		acl::track_qvvf& ACLTrack = ACLTracks[ACLBoneIndex];
		const FName BoneName(ACLTrack.get_name().c_str());

		const float* MostDistantVertexDistance = BoneMaxVertexDistanceMap.Find(BoneName);
		if (MostDistantVertexDistance == nullptr || *MostDistantVertexDistance <= 0.0F)
		{
			continue;	// No skinned vertices for this bone, skipping
		}

		const FBoneData& UE4Bone = CompressibleAnimData.BoneData[ACLBoneIndex];

		acl::track_desc_transformf& Desc = ACLTrack.get_description();

		// We set our shell distance to the most distant vertex distance.
		// This ensures that we measure the error where that vertex lies.
		// Together with the precision value, all vertices skinned to this bone
		// will be guaranteed to have an error smaller or equal to the precision
		// threshold used.
		if (UE4Bone.bHasSocket || UE4Bone.bKeyEndEffector)
		{
			// Bones that have sockets or are key end effectors require extra precision, make sure
			// that our shell distance is at least what we ask of it regardless of the skinning
			// information.
			Desc.shell_distance = FMath::Max(Desc.shell_distance, *MostDistantVertexDistance);
		}
		else
		{
			// This could be higher or lower than the default value used by ordinary bones.
			// This thus taylors the shell distance to the visual mesh.
			Desc.shell_distance = *MostDistantVertexDistance;
		}
	}
}

static TArray<uint32> StripBindPose(const FCompressibleAnimData& CompressibleAnimData, const TArray<FName>& BindPoseStrippingBoneExclusionList, acl::track_array_qvvf& ACLTracks)
{
	const int32 NumTracks = CompressibleAnimData.TrackToSkeletonMapTable.Num();
	const acl::bitset_description ExcludedBitsetDesc = acl::bitset_description::make_from_num_bits(NumTracks);

	TArray<uint32> TracksExcludedFromStrippingBitSet;
	TracksExcludedFromStrippingBitSet.AddZeroed(ExcludedBitsetDesc.get_size());

	const int32 NumBones = CompressibleAnimData.BoneData.Num();
	const rtm::vector4f DefaultScale = rtm::vector_set(1.0f);

	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FBoneData& UE4Bone = CompressibleAnimData.BoneData[BoneIndex];

		acl::track_qvvf& Track = ACLTracks[BoneIndex];
		acl::track_desc_transformf& Desc = Track.get_description();

		// When we decompress a whole pose, the output buffer will already contain the bind pose.
		// As such, we skip all default sub-tracks and avoid writing anything to the output pose.
		// By setting the default value to the bind pose, default sub-tracks will be equal to the bind pose
		// and be stripped from the compressed data buffer entirely.
		// However, when we decompress a single bone, we cannot recover what the bind pose is at the codec level.
		// Since we can't output nothing, we must exclude these bones to make sure the bind pose is still
		// contained in the compressed data buffer. Only if these are equal to the default sub-track value
		// will they be skipped and stripped (e.g. identity rotation/translation/scale).

		// As such, here are the potential behaviors for non-animated bones equal to the default_value below:
		//     A stripped bone equal to the bind pose (stripped)
		//         Skipped during whole pose decompression, already present in output buffer
		//         Single bone decompression will output the identity which is INCORRECT! Hence why if this is needed, we must exclude this bone
		//     A stripped bone not equal to the bind pose (it won't be stripped nor skipped)
		//         Decompressed normally with the rest of the pose and written to the output buffer
		//         Single bone decompression will output the correct value
		//     An excluded bone that is equal to the identity (stripped, normal pre ACL 2.1 behavior)
		//         Skipped during whole pose decompression, already present in output buffer
		//         Single bone decompression will output the identity
		//     An excluded bone that is not equal to the identity (it won't be stripped nor skipped)
		//         Decompressed normally with the rest of the pose and written to the output buffer
		//         Single bone decompression will output the correct value

		const uint32 TrackIndex = Track.get_output_index();

		// The root bone is always excluded from bind pose stripping since we query it during normal decompression
		// UE4 only allows a single root bone but it could have a parent that is stripped during compression
		// As such, we use the output track index to tell it apart
		// If there are more root bones, it doesn't matter, only track 0 is queried for root motion manually
		const bool bIsRootBone = TrackIndex == 0;

		if (!bIsRootBone && !BindPoseStrippingBoneExclusionList.Contains(UE4Bone.Name))
		{
			// This bone isn't excluded, set the default value to the bind pose so that it can be stripped
			Desc.default_value = rtm::qvv_set(QuatCast(UE4Bone.Orientation), VectorCast(UE4Bone.Position), DefaultScale);
		}
		else if (TrackIndex != acl::k_invalid_track_index)
		{
			// This bone is excluded from stripping and is used
			acl::bitset_set(TracksExcludedFromStrippingBitSet.GetData(), ExcludedBitsetDesc, TrackIndex, true);
		}
	}

	return TracksExcludedFromStrippingBitSet;
}

void UAnimBoneCompressionCodec_ACLBase::PopulateStrippedBindPose(const FCompressibleAnimData& CompressibleAnimData, const acl::track_array_qvvf& ACLTracks, ICompressedAnimData& AnimData) const
{
	FACLCompressedAnimData& ACLAnimData = static_cast<FACLCompressedAnimData&>(AnimData);
	::PopulateStrippedBindPose(CompressibleAnimData, ACLTracks, ACLAnimData.StrippedBindPose);
}

void UAnimBoneCompressionCodec_ACLBase::SetExcludedFromStrippingBitSet(const TArray<uint32>& TracksExcludedFromStrippingBitSet, ICompressedAnimData& AnimData) const
{
#if WITH_ACL_EXCLUDED_FROM_STRIPPING_CHECKS
	FACLCompressedAnimData& ACLAnimData = static_cast<FACLCompressedAnimData&>(AnimData);
	ACLAnimData.TracksExcludedFromStrippingBitSet = TracksExcludedFromStrippingBitSet;
#endif
}

bool UAnimBoneCompressionCodec_ACLBase::Compress(const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult)
{
	acl::track_array_qvvf ACLTracks = BuildACLTransformTrackArray(ACLAllocatorImpl, CompressibleAnimData, DefaultVirtualVertexDistance, SafeVirtualVertexDistance, false);

	acl::track_array_qvvf ACLBaseTracks;
	if (CompressibleAnimData.bIsValidAdditive)
		ACLBaseTracks = BuildACLTransformTrackArray(ACLAllocatorImpl, CompressibleAnimData, DefaultVirtualVertexDistance, SafeVirtualVertexDistance, true);

	UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation raw size: %u bytes [%s]"), ACLTracks.get_raw_size(), *CompressibleAnimData.FullName);

	// If we have an optimization target, use it
	TArray<USkeletalMesh*> OptimizationTargets = GetOptimizationTargets();
	if (OptimizationTargets.Num() != 0)
	{
		PopulateShellDistanceFromOptimizationTargets(CompressibleAnimData, OptimizationTargets, ACLTracks);
	}

	// Set our error threshold
	for (acl::track_qvvf& Track : ACLTracks)
		Track.get_description().precision = ErrorThreshold;

	// Override track settings if we need to
	if (IsA<UAnimBoneCompressionCodec_ACLSafe>())
	{
		// Disable constant rotation track detection
		for (acl::track_qvvf& Track : ACLTracks)
		{
			Track.get_description().constant_rotation_threshold_angle = 0.0f;
		}
	}

	TArray<uint32> TracksExcludedFromStrippingBitSet;

	// Enable bind pose stripping if we need to.
	// Additive sequences have their bind pose equivalent as the identity transform and as
	// such, ACL performs stripping by default and everything works great.
	// We thus disable the custom behavior and the exclusion list.
	const bool bUsesBindPoseStripping = bStripBindPose && !CompressibleAnimData.bIsValidAdditive;
	if (bUsesBindPoseStripping)
	{
		TracksExcludedFromStrippingBitSet = StripBindPose(CompressibleAnimData, BindPoseStrippingBoneExclusionList, ACLTracks);
	}

	acl::compression_settings Settings;
	GetCompressionSettings(Settings);

	acl::qvvf_transform_error_metric DefaultErrorMetric;
	acl::additive_qvvf_transform_error_metric<acl::additive_clip_format8::additive1> AdditiveErrorMetric;
	if (!ACLBaseTracks.is_empty())
	{
		Settings.error_metric = &AdditiveErrorMetric;
	}
	else
	{
		Settings.error_metric = &DefaultErrorMetric;
	}

	const acl::additive_clip_format8 AdditiveFormat = acl::additive_clip_format8::additive0;

	acl::output_stats Stats;
	acl::compressed_tracks* CompressedTracks = nullptr;
	const acl::error_result CompressionResult = acl::compress_track_list(ACLAllocatorImpl, ACLTracks, Settings, ACLBaseTracks, AdditiveFormat, CompressedTracks, Stats);

	// Make sure if we managed to compress, that the error is acceptable and if it isn't, re-compress again with safer settings
	// This should be VERY rare with the default threshold
	if (CompressionResult.empty())
	{
		const ACLSafetyFallbackResult FallbackResult = ExecuteSafetyFallback(ACLAllocatorImpl, Settings, ACLTracks, ACLBaseTracks, *CompressedTracks, CompressibleAnimData, OutResult);
		if (FallbackResult != ACLSafetyFallbackResult::Ignored)
		{
			ACLAllocatorImpl.deallocate(CompressedTracks, CompressedTracks->get_size());
			CompressedTracks = nullptr;

			return FallbackResult == ACLSafetyFallbackResult::Success;
		}
	}

	if (!CompressionResult.empty())
	{
		UE_LOG(LogAnimationCompression, Warning, TEXT("ACL failed to compress clip: %s [%s]"), ANSI_TO_TCHAR(CompressionResult.c_str()), *CompressibleAnimData.FullName);
		return false;
	}

	checkSlow(CompressedTracks->is_valid(true).empty());

	const uint32 CompressedClipDataSize = CompressedTracks->get_size();

	OutResult.CompressedByteStream.Empty(CompressedClipDataSize);
	OutResult.CompressedByteStream.AddUninitialized(CompressedClipDataSize);
	FMemory::Memcpy(OutResult.CompressedByteStream.GetData(), CompressedTracks, CompressedClipDataSize);

	OutResult.Codec = this;

	OutResult.AnimData = AllocateAnimData();

#if ENGINE_MAJOR_VERSION >= 5
	OutResult.AnimData->CompressedNumberOfKeys = GetNumSamples(CompressibleAnimData);
#else
	OutResult.AnimData->CompressedNumberOfFrames = GetNumSamples(CompressibleAnimData);
#endif

#if !NO_LOGGING
	{
		// Use debug settings in case codec picked is the fallback
		acl::decompression_context<UE4DebugDecompressionSettings> Context;
		Context.initialize(*CompressedTracks);

		const acl::track_error TrackError = acl::calculate_compression_error(ACLAllocatorImpl, ACLTracks, Context, *Settings.error_metric, ACLBaseTracks);

		UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation compressed size: %u bytes [%s]"), CompressedClipDataSize, *CompressibleAnimData.FullName);
		UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation error: %.4f cm (bone %u @ %.3f) [%s]"), TrackError.error, TrackError.index, TrackError.sample_time, *CompressibleAnimData.FullName);
	}
#endif

	ACLAllocatorImpl.deallocate(CompressedTracks, CompressedClipDataSize);

	if (bUsesBindPoseStripping)
	{
		// Cache the stripped bind pose since we need it in the editor
		PopulateStrippedBindPose(CompressibleAnimData, ACLTracks, *OutResult.AnimData);

		// Allow codecs to cache which tracks are excluded from stripping for debug purposes
		SetExcludedFromStrippingBitSet(TracksExcludedFromStrippingBitSet, *OutResult.AnimData);
	}

	// Allow codecs to override final anim data and result
	PostCompression(CompressibleAnimData, OutResult);

	// Bind our compressed sequence data buffer
	OutResult.AnimData->Bind(OutResult.CompressedByteStream);

	return true;
}

void UAnimBoneCompressionCodec_ACLBase::PopulateDDCKey(FArchive& Ar)
{
	Super::PopulateDDCKey(Ar);

	uint32 ForceRebuildVersion = 10;

	Ar << ForceRebuildVersion << DefaultVirtualVertexDistance << SafeVirtualVertexDistance << ErrorThreshold;
	Ar << CompressionLevel;

	// Add the end effector match name list since if it changes, we need to re-compress
	const TArray<FString>& KeyEndEffectorsMatchNameArray = UAnimationSettings::Get()->KeyEndEffectorsMatchNameArray;
	for (const FString& MatchName : KeyEndEffectorsMatchNameArray)
	{
		uint32 MatchNameHash = GetTypeHash(MatchName);
		Ar << MatchNameHash;
	}

	Ar << bStripBindPose << BindPoseStrippingBoneExclusionList;
}

ACLSafetyFallbackResult UAnimBoneCompressionCodec_ACLBase::ExecuteSafetyFallback(acl::iallocator& Allocator, const acl::compression_settings& Settings, const acl::track_array_qvvf& RawClip, const acl::track_array_qvvf& BaseClip, const acl::compressed_tracks& CompressedClipData, const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult)
{
	return ACLSafetyFallbackResult::Ignored;
}
#endif

TUniquePtr<ICompressedAnimData> UAnimBoneCompressionCodec_ACLBase::AllocateAnimData() const
{
	return MakeUnique<FACLCompressedAnimData>();
}

void UAnimBoneCompressionCodec_ACLBase::ByteSwapIn(ICompressedAnimData& AnimData, TArrayView<uint8> CompressedData, FMemoryReader& MemoryStream) const
{
#if !PLATFORM_LITTLE_ENDIAN
#error "ACL does not currently support big-endian platforms"
#endif

	// ByteSwapIn(..) is called on load

	// TODO: ACL does not support byte swapping
	MemoryStream.Serialize(CompressedData.GetData(), CompressedData.Num());
}

void UAnimBoneCompressionCodec_ACLBase::ByteSwapOut(ICompressedAnimData& AnimData, TArrayView<uint8> CompressedData, FMemoryWriter& MemoryStream) const
{
#if !PLATFORM_LITTLE_ENDIAN
#error "ACL does not currently support big-endian platforms"
#endif

	// ByteSwapOut(..) is called on save, during cooking, or when counting memory

	// TODO: ACL does not support byte swapping
	MemoryStream.Serialize(CompressedData.GetData(), CompressedData.Num());
}
