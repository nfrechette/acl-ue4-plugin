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

#include <acl/algorithm/uniformly_sampled/encoder.h>
#include <acl/algorithm/uniformly_sampled/decoder.h>
#include <acl/compression/skeleton_error_metric.h>
#include <acl/compression/utils.h>
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
	using namespace acl;

	ACLAllocator AllocatorImpl;

	TUniquePtr<RigidSkeleton> ACLSkeleton = BuildACLSkeleton(AllocatorImpl, CompressibleAnimData, DefaultVirtualVertexDistance, SafeVirtualVertexDistance);
	TUniquePtr<AnimationClip> ACLClip = BuildACLClip(AllocatorImpl, CompressibleAnimData, *ACLSkeleton, false);
	TUniquePtr<AnimationClip> ACLBaseClip = nullptr;

	UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation raw size: %u bytes"), ACLClip->get_raw_size());

	if (CompressibleAnimData.bIsValidAdditive)
	{
		ACLBaseClip = BuildACLClip(AllocatorImpl, CompressibleAnimData, *ACLSkeleton, true);

		ACLClip->set_additive_base(ACLBaseClip.Get(), AdditiveClipFormat8::Additive1);
	}

	OutputStats Stats;

	CompressionSettings Settings = get_default_compression_settings();
	Settings.level = GetCompressionLevel(CompressionLevel);

	TransformErrorMetric DefaultErrorMetric;
	AdditiveTransformErrorMetric<AdditiveClipFormat8::Additive1> AdditiveErrorMetric;
	if (ACLBaseClip != nullptr)
		Settings.error_metric = &AdditiveErrorMetric;
	else
		Settings.error_metric = &DefaultErrorMetric;

	Settings.error_threshold = ErrorThreshold;

	AnimationKeyFormat KeyFormat = AKF_ACLDefault;

	CompressedClip* CompressedClipData = nullptr;
	ErrorResult CompressionResult = uniformly_sampled::compress_clip(AllocatorImpl, *ACLClip, Settings, CompressedClipData, Stats);

	// Make sure if we managed to compress, that the error is acceptable and if it isn't, re-compress again with safer settings
	// This should be VERY rare with the default threshold
	if (CompressionResult.empty())
	{
		checkSlow(CompressedClipData->is_valid(true).empty());

		uniformly_sampled::DecompressionContext<uniformly_sampled::DebugDecompressionSettings> Context;
		Context.initialize(*CompressedClipData);
		const BoneError bone_error = calculate_compressed_clip_error(AllocatorImpl, *ACLClip, *Settings.error_metric, Context);
		if (bone_error.error >= SafetyFallbackThreshold)
		{
			UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation compressed size: %u bytes"), CompressedClipData->get_size());
			UE_LOG(LogAnimationCompression, Warning, TEXT("ACL Animation error is too high, a safe fallback will be used instead: %.4f cm"), bone_error.error);

			AllocatorImpl.deallocate(CompressedClipData, CompressedClipData->get_size());
			CompressedClipData = nullptr;

			// 99.999% of the time if we have accuracy issues, it comes from the rotations

			// Fallback to full precision rotations
			Settings.rotation_format = RotationFormat8::Quat_128;

			// Disable rotation range reduction for clip and segments to make sure they remain at maximum precision
			Settings.range_reduction &= ~RangeReductionFlags8::Rotations;
			Settings.segmenting.range_reduction &= ~RangeReductionFlags8::Rotations;

			// Disable constant rotation track detection
			Settings.constant_rotation_threshold_angle = 0.0f;

			KeyFormat = AKF_ACLSafe;

			CompressionResult = uniformly_sampled::compress_clip(AllocatorImpl, *ACLClip, Settings, CompressedClipData, Stats);
		}
	}

	if (!CompressionResult.empty())
	{
		UE_LOG(LogAnimationCompression, Error, TEXT("ACL failed to compress clip: %s"), ANSI_TO_TCHAR(CompressionResult.c_str()));
		return;
	}

	checkSlow(CompressedClipData->is_valid(true).empty());

	const uint32 CompressedClipDataSize = CompressedClipData->get_size();

	OutResult.CompressedByteStream.Empty(CompressedClipDataSize);
	OutResult.CompressedByteStream.AddUninitialized(CompressedClipDataSize);
	memcpy(OutResult.CompressedByteStream.GetData(), CompressedClipData, CompressedClipDataSize);

	OutResult.KeyEncodingFormat = KeyFormat;
	AnimationFormat_SetInterfaceLinks(OutResult);

#if !NO_LOGGING
	{
		uniformly_sampled::DecompressionContext<uniformly_sampled::DebugDecompressionSettings> Context;
		Context.initialize(*CompressedClipData);
		const BoneError bone_error = calculate_compressed_clip_error(AllocatorImpl, *ACLClip, *Settings.error_metric, Context);

		UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation compressed size: %u bytes"), CompressedClipData->get_size());
		UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation error: %.4f cm (bone %u @ %.3f)"), bone_error.error, bone_error.index, bone_error.sample_time);
	}
#endif

	AllocatorImpl.deallocate(CompressedClipData, CompressedClipData->get_size());
}

void UAnimCompress_ACL::PopulateDDCKey(FArchive& Ar)
{
	Super::PopulateDDCKey(Ar);

	using namespace acl;

	CompressionSettings Settings = get_default_compression_settings();
	Settings.level = GetCompressionLevel(CompressionLevel);
	Settings.error_threshold = ErrorThreshold;

	uint32 ForceRebuildVersion = 1;
	uint16 AlgorithmVersion = get_algorithm_version(AlgorithmType8::UniformlySampled);
	uint32 SettingsHash = Settings.get_hash();
	uint32 KeyEndEffectorsHash = 0;

	for (const FString& MatchName : UAnimationSettings::Get()->KeyEndEffectorsMatchNameArray)
	{
		KeyEndEffectorsHash = hash_combine(KeyEndEffectorsHash, GetTypeHash(MatchName));
	}
	
	Ar	<< SafetyFallbackThreshold << ErrorThreshold << DefaultVirtualVertexDistance << SafeVirtualVertexDistance
		<< ForceRebuildVersion << AlgorithmVersion << SettingsHash << KeyEndEffectorsHash;
}
#endif // WITH_EDITOR
