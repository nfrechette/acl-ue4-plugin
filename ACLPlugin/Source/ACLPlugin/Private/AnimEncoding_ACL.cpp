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
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"

#include "ACLImpl.h"

#include <acl/algorithm/uniformly_sampled/decoder.h>

static acl::SampleRoundingPolicy get_rounding_policy(EAnimInterpolationType InterpType) { return InterpType == EAnimInterpolationType::Step ? acl::SampleRoundingPolicy::Floor : acl::SampleRoundingPolicy::None; }

template<bool SkipRotations, bool SkipTranslations, bool SkipScales>
struct UE4OutputWriter : acl::OutputWriter
{
	FTransformArray& Atoms;
	const TArray<uint16, TMemStackAllocator<>>& TrackToAtomsMap;

	UE4OutputWriter(FTransformArray& Atoms_, const TArray<uint16, TMemStackAllocator<>>& TrackToAtomsMap_)
		: Atoms(Atoms_)
		, TrackToAtomsMap(TrackToAtomsMap_)
	{}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a quaternion rotation value for a specified bone index
	void write_bone_rotation(int32_t bone_index, const acl::Quat_32& rotation)
	{
		if (SkipRotations)
			return;

		const uint16 AtomIndex = TrackToAtomsMap[bone_index];
		if (AtomIndex == 0xFFFF)
			return;

		FTransform& BoneAtom = Atoms[AtomIndex];
		BoneAtom.SetRotation(QuatCast(rotation));
	}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a translation value for a specified bone index
	void write_bone_translation(int32_t bone_index, const acl::Vector4_32& translation)
	{
		if (SkipTranslations)
			return;

		const uint16 AtomIndex = TrackToAtomsMap[bone_index];
		if (AtomIndex == 0xFFFF)
			return;

		FTransform& BoneAtom = Atoms[AtomIndex];
		BoneAtom.SetTranslation(VectorCast(translation));
	}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a scale value for a specified bone index
	void write_bone_scale(int32_t bone_index, const acl::Vector4_32& scale)
	{
		if (SkipScales)
			return;

		const uint16 AtomIndex = TrackToAtomsMap[bone_index];
		if (AtomIndex == 0xFFFF)
			return;

		FTransform& BoneAtom = Atoms[AtomIndex];
		BoneAtom.SetScale3D(VectorCast(scale));
	}
};

using UE4RotationWriter = UE4OutputWriter<false, true, true>;
using UE4TranslationWriter = UE4OutputWriter<true, false, true>;
using UE4ScaleWriter = UE4OutputWriter<true, true, false>;

using UE4DefaultDecompressionSettings = acl::uniformly_sampled::DefaultDecompressionSettings;
using UE4DebugDecompressionSettings = acl::uniformly_sampled::DebugDecompressionSettings;

struct UE4SafeDecompressionSettings : public UE4DefaultDecompressionSettings
{
	constexpr bool is_rotation_format_supported(acl::RotationFormat8 format) const { return format == acl::RotationFormat8::Quat_128; }
	constexpr acl::RotationFormat8 get_rotation_format(acl::RotationFormat8 /*format*/) const { return acl::RotationFormat8::Quat_128; }

	constexpr acl::RangeReductionFlags8 get_clip_range_reduction(acl::RangeReductionFlags8 /*flags*/) const { return acl::RangeReductionFlags8::Translations | acl::RangeReductionFlags8::Scales; }

	constexpr bool supports_mixed_packing() const { return true; }
};

void AEFACLCompressionCodec_Base::ByteSwapIn(UAnimSequence& Seq, FMemoryReader& MemoryReader)
{
#if !PLATFORM_LITTLE_ENDIAN
#error "ACL does not currently support big-endian platforms"
#endif

	// TODO: ACL does not support byte swapping
	const int32 OriginalNumBytes = MemoryReader.TotalSize();
	Seq.CompressedByteStream.Empty(OriginalNumBytes);
	Seq.CompressedByteStream.AddUninitialized(OriginalNumBytes);
	MemoryReader.Serialize(Seq.CompressedByteStream.GetData(), Seq.CompressedByteStream.Num());
}

