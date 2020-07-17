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

#include "AnimEncoding_ACL.h"
#include "AnimationCompression.h"
#include "AnimEncoding.h"
#include "Animation/AnimCompressionTypes.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"

#include "ACLImpl.h"

#include <acl/decompression/decompress.h>

constexpr acl::sample_rounding_policy get_rounding_policy(EAnimInterpolationType InterpType) { return InterpType == EAnimInterpolationType::Step ? acl::sample_rounding_policy::floor : acl::sample_rounding_policy::none; }

/*
 * The FTransform type does not support setting the members directly from vector types
 * so we derive from it and expose that functionality.
 */
struct FACLTransform : public FTransform
{
	void RTM_SIMD_CALL SetRotationRaw(rtm::quatf_arg0 Rotation_)
	{
#if PLATFORM_ENABLE_VECTORINTRINSICS
		Rotation = Rotation_;
#else
		rtm::quat_store(Rotation_, &Rotation.X);
#endif
	}

	void RTM_SIMD_CALL SetTranslationRaw(rtm::vector4f_arg0 Translation_)
	{
#if PLATFORM_ENABLE_VECTORINTRINSICS
		Translation = VectorSet_W0(Translation_);
#else
		rtm::vector_store3(Translation_, &Translation.X);
#endif
	}

	void RTM_SIMD_CALL SetScale3DRaw(rtm::vector4f_arg0 Scale_)
	{
#if PLATFORM_ENABLE_VECTORINTRINSICS
		Scale3D = VectorSet_W0(Scale_);
#else
		rtm::vector_store3(Scale_, &Scale3D.X);
#endif
	}

	void CopyRotation(const FACLTransform& Other)
	{
		Rotation = Other.Rotation;
	}

	void CopyTranslation(const FACLTransform& Other)
	{
		Translation = Other.Translation;
	}

	void CopyScale3D(const FACLTransform& Other)
	{
		Scale3D = Other.Scale3D;
	}
};

/*
 * Output pose writer that can selectively skip certain track types.
 */
template<bool SkipRotations, bool SkipTranslations, bool SkipScales>
struct UE4OutputPoseWriter : public acl::track_writer
{
	// Raw pointer for performance reasons, caller is responsible for ensuring data is valid
	FACLTransform* Atoms;
	const uint16* TrackToAtomsMap;

	UE4OutputPoseWriter(FTransformArray& Atoms_, const uint16* TrackToAtomsMap_)
		: Atoms(static_cast<FACLTransform*>(Atoms_.GetData()))
		, TrackToAtomsMap(TrackToAtomsMap_)
	{}

	//////////////////////////////////////////////////////////////////////////
	// Override the track_writer behavior
	static constexpr bool skip_all_rotations() { return SkipRotations; }
	static constexpr bool skip_all_translations() { return SkipTranslations; }
	static constexpr bool skip_all_scales() { return SkipScales; }

	bool skip_track_rotation(uint32_t BoneIndex) const { return TrackToAtomsMap[BoneIndex] == 0xFFFF; }
	bool skip_track_translation(uint32_t BoneIndex) const { return TrackToAtomsMap[BoneIndex] == 0xFFFF; }
	bool skip_track_scale(uint32_t BoneIndex) const { return TrackToAtomsMap[BoneIndex] == 0xFFFF; }

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a quaternion rotation value for a specified bone index
	void RTM_SIMD_CALL write_rotation(uint32_t BoneIndex, rtm::quatf_arg0 Rotation)
	{
		const uint32 AtomIndex = TrackToAtomsMap[BoneIndex];

		FACLTransform& BoneAtom = Atoms[AtomIndex];
		BoneAtom.SetRotationRaw(Rotation);
	}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a translation value for a specified bone index
	void RTM_SIMD_CALL write_translation(uint32_t BoneIndex, rtm::vector4f_arg0 Translation)
	{
		const uint32 AtomIndex = TrackToAtomsMap[BoneIndex];

		FACLTransform& BoneAtom = Atoms[AtomIndex];
		BoneAtom.SetTranslationRaw(Translation);
	}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a scale value for a specified bone index
	void RTM_SIMD_CALL write_scale(uint32_t BoneIndex, rtm::vector4f_arg0 Scale)
	{
		const uint32 AtomIndex = TrackToAtomsMap[BoneIndex];

		FACLTransform& BoneAtom = Atoms[AtomIndex];
		BoneAtom.SetScale3DRaw(Scale);
	}
};

