// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "ACLStatsDumpCommandlet.h"

#if WITH_EDITOR
#include "Runtime/Core/Public/HAL/FileManagerGeneric.h"
#include "Runtime/Core/Public/HAL/PlatformTime.h"
#include "Runtime/CoreUObject/Public/UObject/UObjectIterator.h"
#include "Runtime/Engine/Classes/Animation/AnimBoneCompressionSettings.h"
#include "Runtime/Engine/Classes/Animation/AnimCompress.h"
#include "Runtime/Engine/Classes/Animation/AnimCompress_RemoveLinearKeys.h"
#include "Runtime/Engine/Classes/Animation/Skeleton.h"
#include "Runtime/Engine/Public/AnimationCompression.h"
#include "Editor/UnrealEd/Public/PackageHelperFunctions.h"

#include "AnimBoneCompressionCodec_ACL.h"
#include "ACLImpl.h"

#include <sjson/parser.h>
#include <sjson/writer.h>

#include <acl/algorithm/uniformly_sampled/decoder.h>
#include <acl/compression/animation_clip.h>
#include <acl/compression/skeleton_error_metric.h>
#include <acl/compression/utils.h>
#include <acl/io/clip_reader.h>
#include <acl/io/clip_writer.h>
#include <acl/math/quat_32.h>
#include <acl/math/transform_32.h>

