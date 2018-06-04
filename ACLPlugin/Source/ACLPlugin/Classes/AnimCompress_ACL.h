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
#include "AnimCompress_ACL.generated.h"

/** An enum for potential ACL rotation formats. */
UENUM()
enum class ACLRotationFormat : uint8
{
	ACLRF_Quat_128 UMETA(DisplayName = "Quat Full Bit Rate"),
	ACLRF_QuatDropW_96 UMETA(DisplayName = "Quat Drop W Full Bit Rate"),
	ACLRF_QuatDropW_Variable UMETA(DisplayName = "Quat Drop W Variable Bit Rate"),
};

/** An enum for potential ACL Vector3 formats. */
UENUM()
enum class ACLVectorFormat : uint8
{
	ACLVF_Vector3_96 UMETA(DisplayName = "Vector3 Full Bit Rate"),
	ACLVF_Vector3_Variable UMETA(DisplayName = "Vector3 Variable Bit Rate"),
};

/** The codec implementation for ACL support. */
UCLASS(MinimalAPI)
class UAnimCompress_ACL : public UAnimCompress
{
	GENERATED_UCLASS_BODY()

	/** The default virtual vertex distance for normal bones. */
	UPROPERTY(EditAnywhere, Category = Skeleton)
	float DefaultVirtualVertexDistance;

	/** The virtual vertex distance for bones that requires extra accuracy. */
	UPROPERTY(EditAnywhere, Category = Skeleton)
	float SafeVirtualVertexDistance;

	/** The error threshold after which we fallback on a safer encoding. */
	UPROPERTY(EditAnywhere, Category = SafetyFallback)
	float SafetyFallbackThreshold;

	/** Whether to enable the safety fallback or not. */
	UPROPERTY(EditAnywhere, Category = SafetyFallback)
	uint32 bEnableSafetyFallback : 1;

	/** The error threshold to used when optimizing and compressing the animation sequence. */
	UPROPERTY(EditAnywhere, Category = Clip)
	float ErrorThreshold;

	/** The rotation format to use. */
	UPROPERTY(EditAnywhere, Category = Clip)
	ACLRotationFormat RotationFormat;

	/** The translation format to use. */
	UPROPERTY(EditAnywhere, Category = Clip)
	ACLVectorFormat TranslationFormat;

	/** The scale format to use. */
	UPROPERTY(EditAnywhere, Category = Clip)
	ACLVectorFormat ScaleFormat;

	/** Whether to enable per clip range reduction for rotations or not. */
	UPROPERTY(EditAnywhere, Category = Clip)
	uint32 bClipRangeReduceRotations : 1;

	/** Whether to enable per clip range reduction for translations or not. */
	UPROPERTY(EditAnywhere, Category = Clip)
	uint32 bClipRangeReduceTranslations : 1;

	/** Whether to enable per clip range reduction for scales or not. */
	UPROPERTY(EditAnywhere, Category = Clip)
	uint32 bClipRangeReduceScales : 1;

	/** Whether to enable clip segmenting or not. */
	UPROPERTY(EditAnywhere, Category = Segmenting)
	uint32 bEnableSegmenting : 1;

	/** Whether to enable per segment range reduction for rotations or not. */
	UPROPERTY(EditAnywhere, Category = Segmenting)
	uint32 bSegmentRangeReduceRotations : 1;

	/** Whether to enable per segment range reduction for translations or not. */
	UPROPERTY(EditAnywhere, Category = Segmenting)
	uint32 bSegmentRangeReduceTranslations : 1;

	/** Whether to enable per segment range reduction for scales or not. */
	UPROPERTY(EditAnywhere, Category = Segmenting)
	uint32 bSegmentRangeReduceScales : 1;

	/** The ideal number of key frames to retain per segment for each track. */
	UPROPERTY(EditAnywhere, Category = Segmenting)
	uint16 IdealNumKeyFramesPerSegment;

	/** The maximum number of key frames to retain per segment for each track. */
	UPROPERTY(EditAnywhere, Category = Segmenting)
	uint16 MaxNumKeyFramesPerSegment;

protected:
	//~ Begin UAnimCompress Interface
#if WITH_EDITOR
	virtual void DoReduction(class UAnimSequence* AnimSeq, const TArray<class FBoneData>& BoneData) override;
	virtual void PopulateDDCKey(FArchive& Ar) override;
#endif // WITH_EDITOR
	//~ Begin UAnimCompress Interface
};