void AEFACLCompressionCodec_Base::ByteSwapOut(UAnimSequence& Seq, TArray<uint8>& SerializedData, bool ForceByteSwapping)
{
#if !PLATFORM_LITTLE_ENDIAN
#error "ACL does not currently support big-endian platforms"
#endif

	// TODO: ACL does not support byte swapping
	FMemoryWriter MemoryWriter(SerializedData, true);
	MemoryWriter.SetByteSwapping(ForceByteSwapping);
	MemoryWriter.Serialize(Seq.CompressedByteStream.GetData(), Seq.CompressedByteStream.Num());
}

void AEFACLCompressionCodec_Default::GetBoneAtom(FTransform& OutAtom, const UAnimSequence& Seq, int32 TrackIndex, float Time)
{
	using namespace acl;

	const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(Seq.CompressedByteStream.GetData());
	check(CompressedClipData->is_valid(false).empty());

	uniformly_sampled::DecompressionContext<UE4DefaultDecompressionSettings> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(Time, get_rounding_policy(Seq.Interpolation));

	Quat_32 Rotation;
	Vector4_32 Translation;
	Vector4_32 Scale;
	Context.decompress_bone(TrackIndex, &Rotation, &Translation, &Scale);

	OutAtom = FTransform(QuatCast(Rotation), VectorCast(Translation), VectorCast(Scale));
}

#if USE_ANIMATION_CODEC_BATCH_SOLVER
void AEFACLCompressionCodec_Default::GetPoseRotations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time)
{
	using namespace acl;

	const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(Seq.CompressedByteStream.GetData());
	check(CompressedClipData->is_valid(false).empty());

	const ClipHeader& ClipHeader = get_clip_header(*CompressedClipData);
	const uint32_t ACLBoneCount = ClipHeader.num_bones;
	TArray<uint16, TMemStackAllocator<>> TrackToAtomsMap;
	TrackToAtomsMap.Reserve(ACLBoneCount);
	TrackToAtomsMap.AddUninitialized(ACLBoneCount);
	std::fill(TrackToAtomsMap.GetData(), TrackToAtomsMap.GetData() + ACLBoneCount, 0xFFFF);

	const uint32 PairCount = DesiredPairs.Num();
	for (uint32 PairIndex = 0; PairIndex < PairCount; ++PairIndex)
	{
		const BoneTrackPair& Pair = DesiredPairs[PairIndex];
		TrackToAtomsMap[Pair.TrackIndex] = (uint16)Pair.AtomIndex;
	}

	uniformly_sampled::DecompressionContext<UE4DefaultDecompressionSettings> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(Time, get_rounding_policy(Seq.Interpolation));

	UE4RotationWriter PoseWriter(Atoms, TrackToAtomsMap);
	Context.decompress_pose(PoseWriter);
}

void AEFACLCompressionCodec_Default::GetPoseTranslations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time)
{
	using namespace acl;

	const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(Seq.CompressedByteStream.GetData());
	check(CompressedClipData->is_valid(false).empty());

	const ClipHeader& ClipHeader = get_clip_header(*CompressedClipData);
	const uint32_t ACLBoneCount = ClipHeader.num_bones;
	TArray<uint16, TMemStackAllocator<>> TrackToAtomsMap;
	TrackToAtomsMap.Reserve(ACLBoneCount);
	TrackToAtomsMap.AddUninitialized(ACLBoneCount);
	std::fill(TrackToAtomsMap.GetData(), TrackToAtomsMap.GetData() + ACLBoneCount, 0xFFFF);

	const uint32 PairCount = DesiredPairs.Num();
	for (uint32 PairIndex = 0; PairIndex < PairCount; ++PairIndex)
	{
		const BoneTrackPair& Pair = DesiredPairs[PairIndex];
		TrackToAtomsMap[Pair.TrackIndex] = (uint16)Pair.AtomIndex;
	}

	uniformly_sampled::DecompressionContext<UE4DefaultDecompressionSettings> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(Time, get_rounding_policy(Seq.Interpolation));

	UE4TranslationWriter PoseWriter(Atoms, TrackToAtomsMap);
	Context.decompress_pose(PoseWriter);
}

