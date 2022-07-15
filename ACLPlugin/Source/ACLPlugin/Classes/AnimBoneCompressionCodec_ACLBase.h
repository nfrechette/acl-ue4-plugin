#pragma once

// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimBoneCompressionCodec.h"

#include "ACLImpl.h"

#if WITH_EDITORONLY_DATA
#include <acl/compression/compression_settings.h>
#include <acl/compression/track_array.h>
#include <acl/core/compressed_database.h>
#include <acl/core/iallocator.h>
#endif

#include <acl/core/compressed_tracks.h>

#include "AnimBoneCompressionCodec_ACLBase.generated.h"

/** An enum that represents the result of attempting to use a safety fallback codec. */
enum class ACLSafetyFallbackResult
{
	Success,	// Safety fallback is used and compressed fine
	Failure,	// Safety fallback is used but failed to compress
	Ignored,	// No safety fallback used
};

struct FACLCompressedAnimData final : public ICompressedAnimData
{
	/** Holds the compressed_tracks instance */
	TArrayView<uint8> CompressedByteStream;

#if WITH_EDITORONLY_DATA
	/** Holds the default pose used when bind pose stripping is enabled */
	TArray<FTransform> StrippedBindPose;
#endif

#if WITH_ACL_EXCLUDED_FROM_STRIPPING_CHECKS
	/** Holds an ACL type bitset for each UE4 track to tell if it was excluded from bind pose stripping or not */
	TArray<uint32> TracksExcludedFromStrippingBitSet;
#endif

	const acl::compressed_tracks* GetCompressedTracks() const { return acl::make_compressed_tracks(CompressedByteStream.GetData()); }

	// ICompressedAnimData implementation
	virtual void SerializeCompressedData(class FArchive& Ar) override;
	virtual void Bind(const TArrayView<uint8> BulkData) override { CompressedByteStream = BulkData; }
	virtual int64 GetApproxCompressedSize() const override { return CompressedByteStream.Num(); }
	virtual bool IsValid() const override;
};

/** The base codec implementation for ACL support. */
UCLASS(abstract, MinimalAPI)
class UAnimBoneCompressionCodec_ACLBase : public UAnimBoneCompressionCodec
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITORONLY_DATA
	/** The compression level to use. Higher levels will be slower to compress but yield a lower memory footprint. */
	UPROPERTY(EditAnywhere, Category = "ACL Options")
	TEnumAsByte<ACLCompressionLevel> CompressionLevel;

	/** The default virtual vertex distance for normal bones. */
	UPROPERTY(EditAnywhere, Category = "ACL Options", meta = (ClampMin = "0"))
	float DefaultVirtualVertexDistance;

	/** The virtual vertex distance for bones that requires extra accuracy. */
	UPROPERTY(EditAnywhere, Category = "ACL Options", meta = (ClampMin = "0"))
	float SafeVirtualVertexDistance;

	/** The error threshold to use when optimizing and compressing the animation sequence. */
	UPROPERTY(EditAnywhere, Category = "ACL Options", meta = (ClampMin = "0"))
	float ErrorThreshold;
#endif

	/** Whether or not to strip the bind pose from compressed clips. Note that this is only used in cooked builds and runtime behavior may differ from the editor, see documentation for details. */
	UPROPERTY(EditAnywhere, Category = "ACL Bind Pose (Experimental)")
	bool bStripBindPose;

#if WITH_EDITORONLY_DATA
	/** Bones in this list will not be stripped even when equal to their bind pose value. */
	UPROPERTY(EditAnywhere, Category = "ACL Bind Pose (Experimental)", meta = (DisplayName = "Exclusion List"))
	TArray<FName> BindPoseStrippingBoneExclusionList;

	// UAnimBoneCompressionCodec implementation
	virtual bool Compress(const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult) override;

#if (ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1)
	virtual void PopulateDDCKey(const UAnimSequenceBase& AnimSeq, FArchive& Ar) override;
#else
	virtual void PopulateDDCKey(FArchive& Ar) override;
#endif

	// Our implementation
	virtual void PostCompression(const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult) const {}
	virtual void PopulateStrippedBindPose(const FCompressibleAnimData& CompressibleAnimData, const acl::track_array_qvvf& ACLTracks, ICompressedAnimData& AnimData) const;
	virtual void SetExcludedFromStrippingBitSet(const TArray<uint32>& TracksExcludedFromStrippingBitSet, ICompressedAnimData& AnimData) const;
	virtual void GetCompressionSettings(acl::compression_settings& OutSettings) const PURE_VIRTUAL(UAnimBoneCompressionCodec_ACLBase::GetCompressionSettings, );
	virtual TArray<class USkeletalMesh*> GetOptimizationTargets() const { return TArray<class USkeletalMesh*>(); }
	virtual ACLSafetyFallbackResult ExecuteSafetyFallback(acl::iallocator& Allocator, const acl::compression_settings& Settings, const acl::track_array_qvvf& RawClip, const acl::track_array_qvvf& BaseClip, const acl::compressed_tracks& CompressedClipData, const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult);
#endif

	// UAnimBoneCompressionCodec implementation
	virtual TUniquePtr<ICompressedAnimData> AllocateAnimData() const override;
	virtual void ByteSwapIn(ICompressedAnimData& AnimData, TArrayView<uint8> CompressedData, FMemoryReader& MemoryStream) const override;
	virtual void ByteSwapOut(ICompressedAnimData& AnimData, TArrayView<uint8> CompressedData, FMemoryWriter& MemoryStream) const override;
};
