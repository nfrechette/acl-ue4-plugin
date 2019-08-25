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

#include <acl/algorithm/uniformly_sampled/decoder.h>

static acl::SampleRoundingPolicy get_rounding_policy(EAnimInterpolationType InterpType) { return InterpType == EAnimInterpolationType::Step ? acl::SampleRoundingPolicy::Floor : acl::SampleRoundingPolicy::None; }

/*
 * We disable range checks when using certain allocations for performance reasons.
 */
class ACLMemStackAllocator : public TMemStackAllocator<>
{
public:
	enum { RequireRangeCheck = false };
};

/*
 * The FTransform type does not support setting the members directly from vector types
 * so we derive from it and expose that functionality.
 */
struct FACLTransform : public FTransform
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

/*
 * Output pose writer that can selectively skip certain track types.
 */
template<bool SkipRotations, bool SkipTranslations, bool SkipScales>
struct UE4OutputWriter : public acl::OutputWriter
{
	// Raw pointer for performance reasons, caller is responsible for ensuring data is valid
	FACLTransform* Atoms;
	const uint16* TrackToAtomsMap;

	UE4OutputWriter(FTransformArray& Atoms_, const uint16* TrackToAtomsMap_)
		: Atoms(static_cast<FACLTransform*>(Atoms_.GetData()))
		, TrackToAtomsMap(TrackToAtomsMap_)
	{}

	//////////////////////////////////////////////////////////////////////////
	// Override the OutputWriter behavior
	static constexpr bool skip_all_bone_rotations() { return SkipRotations; }
	static constexpr bool skip_all_bone_translations() { return SkipTranslations; }
	static constexpr bool skip_all_bone_scales() { return SkipScales; }

	bool skip_bone_rotation(uint16_t BoneIndex) const { return TrackToAtomsMap[BoneIndex] == 0xFFFF; }
	bool skip_bone_translation(uint16_t BoneIndex) const { return TrackToAtomsMap[BoneIndex] == 0xFFFF; }
	bool skip_bone_scale(uint16_t BoneIndex) const { return TrackToAtomsMap[BoneIndex] == 0xFFFF; }

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a quaternion rotation value for a specified bone index
	void write_bone_rotation(uint16_t BoneIndex, const acl::Quat_32& Rotation)
	{
		const uint16 AtomIndex = TrackToAtomsMap[BoneIndex];

		FACLTransform& BoneAtom = Atoms[AtomIndex];
		BoneAtom.SetRotationRaw(Rotation);
	}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a translation value for a specified bone index
	void write_bone_translation(uint16_t BoneIndex, const acl::Vector4_32& Translation)
	{
		const uint16 AtomIndex = TrackToAtomsMap[BoneIndex];

		FACLTransform& BoneAtom = Atoms[AtomIndex];
		BoneAtom.SetTranslationRaw(Translation);
	}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a scale value for a specified bone index
	void write_bone_scale(uint16_t BoneIndex, const acl::Vector4_32& Scale)
	{
		const uint16 AtomIndex = TrackToAtomsMap[BoneIndex];

		FACLTransform& BoneAtom = Atoms[AtomIndex];
		BoneAtom.SetScale3DRaw(Scale);
	}
};

using UE4RotationWriter = UE4OutputWriter<false, true, true>;
using UE4TranslationWriter = UE4OutputWriter<true, false, true>;
using UE4ScaleWriter = UE4OutputWriter<true, true, false>;

using UE4DefaultDecompressionSettings = acl::uniformly_sampled::DefaultDecompressionSettings;
using UE4CustomDecompressionSettings = acl::uniformly_sampled::DebugDecompressionSettings;

struct UE4SafeDecompressionSettings : public UE4DefaultDecompressionSettings
{
	constexpr bool is_rotation_format_supported(acl::RotationFormat8 format) const { return format == acl::RotationFormat8::Quat_128; }
	constexpr acl::RotationFormat8 get_rotation_format(acl::RotationFormat8 /*format*/) const { return acl::RotationFormat8::Quat_128; }

	constexpr acl::RangeReductionFlags8 get_clip_range_reduction(acl::RangeReductionFlags8 /*flags*/) const { return acl::RangeReductionFlags8::Translations | acl::RangeReductionFlags8::Scales; }

	constexpr bool supports_mixed_packing() const { return true; }
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
	using namespace acl;

	const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(DecompContext.GetCompressedByteStream());
	check(CompressedClipData->is_valid(false).empty());

	uniformly_sampled::DecompressionContext<DecompressionSettingsType> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(DecompContext.Time, get_rounding_policy(DecompContext.Interpolation));

	Quat_32 Rotation;
	Vector4_32 Translation;
	Vector4_32 Scale;
	Context.decompress_bone(TrackIndex, &Rotation, &Translation, &Scale);