void AEFACLCompressionCodec_Default::GetPoseScales(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time)
{
	using namespace acl;

	const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(Seq.CompressedByteStream.GetData());
	check(CompressedClipData->is_valid(false).empty());

	const ClipHeader& ClipHeader = get_clip_header(*CompressedClipData);
	const uint32_t ACLBoneCount = ClipHeader.num_bones;
	TArray<uint16, TMemStackAllocator<>> TrackToAtomsMap;
	TrackToAtomsMap.Reserve(ACLBoneCount);
	TrackToAtomsMap.AddUninitialized(ACLBoneCount);
	std::fill(TrackToAtomsMap.GetData(), TrackToAtomsMap.GetData() + ACLBoneCount, 0xFFFF);

	const uint32 PairCount = DesiredPairs.Num();
	for (uint32 PairIndex = 0; PairIndex < PairCount; ++PairIndex)
	{
		const BoneTrackPair& Pair = DesiredPairs[PairIndex];
		TrackToAtomsMap[Pair.TrackIndex] = (uint16)Pair.AtomIndex;
	}

	uniformly_sampled::DecompressionContext<UE4DefaultDecompressionSettings> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(Time, get_rounding_policy(Seq.Interpolation));

	UE4ScaleWriter PoseWriter(Atoms, TrackToAtomsMap);
	Context.decompress_pose(PoseWriter);
}
#endif

void AEFACLCompressionCodec_Safe::GetBoneAtom(FTransform& OutAtom, const UAnimSequence& Seq, int32 TrackIndex, float Time)
{
	using namespace acl;

	const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(Seq.CompressedByteStream.GetData());
	check(CompressedClipData->is_valid(false).empty());

	uniformly_sampled::DecompressionContext<UE4SafeDecompressionSettings> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(Time, get_rounding_policy(Seq.Interpolation));

	Quat_32 Rotation;
	Vector4_32 Translation;
	Vector4_32 Scale;
	Context.decompress_bone(TrackIndex, &Rotation, &Translation, &Scale);

	OutAtom = FTransform(QuatCast(Rotation), VectorCast(Translation), VectorCast(Scale));
}

#if USE_ANIMATION_CODEC_BATCH_SOLVER
void AEFACLCompressionCodec_Safe::GetPoseRotations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time)
{
	using namespace acl;

	const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(Seq.CompressedByteStream.GetData());
	check(CompressedClipData->is_valid(false).empty());

	const ClipHeader& ClipHeader = get_clip_header(*CompressedClipData);
	const uint32_t ACLBoneCount = ClipHeader.num_bones;
	TArray<uint16, TMemStackAllocator<>> TrackToAtomsMap;
	TrackToAtomsMap.Reserve(ACLBoneCount);
	TrackToAtomsMap.AddUninitialized(ACLBoneCount);
	std::fill(TrackToAtomsMap.GetData(), TrackToAtomsMap.GetData() + ACLBoneCount, 0xFFFF);

	const uint32 PairCount = DesiredPairs.Num();
	for (uint32 PairIndex = 0; PairIndex < PairCount; ++PairIndex)
	{
		const BoneTrackPair& Pair = DesiredPairs[PairIndex];
		TrackToAtomsMap[Pair.TrackIndex] = (uint16)Pair.AtomIndex;
	}

	uniformly_sampled::DecompressionContext<UE4SafeDecompressionSettings> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(Time, get_rounding_policy(Seq.Interpolation));

	UE4RotationWriter PoseWriter(Atoms, TrackToAtomsMap);
	Context.decompress_pose(PoseWriter);
}

