////////////////////////////////////////////////////////////////////////////////
// The MIT License (MIT)
//
// Copyright (c) 2018 Nicholas Frechette
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
////////////////////////////////////////////////////////////////////////////////

#include "AnimCompress_ACL.h"

#if WITH_EDITOR
#include "AnimationCompression.h"
#include "Animation/AnimationSettings.h"
#include "Animation/AnimCompressionTypes.h"
#include "ACLImpl.h"
#include "AnimEncoding_ACL.h"

#include <acl/compression/compress.h>
#include <acl/compression/transform_error_metrics.h>
#include <acl/compression/track_error.h>
#include <acl/decompression/decompress.h>
#endif	// WITH_EDITOR

UAnimCompress_ACL::UAnimCompress_ACL(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Description = TEXT("ACL");
	bNeedsSkeleton = true;

	CompressionLevel = ACLCL_Medium;

	// We use a higher virtual vertex distance when bones have a socket attached or are keyed end effectors (IK, hand, camera, etc)
	// We use 100cm instead of 3cm. UE 4 usually uses 50cm (END_EFFECTOR_DUMMY_BONE_LENGTH_SOCKET) but
	// we use a higher value anyway due to the fact that ACL has no error compensation and it is more aggressive.
	DefaultVirtualVertexDistance = 3.0f;	// 3cm, suitable for ordinary characters
	SafeVirtualVertexDistance = 100.0f;		// 100cm

	SafetyFallbackThreshold = 1.0f;			// 1cm, should be very rarely exceeded
	ErrorThreshold = 0.01f;					// 0.01cm, conservative enough for cinematographic quality
}

#if WITH_EDITOR
void UAnimCompress_ACL::DoReduction(const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult)
{
	ACLAllocator AllocatorImpl;

	acl::track_array_qvvf ACLTracks = BuildACLTransformTrackArray(AllocatorImpl, CompressibleAnimData, DefaultVirtualVertexDistance, SafeVirtualVertexDistance, false);

	acl::track_array_qvvf ACLBaseTracks;
	if (CompressibleAnimData.bIsValidAdditive)
		ACLBaseTracks = BuildACLTransformTrackArray(AllocatorImpl, CompressibleAnimData, DefaultVirtualVertexDistance, SafeVirtualVertexDistance, true);

	UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation raw size: %u bytes"), ACLTracks.get_raw_size());

	acl::output_stats Stats;

	acl::compression_settings Settings = acl::get_default_compression_settings();
	Settings.level = GetCompressionLevel(CompressionLevel);

	acl::qvvf_transform_error_metric DefaultErrorMetric;
	acl::additive_qvvf_transform_error_metric<acl::additive_clip_format8::additive1> AdditiveErrorMetric;
	if (ACLBaseTracks.get_num_tracks() != 0)
		Settings.error_metric = &AdditiveErrorMetric;
	else
		Settings.error_metric = &DefaultErrorMetric;

	// Set our error threshold
	for (acl::track_qvvf& Track : ACLTracks)
		Track.get_description().precision = ErrorThreshold;

	AnimationKeyFormat KeyFormat = AKF_ACLDefault;
	const acl::additive_clip_format8 AdditiveFormat = acl::additive_clip_format8::additive0;

	acl::compressed_tracks* CompressedTracks = nullptr;
	acl::error_result CompressionResult = acl::compress_track_list(AllocatorImpl, ACLTracks, Settings, ACLBaseTracks, AdditiveFormat, CompressedTracks, Stats);

	// Make sure if we managed to compress, that the error is acceptable and if it isn't, re-compress again with safer settings
	// This should be VERY rare with the default threshold
	if (CompressionResult.empty())
	{
		checkSlow(CompressedTracks->is_valid(true).empty());

		acl::decompression_context<acl::default_transform_decompression_settings> Context;
		Context.initialize(*CompressedTracks);
		const acl::track_error TrackError = acl::calculate_compression_error(AllocatorImpl, ACLTracks, Context, *Settings.error_metric, ACLBaseTracks);
		if (TrackError.error >= SafetyFallbackThreshold)
		{
			UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation compressed size: %u bytes"), CompressedTracks->get_size());
			UE_LOG(LogAnimationCompression, Warning, TEXT("ACL Animation error is too high, a safe fallback will be used instead: %.4f cm"), TrackError.error);

			AllocatorImpl.deallocate(CompressedTracks, CompressedTracks->get_size());
			CompressedTracks = nullptr;

			// 99.999% of the time if we have accuracy issues, it comes from the rotations

			// Fallback to full precision rotations
			Settings.rotation_format = acl::rotation_format8::quatf_full;

			// Disable constant rotation track detection
			for (acl::track_qvvf& Track : ACLTracks)
				Track.get_description().constant_rotation_threshold_angle = 0.0f;

			KeyFormat = AKF_ACLSafe;

			CompressionResult = acl::compress_track_list(AllocatorImpl, ACLTracks, Settings, ACLBaseTracks, AdditiveFormat, CompressedTracks, Stats);
		}
	}

	if (!CompressionResult.empty())
	{
		UE_LOG(LogAnimationCompression, Error, TEXT("ACL failed to compress clip: %s"), ANSI_TO_TCHAR(CompressionResult.c_str()));
		return;
	}

	checkSlow(CompressedTracks->is_valid(true).empty());

	const uint32 CompressedClipDataSize = CompressedTracks->get_size();

	OutResult.CompressedByteStream.Empty(CompressedClipDataSize);
	OutResult.CompressedByteStream.AddUninitialized(CompressedClipDataSize);
	memcpy(OutResult.CompressedByteStream.GetData(), CompressedTracks, CompressedClipDataSize);

	OutResult.KeyEncodingFormat = KeyFormat;
	AnimationFormat_SetInterfaceLinks(OutResult);

#if !NO_LOGGING
	{
		acl::decompression_context<acl::debug_transform_decompression_settings> Context;
		Context.initialize(*CompressedTracks);
		const acl::track_error TrackError = acl::calculate_compression_error(AllocatorImpl, ACLTracks, Context, *Settings.error_metric, ACLBaseTracks);

		UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation compressed size: %u bytes"), CompressedTracks->get_size());
		UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation error: %.4f cm (bone %u @ %.3f)"), TrackError.error, TrackError.index, TrackError.sample_time);
	}
#endif

	AllocatorImpl.deallocate(CompressedTracks, CompressedTracks->get_size());
}

void UAnimCompress_ACL::PopulateDDCKey(FArchive& Ar)
{
	Super::PopulateDDCKey(Ar);

	acl::compression_settings Settings = acl::get_default_compression_settings();
	Settings.level = GetCompressionLevel(CompressionLevel);

	uint32 ForceRebuildVersion = 1;
	uint16 AlgorithmVersion = acl::get_algorithm_version(acl::algorithm_type8::uniformly_sampled);
	uint32 SettingsHash = Settings.get_hash();
	uint32 KeyEndEffectorsHash = 0;

	for (const FString& MatchName : UAnimationSettings::Get()->KeyEndEffectorsMatchNameArray)
	{
		KeyEndEffectorsHash = acl::hash_combine(KeyEndEffectorsHash, GetTypeHash(MatchName));
	}
	
	Ar	<< SafetyFallbackThreshold << ErrorThreshold << DefaultVirtualVertexDistance << SafeVirtualVertexDistance
		<< ForceRebuildVersion << AlgorithmVersion << SettingsHash << KeyEndEffectorsHash;
}
#endif // WITH_EDITOR
