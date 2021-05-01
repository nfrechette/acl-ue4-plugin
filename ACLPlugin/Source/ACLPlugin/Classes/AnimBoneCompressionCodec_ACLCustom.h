#pragma once

// Copyright 2018 Nicholas Frechette. All Rights Reserved.

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

	/** The skeletal meshes used to estimate the skinning deformation during compression. */
	UPROPERTY(EditAnywhere, Category = "ACL Options")
	TArray<class USkeletalMesh*> OptimizationTargets;

	//////////////////////////////////////////////////////////////////////////

	// UAnimBoneCompressionCodec implementation
	virtual void PopulateDDCKey(FArchive& Ar) override;

	// UAnimBoneCompressionCodec_ACLBase implementation
	virtual void GetCompressionSettings(acl::compression_settings& OutSettings) const override;
	virtual TArray<class USkeletalMesh*> GetOptimizationTargets() const override { return OptimizationTargets; }
#endif

	// UAnimBoneCompressionCodec implementation
	virtual void DecompressPose(FAnimSequenceDecompressionContext& DecompContext, const BoneTrackArray& RotationPairs, const BoneTrackArray& TranslationPairs, const BoneTrackArray& ScalePairs, TArrayView<FTransform>& OutAtoms) const override;
	virtual void DecompressBone(FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex, FTransform& OutAtom) const override;
};