void AEFACLCompressionCodec_Safe::GetPoseTranslations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time)
{
	using namespace acl;

	const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(Seq.CompressedByteStream.GetData());
	check(CompressedClipData->is_valid(false).empty());

	const ClipHeader& ClipHeader = get_clip_header(*CompressedClipData);
	const uint32_t ACLBoneCount = ClipHeader.num_bones;
	TArray<uint16, TMemStackAllocator<>> TrackToAtomsMap;
	TrackToAtomsMap.Reserve(ACLBoneCount);
	TrackToAtomsMap.AddUninitialized(ACLBoneCount);
	std::fill(TrackToAtomsMap.GetData(), TrackToAtomsMap.GetData() + ACLBoneCount, 0xFFFF);

	const uint32 PairCount = DesiredPairs.Num();
	for (uint32 PairIndex = 0; PairIndex < PairCount; ++PairIndex)
	{
		const BoneTrackPair& Pair = DesiredPairs[PairIndex];
		TrackToAtomsMap[Pair.TrackIndex] = (uint16)Pair.AtomIndex;
	}

	uniformly_sampled::DecompressionContext<UE4SafeDecompressionSettings> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(Time, get_rounding_policy(Seq.Interpolation));

	UE4TranslationWriter PoseWriter(Atoms, TrackToAtomsMap);
	Context.decompress_pose(PoseWriter);
}

void AEFACLCompressionCodec_Safe::GetPoseScales(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time)
{
	using namespace acl;

	const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(Seq.CompressedByteStream.GetData());
	check(CompressedClipData->is_valid(false).empty());

	const ClipHeader& ClipHeader = get_clip_header(*CompressedClipData);
	const uint32_t ACLBoneCount = ClipHeader.num_bones;
	TArray<uint16, TMemStackAllocator<>> TrackToAtomsMap;
	TrackToAtomsMap.Reserve(ACLBoneCount);
	TrackToAtomsMap.AddUninitialized(ACLBoneCount);
	std::fill(TrackToAtomsMap.GetData(), TrackToAtomsMap.GetData() + ACLBoneCount, 0xFFFF);

	const uint32 PairCount = DesiredPairs.Num();
	for (uint32 PairIndex = 0; PairIndex < PairCount; ++PairIndex)
	{
		const BoneTrackPair& Pair = DesiredPairs[PairIndex];
		TrackToAtomsMap[Pair.TrackIndex] = (uint16)Pair.AtomIndex;
	}

	uniformly_sampled::DecompressionContext<UE4SafeDecompressionSettings> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(Time, get_rounding_policy(Seq.Interpolation));

	UE4ScaleWriter PoseWriter(Atoms, TrackToAtomsMap);
	Context.decompress_pose(PoseWriter);
}
#endif

void AEFACLCompressionCodec_Custom::GetBoneAtom(FTransform& OutAtom, const UAnimSequence& Seq, int32 TrackIndex, float Time)
{
	using namespace acl;

	const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(Seq.CompressedByteStream.GetData());
	check(CompressedClipData->is_valid(false).empty());

	uniformly_sampled::DecompressionContext<UE4DebugDecompressionSettings> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(Time, get_rounding_policy(Seq.Interpolation));

	Quat_32 Rotation;
	Vector4_32 Translation;
	Vector4_32 Scale;
	Context.decompress_bone(TrackIndex, &Rotation, &Translation, &Scale);

	OutAtom = FTransform(QuatCast(Rotation), VectorCast(Translation), VectorCast(Scale));
}

