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
#include "Animation/AnimEncodingRegistry.h"

#if WITH_EDITOR
#include "AnimationCompression.h"
#include "ACLImpl.h"
#include "AnimEncoding_ACL.h"

#include <acl/algorithm/uniformly_sampled/encoder.h>
#include <acl/algorithm/uniformly_sampled/decoder.h>
#include <acl/compression/skeleton_error_metric.h>
#include <acl/compression/utils.h>

#include <sjson/writer.h>
#include <acl/io/clip_writer.h>
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

	bClipRangeReduceRotations = true;
	bClipRangeReduceTranslations = true;
	bClipRangeReduceScales = true;

	//bEnableSegmenting = true;				// TODO: Temporarily renamed to avoid conflict
	EnableSegmenting = true;
	bSegmentRangeReduceRotations = true;
	bSegmentRangeReduceTranslations = true;
	bSegmentRangeReduceScales = true;
	IdealNumKeyFramesPerSegment = 16;
	MaxNumKeyFramesPerSegment = 31;
}

#if WITH_EDITOR
void UAnimCompress_ACLCustom::DoReduction(UAnimSequence* AnimSeq, const TArray<FBoneData>& BoneData)
{
	using namespace acl;

	AnimSeq->KeyEncodingFormat = AKF_MAX;	// Legacy value, should not be used by the engine
	AnimSeq->CompressionScheme = static_cast<UAnimCompress*>(StaticDuplicateObject(this, AnimSeq));

	ACLAllocator AllocatorImpl;

	TUniquePtr<RigidSkeleton> ACLSkeleton = BuildACLSkeleton(AllocatorImpl, *AnimSeq, BoneData, DefaultVirtualVertexDistance, SafeVirtualVertexDistance);
	TUniquePtr<AnimationClip> ACLClip = BuildACLClip(AllocatorImpl, *AnimSeq, *ACLSkeleton, false);
	TUniquePtr<AnimationClip> ACLBaseClip = nullptr;

	UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation raw size: %u bytes"), ACLClip->get_raw_size());

	if (AnimSeq->IsValidAdditive())
	{
		ACLBaseClip = BuildACLClip(AllocatorImpl, *AnimSeq, *ACLSkeleton, true);

		ACLClip->set_additive_base(ACLBaseClip.Get(), AdditiveClipFormat8::Additive1);
	}

	OutputStats Stats;

	CompressionSettings Settings;
	Settings.level = GetCompressionLevel(CompressionLevel);
	Settings.rotation_format = GetRotationFormat(RotationFormat);
	Settings.translation_format = GetVectorFormat(TranslationFormat);
	Settings.scale_format = GetVectorFormat(ScaleFormat);
	Settings.range_reduction |= bClipRangeReduceRotations ? RangeReductionFlags8::Rotations : RangeReductionFlags8::None;
	Settings.range_reduction |= bClipRangeReduceTranslations ? RangeReductionFlags8::Translations : RangeReductionFlags8::None;
	Settings.range_reduction |= bClipRangeReduceScales ? RangeReductionFlags8::Scales : RangeReductionFlags8::None;
	//Settings.segmenting.enabled = bEnableSegmenting != 0;		// TODO: Temporarily renamed to avoid conflict
	Settings.segmenting.enabled = EnableSegmenting != 0;
	Settings.segmenting.ideal_num_samples = IdealNumKeyFramesPerSegment;
	Settings.segmenting.max_num_samples = MaxNumKeyFramesPerSegment;
	Settings.segmenting.range_reduction |= bSegmentRangeReduceRotations ? RangeReductionFlags8::Rotations : RangeReductionFlags8::None;
	Settings.segmenting.range_reduction |= bSegmentRangeReduceTranslations ? RangeReductionFlags8::Translations : RangeReductionFlags8::None;
	Settings.segmenting.range_reduction |= bSegmentRangeReduceScales ? RangeReductionFlags8::Scales : RangeReductionFlags8::None;

	TransformErrorMetric DefaultErrorMetric;
	AdditiveTransformErrorMetric<AdditiveClipFormat8::Additive1> AdditiveErrorMetric;
	if (ACLBaseClip != nullptr)
		Settings.error_metric = &AdditiveErrorMetric;
	else
		Settings.error_metric = &DefaultErrorMetric;

	Settings.constant_rotation_threshold_angle = ConstantRotationThresholdAngle;
	Settings.constant_translation_threshold = ConstantTranslationThreshold;
	Settings.constant_scale_threshold = ConstantScaleThreshold;
	Settings.error_threshold = ErrorThreshold;

	static volatile bool DumpClip = false;
	if (DumpClip)
		write_acl_clip(*ACLSkeleton, *ACLClip, AlgorithmType8::UniformlySampled, Settings, "D:\\acl_clip.acl.sjson");

	CompressedClip* CompressedClipData = nullptr;
	ErrorResult CompressionResult = uniformly_sampled::compress_clip(AllocatorImpl, *ACLClip, Settings, CompressedClipData, Stats);

	if (!CompressionResult.empty())
	{
		AnimSeq->CompressedByteStream.Empty();
		AnimSeq->CompressedCodecFormat = NAME_ACLCustomCodec;
		FAnimEncodingRegistry::Get().SetInterfaceLinks(*AnimSeq);
		UE_LOG(LogAnimationCompression, Error, TEXT("ACL failed to compress clip: %s"), ANSI_TO_TCHAR(CompressionResult.c_str()));
		return;
	}

	checkSlow(CompressedClipData->is_valid(true).empty());

	const uint32 CompressedClipDataSize = CompressedClipData->get_size();

	AnimSeq->CompressedByteStream.Empty(CompressedClipDataSize);
	AnimSeq->CompressedByteStream.AddUninitialized(CompressedClipDataSize);
	memcpy(AnimSeq->CompressedByteStream.GetData(), CompressedClipData, CompressedClipDataSize);

	AnimSeq->CompressedCodecFormat = NAME_ACLCustomCodec;
	FAnimEncodingRegistry::Get().SetInterfaceLinks(*AnimSeq);

#if !NO_LOGGING
	{
		uniformly_sampled::DecompressionContext<uniformly_sampled::DebugDecompressionSettings> Context;
		Context.initialize(*CompressedClipData);
		const BoneError bone_error = calculate_compressed_clip_error(AllocatorImpl, *ACLClip, Settings, Context);

		UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation compressed size: %u bytes"), CompressedClipData->get_size());
		UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation error: %.4f cm (bone %u @ %.3f)"), bone_error.error, bone_error.index, bone_error.sample_time);
	}
#endif

	AllocatorImpl.deallocate(CompressedClipData, CompressedClipData->get_size());
}

