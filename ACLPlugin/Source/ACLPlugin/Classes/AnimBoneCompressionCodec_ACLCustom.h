#pragma once

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

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "AnimBoneCompressionCodec_ACLBase.h"
#include "AnimBoneCompressionCodec_ACLCustom.generated.h"

/** The custom codec implementation for ACL support with all features supported. */
UCLASS(MinimalAPI, config = Engine, meta = (DisplayName = "Anim Compress ACL Custom"))
class UAnimBoneCompressionCodec_ACLCustom : public UAnimBoneCompressionCodec_ACLBase
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITORONLY_DATA
	/** The rotation format to use. */
	UPROPERTY(EditAnywhere, Category = Clip)
	TEnumAsByte<ACLRotationFormat> RotationFormat;

	/** The translation format to use. */
	UPROPERTY(EditAnywhere, Category = Clip)
	TEnumAsByte<ACLVectorFormat> TranslationFormat;

	/** The scale format to use. */
	UPROPERTY(EditAnywhere, Category = Clip)
	TEnumAsByte<ACLVectorFormat> ScaleFormat;

	/** The threshold used to detect constant rotation tracks. */
	UPROPERTY(EditAnywhere, Category = Clip, meta = (ClampMin = "0"))
	float ConstantRotationThresholdAngle;

	/** The threshold used to detect constant translation tracks. */
	UPROPERTY(EditAnywhere, Category = Clip, meta = (ClampMin = "0"))
	float ConstantTranslationThreshold;

	/** The threshold used to detect constant scale tracks. */
	UPROPERTY(EditAnywhere, Category = Clip, meta = (ClampMin = "0"))
	float ConstantScaleThreshold;

	/** Whether to enable per clip range reduction for rotations or not. */
	UPROPERTY(EditAnywhere, Category = Clip)
	bool bClipRangeReduceRotations;

	/** Whether to enable per clip range reduction for translations or not. */
	UPROPERTY(EditAnywhere, Category = Clip)
	bool bClipRangeReduceTranslations;

	/** Whether to enable per clip range reduction for scales or not. */
	UPROPERTY(EditAnywhere, Category = Clip)
	bool bClipRangeReduceScales;

	/** Whether to enable clip segmenting or not. */
	UPROPERTY(EditAnywhere, Category = Segmenting)
	bool bEnableSegmenting;

	/** Whether to enable per segment range reduction for rotations or not. */
	UPROPERTY(EditAnywhere, Category = Segmenting)
	bool bSegmentRangeReduceRotations;

	/** Whether to enable per segment range reduction for translations or not. */
	UPROPERTY(EditAnywhere, Category = Segmenting)
	bool bSegmentRangeReduceTranslations;

	/** Whether to enable per segment range reduction for scales or not. */
	UPROPERTY(EditAnywhere, Category = Segmenting)
	bool bSegmentRangeReduceScales;

	/** The ideal number of key frames to retain per segment for each track. */
	UPROPERTY(EditAnywhere, Category = Segmenting, meta = (ClampMin = "8"))
	uint16 IdealNumKeyFramesPerSegment;

	/** The maximum number of key frames to retain per segment for each track. */
	UPROPERTY(EditAnywhere, Category = Segmenting, meta = (ClampMin = "8"))
	uint16 MaxNumKeyFramesPerSegment;

	//////////////////////////////////////////////////////////////////////////

	// UAnimBoneCompressionCodec implementation
	virtual void PopulateDDCKey(FArchive& Ar) override;

	// UAnimBoneCompressionCodec_ACLBase implementation
	virtual void GetCompressionSettings(acl::CompressionSettings& OutSettings) const override;
#endif

	// UAnimBoneCompressionCodec implementation
	virtual void DecompressPose(FAnimSequenceDecompressionContext& DecompContext, const BoneTrackArray& RotationPairs, const BoneTrackArray& TranslationPairs, const BoneTrackArray& ScalePairs, TArrayView<FTransform>& OutAtoms) const override;
	virtual void DecompressBone(FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex, FTransform& OutAtom) const override;
};
