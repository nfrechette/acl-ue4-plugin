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

#include "AnimBoneCompressionCodec_ACLSafe.h"
#include "ACLDecompressionImpl.h"

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
