// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "ACLStatsDumpCommandlet.h"

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

#include <acl/compression/impl/track_list_context.h>	// For create_output_track_mapping(..)
#include <acl/compression/convert.h>
#include <acl/compression/track_array.h>
#include <acl/compression/transform_error_metrics.h>
#include <acl/compression/track_error.h>
#include <acl/decompression/decompress.h>
#include <acl/io/clip_reader.h>
#include <acl/io/clip_writer.h>

#include <rtm/quatf.h>
#include <rtm/qvvf.h>
#include <rtm/vector4f.h>

//////////////////////////////////////////////////////////////////////////
// Commandlet example inspired by: https://github.com/ue4plugins/CommandletPlugin
// To run the commandlet, add to the commandline: "$(SolutionDir)$(ProjectName).uproject" -run=/Script/ACLPluginEditor.ACLStatsDump "-input=<path/to/raw/acl/sjson/files/directory>" "-output=<path/to/output/stats/directory>" -compress
//
// Usage:
//		-input=<directory>: If present all *acl.sjson files will be used as the input for the commandlet otherwise the current project is used
//		-output=<directory>: The commandlet output will be written at the given path (stats or dumped clips)
//		-compress: Commandlet will compress the input clips and output stats
//		-extract: Commandlet will extract the input clips into output *acl.sjson clips
//		-error: Enables the exhaustive error dumping
//		-auto: Uses automatic compression
//		-acl: Uses ACL compression
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

static const TCHAR* ReadACLClip(FFileManagerGeneric& FileManager, const FString& ACLClipPath, acl::iallocator& Allocator, acl::track_array_qvvf& OutTracks)
{
	FArchive* Reader = FileManager.CreateFileReader(*ACLClipPath);
	const int64 Size = Reader->TotalSize();

	// Allocate directly without a TArray to automatically manage the memory because some
	// clips are larger than 2 GB
	char* RawData = static_cast<char*>(GMalloc->Malloc(Size));

	Reader->Serialize(RawData, Size);
	Reader->Close();

	if (ACLClipPath.EndsWith(TEXT(".acl")))
	{
		acl::compressed_tracks* CompressedTracks = reinterpret_cast<acl::compressed_tracks*>(RawData);
		if (Size != CompressedTracks->get_size() || CompressedTracks->is_valid(true).any())
		{
			GMalloc->Free(RawData);
			return TEXT("Invalid binary ACL file provided");
		}

		const acl::error_result Result = acl::convert_track_list(Allocator, *CompressedTracks, OutTracks);
		if (Result.any())
		{
			GMalloc->Free(RawData);
			return TEXT("Failed to convert input binary track list");
		}
	}
	else
	{
		acl::clip_reader ClipReader(Allocator, RawData, Size);

		if (ClipReader.get_file_type() != acl::sjson_file_type::raw_clip)
		{
			GMalloc->Free(RawData);
			return TEXT("SJSON file isn't a raw clip");
		}

		acl::sjson_raw_clip RawClip;
		if (!ClipReader.read_raw_clip(RawClip))
		{
			GMalloc->Free(RawData);
			return TEXT("Failed to read ACL raw clip from file");
		}

		OutTracks = MoveTemp(RawClip.track_list);
	}

	GMalloc->Free(RawData);
	return nullptr;
}

static void ConvertSkeleton(const acl::track_array_qvvf& Tracks, USkeleton* UE4Skeleton)
{
	// Not terribly clean, we cast away the 'const' to modify the skeleton
	FReferenceSkeleton& RefSkeleton = const_cast<FReferenceSkeleton&>(UE4Skeleton->GetReferenceSkeleton());
	FReferenceSkeletonModifier SkeletonModifier(RefSkeleton, UE4Skeleton);

	const uint32 NumBones = Tracks.get_num_tracks();
	for (uint32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const acl::track_qvvf& Track = Tracks[BoneIndex];
		const acl::track_desc_transformf& Desc = Track.get_description();

		const FString BoneName = ANSI_TO_TCHAR(Track.get_name().c_str());

		FMeshBoneInfo UE4Bone;
		UE4Bone.Name = FName(*BoneName);
		UE4Bone.ParentIndex = Desc.parent_index == acl::k_invalid_track_index ? INDEX_NONE : Desc.parent_index;
		UE4Bone.ExportName = BoneName;

		SkeletonModifier.Add(UE4Bone, FTransform::Identity);
	}

	// When our modifier is destroyed here, it will rebuild the skeleton
}

