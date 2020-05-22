// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "AnimBoneCompressionCodec_ACLCustom.h"

#include "ACLDecompressionImpl.h"

#if WITH_EDITORONLY_DATA
#include <acl/compression/compression_settings.h>
#endif

UAnimBoneCompressionCodec_ACLCustom::UAnimBoneCompressionCodec_ACLCustom(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	RotationFormat = ACLRotationFormat::ACLRF_QuatDropW_Variable;
	TranslationFormat = ACLVectorFormat::ACLVF_Vector3_Variable;
	ScaleFormat = ACLVectorFormat::ACLVF_Vector3_Variable;

	ConstantRotationThresholdAngle = 0.00284714461f;	// The smallest angle a float32 can represent in a quaternion is 0.000690533954 so we use a value just slightly larger
	ConstantTranslationThreshold = 0.001f;				// 0.001cm, very conservative to be safe
	ConstantScaleThreshold = 0.00001f;					// Very small value to be safe since scale is sensitive

	bClipRangeReduceRotations = true;
	bClipRangeReduceTranslations = true;
	bClipRangeReduceScales = true;

	bEnableSegmenting = true;
	bSegmentRangeReduceRotations = true;
	bSegmentRangeReduceTranslations = true;
	bSegmentRangeReduceScales = true;
	IdealNumKeyFramesPerSegment = 16;
	MaxNumKeyFramesPerSegment = 31;
#endif	// WITH_EDITORONLY_DATA
}

#if WITH_EDITORONLY_DATA
void UAnimBoneCompressionCodec_ACLCustom::GetCompressionSettings(acl::CompressionSettings& OutSettings) const
{
	using namespace acl;

	OutSettings = acl::CompressionSettings();
	OutSettings.rotation_format = GetRotationFormat(RotationFormat);
	OutSettings.translation_format = GetVectorFormat(TranslationFormat);
	OutSettings.scale_format = GetVectorFormat(ScaleFormat);
	OutSettings.level = GetCompressionLevel(CompressionLevel);

	OutSettings.range_reduction |= bClipRangeReduceRotations ? RangeReductionFlags8::Rotations : RangeReductionFlags8::None;
	OutSettings.range_reduction |= bClipRangeReduceTranslations ? RangeReductionFlags8::Translations : RangeReductionFlags8::None;
	OutSettings.range_reduction |= bClipRangeReduceScales ? RangeReductionFlags8::Scales : RangeReductionFlags8::None;

	OutSettings.segmenting.enabled = bEnableSegmenting != 0;
	OutSettings.segmenting.ideal_num_samples = IdealNumKeyFramesPerSegment;
	OutSettings.segmenting.max_num_samples = MaxNumKeyFramesPerSegment;
	OutSettings.segmenting.range_reduction |= bSegmentRangeReduceRotations ? RangeReductionFlags8::Rotations : RangeReductionFlags8::None;
	OutSettings.segmenting.range_reduction |= bSegmentRangeReduceTranslations ? RangeReductionFlags8::Translations : RangeReductionFlags8::None;
	OutSettings.segmenting.range_reduction |= bSegmentRangeReduceScales ? RangeReductionFlags8::Scales : RangeReductionFlags8::None;

	OutSettings.constant_rotation_threshold_angle = ConstantRotationThresholdAngle;
	OutSettings.constant_translation_threshold = ConstantTranslationThreshold;
	OutSettings.constant_scale_threshold = ConstantScaleThreshold;
	OutSettings.error_threshold = ErrorThreshold;
}

void UAnimBoneCompressionCodec_ACLCustom::PopulateDDCKey(FArchive& Ar)
{
	Super::PopulateDDCKey(Ar);

	acl::CompressionSettings Settings;
	GetCompressionSettings(Settings);

	uint32 ForceRebuildVersion = 0;
	uint16 AlgorithmVersion = acl::get_algorithm_version(acl::AlgorithmType8::UniformlySampled);
	uint32 SettingsHash = Settings.get_hash();

	Ar	<< ForceRebuildVersion << AlgorithmVersion << SettingsHash;
}
#endif // WITH_EDITORONLY_DATA

void UAnimBoneCompressionCodec_ACLCustom::DecompressPose(FAnimSequenceDecompressionContext& DecompContext, const BoneTrackArray& RotationPairs, const BoneTrackArray& TranslationPairs, const BoneTrackArray& ScalePairs, TArrayView<FTransform>& OutAtoms) const
{
	::DecompressPose<UE4CustomDecompressionSettings>(DecompContext, RotationPairs, TranslationPairs, ScalePairs, OutAtoms);
}

void UAnimBoneCompressionCodec_ACLCustom::DecompressBone(FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex, FTransform& OutAtom) const
{
	::DecompressBone<UE4CustomDecompressionSettings>(DecompContext, TrackIndex, OutAtom);
}
