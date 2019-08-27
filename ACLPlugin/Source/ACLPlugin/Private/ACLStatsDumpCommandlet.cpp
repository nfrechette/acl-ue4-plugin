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

#include "ACLStatsDumpCommandlet.h"

#if WITH_EDITOR
#include "Runtime/Core/Public/HAL/FileManagerGeneric.h"
#include "Runtime/Core/Public/HAL/PlatformTime.h"
#include "Runtime/CoreUObject/Public/UObject/UObjectIterator.h"
#include "Runtime/Engine/Classes/Animation/AnimCompress_Automatic.h"
#include "Runtime/Engine/Classes/Animation/AnimCompress_RemoveLinearKeys.h"
#include "Runtime/Engine/Classes/Animation/Skeleton.h"
#include "Runtime/Engine/Public/AnimationCompression.h"
#include "Runtime/Engine/Public/AnimEncoding.h"
#include "Editor/UnrealEd/Public/PackageHelperFunctions.h"

#include "AnimCompress_ACL.h"
#include "ACLImpl.h"

#include <sjson/parser.h>
#include <sjson/writer.h>

#include <acl/compression/animation_clip.h>
#include <acl/compression/skeleton_error_metric.h>
#include <acl/io/clip_reader.h>
#include <acl/math/quat_32.h>
#include <acl/math/transform_32.h>


// Commandlet example inspired by: https://github.com/ue4plugins/CommandletPlugin
// To run the commandlet, add to the commandline: "$(SolutionDir)$(ProjectName).uproject" -run=/Script/ACLPlugin.ACLStatsDump "-acl=<path/to/raw/acl/sjson/files/directory>" "-stats=<path/to/output/stats/directory>"

class UE4SJSONStreamWriter final : public sjson::StreamWriter
{
public:
	UE4SJSONStreamWriter(FArchive* File_)
		: File(File_)
	{}

	virtual void write(const void* Buffer, size_t BufferSize) override
	{
		File->Serialize(const_cast<void*>(Buffer), BufferSize);
	}

private:
	FArchive* File;
};

static const TCHAR* ReadACLClip(FFileManagerGeneric& FileManager, const FString& ACLClipPath, acl::IAllocator& Allocator, std::unique_ptr<acl::AnimationClip, acl::Deleter<acl::AnimationClip>>& OutACLClip, std::unique_ptr<acl::RigidSkeleton, acl::Deleter<acl::RigidSkeleton>>& OutACLSkeleton)
{
	FArchive* Reader = FileManager.CreateFileReader(*ACLClipPath);
	const int64 Size = Reader->TotalSize();

	// Allocate directly without a TArray to automatically manage the memory because some
	// clips are larger than 2 GB
	char* RawSJSONData = static_cast<char*>(GMalloc->Malloc(Size));

	Reader->Serialize(RawSJSONData, Size);
	Reader->Close();

	acl::ClipReader ClipReader(Allocator, RawSJSONData, Size);

	if (!ClipReader.read_skeleton(OutACLSkeleton))
	{
		GMalloc->Free(RawSJSONData);
		return TEXT("Failed to read ACL RigidSkeleton from file");
	}

	if (!ClipReader.read_clip(OutACLClip, *OutACLSkeleton))
	{
		GMalloc->Free(RawSJSONData);
		return TEXT("Failed to read ACL AnimationClip from file");
	}

	GMalloc->Free(RawSJSONData);
	return nullptr;
}

static void ConvertSkeleton(const acl::RigidSkeleton& ACLSkeleton, USkeleton* UE4Skeleton)
{
	// Not terribly clean, we cast away the 'const' to modify the skeleton
	FReferenceSkeleton& RefSkeleton = const_cast<FReferenceSkeleton&>(UE4Skeleton->GetReferenceSkeleton());
	FReferenceSkeletonModifier SkeletonModifier(RefSkeleton, UE4Skeleton);

	uint16 NumBones = ACLSkeleton.get_num_bones();
	for (uint16 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const acl::RigidBone& ACLBone = ACLSkeleton.get_bone(BoneIndex);

		FMeshBoneInfo UE4Bone;
		UE4Bone.Name = FName(ACLBone.name.c_str());
		UE4Bone.ParentIndex = ACLBone.is_root() ? INDEX_NONE : ACLBone.parent_index;
		UE4Bone.ExportName = ANSI_TO_TCHAR(ACLBone.name.c_str());

		FQuat rotation = QuatCast(acl::quat_normalize(acl::quat_cast(ACLBone.bind_transform.rotation)));
		FVector translation = VectorCast(acl::vector_cast(ACLBone.bind_transform.translation));
		FVector scale = VectorCast(acl::vector_cast(ACLBone.bind_transform.scale));

		FTransform BoneTransform(rotation, translation, scale);
		SkeletonModifier.Add(UE4Bone, BoneTransform);
	}

	// When our modifier is destroyed here, it will rebuild the skeleton
}

static void ConvertClip(const acl::AnimationClip& ACLClip, const acl::RigidSkeleton& ACLSkeleton, UAnimSequence* UE4Clip, USkeleton* UE4Skeleton)
{
	UE4Clip->SequenceLength = FGenericPlatformMath::Max<float>(ACLClip.get_duration(), MINIMUM_ANIMATION_LENGTH);
	UE4Clip->SetRawNumberOfFrame(ACLClip.get_num_samples());
	UE4Clip->SetSkeleton(UE4Skeleton);

	const uint16 NumBones = ACLSkeleton.get_num_bones();
	for (uint16 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const acl::RigidBone& ACLBone = ACLSkeleton.get_bone(BoneIndex);
		const acl::AnimatedBone& Bone = ACLClip.get_animated_bone(BoneIndex);

		FRawAnimSequenceTrack RawTrack;
		RawTrack.PosKeys.Empty();
		RawTrack.RotKeys.Empty();
		RawTrack.ScaleKeys.Empty();

		const uint32 NumRotationSamples = Bone.rotation_track.get_num_samples();
		for (uint32 SampleIndex = 0; SampleIndex < NumRotationSamples; ++SampleIndex)
		{
			const FQuat Rotation = QuatCast(acl::quat_normalize(acl::quat_cast(Bone.rotation_track.get_sample(SampleIndex))));
			RawTrack.RotKeys.Add(Rotation);
		}

		const uint32 NumTranslationSamples = Bone.translation_track.get_num_samples();
		for (uint32 SampleIndex = 0; SampleIndex < NumTranslationSamples; ++SampleIndex)
		{
			const FVector Translation = VectorCast(acl::vector_cast(Bone.translation_track.get_sample(SampleIndex)));
			RawTrack.PosKeys.Add(Translation);
		}

		const uint32 NumScaleSamples = Bone.scale_track.get_num_samples();
		for (uint32 SampleIndex = 0; SampleIndex < NumScaleSamples; ++SampleIndex)
		{
			const FVector Scale = VectorCast(acl::vector_cast(Bone.scale_track.get_sample(SampleIndex)));
			RawTrack.ScaleKeys.Add(Scale);
		}

		if (NumRotationSamples != 0 || NumTranslationSamples != 0 || NumScaleSamples != 0)
		{
			const FName BoneName(ACLBone.name.c_str());
			UE4Clip->AddNewRawTrack(BoneName, &RawTrack);
		}
	}

	UE4Clip->MarkRawDataAsModified();
	UE4Clip->PostProcessSequence();
}

