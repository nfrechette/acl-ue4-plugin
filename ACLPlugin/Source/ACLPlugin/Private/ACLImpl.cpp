// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "ACLImpl.h"

#if WITH_EDITOR
#include "AnimationCompression.h"
#include "AnimationUtils.h"
#include "Animation/AnimCompressionTypes.h"

acl::RotationFormat8 GetRotationFormat(ACLRotationFormat Format)
{
	switch (Format)
	{
	default:
	case ACLRF_Quat_128:			return acl::RotationFormat8::Quat_128;
	case ACLRF_QuatDropW_96:		return acl::RotationFormat8::QuatDropW_96;
	case ACLRF_QuatDropW_Variable:	return acl::RotationFormat8::QuatDropW_Variable;
	}
}

acl::VectorFormat8 GetVectorFormat(ACLVectorFormat Format)
{
	switch (Format)
	{
	default:
	case ACLVF_Vector3_96:			return acl::VectorFormat8::Vector3_96;
	case ACLVF_Vector3_Variable:	return acl::VectorFormat8::Vector3_Variable;
	}
}

acl::CompressionLevel8 GetCompressionLevel(ACLCompressionLevel Level)
{
	switch (Level)
	{
	default:
	case ACLCL_Lowest:	return acl::CompressionLevel8::Lowest;
	case ACLCL_Low:		return acl::CompressionLevel8::Low;
	case ACLCL_Medium:	return acl::CompressionLevel8::Medium;
	case ACLCL_High:	return acl::CompressionLevel8::High;
	case ACLCL_Highest:	return acl::CompressionLevel8::Highest;
	}
}

TUniquePtr<acl::RigidSkeleton> BuildACLSkeleton(ACLAllocator& AllocatorImpl, const FCompressibleAnimData& CompressibleAnimData, float DefaultVirtualVertexDistance, float SafeVirtualVertexDistance)
{
	using namespace acl;

	const int32 NumBones = CompressibleAnimData.BoneData.Num();

	TArray<RigidBone> ACLSkeletonBones;
	ACLSkeletonBones.Empty(NumBones);
	ACLSkeletonBones.AddDefaulted(NumBones);

	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FBoneData& UE4Bone = CompressibleAnimData.BoneData[BoneIndex];
		RigidBone& ACLBone = ACLSkeletonBones[BoneIndex];
		ACLBone.name = String(AllocatorImpl, TCHAR_TO_ANSI(*UE4Bone.Name.ToString()));
		ACLBone.bind_transform = transform_cast(transform_set(quat_normalize(QuatCast(UE4Bone.Orientation)), VectorCast(UE4Bone.Position), vector_set(1.0f)));

		// We use a higher virtual vertex distance when bones have a socket attached or are keyed end effectors (IK, hand, camera, etc)
		ACLBone.vertex_distance = (UE4Bone.bHasSocket || UE4Bone.bKeyEndEffector) ? SafeVirtualVertexDistance : DefaultVirtualVertexDistance;

		const int32 ParentBoneIndex = UE4Bone.GetParent();
		ACLBone.parent_index = ParentBoneIndex >= 0 ? safe_static_cast<uint16_t>(ParentBoneIndex) : acl::k_invalid_bone_index;
	}

	return MakeUnique<RigidSkeleton>(AllocatorImpl, ACLSkeletonBones.GetData(), NumBones);
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

TUniquePtr<acl::AnimationClip> BuildACLClip(ACLAllocator& AllocatorImpl, const FCompressibleAnimData& CompressibleAnimData, const acl::RigidSkeleton& ACLSkeleton, bool bBuildAdditiveBase)
{
	using namespace acl;

	const bool bIsAdditive = bBuildAdditiveBase ? false : CompressibleAnimData.bIsValidAdditive;
	const bool bIsAdditiveBakedIntoRaw = IsAdditiveBakedIntoRaw(CompressibleAnimData);
	check(!bIsAdditive || bIsAdditiveBakedIntoRaw);
	if (bIsAdditive && !bIsAdditiveBakedIntoRaw)
	{
		UE_LOG(LogAnimationCompression, Fatal, TEXT("Animation sequence is additive but it is not baked into the raw data, this is not supported."));
		return TUniquePtr<acl::AnimationClip>();
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
	const String ClipName(AllocatorImpl, TCHAR_TO_ANSI(*CompressibleAnimData.FullName));

	// Additive animations have 0,0,0 scale as the default since we add it
	const FVector UE4DefaultScale(bIsAdditive ? 0.0f : 1.0f);
	const Vector4_64 ACLDefaultScale = vector_set(bIsAdditive ? 0.0 : 1.0);

	TUniquePtr<AnimationClip> ACLClip = MakeUnique<AnimationClip>(AllocatorImpl, ACLSkeleton, NumSamples, SampleRate, ClipName);

	AnimatedBone* ACLBones = ACLClip->get_bones();
	const uint16 NumBones = ACLSkeleton.get_num_bones();
	for (uint16 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const int32 TrackIndex = FindAnimationTrackIndex(CompressibleAnimData, BoneIndex);

		AnimatedBone& ACLBone = ACLBones[BoneIndex];

		// We output bone data in UE4 track order. If a track isn't present, we will use the bind pose and strip it from the
		// compressed stream.
		ACLBone.output_index = TrackIndex >= 0 ? TrackIndex : -1;

		if (TrackIndex >= 0)
		{
			// We have a track for this bone, use it
			const FRawAnimSequenceTrack& RawTrack = RawTracks[TrackIndex];

			for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
			{
				const FQuat& RotationSample = RawTrack.RotKeys.Num() == 1 ? RawTrack.RotKeys[0] : RawTrack.RotKeys[SampleIndex];
				ACLBone.rotation_track.set_sample(SampleIndex, quat_normalize(quat_cast(QuatCast(RotationSample))));

				const FVector& TranslationSample = RawTrack.PosKeys.Num() == 1 ? RawTrack.PosKeys[0] : RawTrack.PosKeys[SampleIndex];
				ACLBone.translation_track.set_sample(SampleIndex, vector_cast(VectorCast(TranslationSample)));

				const FVector& ScaleSample = RawTrack.ScaleKeys.Num() == 0 ? UE4DefaultScale : (RawTrack.ScaleKeys.Num() == 1 ? RawTrack.ScaleKeys[0] : RawTrack.ScaleKeys[SampleIndex]);
				ACLBone.scale_track.set_sample(SampleIndex, vector_cast(VectorCast(ScaleSample)));
			}
		}
		else
		{
			// No track data for this bone, it must be new. Use the bind pose instead
			const RigidBone& ACLRigidBone = ACLSkeleton.get_bone(BoneIndex);

			for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
			{
				ACLBone.rotation_track.set_sample(SampleIndex, ACLRigidBone.bind_transform.rotation);
				ACLBone.translation_track.set_sample(SampleIndex, ACLRigidBone.bind_transform.translation);
				ACLBone.scale_track.set_sample(SampleIndex, ACLDefaultScale);
			}
		}
	}

	return ACLClip;
}
#endif	// WITH_EDITOR
