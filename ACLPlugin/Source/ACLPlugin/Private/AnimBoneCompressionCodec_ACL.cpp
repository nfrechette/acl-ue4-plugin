// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "AnimBoneCompressionCodec_ACL.h"

#if WITH_EDITORONLY_DATA
#include "AnimBoneCompressionCodec_ACLSafe.h"
#include "Rendering/SkeletalMeshModel.h"

#include "ACLImpl.h"

#include <acl/compression/track_error.h>
#include <acl/decompression/decompress.h>
#endif	// WITH_EDITORONLY_DATA

#include "ACLDecompressionImpl.h"

UAnimBoneCompressionCodec_ACL::UAnimBoneCompressionCodec_ACL(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	SafetyFallbackThreshold = 1.0f;			// 1cm, should be very rarely exceeded
#endif	// WITH_EDITORONLY_DATA
}

#if WITH_EDITORONLY_DATA
void UAnimBoneCompressionCodec_ACL::PostInitProperties()
{
	Super::PostInitProperties();

	if (!IsTemplate())
	{
		// Ensure we are never null
		SafetyFallbackCodec = NewObject<UAnimBoneCompressionCodec_ACLSafe>(this, NAME_None, RF_Public);
	}
}

void UAnimBoneCompressionCodec_ACL::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);

	if (SafetyFallbackCodec != nullptr)
	{
		OutDeps.Add(SafetyFallbackCodec);
	}
}

bool UAnimBoneCompressionCodec_ACL::IsCodecValid() const
{
	if (!Super::IsCodecValid())
	{
		return false;
	}

	return SafetyFallbackCodec != nullptr ? SafetyFallbackCodec->IsCodecValid() : true;
}

void UAnimBoneCompressionCodec_ACL::GetCompressionSettings(acl::compression_settings& OutSettings) const
{
	OutSettings = acl::get_default_compression_settings();

	OutSettings.level = GetCompressionLevel(CompressionLevel);
}

ACLSafetyFallbackResult UAnimBoneCompressionCodec_ACL::ExecuteSafetyFallback(acl::iallocator& Allocator, const acl::compression_settings& Settings, const acl::track_array_qvvf& RawClip, const acl::track_array_qvvf& BaseClip, const acl::compressed_tracks& CompressedClipData, const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult)
{
	if (SafetyFallbackCodec != nullptr && SafetyFallbackThreshold > 0.0f)
	{
		checkSlow(CompressedClipData.is_valid(true).empty());

		acl::decompression_context<UE4DefaultDBDecompressionSettings> Context;
		Context.initialize(CompressedClipData);

		const acl::track_error TrackError = acl::calculate_compression_error(Allocator, RawClip, Context, *Settings.error_metric, BaseClip);
		if (TrackError.error >= SafetyFallbackThreshold)
		{
			UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation compressed size: %u bytes"), CompressedClipData.get_size());
			UE_LOG(LogAnimationCompression, Warning, TEXT("ACL Animation error is too high, a safe fallback will be used instead: %.4f cm"), TrackError.error);

			// Just use the safety fallback
			return SafetyFallbackCodec->Compress(CompressibleAnimData, OutResult) ? ACLSafetyFallbackResult::Success : ACLSafetyFallbackResult::Failure;
		}
	}

	return ACLSafetyFallbackResult::Ignored;
}

void UAnimBoneCompressionCodec_ACL::PopulateDDCKey(FArchive& Ar)
{
	Super::PopulateDDCKey(Ar);

	acl::compression_settings Settings;
	GetCompressionSettings(Settings);

	uint32 ForceRebuildVersion = 1;
	uint32 SettingsHash = Settings.get_hash();

	Ar	<< SafetyFallbackThreshold << ForceRebuildVersion << SettingsHash;

	for (USkeletalMesh* SkelMesh : OptimizationTargets)
	{
		FSkeletalMeshModel* MeshModel = SkelMesh != nullptr ? SkelMesh->GetImportedModel() : nullptr;
		if (MeshModel != nullptr)
		{
			Ar << MeshModel->SkeletalMeshModelGUID;
		}
	}

	if (SafetyFallbackCodec != nullptr)
	{
		SafetyFallbackCodec->PopulateDDCKey(Ar);
	}
}
#endif // WITH_EDITORONLY_DATA

UAnimBoneCompressionCodec* UAnimBoneCompressionCodec_ACL::GetCodec(const FString& DDCHandle)
{
	const FString ThisHandle = GetCodecDDCHandle();
	UAnimBoneCompressionCodec* CodecMatch = ThisHandle == DDCHandle ? this : nullptr;

	if (CodecMatch == nullptr && SafetyFallbackCodec != nullptr)
	{
		CodecMatch = SafetyFallbackCodec->GetCodec(DDCHandle);
	}

	return CodecMatch;
}

void UAnimBoneCompressionCodec_ACL::DecompressPose(FAnimSequenceDecompressionContext& DecompContext, const BoneTrackArray& RotationPairs, const BoneTrackArray& TranslationPairs, const BoneTrackArray& ScalePairs, TArrayView<FTransform>& OutAtoms) const
{
	const FACLCompressedAnimData& AnimData = static_cast<const FACLCompressedAnimData&>(DecompContext.CompressedAnimData);
	const acl::compressed_tracks* CompressedClipData = AnimData.GetCompressedTracks();
	check(CompressedClipData != nullptr && CompressedClipData->is_valid(false).empty());

	acl::decompression_context<UE4DefaultDecompressionSettings> ACLContext;
	ACLContext.initialize(*CompressedClipData);

	::DecompressPose(DecompContext, ACLContext, RotationPairs, TranslationPairs, ScalePairs, OutAtoms);
}

void UAnimBoneCompressionCodec_ACL::DecompressBone(FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex, FTransform& OutAtom) const
{
	const FACLCompressedAnimData& AnimData = static_cast<const FACLCompressedAnimData&>(DecompContext.CompressedAnimData);
	const acl::compressed_tracks* CompressedClipData = AnimData.GetCompressedTracks();
	check(CompressedClipData != nullptr && CompressedClipData->is_valid(false).empty());

	acl::decompression_context<UE4DefaultDecompressionSettings> ACLContext;
	ACLContext.initialize(*CompressedClipData);

	::DecompressBone(DecompContext, ACLContext, TrackIndex, OutAtom);
}
