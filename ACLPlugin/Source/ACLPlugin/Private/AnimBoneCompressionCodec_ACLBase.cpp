// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "AnimBoneCompressionCodec_ACLBase.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"

#if WITH_EDITORONLY_DATA
#include "AnimBoneCompressionCodec_ACLSafe.h"
#include "Animation/AnimationSettings.h"

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
bool UAnimBoneCompressionCodec_ACLBase::Compress(const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult)
{
	ACLAllocator AllocatorImpl;

	acl::track_array_qvvf ACLTracks = BuildACLTransformTrackArray(AllocatorImpl, CompressibleAnimData, DefaultVirtualVertexDistance, SafeVirtualVertexDistance, false);

	acl::track_array_qvvf ACLBaseTracks;
	if (CompressibleAnimData.bIsValidAdditive)
		ACLBaseTracks = BuildACLTransformTrackArray(AllocatorImpl, CompressibleAnimData, DefaultVirtualVertexDistance, SafeVirtualVertexDistance, true);

	UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation raw size: %u bytes"), ACLTracks.get_raw_size());

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

	acl::output_stats Stats;
	acl::compressed_tracks* CompressedTracks = nullptr;
	acl::error_result CompressionResult = acl::compress_track_list(AllocatorImpl, ACLTracks, Settings, ACLBaseTracks, AdditiveFormat, CompressedTracks, Stats);

	// Make sure if we managed to compress, that the error is acceptable and if it isn't, re-compress again with safer settings
	// This should be VERY rare with the default threshold
	if (CompressionResult.empty())
	{
		const ACLSafetyFallbackResult FallbackResult = ExecuteSafetyFallback(AllocatorImpl, Settings, ACLTracks, ACLBaseTracks, *CompressedTracks, CompressibleAnimData, OutResult);
		if (FallbackResult != ACLSafetyFallbackResult::Ignored)
		{
			AllocatorImpl.deallocate(CompressedTracks, CompressedTracks->get_size());
			CompressedTracks = nullptr;

			return FallbackResult == ACLSafetyFallbackResult::Success;
		}
	}

	if (!CompressionResult.empty())
	{
		UE_LOG(LogAnimationCompression, Warning, TEXT("ACL failed to compress clip: %s"), ANSI_TO_TCHAR(CompressionResult.c_str()));
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
	OutResult.AnimData->Bind(OutResult.CompressedByteStream);

#if !NO_LOGGING
	{
		acl::decompression_context<acl::debug_transform_decompression_settings> Context;
		Context.initialize(*CompressedTracks);
		const acl::track_error TrackError = acl::calculate_compression_error(AllocatorImpl, ACLTracks, Context, *Settings.error_metric, ACLBaseTracks);

		UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation compressed size: %u bytes"), CompressedClipDataSize);
		UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation error: %.4f cm (bone %u @ %.3f)"), TrackError.error, TrackError.index, TrackError.sample_time);
	}
#endif

	AllocatorImpl.deallocate(CompressedTracks, CompressedClipDataSize);
	return true;
}

void UAnimBoneCompressionCodec_ACLBase::PopulateDDCKey(FArchive& Ar)
{
	Super::PopulateDDCKey(Ar);

	uint32 ForceRebuildVersion = 0;

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

	// TODO: ACL does not support byte swapping
	FACLCompressedAnimData& ACLAnimData = static_cast<FACLCompressedAnimData&>(AnimData);
	MemoryStream.Serialize(ACLAnimData.CompressedByteStream.GetData(), ACLAnimData.CompressedByteStream.Num());
}

void UAnimBoneCompressionCodec_ACLBase::ByteSwapOut(ICompressedAnimData& AnimData, TArrayView<uint8> CompressedData, FMemoryWriter& MemoryStream) const
{
#if !PLATFORM_LITTLE_ENDIAN
#error "ACL does not currently support big-endian platforms"
#endif

	// TODO: ACL does not support byte swapping
	FACLCompressedAnimData& ACLAnimData = static_cast<FACLCompressedAnimData&>(AnimData);
	MemoryStream.Serialize(ACLAnimData.CompressedByteStream.GetData(), ACLAnimData.CompressedByteStream.Num());
}
