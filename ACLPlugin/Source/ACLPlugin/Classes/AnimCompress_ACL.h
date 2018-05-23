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

UENUM()
enum class ACLRotationFormat : uint8
{
	Quat_128,
	QuatDropW_96,
	QuatDropW_Variable,
};

UENUM()
enum class ACLVectorFormat : uint8
{
	Vector3_96,
	Vector3_Variable,
};

UCLASS(MinimalAPI)
class UAnimCompress_ACL : public UAnimCompress
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category = Skeleton)
	float DefaultVirtualVertexDistance;

	UPROPERTY(EditAnywhere, Category = Skeleton)
	float SafeVirtualVertexDistance;

	UPROPERTY(EditAnywhere, Category = Clip)
	float SafetyFallbackThreshold;

	UPROPERTY(EditAnywhere, Category = Clip)
	float ErrorThreshold;

	UPROPERTY(EditAnywhere, Category = Clip)
	ACLRotationFormat RotationFormat;

	UPROPERTY(EditAnywhere, Category = Clip)
	ACLVectorFormat TranslationFormat;

	UPROPERTY(EditAnywhere, Category = Clip)
	ACLVectorFormat ScaleFormat;

	UPROPERTY(EditAnywhere, Category = Clip)
	uint32 bClipRangeReduceRotations : 1;

	UPROPERTY(EditAnywhere, Category = Clip)
	uint32 bClipRangeReduceTranslations : 1;

	UPROPERTY(EditAnywhere, Category = Clip)
	uint32 bClipRangeReduceScales : 1;

	UPROPERTY(EditAnywhere, Category = Segmenting)
	uint32 bEnableSegmenting : 1;

	UPROPERTY(EditAnywhere, Category = Segmenting)
	uint32 bSegmentRangeReduceRotations : 1;

	UPROPERTY(EditAnywhere, Category = Segmenting)
	uint32 bSegmentRangeReduceTranslations : 1;

	UPROPERTY(EditAnywhere, Category = Segmenting)
	uint32 bSegmentRangeReduceScales : 1;

	UPROPERTY(EditAnywhere, Category = Segmenting)
	uint16 IdealNumKeyFramesPerSegment;

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
