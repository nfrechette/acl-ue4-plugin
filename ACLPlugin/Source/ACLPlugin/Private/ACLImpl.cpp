// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "ACLImpl.h"

#if WITH_EDITOR
#include "AnimationCompression.h"
#include "AnimationUtils.h"
#include "Animation/AnimCompressionTypes.h"

acl::rotation_format8 GetRotationFormat(ACLRotationFormat Format)
{
	switch (Format)
	{
	default:
	case ACLRF_Quat_128:			return acl::rotation_format8::quatf_full;
	case ACLRF_QuatDropW_96:		return acl::rotation_format8::quatf_drop_w_full;
	case ACLRF_QuatDropW_Variable:	return acl::rotation_format8::quatf_drop_w_full;
	}
}

acl::vector_format8 GetVectorFormat(ACLVectorFormat Format)
{
	switch (Format)
	{
	default:
	case ACLVF_Vector3_96:			return acl::vector_format8::vector3f_full;
	case ACLVF_Vector3_Variable:	return acl::vector_format8::vector3f_variable;
	}
}

acl::compression_level8 GetCompressionLevel(ACLCompressionLevel Level)
{
	switch (Level)
	{
	default:
	case ACLCL_Lowest:	return acl::compression_level8::lowest;
	case ACLCL_Low:		return acl::compression_level8::low;
	case ACLCL_Medium:	return acl::compression_level8::medium;
	case ACLCL_High:	return acl::compression_level8::high;
	case ACLCL_Highest:	return acl::compression_level8::highest;
	}
}

static int32 FindAnimationTrackIndex(const FCompressibleAnimData& CompressibleAnimData, int32 BoneIndex)
{
	const TArray<FTrackToSkeletonMap>& TrackToSkelMap = CompressibleAnimData.TrackToSkeletonMapTable;
	if (BoneIndex != INDEX_NONE)
	{
		for (int32 TrackIndex = 0; TrackIndex < TrackToSkelMap.Num(); ++TrackIndex)
		{
			const FTrackToSkeletonMap& TrackToSkeleton = TrackToSkelMap[TrackIndex];
			if (TrackToSkeleton.BoneTreeIndex == BoneIndex)
				return TrackIndex;
		}
	}

	return INDEX_NONE;
}

static bool IsAdditiveBakedIntoRaw(const FCompressibleAnimData& CompressibleAnimData)
{
	if (!CompressibleAnimData.bIsValidAdditive)
	{
		return true;	// Sequences that aren't additive don't need baking as they are already baked
	}

	if (CompressibleAnimData.RawAnimationData.Num() == 0)
	{
		return true;	// Sequences with no raw data don't need baking and we can't tell if they are baked or not so assume that they are, it doesn't matter
	}

	if (CompressibleAnimData.AdditiveBaseAnimationData.Num() != 0)
	{
		return true;	// Sequence has raw data and additive base data, it is baked
	}

	return false;	// Sequence has raw data but no additive base data, it isn't baked
}

