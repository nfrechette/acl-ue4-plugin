#pragma once

////////////////////////////////////////////////////////////////////////////////
// The MIT License (MIT)
//
// Copyright (c) 2020 Nicholas Frechette
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
#include "Animation/AnimCurveCompressionCodec.h"
#include "AnimCurveCompressionCodec_ACL.generated.h"

/** The default codec implementation for ACL curve compression support with the minimal set of exposed features for ease of use. */
UCLASS(MinimalAPI, config = Engine, meta = (DisplayName = "ACL Curves"))
class UAnimCurveCompressionCodec_ACL : public UAnimCurveCompressionCodec
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITORONLY_DATA
	/** The curve precision to target when compressing the animation curves. */
	UPROPERTY(EditAnywhere, Category = "ACL Options", meta = (ClampMin = "0"))
	float CurvePrecision;

	/** The mesh deformation precision to target when compressing morph target animation curves. */
	UPROPERTY(EditAnywhere, Category = "ACL Options", meta = (ClampMin = "0", EditCondition = "MorphTargetSource != nullptr"))
	float MorphTargetPositionPrecision;

	/** The skeletal mesh used to estimate the morph target deformation during compression. */
	UPROPERTY(EditAnywhere, Category = "ACL Options")
	class USkeletalMesh* MorphTargetSource;

	//////////////////////////////////////////////////////////////////////////
	// UAnimCurveCompressionCodec implementation
	virtual void PopulateDDCKey(FArchive& Ar) override;
	virtual bool Compress(const FCompressibleAnimData& AnimSeq, FAnimCurveCompressionResult& OutResult) override;
#endif

	// UAnimCurveCompressionCodec implementation
	virtual void DecompressCurves(const FCompressedAnimSequence& AnimSeq, FBlendedCurve& Curves, float CurrentTime) const override;
	virtual float DecompressCurve(const FCompressedAnimSequence& AnimSeq, SmartName::UID_Type CurveUID, float CurrentTime) const override;
};
