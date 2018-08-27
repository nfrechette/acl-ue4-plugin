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
#include "Runtime/Engine/Classes/Animation/AnimCompress_Automatic.h"
#include "Runtime/Engine/Classes/Animation/Skeleton.h"
#include "Runtime/Engine/Public/AnimationCompression.h"

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
	int64 Size = Reader->TotalSize();

	TArray<char> RawSJSONData;
	RawSJSONData.AddUninitialized((int32)Size);

	Reader->Serialize(RawSJSONData.GetData(), Size);
	Reader->Close();

	acl::ClipReader ClipReader(Allocator, RawSJSONData.GetData(), Size);

	if (!ClipReader.read_skeleton(OutACLSkeleton))
		return TEXT("Failed to read ACL RigidSkeleton from file");

	if (!ClipReader.read_clip(OutACLClip, *OutACLSkeleton))
		return TEXT("Failed to read ACL AnimationClip from file");

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
	UE4Clip->NumFrames = ACLClip.get_num_samples();
	UE4Clip->SetSkeleton(UE4Skeleton);

	uint16 NumBones = ACLSkeleton.get_num_bones();
	for (uint16 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const acl::RigidBone& ACLBone = ACLSkeleton.get_bone(BoneIndex);
		const acl::AnimatedBone& Bone = ACLClip.get_animated_bone(BoneIndex);

		FRawAnimSequenceTrack RawTrack;
		RawTrack.PosKeys.Empty();
		RawTrack.RotKeys.Empty();
		RawTrack.ScaleKeys.Empty();

		uint32 NumSamples = Bone.rotation_track.get_num_samples();
		for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
		{
			FQuat Rotation = QuatCast(acl::quat_normalize(acl::quat_cast(Bone.rotation_track.get_sample(SampleIndex))));
			RawTrack.RotKeys.Add(Rotation);
		}

		NumSamples = Bone.translation_track.get_num_samples();
		for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
		{
			FVector Translation = VectorCast(acl::vector_cast(Bone.translation_track.get_sample(SampleIndex)));
			RawTrack.PosKeys.Add(Translation);
		}

		NumSamples = Bone.scale_track.get_num_samples();
		for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
		{
			FVector Scale = VectorCast(acl::vector_cast(Bone.scale_track.get_sample(SampleIndex)));
			RawTrack.ScaleKeys.Add(Scale);
		}

		FName BoneName(ACLBone.name.c_str());
		UE4Clip->AddNewRawTrack(BoneName, &RawTrack);
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

		FTransform BoneAtom;
		if (BoneTrackIndex >= 0)
		{
			UE4Clip->GetBoneTransform(BoneAtom, BoneTrackIndex, SampleTime, false);
		}
		else
		{
			BoneAtom = RefSkeletonPose[BoneTreeIndex];
		}

		const acl::Quat_32 Rotation = QuatCast(BoneAtom.GetRotation());
		const acl::Vector4_32 Translation = VectorCast(BoneAtom.GetTranslation());
		const acl::Vector4_32 Scale = VectorCast(BoneAtom.GetScale3D());
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

	uint16 NumBones = ACLClip.get_num_bones();
	float ClipDuration = ACLClip.get_duration();
	float SampleRate = float(ACLClip.get_sample_rate());
	uint32 NumSamples = calculate_num_samples(ClipDuration, SampleRate);
	bool HasScale = UE4ClipHasScale(UE4Clip);

	TArray<Transform_32> RawPoseTransforms;
	TArray<Transform_32> LossyPoseTransforms;
	RawPoseTransforms.AddUninitialized(NumBones);
	LossyPoseTransforms.AddUninitialized(NumBones);

	uint16 WorstBone = acl::k_invalid_bone_index;
	float MaxError = 0.0f;
	float WorstSampleTime = 0.0f;

	TransformErrorMetric ErrorMetric;

	for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
	{
		// Sample our streams and calculate the error
		float SampleTime = min(float(SampleIndex) / SampleRate, ClipDuration);

		ACLClip.sample_pose(SampleTime, RawPoseTransforms.GetData(), NumBones);
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

	uint16 NumBones = ACLClip.get_num_bones();
	float ClipDuration = ACLClip.get_duration();
	float SampleRate = float(ACLClip.get_sample_rate());
	uint32 NumSamples = calculate_num_samples(ClipDuration, SampleRate);
	bool HasScale = UE4ClipHasScale(UE4Clip);

	TArray<Transform_32> RawPoseTransforms;
	TArray<Transform_32> LossyPoseTransforms;
	RawPoseTransforms.AddUninitialized(NumBones);
	LossyPoseTransforms.AddUninitialized(NumBones);

	TransformErrorMetric ErrorMetric;

	Writer["error_per_frame_and_bone"] = [&](sjson::ArrayWriter& Writer)
	{
		for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
		{
			// Sample our streams and calculate the error
			float SampleTime = min(float(SampleIndex) / SampleRate, ClipDuration);

			ACLClip.sample_pose(SampleTime, RawPoseTransforms.GetData(), NumBones);
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
#endif	// WITH_EDITOR

UACLStatsDumpCommandlet::UACLStatsDumpCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	IsClient = false;
	IsServer = false;
	IsEditor = false;
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

	if (!ParamsMap.Contains(TEXT("acl")))
	{
		UE_LOG(LogAnimationCompression, Error, TEXT("Missing commandlet argument: -acl=<path/to/raw/acl/sjson/files/directory>"));
		return 0;
	}

	if (!ParamsMap.Contains(TEXT("stats")))
	{
		UE_LOG(LogAnimationCompression, Error, TEXT("Missing commandlet argument: -stats=<path/to/output/stats/directory>"));
		return 0;
	}

	FString ACLRawDir = ParamsMap[TEXT("acl")];
	FString UE4StatDir = ParamsMap[TEXT("stats")];

	const bool PerformExhaustiveDump = !Switches.Contains(TEXT("noerror"));
	const bool TryAutomaticCompression = !Switches.Contains(TEXT("noauto"));
	const bool TryACLCompression = !Switches.Contains(TEXT("noacl"));

	float MasterTolerance = 0.1f;	// 0.1cm is a sensible default for production use
	if (ParamsMap.Contains(TEXT("MasterTolerance")))
	{
		MasterTolerance = FCString::Atof(*ParamsMap[TEXT("MasterTolerance")]);
	}

	FFileManagerGeneric FileManager;
	TArray<FString> Files;
	FileManager.FindFiles(Files, *ACLRawDir, TEXT(".acl.sjson"));

	ACLAllocator Allocator;

	UAnimCompress_Automatic* AutoCompressor = NewObject<UAnimCompress_Automatic>(this, UAnimCompress_Automatic::StaticClass());
	AutoCompressor->MaxEndEffectorError = MasterTolerance;
	AutoCompressor->bAutoReplaceIfExistingErrorTooGreat = true;

	UAnimCompress_ACL* ACLCompressor = NewObject<UAnimCompress_ACL>(this, UAnimCompress_ACL::StaticClass());

	const UEnum* AnimFormatEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("AnimationCompressionFormat"), true);

	for (const FString& Filename : Files)
	{
		FString ACLClipPath = FPaths::Combine(*ACLRawDir, *Filename);
		FString UE4StatPath = FPaths::Combine(*UE4StatDir, *Filename.Replace(TEXT(".acl.sjson"), TEXT("_stats.sjson"), ESearchCase::CaseSensitive));

		if (FileManager.FileExists(*UE4StatPath))
			continue;

		FArchive* StatWriter = FileManager.CreateFileWriter(*UE4StatPath);
		UE4SJSONStreamWriter StreamWriter(StatWriter);
		sjson::Writer Writer(StreamWriter);

		std::unique_ptr<acl::AnimationClip, acl::Deleter<acl::AnimationClip>> ACLClip;
		std::unique_ptr<acl::RigidSkeleton, acl::Deleter<acl::RigidSkeleton>> ACLSkeleton;

		const TCHAR* ErrorMsg = ReadACLClip(FileManager, ACLClipPath, Allocator, ACLClip, ACLSkeleton);
		if (ErrorMsg == nullptr)
		{
			USkeleton* UE4Skeleton = NewObject<USkeleton>(this, USkeleton::StaticClass());
			ConvertSkeleton(*ACLSkeleton, UE4Skeleton);

			UAnimSequence* UE4Clip = NewObject<UAnimSequence>(this, UAnimSequence::StaticClass());
			ConvertClip(*ACLClip, *ACLSkeleton, UE4Clip, UE4Skeleton);

			TArray<FBoneData> UE4BoneData;
			FAnimationUtils::BuildSkeletonMetaData(UE4Skeleton, UE4BoneData);

			const uint32 ACLRawSize = ACLClip->get_raw_size();
			const int32 UE4RawSize = UE4Clip->GetApproxRawSize();

			Writer["duration"] = UE4Clip->SequenceLength;
			Writer["num_samples"] = UE4Clip->NumFrames;
			Writer["ue4_raw_size"] = UE4RawSize;
			Writer["acl_raw_size"] = ACLRawSize;

			if (TryAutomaticCompression)
			{
				const uint64 UE4StartTimeCycles = FPlatformTime::Cycles64();

				const bool UE4Success = AutoCompressor->Reduce(UE4Clip, false);

				const uint64 UE4EndTimeCycles = FPlatformTime::Cycles64();

				const uint64 UE4ElapsedCycles = UE4EndTimeCycles - UE4StartTimeCycles;
				const double UE4ElapsedTimeSec = FPlatformTime::ToSeconds64(UE4ElapsedCycles);

				if (UE4Success)
				{
					AnimationErrorStats UE4ErrorStats;
					FAnimationUtils::ComputeCompressionError(UE4Clip, UE4BoneData, UE4ErrorStats);

					uint16 WorstBone;
					float MaxError;
					float WorstSampleTime;
					CalculateClipError(*ACLClip, *ACLSkeleton, UE4Clip, UE4Skeleton, WorstBone, MaxError, WorstSampleTime);

					const int32 CompressedSize = UE4Clip->GetApproxCompressedSize();
					const double UE4CompressionRatio = double(UE4RawSize) / double(CompressedSize);
					const double ACLCompressionRatio = double(ACLRawSize) / double(CompressedSize);

					Writer["ue4_auto"] = [&](sjson::ObjectWriter& Writer)
					{
						Writer["algorithm_name"] = TCHAR_TO_ANSI(*UE4Clip->CompressionScheme->GetClass()->GetName());
						Writer["codec_name"] = TCHAR_TO_ANSI(*UE4Clip->CompressedCodecFormat.ToString());
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
						Writer["rotation_format"] = TCHAR_TO_ANSI(*AnimFormatEnum->GetDisplayNameTextByIndex(UE4Clip->CompressionScheme->RotationCompressionFormat).ToString());
						Writer["translation_format"] = TCHAR_TO_ANSI(*AnimFormatEnum->GetDisplayNameTextByIndex(UE4Clip->CompressionScheme->TranslationCompressionFormat).ToString());
						Writer["scale_format"] = TCHAR_TO_ANSI(*AnimFormatEnum->GetDisplayNameTextByIndex(UE4Clip->CompressionScheme->ScaleCompressionFormat).ToString());

						if (PerformExhaustiveDump)
							DumpClipDetailedError(*ACLClip, *ACLSkeleton, UE4Clip, UE4Skeleton, Writer);
					};
				}
				else
				{
					Writer["error"] = "failed to compress UE4 clip";
				}
			}

			UE4Clip->CompressedTrackOffsets.Empty();
			UE4Clip->CompressedByteStream.Empty();
			UE4Clip->CompressedScaleOffsets.Empty();

			if (TryACLCompression)
			{
				const uint64 ACLStartTimeCycles = FPlatformTime::Cycles64();

				const bool ACLSuccess = ACLCompressor->Reduce(UE4Clip, false);

				const uint64 ACLEndTimeCycles = FPlatformTime::Cycles64();

				const uint64 ACLElapsedCycles = ACLEndTimeCycles - ACLStartTimeCycles;
				const double ACLElapsedTimeSec = FPlatformTime::ToSeconds64(ACLElapsedCycles);

				if (ACLSuccess)
				{
					AnimationErrorStats UE4ErrorStats;
					FAnimationUtils::ComputeCompressionError(UE4Clip, UE4BoneData, UE4ErrorStats);

					uint16 WorstBone;
					float MaxError;
					float WorstSampleTime;
					CalculateClipError(*ACLClip, *ACLSkeleton, UE4Clip, UE4Skeleton, WorstBone, MaxError, WorstSampleTime);

					const int32 CompressedSize = UE4Clip->GetApproxCompressedSize();
					const double UE4CompressionRatio = double(UE4RawSize) / double(CompressedSize);
					const double ACLCompressionRatio = double(ACLRawSize) / double(CompressedSize);

					Writer["ue4_acl"] = [&](sjson::ObjectWriter& Writer)
					{
						Writer["algorithm_name"] = TCHAR_TO_ANSI(*UE4Clip->CompressionScheme->GetClass()->GetName());
						Writer["codec_name"] = TCHAR_TO_ANSI(*UE4Clip->CompressedCodecFormat.ToString());
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
							DumpClipDetailedError(*ACLClip, *ACLSkeleton, UE4Clip, UE4Skeleton, Writer);
					};
				}
				else
				{
					Writer["error"] = "failed to compress UE4 clip";
				}
			}

			UE4Clip->RecycleAnimSequence();
		}
		else
		{
			Writer["error"] = TCHAR_TO_ANSI(ErrorMsg);
		}

		StatWriter->Close();
	}
#endif	// WITH_EDITOR

	return 0;
}
