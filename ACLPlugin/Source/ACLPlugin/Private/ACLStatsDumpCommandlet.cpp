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
#include "Runtime/Engine/Classes/Animation/Skeleton.h"
#include "Runtime/Engine/Public/AnimationCompression.h"
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
	UE4Clip->UpdateCompressedTrackMapFromRaw();
	UE4Clip->PostProcessSequence();
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
		const int32 BoneTrackIndex = UE4Skeleton->GetAnimationTrackIndex(BoneTreeIndex, UE4Clip, false);

		FTransform BoneTransform;
		if (BoneTrackIndex >= 0)
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

	const UEnum* AnimFormatEnum;

	UAnimSequence* UE4Clip;
	USkeleton* UE4Skeleton;
	TArray<FBoneData> UE4BoneData;

	TUniquePtr<acl::AnimationClip> ACLClip;
	TUniquePtr<acl::RigidSkeleton> ACLSkeleton;

	uint32 ACLRawSize;
	int32 UE4RawSize;
};

static void CompressWithUE4Auto(FCompressionContext& Context, bool PerformExhaustiveDump, sjson::Writer& Writer)
{
	const uint64 UE4StartTimeCycles = FPlatformTime::Cycles64();

	FAnimCompressContext CompressContext(true, false);
	const bool UE4Success = Context.AutoCompressor->Reduce(Context.UE4Clip, CompressContext, Context.UE4BoneData);

	const uint64 UE4EndTimeCycles = FPlatformTime::Cycles64();

	const uint64 UE4ElapsedCycles = UE4EndTimeCycles - UE4StartTimeCycles;
	const double UE4ElapsedTimeSec = FPlatformTime::ToSeconds64(UE4ElapsedCycles);

	if (UE4Success)
	{
		AnimationErrorStats UE4ErrorStats;
		FAnimationUtils::ComputeCompressionError(Context.UE4Clip, Context.UE4BoneData, UE4ErrorStats);

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
			Writer["codec_name"] = TCHAR_TO_ANSI(*Context.UE4Clip->CompressedCodecFormat.ToString());
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
			Writer["rotation_format"] = TCHAR_TO_ANSI(*Context.AnimFormatEnum->GetDisplayNameTextByIndex(Context.UE4Clip->CompressionScheme->RotationCompressionFormat).ToString());
			Writer["translation_format"] = TCHAR_TO_ANSI(*Context.AnimFormatEnum->GetDisplayNameTextByIndex(Context.UE4Clip->CompressionScheme->TranslationCompressionFormat).ToString());
			Writer["scale_format"] = TCHAR_TO_ANSI(*Context.AnimFormatEnum->GetDisplayNameTextByIndex(Context.UE4Clip->CompressionScheme->ScaleCompressionFormat).ToString());

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
	const uint64 ACLStartTimeCycles = FPlatformTime::Cycles64();

	FAnimCompressContext CompressContext(true, false);
	const bool ACLSuccess = Context.ACLCompressor->Reduce(Context.UE4Clip, CompressContext, Context.UE4BoneData);

	const uint64 ACLEndTimeCycles = FPlatformTime::Cycles64();

	const uint64 ACLElapsedCycles = ACLEndTimeCycles - ACLStartTimeCycles;
	const double ACLElapsedTimeSec = FPlatformTime::ToSeconds64(ACLElapsedCycles);

	if (ACLSuccess)
	{
		AnimationErrorStats UE4ErrorStats;
		FAnimationUtils::ComputeCompressionError(Context.UE4Clip, Context.UE4BoneData, UE4ErrorStats);

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
			Writer["codec_name"] = TCHAR_TO_ANSI(*Context.UE4Clip->CompressedCodecFormat.ToString());
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

			FAnimationUtils::BuildSkeletonMetaData(UE4Skeleton, Context.UE4BoneData);

			TUniquePtr<acl::RigidSkeleton> ACLSkeleton = BuildACLSkeleton(Allocator, *UE4Clip, Context.UE4BoneData, StatsCommandlet->ACLCompressor->DefaultVirtualVertexDistance, StatsCommandlet->ACLCompressor->SafeVirtualVertexDistance);
			TUniquePtr<acl::AnimationClip> ACLClip = BuildACLClip(Allocator, *UE4Clip, *ACLSkeleton, false);
			TUniquePtr<acl::AnimationClip> ACLBaseClip = nullptr;

			if (UE4Clip->IsValidAdditive())
			{
				if (UE4Clip->RefPoseSeq != nullptr)
				{
					if (UE4Clip->RefPoseSeq->HasAnyFlags(RF_NeedLoad))
					{
						UE4Clip->RefPoseSeq->GetLinker()->Preload(UE4Clip->RefPoseSeq);
					}

					USkeleton* UE4BaseSkeleton = UE4Clip->RefPoseSeq->GetSkeleton();
					if (UE4BaseSkeleton == nullptr)
					{
						continue;
					}

					if (UE4BaseSkeleton->HasAnyFlags(RF_NeedLoad))
					{
						UE4BaseSkeleton->GetLinker()->Preload(UE4BaseSkeleton);
					}
				}

				ACLBaseClip = BuildACLClip(Allocator, *UE4Clip, *ACLSkeleton, true);

				ACLClip->set_additive_base(ACLBaseClip.Get(), acl::AdditiveClipFormat8::Additive1);
			}

			Context.ACLClip = MoveTemp(ACLClip);
			Context.ACLSkeleton = MoveTemp(ACLSkeleton);
			Context.ACLRawSize = Context.ACLClip->get_raw_size();
			Context.UE4RawSize = UE4Clip->GetApproxRawSize();

			{
				Writer["duration"] = UE4Clip->SequenceLength;
				Writer["num_samples"] = UE4Clip->GetCompressedNumberOfFrames();
				Writer["ue4_raw_size"] = Context.UE4RawSize;
				Writer["acl_raw_size"] = Context.ACLRawSize;

				if (StatsCommandlet->TryAutomaticCompression)
				{
					// Clear whatever compression scheme we had to force us to use the default scheme
					UE4Clip->CompressionScheme = nullptr;

					CompressWithUE4Auto(Context, StatsCommandlet->PerformExhaustiveDump, Writer);
				}

				// Reset our anim sequence
				UE4Clip->CompressedTrackOffsets.Empty();
				UE4Clip->CompressedByteStream.Empty();
				UE4Clip->CompressedScaleOffsets.Empty();
				UE4Clip->TranslationCompressionFormat = ACF_None;
				UE4Clip->RotationCompressionFormat = ACF_None;
				UE4Clip->ScaleCompressionFormat = ACF_None;

				if (StatsCommandlet->TryACLCompression)
				{
					CompressWithACL(Context, StatsCommandlet->PerformExhaustiveDump, Writer);
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

	PerformExhaustiveDump = !Switches.Contains(TEXT("noerror"));
	TryAutomaticCompression = !Switches.Contains(TEXT("noauto"));
	TryACLCompression = !Switches.Contains(TEXT("noacl"));

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

				TArray<FBoneData> BoneData;
				FAnimationUtils::BuildSkeletonMetaData(UE4Skeleton, BoneData);

				// Same default values as ACL
				const float DefaultVirtualVertexDistance = 3.0f;	// 3cm, suitable for ordinary characters
				const float SafeVirtualVertexDistance = 100.0f;		// 100cm

				TUniquePtr<acl::RigidSkeleton> ACLSkeleton = BuildACLSkeleton(Allocator, *UE4Clip, BoneData, DefaultVirtualVertexDistance, SafeVirtualVertexDistance);
				TUniquePtr<acl::AnimationClip> ACLClip = BuildACLClip(Allocator, *UE4Clip, *ACLSkeleton, false);
				TUniquePtr<acl::AnimationClip> ACLBaseClip = nullptr;

				if (UE4Clip->IsValidAdditive())
				{
					ACLBaseClip = BuildACLClip(Allocator, *UE4Clip, *ACLSkeleton, true);

					ACLClip->set_additive_base(ACLBaseClip.Get(), acl::AdditiveClipFormat8::Additive1);
				}

				FCompressionContext Context;
				Context.AutoCompressor = AutoCompressor;
				Context.ACLCompressor = ACLCompressor;
				Context.AnimFormatEnum = AnimFormatEnum;
				Context.UE4Clip = UE4Clip;
				Context.UE4Skeleton = UE4Skeleton;
				Context.ACLClip = MoveTemp(ACLClip);
				Context.ACLSkeleton = MoveTemp(ACLSkeleton);
				Context.UE4BoneData = MoveTemp(BoneData);

				Context.ACLRawSize = Context.ACLClip->get_raw_size();
				Context.UE4RawSize = UE4Clip->GetApproxRawSize();

				Writer["duration"] = UE4Clip->SequenceLength;
				Writer["num_samples"] = UE4Clip->GetCompressedNumberOfFrames();
				Writer["ue4_raw_size"] = Context.UE4RawSize;
				Writer["acl_raw_size"] = Context.ACLRawSize;

				if (TryAutomaticCompression)
				{
					CompressWithUE4Auto(Context, PerformExhaustiveDump, Writer);
				}

				// Reset our anim sequence
				UE4Clip->CompressedTrackOffsets.Empty();
				UE4Clip->CompressedByteStream.Empty();
				UE4Clip->CompressedScaleOffsets.Empty();
				UE4Clip->TranslationCompressionFormat = ACF_None;
				UE4Clip->RotationCompressionFormat = ACF_None;
				UE4Clip->ScaleCompressionFormat = ACF_None;

				if (TryACLCompression)
				{
					CompressWithACL(Context, PerformExhaustiveDump, Writer);
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