//////////////////////////////////////////////////////////////////////////
// Commandlet example inspired by: https://github.com/ue4plugins/CommandletPlugin
// To run the commandlet, add to the commandline: "$(SolutionDir)$(ProjectName).uproject" -run=/Script/ACLPlugin.ACLStatsDump "-input=<path/to/raw/acl/sjson/files/directory>" "-output=<path/to/output/stats/directory>" -compress
//
// Usage:
//		-input=<directory>: If present all *acl.sjson files will be used as the input for the commandlet otherwise the current project is used
//		-output=<directory>: The commandlet output will be written at the given path (stats or dumped clips)
//		-compress: Commandlet will compress the input clips and output stats
//		-extract: Commandlet will extract the input clips into output *acl.sjson clips
//		-noerror: Disables the exhaustive error dumping
//		-noauto: Disables automatic compression
//		-noacl: Disables ACL compression
//		-MasterTolerance=<tolerance>: The error threshold used by automatic compression
//		-resume: If present, clip extraction or compression will continue where it left off
//////////////////////////////////////////////////////////////////////////

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

	if (ClipReader.get_file_type() != acl::sjson_file_type::raw_clip)
	{
		GMalloc->Free(RawSJSONData);
		return TEXT("SJSON file isn't a raw clip");
	}

	acl::sjson_raw_clip RawClip;
	if (!ClipReader.read_raw_clip(RawClip))
	{
		GMalloc->Free(RawSJSONData);
		return TEXT("Failed to read ACL raw clip from file");
	}

	OutACLSkeleton = MoveTemp(RawClip.skeleton);
	OutACLClip = MoveTemp(RawClip.clip);

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

	// Use the ACL code if we can to calculate the error instead of approximating it with UE4.
	UAnimBoneCompressionCodec_ACLBase* ACLCodec = Cast<UAnimBoneCompressionCodec_ACLBase>(UE4Clip->CompressedData.BoneCompressionCodec);
	if (ACLCodec != nullptr)
	{
		ACLAllocator AllocatorImpl;
		const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(UE4Clip->CompressedData.CompressedByteStream.GetData());

		CompressionSettings Settings;
		ACLCodec->GetCompressionSettings(Settings);

		TransformErrorMetric DefaultErrorMetric;
		AdditiveTransformErrorMetric<AdditiveClipFormat8::Additive1> AdditiveErrorMetric;
		if (ACLClip.get_additive_base() != nullptr)
		{
			Settings.error_metric = &AdditiveErrorMetric;
		}
		else
		{
			Settings.error_metric = &DefaultErrorMetric;
		}

		uniformly_sampled::DecompressionContext<uniformly_sampled::DebugDecompressionSettings> Context;
		Context.initialize(*CompressedClipData);
		const BoneError bone_error = calculate_compressed_clip_error(AllocatorImpl, ACLClip, *Settings.error_metric, Context);

		OutWorstBone = bone_error.index;
		OutMaxError = bone_error.error;
		OutWorstSampleTime = bone_error.sample_time;
		return;
	}

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

	const TransformErrorMetric ErrorMetric;

	// Use the ACL code if we can to calculate the error instead of approximating it with UE4.
	UAnimBoneCompressionCodec_ACLBase* ACLCodec = Cast<UAnimBoneCompressionCodec_ACLBase>(UE4Clip->CompressedData.BoneCompressionCodec);
	if (ACLCodec != nullptr)
	{
		ACLAllocator Allocator;

		uint16_t NumOutputBones = 0;
		uint16_t* OutputBoneMapping = acl::create_output_bone_mapping(Allocator, ACLClip, NumOutputBones);

		TArray<Transform_32> LossyRemappedPoseTransforms;
		RawPoseTransforms.AddUninitialized(NumBones);
		LossyPoseTransforms.AddUninitialized(NumOutputBones);
		LossyRemappedPoseTransforms.AddUninitialized(NumBones);

		ACLAllocator AllocatorImpl;
		const CompressedClip* CompressedClipData = reinterpret_cast<const CompressedClip*>(UE4Clip->CompressedData.CompressedByteStream.GetData());

		uniformly_sampled::DecompressionContext<uniformly_sampled::DebugDecompressionSettings> Context;
		Context.initialize(*CompressedClipData);

		Writer["error_per_frame_and_bone"] = [&](sjson::ArrayWriter& Writer)
		{
			DefaultOutputWriter PoseWriter(LossyPoseTransforms.GetData(), NumOutputBones);

			for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
			{
				// Sample our streams and calculate the error
				const float SampleTime = min(float(SampleIndex) / SampleRate, ClipDuration);

				ACLClip.sample_pose(SampleTime, SampleRoundingPolicy::Nearest, RawPoseTransforms.GetData(), NumBones);

				Context.seek(SampleTime, SampleRoundingPolicy::Nearest);
				Context.decompress_pose(PoseWriter);

				// Perform remapping by copying the raw pose first and we overwrite with the decompressed pose if
				// the data is available
				LossyRemappedPoseTransforms = RawPoseTransforms;
				for (uint16_t OutputIndex = 0; OutputIndex < NumOutputBones; ++OutputIndex)
				{
					const uint16_t BoneIndex = OutputBoneMapping[OutputIndex];
					LossyRemappedPoseTransforms[BoneIndex] = LossyPoseTransforms[OutputIndex];
				}

				Writer.push_newline();
				Writer.push([&](sjson::ArrayWriter& Writer)
				{
					for (uint16 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
					{
						float Error;
						if (HasScale)
							Error = ErrorMetric.calculate_object_bone_error(ACLSkeleton, RawPoseTransforms.GetData(), nullptr, LossyRemappedPoseTransforms.GetData(), BoneIndex);
						else
							Error = ErrorMetric.calculate_object_bone_error_no_scale(ACLSkeleton, RawPoseTransforms.GetData(), nullptr, LossyRemappedPoseTransforms.GetData(), BoneIndex);

						Writer.push(Error);
					}
				});
			}
		};

		acl::deallocate_type_array(Allocator, OutputBoneMapping, NumOutputBones);
		return;
	}

	RawPoseTransforms.AddUninitialized(NumBones);
	LossyPoseTransforms.AddUninitialized(NumBones);

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
	UAnimBoneCompressionSettings* AutoCompressor;
	UAnimBoneCompressionSettings* ACLCompressor;
	UAnimBoneCompressionSettings* KeyReductionCompressor;

	UAnimSequence* UE4Clip;
	USkeleton* UE4Skeleton;

	TUniquePtr<acl::AnimationClip> ACLClip;
	TUniquePtr<acl::RigidSkeleton> ACLSkeleton;

	uint32 ACLRawSize;
	int32 UE4RawSize;
};

