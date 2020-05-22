#pragma once

// Copyright 2018 Nicholas Frechette. All Rights Reserved.

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
