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
#include "Animation/AnimBoneCompressionCodec.h"
#include "AnimBoneCompressionCodec_ACLBase.generated.h"

namespace acl
{
	class AnimationClip;
	class CompressedClip;
	struct CompressionSettings;
	class IAllocator;
}

/** An enum for ACL rotation formats. */
UENUM()
enum ACLRotationFormat
{
	ACLRF_Quat_128 UMETA(DisplayName = "Quat Full Bit Rate"),
	ACLRF_QuatDropW_96 UMETA(DisplayName = "Quat Drop W Full Bit Rate"),
	ACLRF_QuatDropW_Variable UMETA(DisplayName = "Quat Drop W Variable Bit Rate"),
};

/** An enum for ACL Vector3 formats. */
UENUM()
enum ACLVectorFormat
{
	ACLVF_Vector3_96 UMETA(DisplayName = "Vector3 Full Bit Rate"),
	ACLVF_Vector3_Variable UMETA(DisplayName = "Vector3 Variable Bit Rate"),
};

/** An enum for ACL compression levels. */
UENUM()
enum ACLCompressionLevel
{
	ACLCL_Lowest UMETA(DisplayName = "Lowest"),
	ACLCL_Low UMETA(DisplayName = "Low"),
	ACLCL_Medium UMETA(DisplayName = "Medium"),
	ACLCL_High UMETA(DisplayName = "High"),
	ACLCL_Highest UMETA(DisplayName = "Highest"),
};

/** An enum that represents the result of attempting to use a safety fallback codec. */
enum class ACLSafetyFallbackResult
{
	Success,	// Safety fallback is used and compressed fine
	Failure,	// Safety fallback is used but failed to compress
	Ignored,	// No safety fallback used
};

struct FACLCompressedAnimData final : public ICompressedAnimData
{
	TArrayView<uint8> CompressedByteStream;

	// ICompressedAnimData implementation
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

	// UAnimBoneCompressionCodec implementation
	virtual bool Compress(const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult) override;
	virtual void PopulateDDCKey(FArchive& Ar) override;

	// Our implementation
	virtual void GetCompressionSettings(acl::CompressionSettings& OutSettings) const PURE_VIRTUAL(UAnimBoneCompressionCodec_ACLBase::GetCompressionSettings, );
	virtual ACLSafetyFallbackResult ExecuteSafetyFallback(acl::IAllocator& Allocator, const acl::CompressionSettings& Settings, const acl::AnimationClip& RawClip, const acl::CompressedClip& CompressedClipData, const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult);
#endif

	// UAnimBoneCompressionCodec implementation
	virtual TUniquePtr<ICompressedAnimData> AllocateAnimData() const override;
	virtual void ByteSwapIn(ICompressedAnimData& AnimData, TArrayView<uint8> CompressedData, FMemoryReader& MemoryStream) const override;
	virtual void ByteSwapOut(ICompressedAnimData& AnimData, TArrayView<uint8> CompressedData, FMemoryWriter& MemoryStream) const override;
};
