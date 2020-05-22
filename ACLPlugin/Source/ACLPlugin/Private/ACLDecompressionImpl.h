#pragma once

// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "CoreMinimal.h"
#include "ACLImpl.h"

#include <acl/algorithm/uniformly_sampled/decoder.h>

constexpr acl::SampleRoundingPolicy get_rounding_policy(EAnimInterpolationType InterpType) { return InterpType == EAnimInterpolationType::Step ? acl::SampleRoundingPolicy::Floor : acl::SampleRoundingPolicy::None; }

/*
 * The FTransform type does not support setting the members directly from vector types
 * so we derive from it and expose that functionality.
 */
struct FACLTransform final : public FTransform
{
	void SetRotationRaw(const acl::Quat_32& Rotation_)
	{
#if PLATFORM_ENABLE_VECTORINTRINSICS
		Rotation = Rotation_;
#else
		acl::quat_unaligned_write(Rotation_, &Rotation.X);
#endif
	}

	void SetTranslationRaw(const acl::Vector4_32& Translation_)
	{
#if PLATFORM_ENABLE_VECTORINTRINSICS
		Translation = VectorSet_W0(Translation_);
#else
		acl::vector_unaligned_write3(Translation_, &Translation.X);
#endif
	}

	void SetScale3DRaw(const acl::Vector4_32& Scale_)
	{
#if PLATFORM_ENABLE_VECTORINTRINSICS
		Scale3D = VectorSet_W0(Scale_);
#else
		acl::vector_unaligned_write3(Scale_, &Scale3D.X);
#endif
	}
};

/** These 3 indices map into the output Atom array. */
struct FAtomIndices
{
	uint16 Rotation;
	uint16 Translation;
	uint16 Scale;
};

/*
 * Output pose writer that can selectively skip certain tracks.
 */
struct FUE4OutputWriter final : public acl::OutputWriter
{
	// Raw pointer for performance reasons, caller is responsible for ensuring data is valid
	FACLTransform* Atoms;
	const FAtomIndices* TrackToAtomsMap;

	FUE4OutputWriter(TArrayView<FTransform>& Atoms_, const FAtomIndices* TrackToAtomsMap_)
		: Atoms(static_cast<FACLTransform*>(Atoms_.GetData()))
		, TrackToAtomsMap(TrackToAtomsMap_)
	{}

	//////////////////////////////////////////////////////////////////////////
	// Override the OutputWriter behavior
	bool skip_bone_rotation(uint16_t BoneIndex) const { return TrackToAtomsMap[BoneIndex].Rotation == 0xFFFF; }
	bool skip_bone_translation(uint16_t BoneIndex) const { return TrackToAtomsMap[BoneIndex].Translation == 0xFFFF; }
	bool skip_bone_scale(uint16_t BoneIndex) const { return TrackToAtomsMap[BoneIndex].Scale == 0xFFFF; }

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a quaternion rotation value for a specified bone index
	void write_bone_rotation(uint16_t BoneIndex, const acl::Quat_32& Rotation)
	{
		const uint16 AtomIndex = TrackToAtomsMap[BoneIndex].Rotation;

		FACLTransform& BoneAtom = Atoms[AtomIndex];
		BoneAtom.SetRotationRaw(Rotation);
	}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a translation value for a specified bone index
	void write_bone_translation(uint16_t BoneIndex, const acl::Vector4_32& Translation)
	{
		const uint16 AtomIndex = TrackToAtomsMap[BoneIndex].Translation;

		FACLTransform& BoneAtom = Atoms[AtomIndex];
		BoneAtom.SetTranslationRaw(Translation);
	}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a scale value for a specified bone index
	void write_bone_scale(uint16_t BoneIndex, const acl::Vector4_32& Scale)
	{
		const uint16 AtomIndex = TrackToAtomsMap[BoneIndex].Scale;

		FACLTransform& BoneAtom = Atoms[AtomIndex];
		BoneAtom.SetScale3DRaw(Scale);
	}
};

using UE4DefaultDecompressionSettings = acl::uniformly_sampled::DefaultDecompressionSettings;
using UE4CustomDecompressionSettings = acl::uniformly_sampled::DebugDecompressionSettings;

struct UE4SafeDecompressionSettings final : public UE4DefaultDecompressionSettings
{
	constexpr bool is_rotation_format_supported(acl::RotationFormat8 format) const { return format == acl::RotationFormat8::Quat_128; }
	constexpr acl::RotationFormat8 get_rotation_format(acl::RotationFormat8 /*format*/) const { return acl::RotationFormat8::Quat_128; }

	constexpr acl::RangeReductionFlags8 get_clip_range_reduction(acl::RangeReductionFlags8 /*flags*/) const { return acl::RangeReductionFlags8::Translations | acl::RangeReductionFlags8::Scales; }

	constexpr bool supports_mixed_packing() const { return true; }
};

template<typename DecompressionSettingsType>
FORCEINLINE_DEBUGGABLE void DecompressBone(FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex, FTransform& OutAtom)
{
	using namespace acl;

	const FACLCompressedAnimData& AnimData = static_cast<const FACLCompressedAnimData&>(DecompContext.CompressedAnimData);
	const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(AnimData.CompressedByteStream.GetData());
	check(CompressedClipData->is_valid(false).empty());

	uniformly_sampled::DecompressionContext<DecompressionSettingsType> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(DecompContext.Time, get_rounding_policy(DecompContext.Interpolation));

	Quat_32 Rotation;
	Vector4_32 Translation;
	Vector4_32 Scale;
	Context.decompress_bone(TrackIndex, &Rotation, &Translation, &Scale);

	FACLTransform& BoneAtom = static_cast<FACLTransform&>(OutAtom);
	BoneAtom.SetRotationRaw(Rotation);
	BoneAtom.SetTranslationRaw(Translation);
	BoneAtom.SetScale3DRaw(Scale);
}

