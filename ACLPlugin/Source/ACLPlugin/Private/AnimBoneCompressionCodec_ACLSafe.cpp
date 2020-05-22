// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "AnimBoneCompressionCodec_ACLSafe.h"

#include "ACLDecompressionImpl.h"

#if WITH_EDITORONLY_DATA
#include <acl/compression/compression_settings.h>
#endif

UAnimBoneCompressionCodec_ACLSafe::UAnimBoneCompressionCodec_ACLSafe(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITORONLY_DATA
void UAnimBoneCompressionCodec_ACLSafe::GetCompressionSettings(acl::CompressionSettings& OutSettings) const
{
	using namespace acl;

	OutSettings = get_default_compression_settings();

	// Fallback to full precision rotations
	OutSettings.rotation_format = RotationFormat8::Quat_128;

	// Disable rotation range reduction for clip and segments to make sure they remain at maximum precision
	OutSettings.range_reduction &= ~RangeReductionFlags8::Rotations;
	OutSettings.segmenting.range_reduction &= ~RangeReductionFlags8::Rotations;

	// Disable constant rotation track detection
	OutSettings.constant_rotation_threshold_angle = 0.0f;

	OutSettings.error_threshold = ErrorThreshold;
}

void UAnimBoneCompressionCodec_ACLSafe::PopulateDDCKey(FArchive& Ar)
{
	Super::PopulateDDCKey(Ar);

	acl::CompressionSettings Settings;
	GetCompressionSettings(Settings);

	uint32 ForceRebuildVersion = 0;
	uint16 AlgorithmVersion = acl::get_algorithm_version(acl::AlgorithmType8::UniformlySampled);
	uint32 SettingsHash = Settings.get_hash();

	Ar << DefaultVirtualVertexDistance << SafeVirtualVertexDistance
		<< ForceRebuildVersion << AlgorithmVersion << SettingsHash;
}
#endif // WITH_EDITORONLY_DATA

void UAnimBoneCompressionCodec_ACLSafe::DecompressPose(FAnimSequenceDecompressionContext& DecompContext, const BoneTrackArray& RotationPairs, const BoneTrackArray& TranslationPairs, const BoneTrackArray& ScalePairs, TArrayView<FTransform>& OutAtoms) const
{
	::DecompressPose<UE4SafeDecompressionSettings>(DecompContext, RotationPairs, TranslationPairs, ScalePairs, OutAtoms);
}

void UAnimBoneCompressionCodec_ACLSafe::DecompressBone(FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex, FTransform& OutAtom) const
{
	::DecompressBone<UE4SafeDecompressionSettings>(DecompContext, TrackIndex, OutAtom);
}
