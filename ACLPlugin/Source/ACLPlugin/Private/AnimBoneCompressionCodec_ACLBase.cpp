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
#include <acl/decompression/decompress.h>
#endif	// WITH_EDITORONLY_DATA

#include <acl/core/compressed_tracks.h>

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
#endif	// WITH_EDITORONLY_DATA
}

#if WITH_EDITORONLY_DATA
static void AppendMaxVertexDistances(USkeletalMesh* OptimizationTarget, TMap<FName, float>& BoneMaxVertexDistanceMap)
{
#if ENGINE_MINOR_VERSION >= 27
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
			Track.get_description().constant_rotation_threshold_angle = 0.0f;
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
	const bool bUseStreamingDatabase = UseDatabase();

	if (bUseStreamingDatabase)
	{
		Settings.enable_database_support = true;
	}

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
	OutResult.AnimData->CompressedNumberOfFrames = CompressibleAnimData.NumFrames;

#if !NO_LOGGING
	{
		acl::decompression_context<UE4DebugDBDecompressionSettings> Context;
		Context.initialize(*CompressedTracks);

		const acl::track_error TrackError = acl::calculate_compression_error(ACLAllocatorImpl, ACLTracks, Context, *Settings.error_metric, ACLBaseTracks);

		UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation compressed size: %u bytes [%s]"), CompressedClipDataSize, *CompressibleAnimData.FullName);
		UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation error: %.4f cm (bone %u @ %.3f) [%s]"), TrackError.error, TrackError.index, TrackError.sample_time, *CompressibleAnimData.FullName);
	}
#endif

	ACLAllocatorImpl.deallocate(CompressedTracks, CompressedClipDataSize);

	if (bUseStreamingDatabase)
	{
		RegisterWithDatabase(CompressibleAnimData, OutResult);
	}

	// Bind our compressed sequence data buffer
	OutResult.AnimData->Bind(OutResult.CompressedByteStream);

	return true;
}

void UAnimBoneCompressionCodec_ACLBase::PopulateDDCKey(FArchive& Ar)
{
	Super::PopulateDDCKey(Ar);

	uint32 ForceRebuildVersion = 2;

	Ar << ForceRebuildVersion << DefaultVirtualVertexDistance << SafeVirtualVertexDistance << ErrorThreshold;
	Ar << CompressionLevel;

	// Add the end effector match name list since if it changes, we need to re-compress
	const TArray<FString>& KeyEndEffectorsMatchNameArray = UAnimationSettings::Get()->KeyEndEffectorsMatchNameArray;
	for (const FString& MatchName : KeyEndEffectorsMatchNameArray)
	{
		uint32 MatchNameHash = GetTypeHash(MatchName);
		Ar << MatchNameHash;
	}
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
