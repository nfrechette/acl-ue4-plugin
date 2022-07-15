#pragma once

// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "CoreMinimal.h"

#include "ACLImpl.h"

#include <acl/decompression/decompress.h>
#include <acl/decompression/database/database.h>

constexpr acl::sample_rounding_policy get_rounding_policy(EAnimInterpolationType InterpType) { return InterpType == EAnimInterpolationType::Step ? acl::sample_rounding_policy::floor : acl::sample_rounding_policy::none; }

/*
 * The FTransform type does not support setting the members directly from vector types
 * so we derive from it and expose that functionality.
 */
struct FACLTransform final : public FTransform
{
	// Under UE5, these convert from float32 to float64

	FORCEINLINE_DEBUGGABLE void RTM_SIMD_CALL SetRotationRaw(rtm::quatf_arg0 Rotation_)
	{
#if PLATFORM_ENABLE_VECTORINTRINSICS
		Rotation = Rotation_;
#else
		rtm::quat_store(Rotation_, &Rotation.X);
#endif
	}

	FORCEINLINE_DEBUGGABLE void RTM_SIMD_CALL SetTranslationRaw(rtm::vector4f_arg0 Translation_)
	{
#if PLATFORM_ENABLE_VECTORINTRINSICS
		Translation = VectorSet_W0(Translation_);
#else
		rtm::vector_store3(Translation_, &Translation.X);
#endif
	}

	FORCEINLINE_DEBUGGABLE void RTM_SIMD_CALL SetScale3DRaw(rtm::vector4f_arg0 Scale_)
	{
#if PLATFORM_ENABLE_VECTORINTRINSICS
		Scale3D = VectorSet_W0(Scale_);
#else
		rtm::vector_store3(Scale_, &Scale3D.X);
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
template<bool bSkipDefaultSubTracks>
struct FUE4OutputWriter final : public acl::track_writer
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
	FORCEINLINE_DEBUGGABLE bool skip_track_rotation(uint32_t BoneIndex) const { return TrackToAtomsMap[BoneIndex].Rotation == 0xFFFF; }
	FORCEINLINE_DEBUGGABLE bool skip_track_translation(uint32_t BoneIndex) const { return TrackToAtomsMap[BoneIndex].Translation == 0xFFFF; }
	FORCEINLINE_DEBUGGABLE bool skip_track_scale(uint32_t BoneIndex) const { return TrackToAtomsMap[BoneIndex].Scale == 0xFFFF; }

	static constexpr acl::default_sub_track_mode get_default_rotation_mode() { return bSkipDefaultSubTracks ? acl::default_sub_track_mode::skipped : acl::default_sub_track_mode::constant; }
	static constexpr acl::default_sub_track_mode get_default_translation_mode() { return bSkipDefaultSubTracks ? acl::default_sub_track_mode::skipped : acl::default_sub_track_mode::constant; }
	static constexpr acl::default_sub_track_mode get_default_scale_mode() { return bSkipDefaultSubTracks ? acl::default_sub_track_mode::skipped : acl::default_sub_track_mode::legacy; }

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a quaternion rotation value for a specified bone index
	FORCEINLINE_DEBUGGABLE void RTM_SIMD_CALL write_rotation(uint32_t BoneIndex, rtm::quatf_arg0 Rotation)
	{
		const uint32 AtomIndex = TrackToAtomsMap[BoneIndex].Rotation;

		FACLTransform& BoneAtom = Atoms[AtomIndex];
		BoneAtom.SetRotationRaw(Rotation);
	}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a translation value for a specified bone index
	FORCEINLINE_DEBUGGABLE void RTM_SIMD_CALL write_translation(uint32_t BoneIndex, rtm::vector4f_arg0 Translation)
	{
		const uint32 AtomIndex = TrackToAtomsMap[BoneIndex].Translation;

		FACLTransform& BoneAtom = Atoms[AtomIndex];
		BoneAtom.SetTranslationRaw(Translation);
	}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a scale value for a specified bone index
	FORCEINLINE_DEBUGGABLE void RTM_SIMD_CALL write_scale(uint32_t BoneIndex, rtm::vector4f_arg0 Scale)
	{
		const uint32 AtomIndex = TrackToAtomsMap[BoneIndex].Scale;

		FACLTransform& BoneAtom = Atoms[AtomIndex];
		BoneAtom.SetScale3DRaw(Scale);
	}
};

/*
* Output track writer for a single track.
*/
template<bool bSkipDefaultSubTracks>
struct UE4OutputTrackWriter final : public acl::track_writer
{
	// Raw reference for performance reasons, caller is responsible for ensuring data is valid
	FACLTransform& Atom;