template<typename DecompressionSettingsType>
FORCEINLINE_DEBUGGABLE void DecompressPose(FAnimSequenceDecompressionContext& DecompContext, const BoneTrackArray& RotationPairs, const BoneTrackArray& TranslationPairs, const BoneTrackArray& ScalePairs, TArrayView<FTransform>& OutAtoms)
{
	using namespace acl;

	const FACLCompressedAnimData& AnimData = static_cast<const FACLCompressedAnimData&>(DecompContext.CompressedAnimData);
	const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(AnimData.CompressedByteStream.GetData());
	check(CompressedClipData->is_valid(false).empty());

	uniformly_sampled::DecompressionContext<DecompressionSettingsType> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(DecompContext.Time, get_rounding_policy(DecompContext.Interpolation));

	const ClipHeader& ClipHeader = get_clip_header(*CompressedClipData);
	const int32 ACLBoneCount = ClipHeader.num_bones;

	// TODO: Allocate this with padding and use SIMD to set everything to 0xFF
	FAtomIndices* TrackToAtomsMap = new(FMemStack::Get()) FAtomIndices[ACLBoneCount];
	FMemory::Memset(TrackToAtomsMap, 0xFF, sizeof(FAtomIndices) * ACLBoneCount);

	// TODO: We should only need 1x uint16 atom index for each track/bone index
	// and we need 3 bits to tell whether we care about the rot/trans/scale
	// The rot and scale pairs are often the same array, skip the scale iteration and set both flags while
	// iterating on the rotation pairs. This will reduce the mapping size by 2 bytes if we use 4 bytes.
	// All reads will be aligned, can we pack further with 1x uint16 and 1x uint8 and do unaligned loads?
	// Need to double check what the ASM looks like on x64 and ARM first to make sure it's good
	// Maybe having two arrays side by side is better with uint16/uint8? Or having 1 bitset array?

#if DO_CHECK
	int32 MinAtomIndex = OutAtoms.Num();
	int32 MaxAtomIndex = -1;
	int32 MinTrackIndex = INT_MAX;
	int32 MaxTrackIndex = -1;
#endif

	for (const BoneTrackPair& Pair : RotationPairs)
	{
		TrackToAtomsMap[Pair.TrackIndex].Rotation = (uint16)Pair.AtomIndex;

#if DO_CHECK
		MinAtomIndex = FMath::Min(MinAtomIndex, Pair.AtomIndex);
		MaxAtomIndex = FMath::Max(MaxAtomIndex, Pair.AtomIndex);
		MinTrackIndex = FMath::Min(MinTrackIndex, Pair.TrackIndex);
		MaxTrackIndex = FMath::Max(MaxTrackIndex, Pair.TrackIndex);
#endif
	}

	for (const BoneTrackPair& Pair : TranslationPairs)
	{
		TrackToAtomsMap[Pair.TrackIndex].Translation = (uint16)Pair.AtomIndex;

#if DO_CHECK
		MinAtomIndex = FMath::Min(MinAtomIndex, Pair.AtomIndex);
		MaxAtomIndex = FMath::Max(MaxAtomIndex, Pair.AtomIndex);
		MinTrackIndex = FMath::Min(MinTrackIndex, Pair.TrackIndex);
		MaxTrackIndex = FMath::Max(MaxTrackIndex, Pair.TrackIndex);
#endif
	}

	if (ClipHeader.has_scale)
	{
		for (const BoneTrackPair& Pair : ScalePairs)
		{
			TrackToAtomsMap[Pair.TrackIndex].Scale = (uint16)Pair.AtomIndex;

#if DO_CHECK
			MinAtomIndex = FMath::Min(MinAtomIndex, Pair.AtomIndex);
			MaxAtomIndex = FMath::Max(MaxAtomIndex, Pair.AtomIndex);
			MinTrackIndex = FMath::Min(MinTrackIndex, Pair.TrackIndex);
			MaxTrackIndex = FMath::Max(MaxTrackIndex, Pair.TrackIndex);
#endif
		}
	}

#if DO_CHECK
	// Only assert once for performance reasons, when we write the pose, we won't perform the checks
	checkf(OutAtoms.IsValidIndex(MinAtomIndex), TEXT("Invalid atom index: %d"), MinAtomIndex);
	checkf(OutAtoms.IsValidIndex(MaxAtomIndex), TEXT("Invalid atom index: %d"), MaxAtomIndex);
	checkf(MinTrackIndex >= 0, TEXT("Invalid track index: %d"), MinTrackIndex);
	checkf(MaxTrackIndex < ACLBoneCount, TEXT("Invalid track index: %d"), MaxTrackIndex);
#endif

	// We will decompress the whole pose even if we only care about a smaller subset of bone tracks.
	// This ensures we read the compressed pose data once, linearly.

	FUE4OutputWriter PoseWriter(OutAtoms, TrackToAtomsMap);
	Context.decompress_pose(PoseWriter);
}
