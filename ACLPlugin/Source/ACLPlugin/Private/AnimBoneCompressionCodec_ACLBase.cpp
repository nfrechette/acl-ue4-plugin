// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "AnimBoneCompressionCodec_ACLBase.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"

#if WITH_EDITORONLY_DATA
#include "AnimBoneCompressionCodec_ACLSafe.h"
#include "Animation/AnimationSettings.h"

#include "ACLImpl.h"

#include <acl/algorithm/uniformly_sampled/encoder.h>
#include <acl/algorithm/uniformly_sampled/decoder.h>
#include <acl/compression/skeleton_error_metric.h>
#include <acl/compression/utils.h>
#endif	// WITH_EDITORONLY_DATA

#include <acl/core/compressed_clip.h>

bool FACLCompressedAnimData::IsValid() const
{
	if (CompressedByteStream.Num() == 0)
	{
		return false;
	}

	const acl::CompressedClip* CompressedClipData = reinterpret_cast<const acl::CompressedClip*>(CompressedByteStream.GetData());
	return CompressedClipData->is_valid(false).empty();
}

UAnimBoneCompressionCodec_ACLBase::UAnimBoneCompressionCodec_ACLBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	CompressionLevel = ACLCL_Medium;

	// We use a higher virtual vertex distance when bones have a socket attached or are keyed end effectors (IK, hand, camera, etc)
	// We use 100cm instead of 3cm. UE 4 usually uses 50cm (END_EFFECTOR_DUMMY_BONE_LENGTH_SOCKET) but
	// we use a higher value anyway due to the fact that ACL has no error compensation and it is more aggressive.
	DefaultVirtualVertexDistance = 3.0f;	// 3cm, suitable for ordinary characters
	SafeVirtualVertexDistance = 100.0f;		// 100cm

	ErrorThreshold = 0.01f;					// 0.01cm, conservative enough for cinematographic quality
#endif	// WITH_EDITORONLY_DATA
}

#if WITH_EDITORONLY_DATA
bool UAnimBoneCompressionCodec_ACLBase::Compress(const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult)
{
	using namespace acl;

	ACLAllocator AllocatorImpl;

	TUniquePtr<RigidSkeleton> ACLSkeleton = BuildACLSkeleton(AllocatorImpl, CompressibleAnimData, DefaultVirtualVertexDistance, SafeVirtualVertexDistance);
	TUniquePtr<AnimationClip> ACLClip = BuildACLClip(AllocatorImpl, CompressibleAnimData, *ACLSkeleton, false);
	TUniquePtr<AnimationClip> ACLBaseClip = nullptr;

	UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation raw size: %u bytes"), ACLClip->get_raw_size());

	if (CompressibleAnimData.bIsValidAdditive)
	{
		ACLBaseClip = BuildACLClip(AllocatorImpl, CompressibleAnimData, *ACLSkeleton, true);

		ACLClip->set_additive_base(ACLBaseClip.Get(), AdditiveClipFormat8::Additive1);
	}

	CompressionSettings Settings;
	GetCompressionSettings(Settings);

	TransformErrorMetric DefaultErrorMetric;
	AdditiveTransformErrorMetric<AdditiveClipFormat8::Additive1> AdditiveErrorMetric;
	if (ACLBaseClip != nullptr)
	{
		Settings.error_metric = &AdditiveErrorMetric;
	}
	else
	{
		Settings.error_metric = &DefaultErrorMetric;
	}

	OutputStats Stats;
	CompressedClip* CompressedClipData = nullptr;
	ErrorResult CompressionResult = uniformly_sampled::compress_clip(AllocatorImpl, *ACLClip, Settings, CompressedClipData, Stats);

	// Make sure if we managed to compress, that the error is acceptable and if it isn't, re-compress again with safer settings
	// This should be VERY rare with the default threshold
	if (CompressionResult.empty())
	{
		const ACLSafetyFallbackResult FallbackResult = ExecuteSafetyFallback(AllocatorImpl, Settings, *ACLClip, *CompressedClipData, CompressibleAnimData, OutResult);
		if (FallbackResult != ACLSafetyFallbackResult::Ignored)
		{
			AllocatorImpl.deallocate(CompressedClipData, CompressedClipData->get_size());
			CompressedClipData = nullptr;

			return FallbackResult == ACLSafetyFallbackResult::Success;
		}
	}

	if (!CompressionResult.empty())
	{
		UE_LOG(LogAnimationCompression, Warning, TEXT("ACL failed to compress clip: %s"), ANSI_TO_TCHAR(CompressionResult.c_str()));
		return false;
	}

	checkSlow(CompressedClipData->is_valid(true).empty());

	const uint32 CompressedClipDataSize = CompressedClipData->get_size();

	OutResult.CompressedByteStream.Empty(CompressedClipDataSize);
	OutResult.CompressedByteStream.AddUninitialized(CompressedClipDataSize);
	FMemory::Memcpy(OutResult.CompressedByteStream.GetData(), CompressedClipData, CompressedClipDataSize);

	OutResult.Codec = this;

	OutResult.AnimData = AllocateAnimData();
	OutResult.AnimData->CompressedNumberOfFrames = CompressibleAnimData.NumFrames;
	OutResult.AnimData->Bind(OutResult.CompressedByteStream);

#if !NO_LOGGING
	{
		uniformly_sampled::DecompressionContext<uniformly_sampled::DebugDecompressionSettings> Context;
		Context.initialize(*CompressedClipData);
		const BoneError bone_error = calculate_compressed_clip_error(AllocatorImpl, *ACLClip, *Settings.error_metric, Context);

		UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation compressed size: %u bytes"), CompressedClipDataSize);
		UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation error: %.4f cm (bone %u @ %.3f)"), bone_error.error, bone_error.index, bone_error.sample_time);
	}
#endif

	AllocatorImpl.deallocate(CompressedClipData, CompressedClipDataSize);
	return true;
}