	explicit UE4OutputTrackWriter(FTransform& Atom_)
		: Atom(static_cast<FACLTransform&>(Atom_))
	{}

	//////////////////////////////////////////////////////////////////////////
	// Override the OutputWriter behavior
	static constexpr acl::default_sub_track_mode get_default_rotation_mode() { return bSkipDefaultSubTracks ? acl::default_sub_track_mode::skipped : acl::default_sub_track_mode::constant; }
	static constexpr acl::default_sub_track_mode get_default_translation_mode() { return bSkipDefaultSubTracks ? acl::default_sub_track_mode::skipped : acl::default_sub_track_mode::constant; }

	// TODO: use the right identity value if we are additive! until then we can't skip default sub-tracks
	//static constexpr acl::default_sub_track_mode get_default_scale_mode() { return bSkipDefaultSubTracks ? acl::default_sub_track_mode::skipped : acl::default_sub_track_mode::legacy; }
	static constexpr acl::default_sub_track_mode get_default_scale_mode() { return acl::default_sub_track_mode::legacy; }

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a quaternion rotation value for a specified bone index
	FORCEINLINE_DEBUGGABLE void RTM_SIMD_CALL write_rotation(uint32_t BoneIndex, rtm::quatf_arg0 Rotation)
	{
		Atom.SetRotationRaw(Rotation);
	}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a translation value for a specified bone index
	FORCEINLINE_DEBUGGABLE void RTM_SIMD_CALL write_translation(uint32_t BoneIndex, rtm::vector4f_arg0 Translation)
	{
		Atom.SetTranslationRaw(Translation);
	}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a scale value for a specified bone index
	FORCEINLINE_DEBUGGABLE void RTM_SIMD_CALL write_scale(uint32_t BoneIndex, rtm::vector4f_arg0 Scale)
	{
		Atom.SetScale3DRaw(Scale);
	}
};

template<class ACLContextType>
FORCEINLINE_DEBUGGABLE void DecompressBone(FAnimSequenceDecompressionContext& DecompContext, ACLContextType& ACLContext, int32 TrackIndex, FTransform& OutAtom)
{
	ACLContext.seek(DecompContext.Time, get_rounding_policy(DecompContext.Interpolation));

	// We always skip default sub-tracks
	// In the editor we cache the bind pose and always initialize the output with it
	// At runtime, the bind pose is only stripped from the data if the track is never
	// queried here at runtime (it has been excluded from stripping) or if the clip
	// is additive in which case the output is properly initialized to the identity
	// See: InitializeBoneAtomWithBindPose(..)
	constexpr bool bSkipDefaultSubTracks = true;

	UE4OutputTrackWriter<bSkipDefaultSubTracks> Writer(OutAtom);
	ACLContext.decompress_track(TrackIndex, Writer);
}

template<class ACLContextType>
FORCEINLINE_DEBUGGABLE void DecompressPose(FAnimSequenceDecompressionContext& DecompContext, ACLContextType& ACLContext, bool bIsBindPoseStripped, const BoneTrackArray& RotationPairs, const BoneTrackArray& TranslationPairs, const BoneTrackArray& ScalePairs, TArrayView<FTransform>& OutAtoms)
{
	// Seek first, we'll start prefetching ahead right away
	ACLContext.seek(DecompContext.Time, get_rounding_policy(DecompContext.Interpolation));

	const acl::compressed_tracks* CompressedClipData = ACLContext.get_compressed_tracks();
	const int32 TrackCount = CompressedClipData->get_num_tracks();

	// TODO: Allocate this with padding and use SIMD to set everything to 0xFF
	FAtomIndices* TrackToAtomsMap = new(FMemStack::Get()) FAtomIndices[TrackCount];
	FMemory::Memset(TrackToAtomsMap, 0xFF, sizeof(FAtomIndices) * TrackCount);

	// TODO: We should only need 1x uint16 atom index for each track/bone index
	// and we need 3 bits to tell whether we care about the rot/trans/scale
	// The rot and scale pairs are often the same array, skip the scale iteration and set both flags while
	// iterating on the rotation pairs. This will reduce the mapping size by 2 bytes if we use 4 bytes.
	// All reads will be aligned, can we pack further with 1x uint16 and 1x uint8 and do unaligned loads?
	// Need to double check what the ASM looks like on x64 and ARM first to make sure it's good
	// Maybe having two arrays side by side is better with uint16/uint8? Or having 1 bitset array?
	// Ultimately, when we load these indices, they will be in the L1 since we write them here just before
	// we use them during decompression. Optimizing for quick loading/unpacking it best.

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

	const acl::acl_impl::tracks_header& TracksHeader = acl::acl_impl::get_tracks_header(*CompressedClipData);
	if (TracksHeader.get_has_scale())
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
	checkf(MaxTrackIndex < TrackCount, TEXT("Invalid track index: %d"), MaxTrackIndex);
#endif

	// We will decompress the whole pose even if we only care about a smaller subset of bone tracks.
	// This ensures we read the compressed pose data once, linearly.

	// TODO: By default, UE4 always populates the bind pose before decompressing a pose
	// As such, it should be safe to always skip default tracks
	// Even if bind pose stripping is disabled, tracks that have their default value the identity are
	// stripped and populated regardless at runtime before decompression

	if (bIsBindPoseStripped)
	{
		// If our bind pose has been stripped, we can skip default sub-tracks since the caller will have pre-filled
		// the output pose buffer with it.
		constexpr bool bSkipDefaultSubTracks = true;

		FUE4OutputWriter<bSkipDefaultSubTracks> PoseWriter(OutAtoms, TrackToAtomsMap);
		ACLContext.decompress_tracks(PoseWriter);
	}
	else
	{
		// Bind pose isn't stripped, don't skip anything
		constexpr bool bSkipDefaultSubTracks = false;

		FUE4OutputWriter<bSkipDefaultSubTracks> PoseWriter(OutAtoms, TrackToAtomsMap);
		ACLContext.decompress_tracks(PoseWriter);
	}
}

template<class AnimDataType>
FORCEINLINE_DEBUGGABLE void InitializeBoneAtomWithBindPose(bool bIsBindPoseStripped, const AnimDataType& AnimData, int32 TrackIndex, FTransform& OutAtom)
{
#if WITH_EDITORONLY_DATA
	if (bIsBindPoseStripped && AnimData.StrippedBindPose.Num() != 0)
	{
		// If the bind pose has been stripped, we can recover it in the editor since we cache the data
		// Single bone decompression is often used in the editor, sadly, and the bind pose isn't
		// provided in FAnimSequenceDecompressionContext
		OutAtom = AnimData.StrippedBindPose[TrackIndex];
	}
	else
	{
		// We still skip default sub-tracks even if stripping is disabled
		// Additive clips have the identity (with zero scale) as the bind pose, so stripping or not this is safe
		// Non-additive clips that stripped this bone will fail the check below and return an incorrect result
		// Non-additive clips that excluded this bone will only strip if the value is the identity
		// TODO: use the right identity value if we are additive!
		OutAtom = FTransform::Identity;
	}
#else
	// We still skip default sub-tracks even if stripping is disabled
	// Additive clips have the identity as the bind pose, so stripping or not this is safe
	// Non-additive clips that stripped this bone will fail the check below and return an incorrect result
	// Non-additive clips that excluded this bone will only strip if the value is the identity
	// TODO: use the right identity value if we are additive!
	OutAtom = FTransform::Identity;
#endif

#if WITH_ACL_EXCLUDED_FROM_STRIPPING_CHECKS && !WITH_EDITORONLY_DATA
	// Make sure this bone hasn't been stripped with bind pose stripping in cooked games
	if (bIsBindPoseStripped && AnimData.TracksExcludedFromStrippingBitSet.Num() != 0)
	{
		const acl::bitset_description ExcludedBitsetDesc = acl::bitset_description::make_from_num_bits(AnimData.TracksExcludedFromStrippingBitSet.Num() * 32);
		const bool bIsTrackExcludedFromStripping = acl::bitset_test(AnimData.TracksExcludedFromStrippingBitSet.GetData(), ExcludedBitsetDesc, TrackIndex);
		if (!bIsTrackExcludedFromStripping)
		{
			// If a bone hasn't been excluded from bind pose stripping, then its data is no longer present in the compressed buffer.
			// As such, we cannot recover its value here.
			// DecompressPose() avoids this issue by always pre-filling the output pose with the bind pose.
			// However, here we are out of luck until Epic exposes the bind pose through the FAnimSequenceDecompressionContext.
			UE_LOG(LogAnimationCompression, Error, TEXT("ACL: Track index %u is queried explicitly at runtime but can been stripped if equal to the bind pose. Make sure this bone is excluded or results will be incorrect!"), TrackIndex);
		}
	}
#endif
}
