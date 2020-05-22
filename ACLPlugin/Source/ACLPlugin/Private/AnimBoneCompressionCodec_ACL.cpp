// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "AnimBoneCompressionCodec_ACL.h"

#if WITH_EDITORONLY_DATA
#include "AnimBoneCompressionCodec_ACLSafe.h"

#include "ACLImpl.h"

#include <acl/algorithm/uniformly_sampled/decoder.h>
#include <acl/compression/utils.h>
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

void UAnimBoneCompressionCodec_ACL::GetCompressionSettings(acl::CompressionSettings& OutSettings) const
{
	using namespace acl;

	OutSettings = get_default_compression_settings();

	OutSettings.level = GetCompressionLevel(CompressionLevel);
	OutSettings.error_threshold = ErrorThreshold;
}

ACLSafetyFallbackResult UAnimBoneCompressionCodec_ACL::ExecuteSafetyFallback(acl::IAllocator& Allocator, const acl::CompressionSettings& Settings, const acl::AnimationClip& RawClip, const acl::CompressedClip& CompressedClipData, const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult)
{
	using namespace acl;

	if (SafetyFallbackCodec != nullptr && SafetyFallbackThreshold > 0.0f)
	{
		checkSlow(CompressedClipData.is_valid(true).empty());

		uniformly_sampled::DecompressionContext<UE4DefaultDecompressionSettings> Context;
		Context.initialize(CompressedClipData);
		const BoneError bone_error = calculate_compressed_clip_error(Allocator, RawClip, *Settings.error_metric, Context);
		if (bone_error.error >= SafetyFallbackThreshold)
		{
			UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Animation compressed size: %u bytes"), CompressedClipData.get_size());
			UE_LOG(LogAnimationCompression, Warning, TEXT("ACL Animation error is too high, a safe fallback will be used instead: %.4f cm"), bone_error.error);

			// Just use the safety fallback
			return SafetyFallbackCodec->Compress(CompressibleAnimData, OutResult) ? ACLSafetyFallbackResult::Success : ACLSafetyFallbackResult::Failure;
		}
	}

	return ACLSafetyFallbackResult::Ignored;
}

void UAnimBoneCompressionCodec_ACL::PopulateDDCKey(FArchive& Ar)
{
	Super::PopulateDDCKey(Ar);

	acl::CompressionSettings Settings;
	GetCompressionSettings(Settings);

	uint32 ForceRebuildVersion = 0;
	uint16 AlgorithmVersion = acl::get_algorithm_version(acl::AlgorithmType8::UniformlySampled);
	uint32 SettingsHash = Settings.get_hash();

	Ar	<< SafetyFallbackThreshold << ForceRebuildVersion << AlgorithmVersion << SettingsHash;

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
	::DecompressPose<UE4DefaultDecompressionSettings>(DecompContext, RotationPairs, TranslationPairs, ScalePairs, OutAtoms);
}

void UAnimBoneCompressionCodec_ACL::DecompressBone(FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex, FTransform& OutAtom) const
{
	::DecompressBone<UE4DefaultDecompressionSettings>(DecompContext, TrackIndex, OutAtom);
}