acl::track_array_qvvf BuildACLTransformTrackArray(ACLAllocator& AllocatorImpl, const FCompressibleAnimData& CompressibleAnimData, float DefaultVirtualVertexDistance, float SafeVirtualVertexDistance, bool bBuildAdditiveBase)
{
	const bool bIsAdditive = bBuildAdditiveBase ? false : CompressibleAnimData.bIsValidAdditive;
	const bool bIsAdditiveBakedIntoRaw = IsAdditiveBakedIntoRaw(CompressibleAnimData);
	check(!bIsAdditive || bIsAdditiveBakedIntoRaw);
	if (bIsAdditive && !bIsAdditiveBakedIntoRaw)
	{
		UE_LOG(LogAnimationCompression, Fatal, TEXT("Animation sequence is additive but it is not baked into the raw data, this is not supported."));
		return acl::track_array_qvvf();
	}

	// Ordinary non additive sequences only contain raw data returned by GetRawAnimationData().
	//
	// Additive sequences have their raw data baked with the raw additive base already taken out. At runtime,
	// the additive sequence data returned by GetRawAnimationData() gets applied on top of the
	// additive base sequence whose's data is returned by GetAdditiveBaseAnimationData(). In practice,
	// at runtime the additive base might also be compressed and thus there might be some added noise but
	// it typically isn't visible.
	// The baked additive data should have the same number of frames as the current sequence but if we only care about a specific frame,
	// we use it. When this happens, the additive base will contain that single frame repeated over and over: an animated static pose.
	// To avoid wasting memory, we just grab the first frame.

	const TArray<FRawAnimSequenceTrack>& RawTracks = bBuildAdditiveBase ? CompressibleAnimData.AdditiveBaseAnimationData : CompressibleAnimData.RawAnimationData;
	const uint32 NumSamples = CompressibleAnimData.NumFrames;
	const bool bIsStaticPose = NumSamples <= 1 || CompressibleAnimData.SequenceLength < 0.0001f;
	const float SampleRate = bIsStaticPose ? 30.0f : (float(CompressibleAnimData.NumFrames - 1) / CompressibleAnimData.SequenceLength);
	const int32 NumBones = CompressibleAnimData.BoneData.Num();

	// Additive animations have 0,0,0 scale as the default since we add it
	const FVector UE4DefaultScale(bIsAdditive ? 0.0f : 1.0f);
	const rtm::vector4f ACLDefaultScale = rtm::vector_set(bIsAdditive ? 0.0f : 1.0f);

	acl::track_array_qvvf Tracks(AllocatorImpl, NumBones);
	Tracks.set_name(acl::string(AllocatorImpl, TCHAR_TO_ANSI(*CompressibleAnimData.FullName)));

	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FBoneData& UE4Bone = CompressibleAnimData.BoneData[BoneIndex];

		acl::track_desc_transformf Desc;

		// We use a higher virtual vertex distance when bones have a socket attached or are keyed end effectors (IK, hand, camera, etc)
		Desc.shell_distance = (UE4Bone.bHasSocket || UE4Bone.bKeyEndEffector) ? SafeVirtualVertexDistance : DefaultVirtualVertexDistance;

		const int32 ParentBoneIndex = UE4Bone.GetParent();
		Desc.parent_index = ParentBoneIndex >= 0 ? ParentBoneIndex : acl::k_invalid_track_index;

		const int32 TrackIndex = FindAnimationTrackIndex(CompressibleAnimData, BoneIndex);

		// We output bone data in UE4 track order. If a track isn't present, we will use the bind pose and strip it from the
		// compressed stream.
		Desc.output_index = TrackIndex >= 0 ? TrackIndex : acl::k_invalid_track_index;

		acl::track_qvvf Track = acl::track_qvvf::make_reserve(Desc, AllocatorImpl, NumSamples, SampleRate);
		Track.set_name(acl::string(AllocatorImpl, TCHAR_TO_ANSI(*UE4Bone.Name.ToString())));

		if (TrackIndex >= 0)
		{
			// We have a track for this bone, use it
			const FRawAnimSequenceTrack& RawTrack = RawTracks[TrackIndex];

			for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
			{
				const FQuat& RotationSample = RawTrack.RotKeys.Num() == 1 ? RawTrack.RotKeys[0] : RawTrack.RotKeys[SampleIndex];
				Track[SampleIndex].rotation = QuatCast(RotationSample);


				const FVector& TranslationSample = RawTrack.PosKeys.Num() == 1 ? RawTrack.PosKeys[0] : RawTrack.PosKeys[SampleIndex];
				Track[SampleIndex].translation = VectorCast(TranslationSample);

				const FVector& ScaleSample = RawTrack.ScaleKeys.Num() == 0 ? UE4DefaultScale : (RawTrack.ScaleKeys.Num() == 1 ? RawTrack.ScaleKeys[0] : RawTrack.ScaleKeys[SampleIndex]);
				Track[SampleIndex].scale = VectorCast(ScaleSample);
			}
		}
		else
		{
			// No track data for this bone, it must be new. Use the bind pose instead
			const rtm::qvvf BindTransform = rtm::qvv_set(QuatCast(UE4Bone.Orientation), VectorCast(UE4Bone.Position), ACLDefaultScale);

			for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
				Track[SampleIndex] = BindTransform;
		}

		Tracks[BoneIndex] = MoveTemp(Track);
	}

	return Tracks;
}
#endif	// WITH_EDITOR
