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

#include "AnimCompress_ACLCustom.h"

#if WITH_EDITOR
#include "AnimationCompression.h"
#include "Animation/AnimCompressionTypes.h"
#include "ACLImpl.h"
#include "AnimEncoding_ACL.h"

#include <acl/compression/compress.h>
#include <acl/compression/transform_error_metrics.h>
#include <acl/compression/track_error.h>
#include <acl/decompression/decompress.h>
#endif

UAnimCompress_ACLCustom::UAnimCompress_ACLCustom(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Description = TEXT("ACL Custom");
	bNeedsSkeleton = true;

	CompressionLevel = ACLCL_Medium;

	// We use a higher virtual vertex distance when bones have a socket attached or are keyed end effectors (IK, hand, camera, etc)
	// We use 100cm instead of 3cm. UE 4 usually uses 50cm (END_EFFECTOR_DUMMY_BONE_LENGTH_SOCKET) but
	// we use a higher value anyway due to the fact that ACL has no error compensation and it is more aggressive.
	DefaultVirtualVertexDistance = 3.0f;	// 3cm, suitable for ordinary characters
	SafeVirtualVertexDistance = 100.0f;		// 100cm

	RotationFormat = ACLRotationFormat::ACLRF_QuatDropW_Variable;
	TranslationFormat = ACLVectorFormat::ACLVF_Vector3_Variable;
	ScaleFormat = ACLVectorFormat::ACLVF_Vector3_Variable;

	ErrorThreshold = 0.01f;								// 0.01cm, conservative enough for cinematographic quality
	ConstantRotationThresholdAngle = 0.00284714461f;	// The smallest angle a float32 can represent in a quaternion is 0.000690533954 so we use a value just slightly larger
	ConstantTranslationThreshold = 0.001f;				// 0.001cm, very conservative to be safe
	ConstantScaleThreshold = 0.00001f;					// Very small value to be safe since scale is sensitive

	IdealNumKeyFramesPerSegment = 16;
	MaxNumKeyFramesPerSegment = 31;
}

#if WITH_EDITOR
void UAnimCompress_ACLCustom::DoReduction(const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult)
{
	ACLAllocator AllocatorImpl;

	acl::track_array_qvvf ACLTracks = BuildACLTransformTrackArray(AllocatorImpl, CompressibleAnimData, DefaultVirtualVertexDistance, SafeVirtualVertexDistance, false);
	acl::track_array_qvvf ACLBaseTracks;

	UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation raw size: %u bytes"), ACLTracks.get_raw_size());

	if (CompressibleAnimData.bIsValidAdditive)
		ACLBaseTracks = BuildACLTransformTrackArray(AllocatorImpl, CompressibleAnimData, DefaultVirtualVertexDistance, SafeVirtualVertexDistance, true);

	acl::output_stats Stats;

	acl::compression_settings Settings;
	Settings.level = GetCompressionLevel(CompressionLevel);
	Settings.rotation_format = GetRotationFormat(RotationFormat);
	Settings.translation_format = GetVectorFormat(TranslationFormat);
	Settings.scale_format = GetVectorFormat(ScaleFormat);
	Settings.segmenting.ideal_num_samples = IdealNumKeyFramesPerSegment;
	Settings.segmenting.max_num_samples = MaxNumKeyFramesPerSegment;

	acl::qvvf_transform_error_metric DefaultErrorMetric;
	acl::additive_qvvf_transform_error_metric<acl::additive_clip_format8::additive1> AdditiveErrorMetric;
	if (ACLBaseTracks.get_num_tracks() != 0)
		Settings.error_metric = &AdditiveErrorMetric;
	else
		Settings.error_metric = &DefaultErrorMetric;

	// Set our thresholds
	for (acl::track_qvvf& Track : ACLTracks)
	{
		acl::track_desc_transformf& Desc = Track.get_description();
		
		Desc.constant_rotation_threshold_angle = ConstantRotationThresholdAngle;
		Desc.constant_translation_threshold = ConstantTranslationThreshold;
		Desc.constant_scale_threshold = ConstantScaleThreshold;
		Desc.precision = ErrorThreshold;
	}

	const acl::additive_clip_format8 AdditiveFormat = acl::additive_clip_format8::additive0;

	acl::compressed_tracks* CompressedTracks = nullptr;
	acl::error_result CompressionResult = acl::compress_track_list(AllocatorImpl, ACLTracks, Settings, ACLBaseTracks, AdditiveFormat, CompressedTracks, Stats);

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

	OutResult.KeyEncodingFormat = AKF_ACLCustom;
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

void UAnimCompress_ACLCustom::PopulateDDCKey(FArchive& Ar)
{
	Super::PopulateDDCKey(Ar);

	uint32 ForceRebuildVersion = 1;
	uint16 AlgorithmVersion = acl::get_algorithm_version(acl::algorithm_type8::uniformly_sampled);

	Ar	<< CompressionLevel << RotationFormat << TranslationFormat << ScaleFormat
		<< DefaultVirtualVertexDistance << SafeVirtualVertexDistance
		<< IdealNumKeyFramesPerSegment << MaxNumKeyFramesPerSegment
		<< ErrorThreshold << ConstantRotationThresholdAngle << ConstantTranslationThreshold << ConstantScaleThreshold
		<< ForceRebuildVersion << AlgorithmVersion;
}
#endif // WITH_EDITOR