using UE4RotationPoseWriter = UE4OutputPoseWriter<false, true, true>;
using UE4TranslationPoseWriter = UE4OutputPoseWriter<true, false, true>;
using UE4ScalePoseWriter = UE4OutputPoseWriter<true, true, false>;

/*
* Output track writer that can selectively skip certain track types.
*/
struct UE4OutputTrackWriter : public acl::track_writer
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

using UE4DefaultDecompressionSettings = acl::default_transform_decompression_settings;
using UE4CustomDecompressionSettings = acl::debug_transform_decompression_settings;

struct UE4SafeDecompressionSettings final : public UE4DefaultDecompressionSettings
{
	static constexpr bool is_rotation_format_supported(acl::rotation_format8 format) { return format == acl::rotation_format8::quatf_full; }
	static constexpr acl::rotation_format8 get_rotation_format(acl::rotation_format8 /*format*/) { return acl::rotation_format8::quatf_full; }
};

void AEFACLCompressionCodec_Base::ByteSwapIn(FUECompressedAnimData& CompressedData, FMemoryReader& MemoryReader)
{
#if !PLATFORM_LITTLE_ENDIAN
#error "ACL does not currently support big-endian platforms"
#endif

	// TODO: ACL does not support byte swapping
	MemoryReader.Serialize(CompressedData.CompressedByteStream.GetData(), CompressedData.CompressedByteStream.Num());
}

void AEFACLCompressionCodec_Base::ByteSwapOut(FUECompressedAnimData& CompressedData, FMemoryWriter& MemoryWriter)
{
#if !PLATFORM_LITTLE_ENDIAN
#error "ACL does not currently support big-endian platforms"
#endif

	// TODO: ACL does not support byte swapping
	MemoryWriter.Serialize(CompressedData.CompressedByteStream.GetData(), CompressedData.CompressedByteStream.Num());
}

template<typename DecompressionSettingsType>
static FORCEINLINE_DEBUGGABLE void GetBoneAtomImpl(FTransform& OutAtom, const FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex)
{
	const acl::compressed_tracks* CompressedClipData = reinterpret_cast<const acl::compressed_tracks*>(DecompContext.GetCompressedByteStream());
	check(CompressedClipData->is_valid(false).empty());

	acl::decompression_context<DecompressionSettingsType> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(DecompContext.Time, get_rounding_policy(DecompContext.Interpolation));

	UE4OutputTrackWriter Writer(OutAtom);
	Context.decompress_track(TrackIndex, Writer);
}

void AEFACLCompressionCodec_Default::GetBoneAtom(FTransform& OutAtom, FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex)
{
	GetBoneAtomImpl<UE4DefaultDecompressionSettings>(OutAtom, DecompContext, TrackIndex);
}

