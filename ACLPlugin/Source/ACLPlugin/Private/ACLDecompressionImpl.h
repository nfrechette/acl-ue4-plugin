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
struct UE4OutputTrackWriter final : public acl::track_writer
{
	// Raw pointer for performance reasons, caller is responsible for ensuring data is valid
	FACLTransform* Atom;

	UE4OutputTrackWriter(FTransform& Atom_)
		: Atom(static_cast<FACLTransform*>(&Atom_))
	{}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a quaternion rotation value for a specified bone index
	void RTM_SIMD_CALL write_rotation(uint32_t BoneIndex, rtm::quatf_arg0 Rotation)
	{
		Atom->SetRotationRaw(Rotation);
	}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a translation value for a specified bone index
	void RTM_SIMD_CALL write_translation(uint32_t BoneIndex, rtm::vector4f_arg0 Translation)
	{
		Atom->SetTranslationRaw(Translation);
	}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a scale value for a specified bone index
	void RTM_SIMD_CALL write_scale(uint32_t BoneIndex, rtm::vector4f_arg0 Scale)
	{
		Atom->SetScale3DRaw(Scale);
	}
};

template<class ACLContextType>
FORCEINLINE_DEBUGGABLE void DecompressBone(FAnimSequenceDecompressionContext& DecompContext, ACLContextType& ACLContext, int32 TrackIndex, FTransform& OutAtom)
{
	ACLContext.seek(DecompContext.Time, get_rounding_policy(DecompContext.Interpolation));

	UE4OutputTrackWriter Writer(OutAtom);
	ACLContext.decompress_track(TrackIndex, Writer);
}

template<class ACLContextType>
FORCEINLINE_DEBUGGABLE void DecompressPose(FAnimSequenceDecompressionContext& DecompContext, ACLContextType& ACLContext, const BoneTrackArray& RotationPairs, const BoneTrackArray& TranslationPairs, const BoneTrackArray& ScalePairs, TArrayView<FTransform>& OutAtoms)
{
	// Seek first, we'll start prefetching ahead right away
	ACLContext.seek(DecompContext.Time, get_rounding_policy(DecompContext.Interpolation));

	const acl::compressed_tracks* CompressedClipData = ACLContext.get_compressed_tracks();
	const int32 ACLBoneCount = CompressedClipData->get_num_tracks();

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
	checkf(MaxTrackIndex < ACLBoneCount, TEXT("Invalid track index: %d"), MaxTrackIndex);
#endif

	// We will decompress the whole pose even if we only care about a smaller subset of bone tracks.
	// This ensures we read the compressed pose data once, linearly.

	FUE4OutputWriter PoseWriter(OutAtoms, TrackToAtomsMap);
	ACLContext.decompress_tracks(PoseWriter);
}