#if USE_ANIMATION_CODEC_BATCH_SOLVER
void AEFACLCompressionCodec_Custom::GetPoseRotations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time)
{
	using namespace acl;

	const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(Seq.CompressedByteStream.GetData());
	check(CompressedClipData->is_valid(false).empty());

	const ClipHeader& ClipHeader = get_clip_header(*CompressedClipData);
	const uint32_t ACLBoneCount = ClipHeader.num_bones;
	TArray<uint16, TMemStackAllocator<>> TrackToAtomsMap;
	TrackToAtomsMap.Reserve(ACLBoneCount);
	TrackToAtomsMap.AddUninitialized(ACLBoneCount);
	std::fill(TrackToAtomsMap.GetData(), TrackToAtomsMap.GetData() + ACLBoneCount, 0xFFFF);

	const uint32 PairCount = DesiredPairs.Num();
	for (uint32 PairIndex = 0; PairIndex < PairCount; ++PairIndex)
	{
		const BoneTrackPair& Pair = DesiredPairs[PairIndex];
		TrackToAtomsMap[Pair.TrackIndex] = (uint16)Pair.AtomIndex;
	}

	uniformly_sampled::DecompressionContext<UE4DebugDecompressionSettings> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(Time, get_rounding_policy(Seq.Interpolation));

	UE4RotationWriter PoseWriter(Atoms, TrackToAtomsMap);
	Context.decompress_pose(PoseWriter);
}

void AEFACLCompressionCodec_Custom::GetPoseTranslations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time)
{
	using namespace acl;

	const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(Seq.CompressedByteStream.GetData());
	check(CompressedClipData->is_valid(false).empty());

	const ClipHeader& ClipHeader = get_clip_header(*CompressedClipData);
	const uint32_t ACLBoneCount = ClipHeader.num_bones;
	TArray<uint16, TMemStackAllocator<>> TrackToAtomsMap;
	TrackToAtomsMap.Reserve(ACLBoneCount);
	TrackToAtomsMap.AddUninitialized(ACLBoneCount);
	std::fill(TrackToAtomsMap.GetData(), TrackToAtomsMap.GetData() + ACLBoneCount, 0xFFFF);

	const uint32 PairCount = DesiredPairs.Num();
	for (uint32 PairIndex = 0; PairIndex < PairCount; ++PairIndex)
	{
		const BoneTrackPair& Pair = DesiredPairs[PairIndex];
		TrackToAtomsMap[Pair.TrackIndex] = (uint16)Pair.AtomIndex;
	}

	uniformly_sampled::DecompressionContext<UE4DebugDecompressionSettings> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(Time, get_rounding_policy(Seq.Interpolation));

	UE4TranslationWriter PoseWriter(Atoms, TrackToAtomsMap);
	Context.decompress_pose(PoseWriter);
}

void AEFACLCompressionCodec_Custom::GetPoseScales(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time)
{
	using namespace acl;

	const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(Seq.CompressedByteStream.GetData());
	check(CompressedClipData->is_valid(false).empty());

	const ClipHeader& ClipHeader = get_clip_header(*CompressedClipData);
	const uint32_t ACLBoneCount = ClipHeader.num_bones;
	TArray<uint16, TMemStackAllocator<>> TrackToAtomsMap;
	TrackToAtomsMap.Reserve(ACLBoneCount);
	TrackToAtomsMap.AddUninitialized(ACLBoneCount);
	std::fill(TrackToAtomsMap.GetData(), TrackToAtomsMap.GetData() + ACLBoneCount, 0xFFFF);

	const uint32 PairCount = DesiredPairs.Num();
	for (uint32 PairIndex = 0; PairIndex < PairCount; ++PairIndex)
	{
		const BoneTrackPair& Pair = DesiredPairs[PairIndex];
		TrackToAtomsMap[Pair.TrackIndex] = (uint16)Pair.AtomIndex;
	}

	uniformly_sampled::DecompressionContext<UE4DebugDecompressionSettings> Context;
	Context.initialize(*CompressedClipData);
	Context.seek(Time, get_rounding_policy(Seq.Interpolation));

	UE4ScaleWriter PoseWriter(Atoms, TrackToAtomsMap);
	Context.decompress_pose(PoseWriter);
}
#endif