#if USE_ANIMATION_CODEC_BATCH_SOLVER
template<typename DecompressionSettingsType, typename WriterType>
static FORCEINLINE_DEBUGGABLE void GetPoseTracks(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const FAnimSequenceDecompressionContext& DecompContext)
{
	const acl::compressed_tracks* CompressedClipData = reinterpret_cast<const acl::compressed_tracks*>(DecompContext.GetCompressedByteStream());
	check(CompressedClipData->is_valid(false).empty());

	const acl::acl_impl::transform_tracks_header& TransformHeader = acl::acl_impl::get_transform_tracks_header(*CompressedClipData);
	if (!WriterType::skip_all_scales() && !TransformHeader.has_scale)
	{
		return;
	}

	acl::decompression_context<DecompressionSettingsType> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(DecompContext.Time, get_rounding_policy(DecompContext.Interpolation));

	// It is currently faster to decompress the whole pose if most of them are required,
	// we only decompress individual bones if we need just a few.

	const int32 ACLBoneCount = CompressedClipData->get_num_tracks();
	const int32 PairCount = DesiredPairs.Num();
	if (PairCount * 4 < ACLBoneCount)
	{
		// Check if we have fewer than 25% of the tracks we are interested in, in that case, use decompress_bone
		// PairCount <= ACLBoneCount * 0.25
		// PC <= BC * 25 / 100
		// PC * 100 <= BC * 25
		// PC * 100 / 25 <= BC
		// PC * 4 <= BC
		for (const BoneTrackPair& Pair : DesiredPairs)
		{
			FACLTransform Atom;
			UE4OutputTrackWriter Writer(Atom);
			Context.decompress_track(Pair.TrackIndex, Writer);

			// We only care about one of these at a time
			FACLTransform& BoneAtom = static_cast<FACLTransform&>(Atoms[Pair.AtomIndex]);
			if (!WriterType::skip_all_rotations())
			{
				BoneAtom.CopyRotation(Atom);
			}
			else if (!WriterType::skip_all_translations())
			{
				BoneAtom.CopyTranslation(Atom);
			}
			else if (!WriterType::skip_all_scales())
			{
				BoneAtom.CopyScale3D(Atom);
			}
		}
	}
	else
	{
		uint16* TrackToAtomsMap = new(FMemStack::Get()) uint16[ACLBoneCount];
		std::fill(TrackToAtomsMap, TrackToAtomsMap + ACLBoneCount, 0xFFFF);

#if DO_CHECK
		int32 MinAtomIndex = Atoms.Num();
		int32 MaxAtomIndex = -1;
		int32 MinTrackIndex = INT_MAX;
		int32 MaxTrackIndex = -1;
#endif

		for (const BoneTrackPair& Pair : DesiredPairs)
		{
			TrackToAtomsMap[Pair.TrackIndex] = (uint16)Pair.AtomIndex;

#if DO_CHECK
			MinAtomIndex = FMath::Min(MinAtomIndex, Pair.AtomIndex);
			MaxAtomIndex = FMath::Max(MaxAtomIndex, Pair.AtomIndex);
			MinTrackIndex = FMath::Min(MinTrackIndex, Pair.TrackIndex);
			MaxTrackIndex = FMath::Max(MaxTrackIndex, Pair.TrackIndex);
#endif
		}

		// Only assert once for performance reasons, when we write the pose, we won't perform the checks
		checkf(Atoms.IsValidIndex(MinAtomIndex), TEXT("Invalid atom index: %d"), MinAtomIndex);
		checkf(Atoms.IsValidIndex(MaxAtomIndex), TEXT("Invalid atom index: %d"), MaxAtomIndex);
		checkf(MinTrackIndex >= 0, TEXT("Invalid track index: %d"), MinTrackIndex);
		checkf(MaxTrackIndex < ACLBoneCount, TEXT("Invalid track index: %d"), MaxTrackIndex);

		WriterType PoseWriter(Atoms, TrackToAtomsMap);
		Context.decompress_tracks(PoseWriter);
	}
}

void AEFACLCompressionCodec_Default::GetPoseRotations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4DefaultDecompressionSettings, UE4RotationPoseWriter>(Atoms, DesiredPairs, DecompContext);
}

void AEFACLCompressionCodec_Default::GetPoseTranslations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4DefaultDecompressionSettings, UE4TranslationPoseWriter>(Atoms, DesiredPairs, DecompContext);
}

void AEFACLCompressionCodec_Default::GetPoseScales(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4DefaultDecompressionSettings, UE4ScalePoseWriter>(Atoms, DesiredPairs, DecompContext);
}
#endif

void AEFACLCompressionCodec_Safe::GetBoneAtom(FTransform& OutAtom, FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex)
{
	GetBoneAtomImpl<UE4SafeDecompressionSettings>(OutAtom, DecompContext, TrackIndex);
}

#if USE_ANIMATION_CODEC_BATCH_SOLVER
void AEFACLCompressionCodec_Safe::GetPoseRotations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4SafeDecompressionSettings, UE4RotationPoseWriter>(Atoms, DesiredPairs, DecompContext);
}

void AEFACLCompressionCodec_Safe::GetPoseTranslations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4SafeDecompressionSettings, UE4TranslationPoseWriter>(Atoms, DesiredPairs, DecompContext);
}

void AEFACLCompressionCodec_Safe::GetPoseScales(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4SafeDecompressionSettings, UE4ScalePoseWriter>(Atoms, DesiredPairs, DecompContext);
}
#endif

void AEFACLCompressionCodec_Custom::GetBoneAtom(FTransform& OutAtom, FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex)
{
	GetBoneAtomImpl<UE4CustomDecompressionSettings>(OutAtom, DecompContext, TrackIndex);
}

#if USE_ANIMATION_CODEC_BATCH_SOLVER
void AEFACLCompressionCodec_Custom::GetPoseRotations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4CustomDecompressionSettings, UE4RotationPoseWriter>(Atoms, DesiredPairs, DecompContext);
}

void AEFACLCompressionCodec_Custom::GetPoseTranslations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4CustomDecompressionSettings, UE4TranslationPoseWriter>(Atoms, DesiredPairs, DecompContext);
}

void AEFACLCompressionCodec_Custom::GetPoseScales(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4CustomDecompressionSettings, UE4ScalePoseWriter>(Atoms, DesiredPairs, DecompContext);
}
#endif