static FString GetCodecName(UAnimBoneCompressionCodec* Codec)
{
	if (Codec == nullptr)
	{
		return TEXT("<null>");
	}

	if (Codec->Description.Len() > 0 && Codec->Description != TEXT("None"))
	{
		return Codec->Description;
	}

	return Codec->GetClass()->GetName();
}

static void CompressWithUE4Auto(FCompressionContext& Context, bool PerformExhaustiveDump, sjson::Writer& Writer)
{
	// Force recompression and avoid the DDC
	TGuardValue<int32> CompressGuard(Context.UE4Clip->CompressCommandletVersion, INDEX_NONE);

	const uint64 UE4StartTimeCycles = FPlatformTime::Cycles64();

	Context.UE4Clip->BoneCompressionSettings = Context.AutoCompressor;
	Context.UE4Clip->RequestSyncAnimRecompression();

	const uint64 UE4EndTimeCycles = FPlatformTime::Cycles64();

	const uint64 UE4ElapsedCycles = UE4EndTimeCycles - UE4StartTimeCycles;
	const double UE4ElapsedTimeSec = FPlatformTime::ToSeconds64(UE4ElapsedCycles);

	if (Context.UE4Clip->IsCompressedDataValid())
	{
		const AnimationErrorStats UE4ErrorStats = Context.UE4Clip->CompressedData.CompressedDataStructure->BoneCompressionErrorStats;

		uint16 WorstBone;
		float MaxError;
		float WorstSampleTime;
		CalculateClipError(*Context.ACLClip, *Context.ACLSkeleton, Context.UE4Clip, Context.UE4Skeleton, WorstBone, MaxError, WorstSampleTime);

		const int32 CompressedSize = Context.UE4Clip->GetApproxCompressedSize();
		const double UE4CompressionRatio = double(Context.UE4RawSize) / double(CompressedSize);
		const double ACLCompressionRatio = double(Context.ACLRawSize) / double(CompressedSize);

		Writer["ue4_auto"] = [&](sjson::ObjectWriter& Writer)
		{
			Writer["algorithm_name"] = TCHAR_TO_ANSI(*Context.UE4Clip->BoneCompressionSettings->GetClass()->GetName());
			Writer["codec_name"] = TCHAR_TO_ANSI(*GetCodecName(Context.UE4Clip->CompressedData.BoneCompressionCodec));
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

			if (Context.UE4Clip->CompressedData.BoneCompressionCodec != nullptr && Context.UE4Clip->CompressedData.BoneCompressionCodec->IsA<UAnimCompress>())
			{
				const FUECompressedAnimData& AnimData = static_cast<FUECompressedAnimData&>(*Context.UE4Clip->CompressedData.CompressedDataStructure);
				Writer["rotation_format"] = TCHAR_TO_ANSI(*FAnimationUtils::GetAnimationCompressionFormatString(AnimData.RotationCompressionFormat));
				Writer["translation_format"] = TCHAR_TO_ANSI(*FAnimationUtils::GetAnimationCompressionFormatString(AnimData.TranslationCompressionFormat));
				Writer["scale_format"] = TCHAR_TO_ANSI(*FAnimationUtils::GetAnimationCompressionFormatString(AnimData.ScaleCompressionFormat));
			}

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

	Context.UE4Clip->BoneCompressionSettings = Context.ACLCompressor;
	Context.UE4Clip->RequestSyncAnimRecompression();

	const uint64 ACLEndTimeCycles = FPlatformTime::Cycles64();

	const uint64 ACLElapsedCycles = ACLEndTimeCycles - ACLStartTimeCycles;
	const double ACLElapsedTimeSec = FPlatformTime::ToSeconds64(ACLElapsedCycles);

	if (Context.UE4Clip->IsCompressedDataValid())
	{
		const AnimationErrorStats UE4ErrorStats = Context.UE4Clip->CompressedData.CompressedDataStructure->BoneCompressionErrorStats;

		uint16 WorstBone;
		float MaxError;
		float WorstSampleTime;
		CalculateClipError(*Context.ACLClip, *Context.ACLSkeleton, Context.UE4Clip, Context.UE4Skeleton, WorstBone, MaxError, WorstSampleTime);

		const int32 CompressedSize = Context.UE4Clip->GetApproxCompressedSize();
		const double UE4CompressionRatio = double(Context.UE4RawSize) / double(CompressedSize);
		const double ACLCompressionRatio = double(Context.ACLRawSize) / double(CompressedSize);

		Writer["ue4_acl"] = [&](sjson::ObjectWriter& Writer)
		{
			Writer["algorithm_name"] = TCHAR_TO_ANSI(*Context.UE4Clip->BoneCompressionSettings->GetClass()->GetName());
			Writer["codec_name"] = TCHAR_TO_ANSI(*GetCodecName(Context.UE4Clip->CompressedData.BoneCompressionCodec));
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

	Context.UE4Clip->BoneCompressionSettings = Context.KeyReductionCompressor;
	Context.UE4Clip->RequestSyncAnimRecompression();

	const uint64 UE4EndTimeCycles = FPlatformTime::Cycles64();

	const uint64 UE4ElapsedCycles = UE4EndTimeCycles - UE4StartTimeCycles;
	const double UE4ElapsedTimeSec = FPlatformTime::ToSeconds64(UE4ElapsedCycles);

	if (Context.UE4Clip->IsCompressedDataValid())
	{
		const FUECompressedAnimData& AnimData = static_cast<FUECompressedAnimData&>(*Context.UE4Clip->CompressedData.CompressedDataStructure);
		const AnimationErrorStats UE4ErrorStats = Context.UE4Clip->CompressedData.CompressedDataStructure->BoneCompressionErrorStats;

		uint16 WorstBone;
		float MaxError;
		float WorstSampleTime;
		CalculateClipError(*Context.ACLClip, *Context.ACLSkeleton, Context.UE4Clip, Context.UE4Skeleton, WorstBone, MaxError, WorstSampleTime);

		const int32 CompressedSize = Context.UE4Clip->GetApproxCompressedSize();
		const double UE4CompressionRatio = double(Context.UE4RawSize) / double(CompressedSize);
		const double ACLCompressionRatio = double(Context.ACLRawSize) / double(CompressedSize);

		Writer["ue4_keyreduction"] = [&](sjson::ObjectWriter& Writer)
		{
			Writer["algorithm_name"] = TCHAR_TO_ANSI(*Context.UE4Clip->BoneCompressionSettings->GetClass()->GetName());
			Writer["codec_name"] = TCHAR_TO_ANSI(*GetCodecName(Context.UE4Clip->CompressedData.BoneCompressionCodec));
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

				const int32* TrackOffsets = AnimData.CompressedTrackOffsets.GetData();
				const auto& ScaleOffsets = AnimData.CompressedScaleOffsets;

				const AnimationCompressionFormat RotationFormat = AnimData.RotationCompressionFormat;
				const AnimationCompressionFormat TranslationFormat = AnimData.TranslationCompressionFormat;
				const AnimationCompressionFormat ScaleFormat = AnimData.ScaleCompressionFormat;

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

				const uint8* ByteStream = AnimData.CompressedByteStream.GetData();
				const int32* TrackOffsets = AnimData.CompressedTrackOffsets.GetData();
				const auto& ScaleOffsets = AnimData.CompressedScaleOffsets;

				const AnimationCompressionFormat RotationFormat = AnimData.RotationCompressionFormat;
				const AnimationCompressionFormat TranslationFormat = AnimData.TranslationCompressionFormat;
				const AnimationCompressionFormat ScaleFormat = AnimData.ScaleCompressionFormat;

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
						if (NumTransKeys > 1 && IsKeyDropped(AnimData.CompressedNumberOfFrames, TransFrameTable, NumTransKeys, FrameRate, SampleTime))
						{
							DroppedTransCount++;
						}

						const int32 RotKeysOffset = TrackData[2];
						const int32 NumRotKeys = TrackData[3];
						const uint8* RotStream = ByteStream + RotKeysOffset;

						const uint8* RotFrameTable = RotStream + RotationStreamOffset + (NumRotKeys * CompressedRotationStrides[RotationFormat] * CompressedRotationNum[RotationFormat]);
						RotFrameTable = Align(RotFrameTable, 4);

						// Skip constant/default tracks
						if (NumRotKeys > 1 && IsKeyDropped(AnimData.CompressedNumberOfFrames, RotFrameTable, NumRotKeys, FrameRate, SampleTime))
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
							if (NumScaleKeys > 1 && IsKeyDropped(AnimData.CompressedNumberOfFrames, ScaleFrameTable, NumScaleKeys, FrameRate, SampleTime))
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

			// Make sure all our required dependencies are loaded
			FAnimationUtils::EnsureAnimSequenceLoaded(*UE4Clip);

			USkeleton* UE4Skeleton = UE4Clip->GetSkeleton();
			if (UE4Skeleton == nullptr)
			{
				continue;
			}

			FString Filename = UE4Clip->GetPathName();
			if (StatsCommandlet->PerformCompression)
			{
				Filename = FString::Printf(TEXT("%X_stats.sjson"), GetTypeHash(Filename));
			}
			else if (StatsCommandlet->PerformClipExtraction)
			{
				Filename = FString::Printf(TEXT("%X.acl.sjson"), GetTypeHash(Filename));
			}

			FString UE4OutputPath = FPaths::Combine(*StatsCommandlet->OutputDir, *Filename).Replace(TEXT("/"), TEXT("\\"));

			if (StatsCommandlet->ResumeTask && FileManager.FileExists(*UE4OutputPath))
			{
				continue;
			}

			const bool bIsAdditive = UE4Clip->IsValidAdditive();
			if (bIsAdditive && StatsCommandlet->SkipAdditiveClips)
			{
				continue;
			}

			FCompressionContext Context;
			Context.AutoCompressor = StatsCommandlet->AutoCompressionSettings;
			Context.ACLCompressor = StatsCommandlet->ACLCompressionSettings;
			Context.UE4Clip = UE4Clip;
			Context.UE4Skeleton = UE4Skeleton;

			FCompressibleAnimData CompressibleData(UE4Clip, false);

			TUniquePtr<acl::RigidSkeleton> ACLSkeleton = BuildACLSkeleton(Allocator, CompressibleData, StatsCommandlet->ACLCodec->DefaultVirtualVertexDistance, StatsCommandlet->ACLCodec->SafeVirtualVertexDistance);

			TUniquePtr<acl::AnimationClip> ACLClip = BuildACLClip(Allocator, CompressibleData, *ACLSkeleton, false);
			TUniquePtr<acl::AnimationClip> ACLBaseClip;
			if (bIsAdditive)
			{
				ACLBaseClip = BuildACLClip(Allocator, CompressibleData, *ACLSkeleton, true);

				ACLClip->set_additive_base(ACLBaseClip.Get(), acl::AdditiveClipFormat8::Additive1);
			}

			Context.ACLClip = MoveTemp(ACLClip);
			Context.ACLSkeleton = MoveTemp(ACLSkeleton);
			Context.ACLRawSize = Context.ACLClip->get_raw_size();
			Context.UE4RawSize = UE4Clip->GetApproxRawSize();

			if (StatsCommandlet->PerformCompression)
			{
				UE_LOG(LogAnimationCompression, Verbose, TEXT("Compressing: %s (%d / %d)"), *UE4Clip->GetPathName(), SequenceIndex, NumAnimSequences);

				FArchive* OutputWriter = FileManager.CreateFileWriter(*UE4OutputPath);
				if (OutputWriter == nullptr)
				{
					// Opening the file handle can fail if the file path is too long on Windows. UE4 does not properly handle long paths
					// and adding the \\?\ prefix manually doesn't work, UE4 mangles it when it normalizes the path.
					UE4Clip->RecycleAnimSequence();
					continue;
				}

				// Make sure any pending async compression that might have started during load or construction is done
				UE4Clip->WaitOnExistingCompression();

				UE4SJSONStreamWriter StreamWriter(OutputWriter);
				sjson::Writer Writer(StreamWriter);

				Writer["duration"] = UE4Clip->SequenceLength;
				Writer["num_samples"] = CompressibleData.NumFrames;
				Writer["ue4_raw_size"] = Context.UE4RawSize;
				Writer["acl_raw_size"] = Context.ACLRawSize;

				if (StatsCommandlet->TryAutomaticCompression)
				{
					CompressWithUE4Auto(Context, StatsCommandlet->PerformExhaustiveDump, Writer);
				}

				if (StatsCommandlet->TryACLCompression)
				{
					CompressWithACL(Context, StatsCommandlet->PerformExhaustiveDump, Writer);
				}

				if (StatsCommandlet->TryKeyReduction)
				{
					CompressWithUE4KeyReduction(Context, StatsCommandlet->PerformExhaustiveDump, Writer);
				}

				OutputWriter->Close();
			}
			else if (StatsCommandlet->PerformClipExtraction)
			{
				UE_LOG(LogAnimationCompression, Verbose, TEXT("Extracting: %s (%d / %d)"), *UE4Clip->GetPathName(), SequenceIndex, NumAnimSequences);

				acl::CompressionSettings Settings;
				StatsCommandlet->ACLCodec->GetCompressionSettings(Settings);

				const char* Error = acl::write_acl_clip(*Context.ACLSkeleton, *Context.ACLClip, acl::AlgorithmType8::UniformlySampled, Settings, TCHAR_TO_ANSI(*UE4OutputPath));
				if (Error != nullptr)
				{
					UE_LOG(LogAnimationCompression, Warning, TEXT("Failed to write ACL clip file: %s"), ANSI_TO_TCHAR(Error));
				}
			}

			UE4Clip->RecycleAnimSequence();
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

	if (!ParamsMap.Contains(TEXT("output")))
	{
		UE_LOG(LogAnimationCompression, Error, TEXT("Missing commandlet argument: -output=<path/to/output/directory>"));
		return 0;
	}

	OutputDir = ParamsMap[TEXT("output")];

	PerformExhaustiveDump = Switches.Contains(TEXT("error"));
	PerformCompression = Switches.Contains(TEXT("compress"));
	PerformClipExtraction = Switches.Contains(TEXT("extract"));
	TryAutomaticCompression = Switches.Contains(TEXT("auto"));
	TryACLCompression = Switches.Contains(TEXT("acl"));
	TryKeyReductionRetarget = Switches.Contains(TEXT("keyreductionrt"));
	TryKeyReduction = TryKeyReductionRetarget || Switches.Contains(TEXT("keyreduction"));
	ResumeTask = Switches.Contains(TEXT("resume"));
	SkipAdditiveClips = Switches.Contains(TEXT("noadditive")) || true;	// Disabled for now, TODO add support for it
	const bool HasInput = ParamsMap.Contains(TEXT("input"));

	if (PerformClipExtraction)
	{
		// We don't support extracting additive clips
		SkipAdditiveClips = true;
	}

	if (PerformCompression && PerformClipExtraction)
	{
		UE_LOG(LogAnimationCompression, Error, TEXT("Cannot compress and extract clips at the same time"));
		return 0;
	}

	if (!PerformCompression && !PerformClipExtraction)
	{
		UE_LOG(LogAnimationCompression, Error, TEXT("Must compress or extract clips"));
		return 0;
	}

	if (PerformClipExtraction && ParamsMap.Contains(TEXT("input")))
	{
		UE_LOG(LogAnimationCompression, Error, TEXT("Cannot use an input directory when extracting clips"));
		return 0;
	}

	// Make sure to log everything
	LogAnimationCompression.SetVerbosity(ELogVerbosity::All);

	if (TryAutomaticCompression)
	{
		AutoCompressionSettings = FAnimationUtils::GetDefaultAnimationBoneCompressionSettings();
		AutoCompressionSettings->bForceBelowThreshold = true;

		if (ParamsMap.Contains(TEXT("MasterTolerance")))
		{
			AutoCompressionSettings->ErrorThreshold = FCString::Atof(*ParamsMap[TEXT("MasterTolerance")]);
		}
	}

	if (TryACLCompression || !HasInput)
	{
		ACLCompressionSettings = NewObject<UAnimBoneCompressionSettings>(this, UAnimBoneCompressionSettings::StaticClass());
		ACLCodec = NewObject<UAnimBoneCompressionCodec_ACL>(this, UAnimBoneCompressionCodec_ACL::StaticClass());
		ACLCompressionSettings->Codecs.Add(ACLCodec);
		ACLCompressionSettings->AddToRoot();
	}

	if (TryKeyReduction)
	{
		KeyReductionCompressionSettings = NewObject<UAnimBoneCompressionSettings>(this, UAnimBoneCompressionSettings::StaticClass());
		KeyReductionCodec = NewObject<UAnimCompress_RemoveLinearKeys>(this, UAnimCompress_RemoveLinearKeys::StaticClass());
		KeyReductionCodec->RotationCompressionFormat = ACF_Float96NoW;
		KeyReductionCodec->TranslationCompressionFormat = ACF_None;
		KeyReductionCodec->ScaleCompressionFormat = ACF_None;
		KeyReductionCodec->bActuallyFilterLinearKeys = true;
		KeyReductionCodec->bRetarget = TryKeyReductionRetarget;
		KeyReductionCompressionSettings->Codecs.Add(KeyReductionCodec);
		KeyReductionCompressionSettings->AddToRoot();
	}

	FFileManagerGeneric FileManager;
	FileManager.MakeDirectory(*OutputDir, true);

	if (!HasInput)
	{
		// No source directory, use the current project instead
		ACLRawDir = TEXT("");

		DoActionToAllPackages<UAnimSequence, CompressAnimationsFunctor>(this, Params.ToUpper());
		return 0;
	}
	else
	{
		check(PerformCompression);

		// Use source directory
		ACLRawDir = ParamsMap[TEXT("input")];

		UPackage* TempPackage = CreatePackage(nullptr, TEXT("/Temp/ACL"));

		ACLAllocator Allocator;

		TArray<FString> Files;
		FileManager.FindFiles(Files, *ACLRawDir, TEXT(".acl.sjson"));

		for (const FString& Filename : Files)
		{
			const FString ACLClipPath = FPaths::Combine(*ACLRawDir, *Filename);
			const FString UE4StatPath = FPaths::Combine(*OutputDir, *Filename.Replace(TEXT(".acl.sjson"), TEXT("_stats.sjson"), ESearchCase::CaseSensitive));

			if (ResumeTask && FileManager.FileExists(*UE4StatPath))
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

				// Make sure any pending async compression that might have started during load or construction is done
				UE4Clip->WaitOnExistingCompression();

				FCompressionContext Context;
				Context.AutoCompressor = AutoCompressionSettings;
				Context.ACLCompressor = ACLCompressionSettings;
				Context.KeyReductionCompressor = KeyReductionCompressionSettings;
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

					// Reset our compressed data
					UE4Clip->ClearCompressedBoneData();
					UE4Clip->ClearCompressedCurveData();
				}

				if (TryACLCompression)
				{
					CompressWithACL(Context, PerformExhaustiveDump, Writer);

					// Reset our compressed data
					UE4Clip->ClearCompressedBoneData();
					UE4Clip->ClearCompressedCurveData();
				}

				if (TryKeyReduction)
				{
					CompressWithUE4KeyReduction(Context, PerformExhaustiveDump, Writer);

					// Reset our compressed data
					UE4Clip->ClearCompressedBoneData();
					UE4Clip->ClearCompressedCurveData();
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