void UAnimBoneCompressionCodec_ACLBase::PopulateDDCKey(FArchive& Ar)
{
	Super::PopulateDDCKey(Ar);

	uint32 ForceRebuildVersion = 0;

	Ar << ForceRebuildVersion << DefaultVirtualVertexDistance << SafeVirtualVertexDistance << ErrorThreshold;
	Ar << CompressionLevel;

	// Add the end effector match name list since if it changes, we need to re-compress
	const TArray<FString>& KeyEndEffectorsMatchNameArray = UAnimationSettings::Get()->KeyEndEffectorsMatchNameArray;
	for (const FString& MatchName : KeyEndEffectorsMatchNameArray)
	{
		uint32 MatchNameHash = GetTypeHash(MatchName);
		Ar << MatchNameHash;
	}
}

ACLSafetyFallbackResult UAnimBoneCompressionCodec_ACLBase::ExecuteSafetyFallback(acl::IAllocator& Allocator, const acl::CompressionSettings& Settings, const acl::AnimationClip& RawClip, const acl::CompressedClip& CompressedClipData, const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult)
{
	return ACLSafetyFallbackResult::Ignored;
}
#endif

TUniquePtr<ICompressedAnimData> UAnimBoneCompressionCodec_ACLBase::AllocateAnimData() const
{
	return MakeUnique<FACLCompressedAnimData>();
}

void UAnimBoneCompressionCodec_ACLBase::ByteSwapIn(ICompressedAnimData& AnimData, TArrayView<uint8> CompressedData, FMemoryReader& MemoryStream) const
{
#if !PLATFORM_LITTLE_ENDIAN
#error "ACL does not currently support big-endian platforms"
#endif

	// TODO: ACL does not support byte swapping
	FACLCompressedAnimData& ACLAnimData = static_cast<FACLCompressedAnimData&>(AnimData);
	MemoryStream.Serialize(ACLAnimData.CompressedByteStream.GetData(), ACLAnimData.CompressedByteStream.Num());
}

void UAnimBoneCompressionCodec_ACLBase::ByteSwapOut(ICompressedAnimData& AnimData, TArrayView<uint8> CompressedData, FMemoryWriter& MemoryStream) const
{
#if !PLATFORM_LITTLE_ENDIAN
#error "ACL does not currently support big-endian platforms"
#endif

	// TODO: ACL does not support byte swapping
	FACLCompressedAnimData& ACLAnimData = static_cast<FACLCompressedAnimData&>(AnimData);
	MemoryStream.Serialize(ACLAnimData.CompressedByteStream.GetData(), ACLAnimData.CompressedByteStream.Num());
}
