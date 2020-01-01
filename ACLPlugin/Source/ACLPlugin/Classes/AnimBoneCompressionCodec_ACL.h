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
#include "AnimBoneCompressionCodec_ACL.generated.h"

/** The default codec implementation for ACL support with the minimal set of exposed features for ease of use. */
UCLASS(MinimalAPI, config = Engine, meta = (DisplayName = "Anim Compress ACL"))
class UAnimBoneCompressionCodec_ACL : public UAnimBoneCompressionCodec_ACLBase
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category = "ACL Options", Instanced, meta = (EditInline))
	UAnimBoneCompressionCodec* SafetyFallbackCodec;

#if WITH_EDITORONLY_DATA
	/** The error threshold after which we fallback on a safer encoding. */
	UPROPERTY(EditAnywhere, Category = "ACL Options", meta = (ClampMin = "0"))
	float SafetyFallbackThreshold;

	//////////////////////////////////////////////////////////////////////////
	// UObject implementation
	virtual void PostInitProperties() override;
	virtual void GetPreloadDependencies(TArray<UObject*>& OutDeps) override;

	// UAnimBoneCompressionCodec implementation
	virtual bool IsCodecValid() const override;
	virtual void PopulateDDCKey(FArchive& Ar) override;

	// UAnimBoneCompressionCodec_ACLBase implementation
	virtual void GetCompressionSettings(acl::CompressionSettings& OutSettings) const override;
	virtual ACLSafetyFallbackResult ExecuteSafetyFallback(acl::IAllocator& Allocator, const acl::CompressionSettings& Settings, const acl::AnimationClip& RawClip, const acl::CompressedClip& CompressedClipData, const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult);
#endif

	// UAnimBoneCompressionCodec implementation
	virtual UAnimBoneCompressionCodec* GetCodec(const FString& DDCHandle);
	virtual void DecompressPose(FAnimSequenceDecompressionContext& DecompContext, const BoneTrackArray& RotationPairs, const BoneTrackArray& TranslationPairs, const BoneTrackArray& ScalePairs, TArrayView<FTransform>& OutAtoms) const override;
	virtual void DecompressBone(FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex, FTransform& OutAtom) const override;
};