static void ConvertClip(const acl::track_array_qvvf& Tracks, UAnimSequence* UE4Clip, USkeleton* UE4Skeleton)
{
	const uint32 NumSamples = Tracks.get_num_samples_per_track();

	UE4Clip->SequenceLength = FGenericPlatformMath::Max<float>(Tracks.get_duration(), MINIMUM_ANIMATION_LENGTH);
	UE4Clip->SetRawNumberOfFrame(NumSamples);
	UE4Clip->SetSkeleton(UE4Skeleton);

	if (NumSamples != 0)
	{
		const uint32 NumBones = Tracks.get_num_tracks();
		for (uint32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			const acl::track_qvvf& Track = Tracks[BoneIndex];

			FRawAnimSequenceTrack RawTrack;
			RawTrack.PosKeys.Empty();
			RawTrack.RotKeys.Empty();
			RawTrack.ScaleKeys.Empty();

			for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
			{
				const FQuat Rotation = QuatCast(rtm::quat_normalize(Track[SampleIndex].rotation));
				RawTrack.RotKeys.Add(Rotation);
			}

			for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
			{
				const FVector Translation = VectorCast(Track[SampleIndex].translation);
				RawTrack.PosKeys.Add(Translation);
			}

			for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
			{
				const FVector Scale = VectorCast(Track[SampleIndex].scale);
				RawTrack.ScaleKeys.Add(Scale);
			}

			const FName BoneName(Track.get_name().c_str());
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

static void SampleUE4Clip(const acl::track_array_qvvf& Tracks, USkeleton* UE4Skeleton, const UAnimSequence* UE4Clip, float SampleTime, rtm::qvvf* LossyPoseTransforms)
{
	const FReferenceSkeleton& RefSkeleton = UE4Skeleton->GetReferenceSkeleton();
	const TArray<FTransform>& RefSkeletonPose = UE4Skeleton->GetRefLocalPoses();

	const uint32 NumBones = Tracks.get_num_tracks();
	for (uint32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const acl::track_qvvf& Track = Tracks[BoneIndex];
		const FName BoneName(Track.get_name().c_str());
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

		const rtm::quatf Rotation = QuatCast(BoneTransform.GetRotation());
		const rtm::vector4f Translation = VectorCast(BoneTransform.GetTranslation());
		const rtm::vector4f Scale = VectorCast(BoneTransform.GetScale3D());
		LossyPoseTransforms[BoneIndex] = rtm::qvv_set(Rotation, Translation, Scale);
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

struct SimpleTransformWriter final : public acl::track_writer
{
	explicit SimpleTransformWriter(TArray<rtm::qvvf>& Transforms_) : Transforms(Transforms_) {}

	TArray<rtm::qvvf>& Transforms;

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a quaternion rotation value for a specified bone index.
	void RTM_SIMD_CALL write_rotation(uint32_t TrackIndex, rtm::quatf_arg0 Rotation)
	{
		Transforms[TrackIndex].rotation = Rotation;
	}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a translation value for a specified bone index.
	void RTM_SIMD_CALL write_translation(uint32_t TrackIndex, rtm::vector4f_arg0 Translation)
	{
		Transforms[TrackIndex].translation = Translation;
	}

	//////////////////////////////////////////////////////////////////////////
	// Called by the decoder to write out a scale value for a specified bone index.
	void RTM_SIMD_CALL write_scale(uint32_t TrackIndex, rtm::vector4f_arg0 Scale)
	{
		Transforms[TrackIndex].scale = Scale;
	}
};

static void CalculateClipError(const acl::track_array_qvvf& Tracks, const UAnimSequence* UE4Clip, USkeleton* UE4Skeleton, uint32& OutWorstBone, float& OutMaxError, float& OutWorstSampleTime)
{
	// Use the ACL code if we can to calculate the error instead of approximating it with UE4.
	UAnimBoneCompressionCodec_ACLBase* ACLCodec = Cast<UAnimBoneCompressionCodec_ACLBase>(UE4Clip->CompressedData.BoneCompressionCodec);
	if (ACLCodec != nullptr)
	{
		const acl::compressed_tracks* CompressedClipData = acl::make_compressed_tracks(UE4Clip->CompressedData.CompressedByteStream.GetData());

		const acl::qvvf_transform_error_metric ErrorMetric;

		acl::decompression_context<acl::debug_transform_decompression_settings> Context;
		Context.initialize(*CompressedClipData);
		const acl::track_error TrackError = acl::calculate_compression_error(ACLAllocatorImpl, Tracks, Context, ErrorMetric);

		OutWorstBone = TrackError.index;
		OutMaxError = TrackError.error;
		OutWorstSampleTime = TrackError.sample_time;
		return;
	}

	const uint32 NumBones = Tracks.get_num_tracks();
	const float ClipDuration = Tracks.get_duration();
	const float SampleRate = Tracks.get_sample_rate();
	const uint32 NumSamples = Tracks.get_num_samples_per_track();
	const bool HasScale = UE4ClipHasScale(UE4Clip);

	TArray<rtm::qvvf> RawLocalPoseTransforms;
	TArray<rtm::qvvf> RawObjectPoseTransforms;
	TArray<rtm::qvvf> LossyLocalPoseTransforms;
	TArray<rtm::qvvf> LossyObjectPoseTransforms;
	RawLocalPoseTransforms.AddUninitialized(NumBones);
	RawObjectPoseTransforms.AddUninitialized(NumBones);
	LossyLocalPoseTransforms.AddUninitialized(NumBones);
	LossyObjectPoseTransforms.AddUninitialized(NumBones);

	uint32 WorstBone = acl::k_invalid_track_index;
	float MaxError = 0.0f;
	float WorstSampleTime = 0.0f;

	const acl::qvvf_transform_error_metric ErrorMetric;
	SimpleTransformWriter RawWriter(RawLocalPoseTransforms);

	TArray<uint32> ParentTransformIndices;
	TArray<uint32> SelfTransformIndices;
	ParentTransformIndices.AddUninitialized(NumBones);
	SelfTransformIndices.AddUninitialized(NumBones);

	for (uint32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const acl::track_qvvf& Track = Tracks[BoneIndex];
		const acl::track_desc_transformf& Desc = Track.get_description();

		ParentTransformIndices[BoneIndex] = Desc.parent_index;
		SelfTransformIndices[BoneIndex] = BoneIndex;
	}

	acl::itransform_error_metric::local_to_object_space_args local_to_object_space_args_raw;
	local_to_object_space_args_raw.dirty_transform_indices = SelfTransformIndices.GetData();
	local_to_object_space_args_raw.num_dirty_transforms = NumBones;
	local_to_object_space_args_raw.parent_transform_indices = ParentTransformIndices.GetData();
	local_to_object_space_args_raw.local_transforms = RawLocalPoseTransforms.GetData();
	local_to_object_space_args_raw.num_transforms = NumBones;

	acl::itransform_error_metric::local_to_object_space_args local_to_object_space_args_lossy = local_to_object_space_args_raw;
	local_to_object_space_args_lossy.local_transforms = LossyLocalPoseTransforms.GetData();

	for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
	{
		// Sample our streams and calculate the error
		const float SampleTime = rtm::scalar_min(float(SampleIndex) / SampleRate, ClipDuration);

		Tracks.sample_tracks(SampleTime, acl::sample_rounding_policy::none, RawWriter);
		SampleUE4Clip(Tracks, UE4Skeleton, UE4Clip, SampleTime, LossyLocalPoseTransforms.GetData());

		if (HasScale)
		{
			ErrorMetric.local_to_object_space(local_to_object_space_args_raw, RawObjectPoseTransforms.GetData());
			ErrorMetric.local_to_object_space(local_to_object_space_args_lossy, LossyObjectPoseTransforms.GetData());
		}
		else
		{
			ErrorMetric.local_to_object_space_no_scale(local_to_object_space_args_raw, RawObjectPoseTransforms.GetData());
			ErrorMetric.local_to_object_space_no_scale(local_to_object_space_args_lossy, LossyObjectPoseTransforms.GetData());
		}

		for (uint32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			const acl::track_qvvf& Track = Tracks[BoneIndex];
			const acl::track_desc_transformf& Desc = Track.get_description();

			acl::itransform_error_metric::calculate_error_args calculate_error_args;
			calculate_error_args.transform0 = &RawObjectPoseTransforms[BoneIndex];
			calculate_error_args.transform1 = &LossyObjectPoseTransforms[BoneIndex];
			calculate_error_args.construct_sphere_shell(Desc.shell_distance);

			float Error;
			if (HasScale)
				Error = rtm::scalar_cast(ErrorMetric.calculate_error(calculate_error_args));
			else
				Error = rtm::scalar_cast(ErrorMetric.calculate_error_no_scale(calculate_error_args));

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

static void DumpClipDetailedError(const acl::track_array_qvvf& Tracks, UAnimSequence* UE4Clip, USkeleton* UE4Skeleton, sjson::ObjectWriter& Writer)
{
	const uint32 NumBones = Tracks.get_num_tracks();
	const float ClipDuration = Tracks.get_duration();
	const float SampleRate = Tracks.get_sample_rate();
	const uint32 NumSamples = Tracks.get_num_samples_per_track();
	const bool HasScale = UE4ClipHasScale(UE4Clip);

	TArray<rtm::qvvf> RawLocalPoseTransforms;
	TArray<rtm::qvvf> RawObjectPoseTransforms;
	TArray<rtm::qvvf> LossyLocalPoseTransforms;
	TArray<rtm::qvvf> LossyObjectPoseTransforms;
	RawLocalPoseTransforms.AddUninitialized(NumBones);
	RawObjectPoseTransforms.AddUninitialized(NumBones);
	LossyLocalPoseTransforms.AddUninitialized(NumBones);
	LossyObjectPoseTransforms.AddUninitialized(NumBones);

	const acl::qvvf_transform_error_metric ErrorMetric;

	SimpleTransformWriter RawWriter(RawLocalPoseTransforms);

	TArray<uint32> ParentTransformIndices;
	TArray<uint32> SelfTransformIndices;
	ParentTransformIndices.AddUninitialized(NumBones);
	SelfTransformIndices.AddUninitialized(NumBones);

	for (uint32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const acl::track_qvvf& Track = Tracks[BoneIndex];
		const acl::track_desc_transformf& Desc = Track.get_description();

		ParentTransformIndices[BoneIndex] = Desc.parent_index;
		SelfTransformIndices[BoneIndex] = BoneIndex;
	}

	acl::itransform_error_metric::local_to_object_space_args local_to_object_space_args_raw;
	local_to_object_space_args_raw.dirty_transform_indices = SelfTransformIndices.GetData();
	local_to_object_space_args_raw.num_dirty_transforms = NumBones;
	local_to_object_space_args_raw.parent_transform_indices = ParentTransformIndices.GetData();
	local_to_object_space_args_raw.local_transforms = RawLocalPoseTransforms.GetData();
	local_to_object_space_args_raw.num_transforms = NumBones;

	acl::itransform_error_metric::local_to_object_space_args local_to_object_space_args_lossy = local_to_object_space_args_raw;
	local_to_object_space_args_lossy.local_transforms = LossyLocalPoseTransforms.GetData();

	// Use the ACL code if we can to calculate the error instead of approximating it with UE4.
	UAnimBoneCompressionCodec_ACLBase* ACLCodec = Cast<UAnimBoneCompressionCodec_ACLBase>(UE4Clip->CompressedData.BoneCompressionCodec);
	if (ACLCodec != nullptr)
	{
		uint32 NumOutputBones = 0;
		uint32* OutputBoneMapping = acl::acl_impl::create_output_track_mapping(ACLAllocatorImpl, Tracks, NumOutputBones);

		TArray<rtm::qvvf> LossyRemappedLocalPoseTransforms;
		LossyRemappedLocalPoseTransforms.AddUninitialized(NumBones);

		local_to_object_space_args_lossy.local_transforms = LossyRemappedLocalPoseTransforms.GetData();

		const acl::compressed_tracks* CompressedClipData = acl::make_compressed_tracks(UE4Clip->CompressedData.CompressedByteStream.GetData());

		acl::decompression_context<acl::debug_transform_decompression_settings> Context;
		Context.initialize(*CompressedClipData);

		Writer["error_per_frame_and_bone"] = [&](sjson::ArrayWriter& Writer)
		{
			SimpleTransformWriter PoseWriter(LossyLocalPoseTransforms);

			for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
			{
				// Sample our streams and calculate the error
				const float SampleTime = rtm::scalar_min(float(SampleIndex) / SampleRate, ClipDuration);

				Tracks.sample_tracks(SampleTime, acl::sample_rounding_policy::none, RawWriter);

				Context.seek(SampleTime, acl::sample_rounding_policy::none);
				Context.decompress_tracks(PoseWriter);

				// Perform remapping by copying the raw pose first and we overwrite with the decompressed pose if
				// the data is available
				LossyRemappedLocalPoseTransforms = RawLocalPoseTransforms;
				for (uint32 OutputIndex = 0; OutputIndex < NumOutputBones; ++OutputIndex)
				{
					const uint32 BoneIndex = OutputBoneMapping[OutputIndex];
					LossyRemappedLocalPoseTransforms[BoneIndex] = LossyLocalPoseTransforms[OutputIndex];
				}

				if (HasScale)
				{
					ErrorMetric.local_to_object_space(local_to_object_space_args_raw, RawObjectPoseTransforms.GetData());
					ErrorMetric.local_to_object_space(local_to_object_space_args_lossy, LossyObjectPoseTransforms.GetData());
				}
				else
				{
					ErrorMetric.local_to_object_space_no_scale(local_to_object_space_args_raw, RawObjectPoseTransforms.GetData());
					ErrorMetric.local_to_object_space_no_scale(local_to_object_space_args_lossy, LossyObjectPoseTransforms.GetData());
				}

				Writer.push_newline();
				Writer.push([&](sjson::ArrayWriter& Writer)
					{
						for (uint32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
						{
							const acl::track_qvvf& Track = Tracks[BoneIndex];
							const acl::track_desc_transformf& Desc = Track.get_description();

							acl::itransform_error_metric::calculate_error_args calculate_error_args;
							calculate_error_args.transform0 = &RawObjectPoseTransforms[BoneIndex];
							calculate_error_args.transform1 = &LossyObjectPoseTransforms[BoneIndex];
							calculate_error_args.construct_sphere_shell(Desc.shell_distance);

							float Error;
							if (HasScale)
								Error = rtm::scalar_cast(ErrorMetric.calculate_error(calculate_error_args));
							else
								Error = rtm::scalar_cast(ErrorMetric.calculate_error_no_scale(calculate_error_args));

							Writer.push(Error);
						}
					});
			}
		};

		acl::deallocate_type_array(ACLAllocatorImpl, OutputBoneMapping, NumOutputBones);
		return;
	}

	Writer["error_per_frame_and_bone"] = [&](sjson::ArrayWriter& Writer)
	{
		for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
		{
			// Sample our streams and calculate the error
			const float SampleTime = rtm::scalar_min(float(SampleIndex) / SampleRate, ClipDuration);

			Tracks.sample_tracks(SampleTime, acl::sample_rounding_policy::none, RawWriter);
			SampleUE4Clip(Tracks, UE4Skeleton, UE4Clip, SampleTime, LossyLocalPoseTransforms.GetData());

			if (HasScale)
			{
				ErrorMetric.local_to_object_space(local_to_object_space_args_raw, RawObjectPoseTransforms.GetData());
				ErrorMetric.local_to_object_space(local_to_object_space_args_lossy, LossyObjectPoseTransforms.GetData());
			}
			else
			{
				ErrorMetric.local_to_object_space_no_scale(local_to_object_space_args_raw, RawObjectPoseTransforms.GetData());
				ErrorMetric.local_to_object_space_no_scale(local_to_object_space_args_lossy, LossyObjectPoseTransforms.GetData());
			}

			Writer.push_newline();
			Writer.push([&](sjson::ArrayWriter& Writer)
				{
					for (uint32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
					{
						const acl::track_qvvf& Track = Tracks[BoneIndex];
						const acl::track_desc_transformf& Desc = Track.get_description();

						acl::itransform_error_metric::calculate_error_args calculate_error_args;
						calculate_error_args.transform0 = &RawObjectPoseTransforms[BoneIndex];
						calculate_error_args.transform1 = &LossyObjectPoseTransforms[BoneIndex];
						calculate_error_args.construct_sphere_shell(Desc.shell_distance);

						float Error;
						if (HasScale)
							Error = rtm::scalar_cast(ErrorMetric.calculate_error(calculate_error_args));
						else
							Error = rtm::scalar_cast(ErrorMetric.calculate_error_no_scale(calculate_error_args));

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

	acl::track_array_qvvf ACLTracks;

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

		uint32 WorstBone;
		float MaxError;
		float WorstSampleTime;
		CalculateClipError(Context.ACLTracks, Context.UE4Clip, Context.UE4Skeleton, WorstBone, MaxError, WorstSampleTime);

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
				DumpClipDetailedError(Context.ACLTracks, Context.UE4Clip, Context.UE4Skeleton, Writer);
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

		uint32 WorstBone;
		float MaxError;
		float WorstSampleTime;
		CalculateClipError(Context.ACLTracks, Context.UE4Clip, Context.UE4Skeleton, WorstBone, MaxError, WorstSampleTime);

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
				DumpClipDetailedError(Context.ACLTracks, Context.UE4Clip, Context.UE4Skeleton, Writer);
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

		uint32 WorstBone;
		float MaxError;
		float WorstSampleTime;
		CalculateClipError(Context.ACLTracks, Context.UE4Clip, Context.UE4Skeleton, WorstBone, MaxError, WorstSampleTime);

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
				DumpClipDetailedError(Context.ACLTracks, Context.UE4Clip, Context.UE4Skeleton, Writer);
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

			acl::track_array_qvvf ACLTracks = BuildACLTransformTrackArray(ACLAllocatorImpl, CompressibleData, StatsCommandlet->ACLCodec->DefaultVirtualVertexDistance, StatsCommandlet->ACLCodec->SafeVirtualVertexDistance, false);

			// TODO: Add support for additive clips
			//acl::track_array_qvvf ACLBaseTracks;
			//if (CompressibleData.bIsValidAdditive)
				//ACLBaseTracks = BuildACLTransformTrackArray(Allocator, CompressibleData, StatsCommandlet->ACLCodec->DefaultVirtualVertexDistance, StatsCommandlet->ACLCodec->SafeVirtualVertexDistance, true);

			Context.ACLTracks = MoveTemp(ACLTracks);
			Context.ACLRawSize = Context.ACLTracks.get_raw_size();
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

				acl::compression_settings Settings;
				StatsCommandlet->ACLCodec->GetCompressionSettings(Settings);

				const acl::error_result Error = acl::write_track_list(Context.ACLTracks, Settings, TCHAR_TO_ANSI(*UE4OutputPath));
				if (Error.any())
				{
					UE_LOG(LogAnimationCompression, Warning, TEXT("Failed to write ACL clip file: %s"), ANSI_TO_TCHAR(Error.c_str()));
				}
			}

			UE4Clip->RecycleAnimSequence();
		}
	}
};

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

#if ENGINE_MINOR_VERSION >= 26
		UPackage* TempPackage = CreatePackage(TEXT("/Temp/ACL"));
#else
		UPackage* TempPackage = CreatePackage(nullptr, TEXT("/Temp/ACL"));
#endif

		TArray<FString> FilesLegacy;
		FileManager.FindFiles(FilesLegacy, *ACLRawDir, TEXT(".acl.sjson"));		// Legacy ASCII file format

		TArray<FString> FilesBinary;
		FileManager.FindFiles(FilesBinary, *ACLRawDir, TEXT(".acl"));			// ACL 2.0+ binary format

		TArray<FString> Files;
		Files.Append(FilesLegacy);
		Files.Append(FilesBinary);

		for (const FString& Filename : Files)
		{
			const FString ACLClipPath = FPaths::Combine(*ACLRawDir, *Filename);

			FString UE4StatFilename = Filename.Replace(TEXT(".acl.sjson"), TEXT("_stats.sjson"), ESearchCase::CaseSensitive);
			UE4StatFilename = UE4StatFilename.Replace(TEXT(".acl"), TEXT("_stats.sjson"), ESearchCase::CaseSensitive);

			const FString UE4StatPath = FPaths::Combine(*OutputDir, *UE4StatFilename);

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

			acl::track_array_qvvf ACLTracks;

			const TCHAR* ErrorMsg = ReadACLClip(FileManager, ACLClipPath, ACLAllocatorImpl, ACLTracks);
			if (ErrorMsg == nullptr)
			{
				USkeleton* UE4Skeleton = NewObject<USkeleton>(TempPackage, USkeleton::StaticClass());
				ConvertSkeleton(ACLTracks, UE4Skeleton);

				UAnimSequence* UE4Clip = NewObject<UAnimSequence>(TempPackage, UAnimSequence::StaticClass());
				ConvertClip(ACLTracks, UE4Clip, UE4Skeleton);

				// Make sure any pending async compression that might have started during load or construction is done
				UE4Clip->WaitOnExistingCompression();

				FCompressionContext Context;
				Context.AutoCompressor = AutoCompressionSettings;
				Context.ACLCompressor = ACLCompressionSettings;
				Context.KeyReductionCompressor = KeyReductionCompressionSettings;
				Context.UE4Clip = UE4Clip;
				Context.UE4Skeleton = UE4Skeleton;
				Context.ACLTracks = MoveTemp(ACLTracks);

				Context.ACLRawSize = Context.ACLTracks.get_raw_size();
				Context.UE4RawSize = UE4Clip->GetApproxRawSize();

				Writer["duration"] = UE4Clip->SequenceLength;
				Writer["num_samples"] = Context.ACLTracks.get_num_samples_per_track();
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

	return 0;
}