void UAnimCompress_ACLCustom::PopulateDDCKey(FArchive& Ar)
{
	Super::PopulateDDCKey(Ar);

	using namespace acl;

	uint8 ClipFlags =	MakeBitForFlag(bClipRangeReduceRotations, 0) +
						MakeBitForFlag(bClipRangeReduceTranslations, 1) +
						MakeBitForFlag(bClipRangeReduceScales, 2);

	//uint8 SegmentingFlags =	MakeBitForFlag(bEnableSegmenting, 0) +		// TODO: Temporarily renamed to avoid conflict
	uint8 SegmentingFlags =	MakeBitForFlag(EnableSegmenting, 0) +
							MakeBitForFlag(bSegmentRangeReduceRotations, 1) +
							MakeBitForFlag(bSegmentRangeReduceTranslations, 2) +
							MakeBitForFlag(bSegmentRangeReduceScales, 3);

	uint32 ForceRebuildVersion = 1;
	uint16 AlgorithmVersion = get_algorithm_version(AlgorithmType8::UniformlySampled);

	Ar	<< CompressionLevel << RotationFormat << TranslationFormat << ScaleFormat
		<< DefaultVirtualVertexDistance << SafeVirtualVertexDistance
		<< ClipFlags << SegmentingFlags
		<< IdealNumKeyFramesPerSegment << MaxNumKeyFramesPerSegment
		<< ErrorThreshold << ConstantRotationThresholdAngle << ConstantTranslationThreshold << ConstantScaleThreshold
		<< ForceRebuildVersion << AlgorithmVersion;
}
#endif // WITH_EDITOR
