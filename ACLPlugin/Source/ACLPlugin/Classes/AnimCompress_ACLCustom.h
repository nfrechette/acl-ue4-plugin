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
#include "Animation/AnimCompress.h"
#include "AnimCompress_ACLBase.h"
#include "AnimCompress_ACLCustom.generated.h"

/** The custom codec implementation for ACL support with all features supported. */
UCLASS(MinimalAPI, config = Engine, meta = (DisplayName = "Anim Compress ACL Custom"))
class UAnimCompress_ACLCustom : public UAnimCompress_ACLBase
{
	GENERATED_UCLASS_BODY()

	/** The default virtual vertex distance for normal bones. */
	UPROPERTY(EditAnywhere, Category = Skeleton, meta = (ClampMin = "0"))
	float DefaultVirtualVertexDistance;

	/** The virtual vertex distance for bones that requires extra accuracy. */
	UPROPERTY(EditAnywhere, Category = Skeleton, meta = (ClampMin = "0"))
	float SafeVirtualVertexDistance;

	/** The compression level to use. Higher levels will be slower to compress but yield a lower memory footprint. */
	UPROPERTY(EditAnywhere, Category = Clip)
	TEnumAsByte<ACLCompressionLevel> CompressionLevel;

	/** The rotation format to use. */
	UPROPERTY(EditAnywhere, Category = Clip)
	TEnumAsByte<ACLRotationFormat> RotationFormat;

	/** The translation format to use. */
	UPROPERTY(EditAnywhere, Category = Clip)
	TEnumAsByte<ACLVectorFormat> TranslationFormat;

	/** The scale format to use. */
	UPROPERTY(EditAnywhere, Category = Clip)
	TEnumAsByte<ACLVectorFormat> ScaleFormat;

	/** The error threshold to used when optimizing and compressing the animation sequence. */
	UPROPERTY(EditAnywhere, Category = Clip, meta = (ClampMin = "0"))
	float ErrorThreshold;

	/** The threshold used to detect constant rotation tracks. */
	UPROPERTY(EditAnywhere, Category = Clip, meta = (ClampMin = "0"))
	float ConstantRotationThresholdAngle;

	/** The threshold used to detect constant translation tracks. */
	UPROPERTY(EditAnywhere, Category = Clip, meta = (ClampMin = "0"))
	float ConstantTranslationThreshold;

	/** The threshold used to detect constant scale tracks. */
	UPROPERTY(EditAnywhere, Category = Clip, meta = (ClampMin = "0"))
	float ConstantScaleThreshold;

	/** The ideal number of key frames to retain per segment for each track. */
	UPROPERTY(EditAnywhere, Category = Segmenting, meta = (ClampMin = "8"))
	uint16 IdealNumKeyFramesPerSegment;

	/** The maximum number of key frames to retain per segment for each track. */
	UPROPERTY(EditAnywhere, Category = Segmenting, meta = (ClampMin = "8"))
	uint16 MaxNumKeyFramesPerSegment;

protected:
	//~ Begin UAnimCompress Interface
#if WITH_EDITOR
	virtual void DoReduction(const struct FCompressibleAnimData& CompressibleAnimData, struct FCompressibleAnimDataResult& OutResult) override;
	virtual void PopulateDDCKey(FArchive& Ar) override;
#endif // WITH_EDITOR
	//~ Begin UAnimCompress Interface
};