	OutAtom = FTransform(QuatCast(Rotation), VectorCast(Translation), VectorCast(Scale));
}

void AEFACLCompressionCodec_Default::GetBoneAtom(FTransform& OutAtom, FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex)
{
	GetBoneAtomImpl<UE4DefaultDecompressionSettings>(OutAtom, DecompContext, TrackIndex);
}

#if USE_ANIMATION_CODEC_BATCH_SOLVER
template<typename DecompressionSettingsType, typename WriterType>
static FORCEINLINE_DEBUGGABLE void GetPoseTracks(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const FAnimSequenceDecompressionContext& DecompContext)
{
	using namespace acl;

	const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(DecompContext.GetCompressedByteStream());
	check(CompressedClipData->is_valid(false).empty());

	const ClipHeader& ClipHeader = get_clip_header(*CompressedClipData);
	if (!WriterType::skip_all_bone_scales() && !ClipHeader.has_scale)
	{
		return;
	}

	uniformly_sampled::DecompressionContext<DecompressionSettingsType> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(DecompContext.Time, get_rounding_policy(DecompContext.Interpolation));

	// It is currently faster to decompress the whole pose if most of them are required,
	// we only decompress individual bones if we need just a few.

	const int32 ACLBoneCount = ClipHeader.num_bones;
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
			Quat_32 Rotation;
			Vector4_32 Translation;
			Vector4_32 Scale;
			Context.decompress_bone(Pair.TrackIndex,
				WriterType::skip_all_bone_rotations() ? nullptr : &Rotation,
				WriterType::skip_all_bone_translations() ? nullptr : &Translation,
				WriterType::skip_all_bone_scales() ? nullptr : &Scale);

			// We only care about one of these at a time
			FACLTransform& BoneAtom = static_cast<FACLTransform&>(Atoms[Pair.AtomIndex]);
			if (!WriterType::skip_all_bone_rotations())
			{
				BoneAtom.SetRotationRaw(Rotation);
			}
			else if (!WriterType::skip_all_bone_translations())
			{
				BoneAtom.SetTranslationRaw(Translation);
			}
			else if (!WriterType::skip_all_bone_scales())
			{
				BoneAtom.SetScale3DRaw(Scale);
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
		Context.decompress_pose(PoseWriter);
	}
}

void AEFACLCompressionCodec_Default::GetPoseRotations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4DefaultDecompressionSettings, UE4RotationWriter>(Atoms, DesiredPairs, DecompContext);
}

void AEFACLCompressionCodec_Default::GetPoseTranslations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4DefaultDecompressionSettings, UE4TranslationWriter>(Atoms, DesiredPairs, DecompContext);
}

void AEFACLCompressionCodec_Default::GetPoseScales(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4DefaultDecompressionSettings, UE4ScaleWriter>(Atoms, DesiredPairs, DecompContext);
}
#endif

void AEFACLCompressionCodec_Safe::GetBoneAtom(FTransform& OutAtom, FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex)
{
	GetBoneAtomImpl<UE4SafeDecompressionSettings>(OutAtom, DecompContext, TrackIndex);
}

#if USE_ANIMATION_CODEC_BATCH_SOLVER
void AEFACLCompressionCodec_Safe::GetPoseRotations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4SafeDecompressionSettings, UE4RotationWriter>(Atoms, DesiredPairs, DecompContext);
}

void AEFACLCompressionCodec_Safe::GetPoseTranslations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4SafeDecompressionSettings, UE4TranslationWriter>(Atoms, DesiredPairs, DecompContext);
}

void AEFACLCompressionCodec_Safe::GetPoseScales(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4SafeDecompressionSettings, UE4ScaleWriter>(Atoms, DesiredPairs, DecompContext);
}
#endif

void AEFACLCompressionCodec_Custom::GetBoneAtom(FTransform& OutAtom, FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex)
{
	GetBoneAtomImpl<UE4CustomDecompressionSettings>(OutAtom, DecompContext, TrackIndex);
}

#if USE_ANIMATION_CODEC_BATCH_SOLVER
void AEFACLCompressionCodec_Custom::GetPoseRotations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4CustomDecompressionSettings, UE4RotationWriter>(Atoms, DesiredPairs, DecompContext);
}

void AEFACLCompressionCodec_Custom::GetPoseTranslations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4CustomDecompressionSettings, UE4TranslationWriter>(Atoms, DesiredPairs, DecompContext);
}

void AEFACLCompressionCodec_Custom::GetPoseScales(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, FAnimSequenceDecompressionContext& DecompContext)
{
	GetPoseTracks<UE4CustomDecompressionSettings, UE4ScaleWriter>(Atoms, DesiredPairs, DecompContext);
}
#endif