static int32 GetAnimationTrackIndex(const int32 BoneIndex, const UAnimSequence* AnimSeq)
{
	if (BoneIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	const TArray<FTrackToSkeletonMap>& TrackToSkelMap = AnimSeq->CompressedData.CompressedTrackToSkeletonMapTable;
	for (int32 TrackIndex = 0; TrackIndex < TrackToSkelMap.Num(); ++TrackIndex)
	{
		const FTrackToSkeletonMap& TrackToSkeleton = TrackToSkelMap[TrackIndex];
		if (TrackToSkeleton.BoneTreeIndex == BoneIndex)
		{
			return TrackIndex;
		}
	}

	return INDEX_NONE;
}

static void SampleUE4Clip(const acl::RigidSkeleton& ACLSkeleton, USkeleton* UE4Skeleton, const UAnimSequence* UE4Clip, float SampleTime, acl::Transform_32* LossyPoseTransforms)
{
	const FReferenceSkeleton& RefSkeleton = UE4Skeleton->GetReferenceSkeleton();
	const TArray<FTransform>& RefSkeletonPose = UE4Skeleton->GetRefLocalPoses();

	const uint16 NumBones = ACLSkeleton.get_num_bones();
	for (uint16 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const acl::RigidBone& ACLBone = ACLSkeleton.get_bone(BoneIndex);
		const FName BoneName(ACLBone.name.c_str());
		const int32 BoneTreeIndex = RefSkeleton.FindBoneIndex(BoneName);
		const int32 BoneTrackIndex = GetAnimationTrackIndex(BoneTreeIndex, UE4Clip);

		FTransform BoneTransform;
		if (BoneTrackIndex != INDEX_NONE)
		{
			UE4Clip->GetBoneTransform(BoneTransform, BoneTrackIndex, SampleTime, false);
		}
		else
		{
			BoneTransform = RefSkeletonPose[BoneTreeIndex];
		}

		const acl::Quat_32 Rotation = QuatCast(BoneTransform.GetRotation());
		const acl::Vector4_32 Translation = VectorCast(BoneTransform.GetTranslation());
		const acl::Vector4_32 Scale = VectorCast(BoneTransform.GetScale3D());
		LossyPoseTransforms[BoneIndex] = acl::transform_set(Rotation, Translation, Scale);
	}
}

static bool UE4ClipHasScale(const UAnimSequence* UE4Clip)
{
	const TArray<FRawAnimSequenceTrack>& Tracks = UE4Clip->GetRawAnimationData();
	for (const FRawAnimSequenceTrack& track : Tracks)
	{
		if (track.ScaleKeys.Num() != 0)
			return true;
	}

	return false;
}

static void CalculateClipError(const acl::AnimationClip& ACLClip, const acl::RigidSkeleton& ACLSkeleton, const UAnimSequence* UE4Clip, USkeleton* UE4Skeleton, uint16_t& OutWorstBone, float& OutMaxError, float& OutWorstSampleTime)
{
	using namespace acl;

	const uint16 NumBones = ACLClip.get_num_bones();
	const float ClipDuration = ACLClip.get_duration();
	const float SampleRate = ACLClip.get_sample_rate();
	const uint32 NumSamples = ACLClip.get_num_samples();
	const bool HasScale = UE4ClipHasScale(UE4Clip);

	TArray<Transform_32> RawPoseTransforms;
	TArray<Transform_32> LossyPoseTransforms;
	RawPoseTransforms.AddUninitialized(NumBones);
	LossyPoseTransforms.AddUninitialized(NumBones);

	uint16 WorstBone = acl::k_invalid_bone_index;
	float MaxError = 0.0f;
	float WorstSampleTime = 0.0f;

	const TransformErrorMetric ErrorMetric;

	for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
	{
		// Sample our streams and calculate the error
		const float SampleTime = min(float(SampleIndex) / SampleRate, ClipDuration);

		ACLClip.sample_pose(SampleTime, SampleRoundingPolicy::None, RawPoseTransforms.GetData(), NumBones);
		SampleUE4Clip(ACLSkeleton, UE4Skeleton, UE4Clip, SampleTime, LossyPoseTransforms.GetData());

		for (uint16 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			float Error;
			if (HasScale)
				Error = ErrorMetric.calculate_object_bone_error(ACLSkeleton, RawPoseTransforms.GetData(), nullptr, LossyPoseTransforms.GetData(), BoneIndex);
			else
				Error = ErrorMetric.calculate_object_bone_error_no_scale(ACLSkeleton, RawPoseTransforms.GetData(), nullptr, LossyPoseTransforms.GetData(), BoneIndex);

			if (Error > MaxError)
			{
				MaxError = Error;
				WorstBone = BoneIndex;
				WorstSampleTime = SampleTime;
			}
		}
	}

	OutWorstBone = WorstBone;
	OutMaxError = MaxError;
	OutWorstSampleTime = WorstSampleTime;
}

static void DumpClipDetailedError(const acl::AnimationClip& ACLClip, const acl::RigidSkeleton& ACLSkeleton, UAnimSequence* UE4Clip, USkeleton* UE4Skeleton, sjson::ObjectWriter& Writer)
{
	using namespace acl;

	const uint16 NumBones = ACLClip.get_num_bones();
	const float ClipDuration = ACLClip.get_duration();
	const float SampleRate = ACLClip.get_sample_rate();
	const uint32 NumSamples = ACLClip.get_num_samples();
	const bool HasScale = UE4ClipHasScale(UE4Clip);

	TArray<Transform_32> RawPoseTransforms;
	TArray<Transform_32> LossyPoseTransforms;
	RawPoseTransforms.AddUninitialized(NumBones);
	LossyPoseTransforms.AddUninitialized(NumBones);

	const TransformErrorMetric ErrorMetric;

	Writer["error_per_frame_and_bone"] = [&](sjson::ArrayWriter& Writer)
	{
		for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
		{
			// Sample our streams and calculate the error
			const float SampleTime = min(float(SampleIndex) / SampleRate, ClipDuration);

			ACLClip.sample_pose(SampleTime, SampleRoundingPolicy::None, RawPoseTransforms.GetData(), NumBones);
			SampleUE4Clip(ACLSkeleton, UE4Skeleton, UE4Clip, SampleTime, LossyPoseTransforms.GetData());

			Writer.push_newline();
			Writer.push([&](sjson::ArrayWriter& Writer)
			{
				for (uint16 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
				{
					float Error;
					if (HasScale)
						Error = ErrorMetric.calculate_object_bone_error(ACLSkeleton, RawPoseTransforms.GetData(), nullptr, LossyPoseTransforms.GetData(), BoneIndex);
					else
						Error = ErrorMetric.calculate_object_bone_error_no_scale(ACLSkeleton, RawPoseTransforms.GetData(), nullptr, LossyPoseTransforms.GetData(), BoneIndex);

					Writer.push(Error);
				}
			});
		}
	};
}

struct FCompressionContext
{
	UAnimCompress_Automatic* AutoCompressor;
	UAnimCompress_ACL* ACLCompressor;
	UAnimCompress_RemoveLinearKeys* KeyReductionCompressor;

	const UEnum* AnimFormatEnum;

	UAnimSequence* UE4Clip;
	USkeleton* UE4Skeleton;

	TUniquePtr<acl::AnimationClip> ACLClip;
	TUniquePtr<acl::RigidSkeleton> ACLSkeleton;

	uint32 ACLRawSize;
	int32 UE4RawSize;
};

static void CompressWithUE4Auto(FCompressionContext& Context, bool PerformExhaustiveDump, sjson::Writer& Writer)
{
	// Force recompression and avoid the DDC
	TGuardValue<int32> CompressGuard(Context.UE4Clip->CompressCommandletVersion, INDEX_NONE);

	const uint64 UE4StartTimeCycles = FPlatformTime::Cycles64();

	Context.UE4Clip->CompressionScheme = Context.AutoCompressor;
	Context.UE4Clip->RequestSyncAnimRecompression();

	const uint64 UE4EndTimeCycles = FPlatformTime::Cycles64();

	const uint64 UE4ElapsedCycles = UE4EndTimeCycles - UE4StartTimeCycles;
	const double UE4ElapsedTimeSec = FPlatformTime::ToSeconds64(UE4ElapsedCycles);

	if (Context.UE4Clip->IsCompressedDataValid())
	{
		FCompressibleAnimData CompressibleData(Context.UE4Clip, false);

		FCompressibleAnimDataResult CompressibleResult;
		CompressibleResult.CompressionScheme = Context.UE4Clip->CompressionScheme;
		CompressibleResult.CompressedNumberOfFrames = Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedNumberOfFrames;
		CompressibleResult.CompressedScaleOffsets.StripSize = Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedScaleOffsets.StripSize;
		CompressibleResult.CompressedScaleOffsets.OffsetData.AddUninitialized(Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedScaleOffsets.OffsetData.Num());
		FMemory::Memcpy(CompressibleResult.CompressedScaleOffsets.OffsetData.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedScaleOffsets.OffsetData.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedScaleOffsets.OffsetData.Num() * sizeof(int32));
		CompressibleResult.CompressedTrackOffsets.AddUninitialized(Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedTrackOffsets.Num());
		FMemory::Memcpy(CompressibleResult.CompressedTrackOffsets.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedTrackOffsets.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedTrackOffsets.Num() * sizeof(int32));
		CompressibleResult.CompressedByteStream.AddUninitialized(Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedByteStream.Num());
		FMemory::Memcpy(CompressibleResult.CompressedByteStream.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedByteStream.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedByteStream.Num());
		CompressibleResult.KeyEncodingFormat = Context.UE4Clip->CompressedData.CompressedDataStructure.KeyEncodingFormat;
		CompressibleResult.RotationCompressionFormat = Context.UE4Clip->CompressedData.CompressedDataStructure.RotationCompressionFormat;
		CompressibleResult.TranslationCompressionFormat = Context.UE4Clip->CompressedData.CompressedDataStructure.TranslationCompressionFormat;
		CompressibleResult.ScaleCompressionFormat = Context.UE4Clip->CompressedData.CompressedDataStructure.ScaleCompressionFormat;
		CompressibleResult.RotationCodec = Context.UE4Clip->CompressedData.CompressedDataStructure.RotationCodec;
		CompressibleResult.TranslationCodec = Context.UE4Clip->CompressedData.CompressedDataStructure.TranslationCodec;
		CompressibleResult.ScaleCodec = Context.UE4Clip->CompressedData.CompressedDataStructure.ScaleCodec;

		AnimationErrorStats UE4ErrorStats;
		FAnimationUtils::ComputeCompressionError(CompressibleData, CompressibleResult, UE4ErrorStats);

		uint16 WorstBone;
		float MaxError;
		float WorstSampleTime;
		CalculateClipError(*Context.ACLClip, *Context.ACLSkeleton, Context.UE4Clip, Context.UE4Skeleton, WorstBone, MaxError, WorstSampleTime);

		const int32 CompressedSize = Context.UE4Clip->GetApproxCompressedSize();
		const double UE4CompressionRatio = double(Context.UE4RawSize) / double(CompressedSize);
		const double ACLCompressionRatio = double(Context.ACLRawSize) / double(CompressedSize);

		Writer["ue4_auto"] = [&](sjson::ObjectWriter& Writer)
		{
			Writer["algorithm_name"] = TCHAR_TO_ANSI(*Context.UE4Clip->CompressionScheme->GetClass()->GetName());
			Writer["codec_name"] = TCHAR_TO_ANSI(*FAnimationUtils::GetAnimationKeyFormatString(Context.UE4Clip->CompressedData.CompressedDataStructure.KeyEncodingFormat));
			Writer["compressed_size"] = CompressedSize;
			Writer["ue4_compression_ratio"] = UE4CompressionRatio;
			Writer["acl_compression_ratio"] = ACLCompressionRatio;
			Writer["compression_time"] = UE4ElapsedTimeSec;
			Writer["ue4_max_error"] = UE4ErrorStats.MaxError;
			Writer["ue4_avg_error"] = UE4ErrorStats.AverageError;
			Writer["ue4_worst_bone"] = UE4ErrorStats.MaxErrorBone;
			Writer["ue4_worst_time"] = UE4ErrorStats.MaxErrorTime;
			Writer["acl_max_error"] = MaxError;
			Writer["acl_worst_bone"] = WorstBone;
			Writer["acl_worst_time"] = WorstSampleTime;
			Writer["rotation_format"] = TCHAR_TO_ANSI(*Context.AnimFormatEnum->GetDisplayNameTextByIndex(Context.UE4Clip->CompressedData.CompressedDataStructure.RotationCompressionFormat).ToString());
			Writer["translation_format"] = TCHAR_TO_ANSI(*Context.AnimFormatEnum->GetDisplayNameTextByIndex(Context.UE4Clip->CompressedData.CompressedDataStructure.TranslationCompressionFormat).ToString());
			Writer["scale_format"] = TCHAR_TO_ANSI(*Context.AnimFormatEnum->GetDisplayNameTextByIndex(Context.UE4Clip->CompressedData.CompressedDataStructure.ScaleCompressionFormat).ToString());

			if (PerformExhaustiveDump)
			{
				DumpClipDetailedError(*Context.ACLClip, *Context.ACLSkeleton, Context.UE4Clip, Context.UE4Skeleton, Writer);
			}
		};
	}
	else
	{
		Writer["error"] = "failed to compress UE4 clip";
	}
}

static void CompressWithACL(FCompressionContext& Context, bool PerformExhaustiveDump, sjson::Writer& Writer)
{
	// Force recompression and avoid the DDC
	TGuardValue<int32> CompressGuard(Context.UE4Clip->CompressCommandletVersion, INDEX_NONE);

	const uint64 ACLStartTimeCycles = FPlatformTime::Cycles64();

	Context.UE4Clip->CompressionScheme = Context.ACLCompressor;
	Context.UE4Clip->RequestSyncAnimRecompression();

	const uint64 ACLEndTimeCycles = FPlatformTime::Cycles64();

	const uint64 ACLElapsedCycles = ACLEndTimeCycles - ACLStartTimeCycles;
	const double ACLElapsedTimeSec = FPlatformTime::ToSeconds64(ACLElapsedCycles);

	if (Context.UE4Clip->IsCompressedDataValid())
	{
		FCompressibleAnimData CompressibleData(Context.UE4Clip, false);

		FCompressibleAnimDataResult CompressibleResult;
		CompressibleResult.CompressionScheme = Context.UE4Clip->CompressionScheme;
		CompressibleResult.CompressedNumberOfFrames = Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedNumberOfFrames;
		CompressibleResult.CompressedScaleOffsets.StripSize = Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedScaleOffsets.StripSize;
		CompressibleResult.CompressedScaleOffsets.OffsetData.AddUninitialized(Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedScaleOffsets.OffsetData.Num());
		FMemory::Memcpy(CompressibleResult.CompressedScaleOffsets.OffsetData.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedScaleOffsets.OffsetData.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedScaleOffsets.OffsetData.Num() * sizeof(int32));
		CompressibleResult.CompressedTrackOffsets.AddUninitialized(Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedTrackOffsets.Num());
		FMemory::Memcpy(CompressibleResult.CompressedTrackOffsets.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedTrackOffsets.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedTrackOffsets.Num() * sizeof(int32));
		CompressibleResult.CompressedByteStream.AddUninitialized(Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedByteStream.Num());
		FMemory::Memcpy(CompressibleResult.CompressedByteStream.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedByteStream.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedByteStream.Num());
		CompressibleResult.KeyEncodingFormat = Context.UE4Clip->CompressedData.CompressedDataStructure.KeyEncodingFormat;
		CompressibleResult.RotationCompressionFormat = Context.UE4Clip->CompressedData.CompressedDataStructure.RotationCompressionFormat;
		CompressibleResult.TranslationCompressionFormat = Context.UE4Clip->CompressedData.CompressedDataStructure.TranslationCompressionFormat;
		CompressibleResult.ScaleCompressionFormat = Context.UE4Clip->CompressedData.CompressedDataStructure.ScaleCompressionFormat;
		CompressibleResult.RotationCodec = Context.UE4Clip->CompressedData.CompressedDataStructure.RotationCodec;
		CompressibleResult.TranslationCodec = Context.UE4Clip->CompressedData.CompressedDataStructure.TranslationCodec;
		CompressibleResult.ScaleCodec = Context.UE4Clip->CompressedData.CompressedDataStructure.ScaleCodec;

		AnimationErrorStats UE4ErrorStats;
		FAnimationUtils::ComputeCompressionError(CompressibleData, CompressibleResult, UE4ErrorStats);

		uint16 WorstBone;
		float MaxError;
		float WorstSampleTime;
		CalculateClipError(*Context.ACLClip, *Context.ACLSkeleton, Context.UE4Clip, Context.UE4Skeleton, WorstBone, MaxError, WorstSampleTime);

		const int32 CompressedSize = Context.UE4Clip->GetApproxCompressedSize();
		const double UE4CompressionRatio = double(Context.UE4RawSize) / double(CompressedSize);
		const double ACLCompressionRatio = double(Context.ACLRawSize) / double(CompressedSize);

		Writer["ue4_acl"] = [&](sjson::ObjectWriter& Writer)
		{
			Writer["algorithm_name"] = TCHAR_TO_ANSI(*Context.UE4Clip->CompressionScheme->GetClass()->GetName());
			Writer["codec_name"] = TCHAR_TO_ANSI(*FAnimationUtils::GetAnimationKeyFormatString(Context.UE4Clip->CompressedData.CompressedDataStructure.KeyEncodingFormat));
			Writer["compressed_size"] = CompressedSize;
			Writer["ue4_compression_ratio"] = UE4CompressionRatio;
			Writer["acl_compression_ratio"] = ACLCompressionRatio;
			Writer["compression_time"] = ACLElapsedTimeSec;
			Writer["ue4_max_error"] = UE4ErrorStats.MaxError;
			Writer["ue4_avg_error"] = UE4ErrorStats.AverageError;
			Writer["ue4_worst_bone"] = UE4ErrorStats.MaxErrorBone;
			Writer["ue4_worst_time"] = UE4ErrorStats.MaxErrorTime;
			Writer["acl_max_error"] = MaxError;
			Writer["acl_worst_bone"] = WorstBone;
			Writer["acl_worst_time"] = WorstSampleTime;

			if (PerformExhaustiveDump)
			{
				DumpClipDetailedError(*Context.ACLClip, *Context.ACLSkeleton, Context.UE4Clip, Context.UE4Skeleton, Writer);
			}
		};
	}
	else
	{
		Writer["error"] = "failed to compress UE4 clip";
	}
}

static bool IsKeyDropped(int32 NumFrames, const uint8* FrameTable, int32 NumKeys, float FrameRate, float SampleTime)
{
	if (NumFrames > 0xFF)
	{
		const uint16* Frames = (const uint16*)FrameTable;
		for (int32 KeyIndex = 0; KeyIndex < NumKeys; ++KeyIndex)
		{
			const float FrameTime = Frames[KeyIndex] / FrameRate;
			if (FMath::IsNearlyEqual(FrameTime, SampleTime, 0.001f))
			{
				return false;
			}
		}
		return true;
	}
	else
	{
		const uint8* Frames = (const uint8*)FrameTable;
		for (int32 KeyIndex = 0; KeyIndex < NumKeys; ++KeyIndex)
		{
			const float FrameTime = Frames[KeyIndex] / FrameRate;
			if (FMath::IsNearlyEqual(FrameTime, SampleTime, 0.001f))
			{
				return false;
			}
		}
		return true;
	}
}

static void CompressWithUE4KeyReduction(FCompressionContext& Context, bool PerformExhaustiveDump, sjson::Writer& Writer)
{
	if (Context.UE4Clip->GetRawNumberOfFrames() <= 1)
	{
		return;
	}

	const uint64 UE4StartTimeCycles = FPlatformTime::Cycles64();

	Context.UE4Clip->CompressionScheme = Context.KeyReductionCompressor;
	Context.UE4Clip->RequestSyncAnimRecompression();

	const uint64 UE4EndTimeCycles = FPlatformTime::Cycles64();

	const uint64 UE4ElapsedCycles = UE4EndTimeCycles - UE4StartTimeCycles;
	const double UE4ElapsedTimeSec = FPlatformTime::ToSeconds64(UE4ElapsedCycles);

	if (Context.UE4Clip->IsCompressedDataValid())
	{
		FCompressibleAnimData CompressibleData(Context.UE4Clip, false);

		FCompressibleAnimDataResult CompressibleResult;
		CompressibleResult.CompressionScheme = Context.UE4Clip->CompressionScheme;
		CompressibleResult.CompressedNumberOfFrames = Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedNumberOfFrames;
		CompressibleResult.CompressedScaleOffsets.StripSize = Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedScaleOffsets.StripSize;
		CompressibleResult.CompressedScaleOffsets.OffsetData.AddUninitialized(Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedScaleOffsets.OffsetData.Num());
		FMemory::Memcpy(CompressibleResult.CompressedScaleOffsets.OffsetData.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedScaleOffsets.OffsetData.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedScaleOffsets.OffsetData.Num() * sizeof(int32));
		CompressibleResult.CompressedTrackOffsets.AddUninitialized(Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedTrackOffsets.Num());
		FMemory::Memcpy(CompressibleResult.CompressedTrackOffsets.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedTrackOffsets.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedTrackOffsets.Num() * sizeof(int32));
		CompressibleResult.CompressedByteStream.AddUninitialized(Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedByteStream.Num());
		FMemory::Memcpy(CompressibleResult.CompressedByteStream.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedByteStream.GetData(), Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedByteStream.Num());
		CompressibleResult.KeyEncodingFormat = Context.UE4Clip->CompressedData.CompressedDataStructure.KeyEncodingFormat;
		CompressibleResult.RotationCompressionFormat = Context.UE4Clip->CompressedData.CompressedDataStructure.RotationCompressionFormat;
		CompressibleResult.TranslationCompressionFormat = Context.UE4Clip->CompressedData.CompressedDataStructure.TranslationCompressionFormat;
		CompressibleResult.ScaleCompressionFormat = Context.UE4Clip->CompressedData.CompressedDataStructure.ScaleCompressionFormat;
		CompressibleResult.RotationCodec = Context.UE4Clip->CompressedData.CompressedDataStructure.RotationCodec;
		CompressibleResult.TranslationCodec = Context.UE4Clip->CompressedData.CompressedDataStructure.TranslationCodec;
		CompressibleResult.ScaleCodec = Context.UE4Clip->CompressedData.CompressedDataStructure.ScaleCodec;

		AnimationErrorStats UE4ErrorStats;
		FAnimationUtils::ComputeCompressionError(CompressibleData, CompressibleResult, UE4ErrorStats);

		uint16 WorstBone;
		float MaxError;
		float WorstSampleTime;
		CalculateClipError(*Context.ACLClip, *Context.ACLSkeleton, Context.UE4Clip, Context.UE4Skeleton, WorstBone, MaxError, WorstSampleTime);

		const int32 CompressedSize = Context.UE4Clip->GetApproxCompressedSize();
		const double UE4CompressionRatio = double(Context.UE4RawSize) / double(CompressedSize);
		const double ACLCompressionRatio = double(Context.ACLRawSize) / double(CompressedSize);

		Writer["ue4_keyreduction"] = [&](sjson::ObjectWriter& Writer)
		{
			Writer["algorithm_name"] = TCHAR_TO_ANSI(*Context.UE4Clip->CompressionScheme->GetClass()->GetName());
			Writer["codec_name"] = TCHAR_TO_ANSI(*FAnimationUtils::GetAnimationKeyFormatString(Context.UE4Clip->CompressedData.CompressedDataStructure.KeyEncodingFormat));
			Writer["compressed_size"] = CompressedSize;
			Writer["ue4_compression_ratio"] = UE4CompressionRatio;
			Writer["acl_compression_ratio"] = ACLCompressionRatio;
			Writer["compression_time"] = UE4ElapsedTimeSec;
			Writer["ue4_max_error"] = UE4ErrorStats.MaxError;
			Writer["ue4_avg_error"] = UE4ErrorStats.AverageError;
			Writer["ue4_worst_bone"] = UE4ErrorStats.MaxErrorBone;
			Writer["ue4_worst_time"] = UE4ErrorStats.MaxErrorTime;
			Writer["acl_max_error"] = MaxError;
			Writer["acl_worst_bone"] = WorstBone;
			Writer["acl_worst_time"] = WorstSampleTime;
			Writer["rotation_format"] = TCHAR_TO_ANSI(*Context.AnimFormatEnum->GetDisplayNameTextByIndex(Context.UE4Clip->CompressedData.CompressedDataStructure.RotationCompressionFormat).ToString());
			Writer["translation_format"] = TCHAR_TO_ANSI(*Context.AnimFormatEnum->GetDisplayNameTextByIndex(Context.UE4Clip->CompressedData.CompressedDataStructure.TranslationCompressionFormat).ToString());
			Writer["scale_format"] = TCHAR_TO_ANSI(*Context.AnimFormatEnum->GetDisplayNameTextByIndex(Context.UE4Clip->CompressedData.CompressedDataStructure.ScaleCompressionFormat).ToString());

			if (PerformExhaustiveDump)
			{
				DumpClipDetailedError(*Context.ACLClip, *Context.ACLSkeleton, Context.UE4Clip, Context.UE4Skeleton, Writer);
			}

			// Number of animated keys before any key reduction for animated tracks (without constant/default tracks)
			int32 TotalNumAnimatedKeys = 0;

			// Number of animated keys dropped after key reduction for animated tracks (without constant/default tracks)
			int32 TotalNumDroppedAnimatedKeys = 0;

			// Number of animated tracks (not constant/default)
			int32 NumAnimatedTracks = 0;

			Writer["dropped_track_keys"] = [&](sjson::ArrayWriter& Writer)
			{
				const TArray<FRawAnimSequenceTrack>& RawTracks = Context.UE4Clip->GetRawAnimationData();
				const int32 NumTracks = RawTracks.Num();
				const int32 NumSamples = Context.UE4Clip->GetRawNumberOfFrames();

				const int32* TrackOffsets = CompressibleResult.CompressedTrackOffsets.GetData();
				const auto& ScaleOffsets = Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedScaleOffsets;

				const AnimationCompressionFormat RotationFormat = CompressibleResult.RotationCompressionFormat;
				const AnimationCompressionFormat TranslationFormat = CompressibleResult.TranslationCompressionFormat;
				const AnimationCompressionFormat ScaleFormat = CompressibleResult.ScaleCompressionFormat;

				// offset past Min and Range data
				const int32 RotationStreamOffset = (RotationFormat == ACF_IntervalFixed32NoW) ? (sizeof(float) * 6) : 0;
				const int32 TranslationStreamOffset = (TranslationFormat == ACF_IntervalFixed32NoW) ? (sizeof(float) * 6) : 0;
				const int32 ScaleStreamOffset = (ScaleFormat == ACF_IntervalFixed32NoW) ? (sizeof(float) * 6) : 0;

				for (int32 TrackIndex = 0; TrackIndex < NumTracks; ++TrackIndex)
				{
					const int32* TrackData = TrackOffsets + (TrackIndex * 4);
					const int32 NumTransKeys = TrackData[1];

					// Skip constant/default tracks
					if (NumTransKeys > 1)
					{
						const int32 DroppedTransCount = NumSamples - NumTransKeys;
						const float DroppedRatio = float(DroppedTransCount) / float(NumSamples);
						Writer.push(DroppedRatio);

						TotalNumAnimatedKeys += NumSamples;
						TotalNumDroppedAnimatedKeys += DroppedTransCount;
						NumAnimatedTracks++;
					}

					const int32 NumRotKeys = TrackData[3];

					// Skip constant/default tracks
					if (NumRotKeys > 1)
					{
						const int32 DroppedRotCount = NumSamples - NumRotKeys;
						const float DroppedRatio = float(DroppedRotCount) / float(NumSamples);
						Writer.push(DroppedRatio);

						TotalNumAnimatedKeys += NumSamples;
						TotalNumDroppedAnimatedKeys += DroppedRotCount;
						NumAnimatedTracks++;
					}

					if (ScaleOffsets.IsValid())
					{
						const int32 NumScaleKeys = ScaleOffsets.GetOffsetData(TrackIndex, 1);

						// Skip constant/default tracks
						if (NumScaleKeys > 1)
						{
							const int32 DroppedScaleCount = NumSamples - NumScaleKeys;
							const float DroppedRatio = float(DroppedScaleCount) / float(NumSamples);
							Writer.push(DroppedRatio);

							TotalNumAnimatedKeys += NumSamples;
							TotalNumDroppedAnimatedKeys += DroppedScaleCount;
							NumAnimatedTracks++;
						}
					}
				}
			};

			Writer["total_num_animated_keys"] = TotalNumAnimatedKeys;
			Writer["total_num_dropped_animated_keys"] = TotalNumDroppedAnimatedKeys;

			Writer["dropped_pose_keys"] = [&](sjson::ArrayWriter& Writer)
			{
				const TArray<FRawAnimSequenceTrack>& RawTracks = Context.UE4Clip->GetRawAnimationData();
				const int32 NumTracks = RawTracks.Num();
				const int32 NumSamples = Context.UE4Clip->GetRawNumberOfFrames();

				const float FrameRate = (NumSamples - 1) / Context.UE4Clip->SequenceLength;

				const uint8* ByteStream = CompressibleResult.CompressedByteStream.GetData();
				const int32* TrackOffsets = CompressibleResult.CompressedTrackOffsets.GetData();
				const auto& ScaleOffsets = Context.UE4Clip->CompressedData.CompressedDataStructure.CompressedScaleOffsets;

				const AnimationCompressionFormat RotationFormat = CompressibleResult.RotationCompressionFormat;
				const AnimationCompressionFormat TranslationFormat = CompressibleResult.TranslationCompressionFormat;
				const AnimationCompressionFormat ScaleFormat = CompressibleResult.ScaleCompressionFormat;

				// offset past Min and Range data
				const int32 RotationStreamOffset = (RotationFormat == ACF_IntervalFixed32NoW) ? (sizeof(float) * 6) : 0;
				const int32 TranslationStreamOffset = (TranslationFormat == ACF_IntervalFixed32NoW) ? (sizeof(float) * 6) : 0;
				const int32 ScaleStreamOffset = (ScaleFormat == ACF_IntervalFixed32NoW) ? (sizeof(float) * 6) : 0;

				for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
				{
					const float SampleTime = float(SampleIndex) / FrameRate;

					int32 DroppedRotCount = 0;
					int32 DroppedTransCount = 0;
					int32 DroppedScaleCount = 0;
					for (int32 TrackIndex = 0; TrackIndex < NumTracks; ++TrackIndex)
					{
						const int32* TrackData = TrackOffsets + (TrackIndex * 4);

						const int32 TransKeysOffset = TrackData[0];
						const int32 NumTransKeys = TrackData[1];
						const uint8* TransStream = ByteStream + TransKeysOffset;

						const uint8* TransFrameTable = TransStream + TranslationStreamOffset + (NumTransKeys * CompressedTranslationStrides[TranslationFormat] * CompressedTranslationNum[TranslationFormat]);
						TransFrameTable = Align(TransFrameTable, 4);

						// Skip constant/default tracks
						if (NumTransKeys > 1 && IsKeyDropped(CompressibleData.NumFrames, TransFrameTable, NumTransKeys, FrameRate, SampleTime))
						{
							DroppedTransCount++;
						}

						const int32 RotKeysOffset = TrackData[2];
						const int32 NumRotKeys = TrackData[3];
						const uint8* RotStream = ByteStream + RotKeysOffset;

						const uint8* RotFrameTable = RotStream + RotationStreamOffset + (NumRotKeys * CompressedRotationStrides[RotationFormat] * CompressedRotationNum[RotationFormat]);
						RotFrameTable = Align(RotFrameTable, 4);

						// Skip constant/default tracks
						if (NumRotKeys > 1 && IsKeyDropped(CompressibleData.NumFrames, RotFrameTable, NumRotKeys, FrameRate, SampleTime))
						{
							DroppedRotCount++;
						}

						if (ScaleOffsets.IsValid())
						{
							const int32 ScaleKeysOffset = ScaleOffsets.GetOffsetData(TrackIndex, 0);
							const int32 NumScaleKeys = ScaleOffsets.GetOffsetData(TrackIndex, 1);
							const uint8* ScaleStream = ByteStream + ScaleKeysOffset;

							const uint8* ScaleFrameTable = ScaleStream + ScaleStreamOffset + (NumScaleKeys * CompressedScaleStrides[ScaleFormat] * CompressedScaleNum[ScaleFormat]);
							ScaleFrameTable = Align(ScaleFrameTable, 4);

							// Skip constant/default tracks
							if (NumScaleKeys > 1 && IsKeyDropped(CompressibleData.NumFrames, ScaleFrameTable, NumScaleKeys, FrameRate, SampleTime))
							{
								DroppedScaleCount++;
							}
						}
					}

					const int32 TotalDroppedCount = DroppedRotCount + DroppedTransCount + DroppedScaleCount;
					const float DropRatio = NumAnimatedTracks != 0 ? (float(TotalDroppedCount) / float(NumAnimatedTracks)) : 1.0f;
					Writer.push(DropRatio);
				}
			};

#if DO_CHECK && 0
			{
				// Double check our count
				const int32 NumSamples = Context.UE4Clip->GetRawNumberOfFrames();
				const TArray<FRawAnimSequenceTrack>& RawTracks = Context.UE4Clip->GetRawAnimationData();
				const int32 NumTracks = RawTracks.Num();
				const int32* TrackOffsets = Context.UE4Clip->CompressedTrackOffsets.GetData();
				const FCompressedOffsetData& ScaleOffsets = Context.UE4Clip->CompressedScaleOffsets;

				int32 DroppedRotCount = 0;
				int32 DroppedTransCount = 0;
				int32 DroppedScaleCount = 0;
				for (int32 TrackIndex = 0; TrackIndex < NumTracks; ++TrackIndex)
				{
					const int32* TrackData = TrackOffsets + (TrackIndex * 4);

					const int32 NumTransKeys = TrackData[1];

					// Skip constant/default tracks
					if (NumTransKeys > 1)
					{
						DroppedTransCount += NumSamples - NumTransKeys;
					}

					const int32 NumRotKeys = TrackData[3];

					// Skip constant/default tracks
					if (NumRotKeys > 1)
					{
						DroppedRotCount += NumSamples - NumRotKeys;
					}

					if (ScaleOffsets.IsValid())
					{
						const int32 NumScaleKeys = ScaleOffsets.GetOffsetData(TrackIndex, 1);

						// Skip constant/default tracks
						if (NumScaleKeys > 1)
						{
							DroppedScaleCount += NumSamples - NumScaleKeys;
						}
					}
				}
				check(TotalNumDroppedKeys == (DroppedRotCount + DroppedTransCount + DroppedScaleCount));
			}
#endif
		};
	}
	else
	{
		Writer["error"] = "failed to compress UE4 clip";
	}
}

struct CompressAnimationsFunctor
{
	template<typename ObjectType>
	void DoIt(UCommandlet* Commandlet, UPackage* Package, const TArray<FString>& Tokens, const TArray<FString>& Switches)
	{
		TArray<UAnimSequence*> AnimSequences;
		for (TObjectIterator<UAnimSequence> It; It; ++It)
		{
			UAnimSequence* AnimSeq = *It;
			if (AnimSeq->IsIn(Package))
			{
				AnimSequences.Add(AnimSeq);
			}
		}

		// Skip packages that contain no Animations.
		const int32 NumAnimSequences = AnimSequences.Num();
		if (NumAnimSequences == 0)
		{
			return;
		}

		UACLStatsDumpCommandlet* StatsCommandlet = Cast<UACLStatsDumpCommandlet>(Commandlet);
		FFileManagerGeneric FileManager;
		ACLAllocator Allocator;

		for (int32 SequenceIndex = 0; SequenceIndex < NumAnimSequences; ++SequenceIndex)
		{
			UAnimSequence* UE4Clip = AnimSequences[SequenceIndex];

			USkeleton* UE4Skeleton = UE4Clip->GetSkeleton();
			if (UE4Skeleton == nullptr)
			{
				continue;
			}

			if (UE4Skeleton->HasAnyFlags(RF_NeedLoad))
			{
				UE4Skeleton->GetLinker()->Preload(UE4Skeleton);
			}

			FString Filename = UE4Clip->GetPathName();
			Filename = Filename.Replace(TEXT("/"), TEXT("_"));
			Filename = Filename.Replace(TEXT("."), TEXT("_"));
			Filename += TEXT("_stats.sjson");
			Filename.RemoveFromStart(TEXT("_"));

			FString UE4StatPath = FPaths::Combine(*StatsCommandlet->UE4StatDir, *Filename);

			if (FileManager.FileExists(*UE4StatPath))
			{
				continue;
			}

			FArchive* StatWriter = FileManager.CreateFileWriter(*UE4StatPath);
			UE4SJSONStreamWriter StreamWriter(StatWriter);
			sjson::Writer Writer(StreamWriter);

			FCompressionContext Context;
			Context.AutoCompressor = StatsCommandlet->AutoCompressor;
			Context.ACLCompressor = StatsCommandlet->ACLCompressor;
			Context.AnimFormatEnum = StatsCommandlet->AnimFormatEnum;
			Context.UE4Clip = UE4Clip;
			Context.UE4Skeleton = UE4Skeleton;

			FCompressibleAnimData CompressibleData(UE4Clip, false);

			TUniquePtr<acl::RigidSkeleton> ACLSkeleton = BuildACLSkeleton(Allocator, CompressibleData, StatsCommandlet->ACLCompressor->DefaultVirtualVertexDistance, StatsCommandlet->ACLCompressor->SafeVirtualVertexDistance);
			TUniquePtr<acl::AnimationClip> ACLClip = BuildACLClip(Allocator, CompressibleData, *ACLSkeleton, false);
			TUniquePtr<acl::AnimationClip> ACLBaseClip;

			if (CompressibleData.bIsValidAdditive)
			{
				ACLBaseClip = BuildACLClip(Allocator, CompressibleData, *ACLSkeleton, true);

				ACLClip->set_additive_base(ACLBaseClip.Get(), acl::AdditiveClipFormat8::Additive1);
			}

			Context.ACLClip = MoveTemp(ACLClip);
			Context.ACLSkeleton = MoveTemp(ACLSkeleton);
			Context.ACLRawSize = Context.ACLClip->get_raw_size();
			Context.UE4RawSize = UE4Clip->GetApproxRawSize();

			{
				Writer["duration"] = UE4Clip->SequenceLength;
				Writer["num_samples"] = CompressibleData.NumFrames;
				Writer["ue4_raw_size"] = Context.UE4RawSize;
				Writer["acl_raw_size"] = Context.ACLRawSize;

				if (StatsCommandlet->TryAutomaticCompression)
				{
					// Clear whatever compression scheme we had to force us to use the default scheme
					UE4Clip->CompressionScheme = nullptr;

					CompressWithUE4Auto(Context, StatsCommandlet->PerformExhaustiveDump, Writer);
				}

				// Reset our anim sequence
				UE4Clip->CompressedData.CompressedDataStructure.Reset();

				if (StatsCommandlet->TryACLCompression)
				{
					CompressWithACL(Context, StatsCommandlet->PerformExhaustiveDump, Writer);
				}

				// Reset our anim sequence
				UE4Clip->CompressedData.CompressedDataStructure.Reset();

				if (StatsCommandlet->TryKeyReduction)
				{
					CompressWithUE4KeyReduction(Context, StatsCommandlet->PerformExhaustiveDump, Writer);
				}

				UE4Clip->RecycleAnimSequence();
			}

			StatWriter->Close();
		}
	}
};
#endif	// WITH_EDITOR

UACLStatsDumpCommandlet::UACLStatsDumpCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UACLStatsDumpCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamsMap;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	if (!ParamsMap.Contains(TEXT("stats")))
	{
		UE_LOG(LogAnimationCompression, Error, TEXT("Missing commandlet argument: -stats=<path/to/output/stats/directory>"));
		return 0;
	}

	UE4StatDir = ParamsMap[TEXT("stats")];

	PerformExhaustiveDump = Switches.Contains(TEXT("error"));
	TryAutomaticCompression = Switches.Contains(TEXT("auto"));
	TryACLCompression = Switches.Contains(TEXT("acl"));

	TryKeyReductionRetarget = Switches.Contains(TEXT("keyreductionrt"));
	TryKeyReduction = TryKeyReductionRetarget || Switches.Contains(TEXT("keyreduction"));

	MasterTolerance = 0.1f;	// 0.1cm is a sensible default for production use
	if (ParamsMap.Contains(TEXT("MasterTolerance")))
	{
		MasterTolerance = FCString::Atof(*ParamsMap[TEXT("MasterTolerance")]);
	}

	AutoCompressor = NewObject<UAnimCompress_Automatic>(this, UAnimCompress_Automatic::StaticClass());
	AutoCompressor->MaxEndEffectorError = MasterTolerance;
	AutoCompressor->bAutoReplaceIfExistingErrorTooGreat = true;
	AutoCompressor->AddToRoot();

	ACLCompressor = NewObject<UAnimCompress_ACL>(this, UAnimCompress_ACL::StaticClass());
	ACLCompressor->AddToRoot();

	UAnimCompress_RemoveLinearKeys* KeyReductionCompressor = NewObject<UAnimCompress_RemoveLinearKeys>(this, UAnimCompress_RemoveLinearKeys::StaticClass());
	KeyReductionCompressor->RotationCompressionFormat = ACF_Float96NoW;
	KeyReductionCompressor->TranslationCompressionFormat = ACF_None;
	KeyReductionCompressor->ScaleCompressionFormat = ACF_None;
	KeyReductionCompressor->bActuallyFilterLinearKeys = true;
	KeyReductionCompressor->bRetarget = TryKeyReductionRetarget;
	KeyReductionCompressor->AddToRoot();

	AnimFormatEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("AnimationCompressionFormat"), true);

	if (!ParamsMap.Contains(TEXT("acl")))
	{
		// No source directory, use the current project instead
		ACLRawDir = TEXT("");

		DoActionToAllPackages<UAnimSequence, CompressAnimationsFunctor>(this, Params.ToUpper());
		return 0;
	}
	else
	{
		// Use source directory
		ACLRawDir = ParamsMap[TEXT("acl")];

		UPackage* TempPackage = CreatePackage(nullptr, TEXT("/Temp/ACL"));

		ACLAllocator Allocator;

		FFileManagerGeneric FileManager;
		TArray<FString> Files;
		FileManager.FindFiles(Files, *ACLRawDir, TEXT(".acl.sjson"));

		for (const FString& Filename : Files)
		{
			const FString ACLClipPath = FPaths::Combine(*ACLRawDir, *Filename);
			const FString UE4StatPath = FPaths::Combine(*UE4StatDir, *Filename.Replace(TEXT(".acl.sjson"), TEXT("_stats.sjson"), ESearchCase::CaseSensitive));

			if (FileManager.FileExists(*UE4StatPath))
			{
				continue;
			}

			UE_LOG(LogAnimationCompression, Verbose, TEXT("Compressing: %s"), *Filename);

			FArchive* StatWriter = FileManager.CreateFileWriter(*UE4StatPath);
			if (StatWriter == nullptr)
			{
				// Opening the file handle can fail if the file path is too long on Windows. UE4 does not properly handle long paths
				// and adding the \\?\ prefix manually doesn't work, UE4 mangles it when it normalizes the path.
				continue;
			}

			UE4SJSONStreamWriter StreamWriter(StatWriter);
			sjson::Writer Writer(StreamWriter);

			std::unique_ptr<acl::AnimationClip, acl::Deleter<acl::AnimationClip>> ACLClipRaw;
			std::unique_ptr<acl::RigidSkeleton, acl::Deleter<acl::RigidSkeleton>> ACLSkeletonRaw;

			const TCHAR* ErrorMsg = ReadACLClip(FileManager, ACLClipPath, Allocator, ACLClipRaw, ACLSkeletonRaw);
			if (ErrorMsg == nullptr)
			{
				USkeleton* UE4Skeleton = NewObject<USkeleton>(TempPackage, USkeleton::StaticClass());
				ConvertSkeleton(*ACLSkeletonRaw, UE4Skeleton);

				UAnimSequence* UE4Clip = NewObject<UAnimSequence>(TempPackage, UAnimSequence::StaticClass());
				ConvertClip(*ACLClipRaw, *ACLSkeletonRaw, UE4Clip, UE4Skeleton);

				// Re-create the ACL clip/skeleton since they might differ slightly from the ones we had due to round-tripping (float64 arithmetic)

				// Same default values as ACL
				const float DefaultVirtualVertexDistance = 3.0f;	// 3cm, suitable for ordinary characters
				const float SafeVirtualVertexDistance = 100.0f;		// 100cm

				FCompressibleAnimData CompressibleData(UE4Clip, false);

				TUniquePtr<acl::RigidSkeleton> ACLSkeleton = BuildACLSkeleton(Allocator, CompressibleData, DefaultVirtualVertexDistance, SafeVirtualVertexDistance);
				TUniquePtr<acl::AnimationClip> ACLClip = BuildACLClip(Allocator, CompressibleData, *ACLSkeleton, false);
				TUniquePtr<acl::AnimationClip> ACLBaseClip = nullptr;

				if (UE4Clip->IsValidAdditive())
				{
					ACLBaseClip = BuildACLClip(Allocator, CompressibleData, *ACLSkeleton, true);

					ACLClip->set_additive_base(ACLBaseClip.Get(), acl::AdditiveClipFormat8::Additive1);
				}

				FCompressionContext Context;
				Context.AutoCompressor = AutoCompressor;
				Context.ACLCompressor = ACLCompressor;
				Context.KeyReductionCompressor = KeyReductionCompressor;
				Context.AnimFormatEnum = AnimFormatEnum;
				Context.UE4Clip = UE4Clip;
				Context.UE4Skeleton = UE4Skeleton;
				Context.ACLClip = MoveTemp(ACLClip);
				Context.ACLSkeleton = MoveTemp(ACLSkeleton);

				Context.ACLRawSize = Context.ACLClip->get_raw_size();
				Context.UE4RawSize = UE4Clip->GetApproxRawSize();

				Writer["duration"] = UE4Clip->SequenceLength;
				Writer["num_samples"] = CompressibleData.NumFrames;
				Writer["ue4_raw_size"] = Context.UE4RawSize;
				Writer["acl_raw_size"] = Context.ACLRawSize;

				if (TryAutomaticCompression)
				{
					CompressWithUE4Auto(Context, PerformExhaustiveDump, Writer);
				}

				// Reset our anim sequence
				UE4Clip->CompressedData.CompressedDataStructure.Reset();

				if (TryACLCompression)
				{
					CompressWithACL(Context, PerformExhaustiveDump, Writer);
				}

				// Reset our anim sequence
				UE4Clip->CompressedData.CompressedDataStructure.Reset();

				if (TryKeyReduction)
				{
					CompressWithUE4KeyReduction(Context, PerformExhaustiveDump, Writer);
				}

				UE4Clip->RecycleAnimSequence();
			}
			else
			{
				Writer["error"] = TCHAR_TO_ANSI(ErrorMsg);
			}

			StatWriter->Close();
		}
	}
#endif	// WITH_EDITOR

	return 0;
}
