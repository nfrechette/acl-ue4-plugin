// Copyright 2020 Nicholas Frechette. All Rights Reserved.

#include "AnimCurveCompressionCodec_ACL.h"

#include "ACLImpl.h"

#if WITH_EDITORONLY_DATA
#include "AnimationCompression.h"
#include "Animation/MorphTarget.h"
#include "Rendering/SkeletalMeshModel.h"

#include <acl/compression/compress.h>
#include <acl/compression/track.h>
#include <acl/compression/track_array.h>
#include <acl/compression/track_error.h>
#endif	// WITH_EDITOR

#include <acl/decompression/decompress.h>

UAnimCurveCompressionCodec_ACL::UAnimCurveCompressionCodec_ACL(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	CurvePrecision = 0.001f;
	MorphTargetPositionPrecision = 0.01f;		// 0.01cm, conservative enough for cinematographic quality
#endif
}

#if WITH_EDITORONLY_DATA
void UAnimCurveCompressionCodec_ACL::PopulateDDCKey(FArchive& Ar)
{
	Super::PopulateDDCKey(Ar);

	Ar << CurvePrecision;
	Ar << MorphTargetPositionPrecision;

	if (MorphTargetSource != nullptr)
	{
		FSkeletalMeshModel* MeshModel = MorphTargetSource->GetImportedModel();
		if (MeshModel != nullptr)
		{
			Ar << MeshModel->SkeletalMeshModelGUID;
		}
	}

	uint32 ForceRebuildVersion = 2;
	Ar << ForceRebuildVersion;

	acl::compression_settings Settings;
	uint32 SettingsHash = Settings.get_hash();
	Ar << SettingsHash;
}

// For each curve, returns its largest position delta if the curve is for a morph target, 0.0 otherwise
static TArray<float> GetMorphTargetMaxPositionDeltas(const FCompressibleAnimData& AnimSeq, const USkeletalMesh* MorphTargetSource)
{
	const int32 NumCurves = AnimSeq.RawCurveData.FloatCurves.Num();

	TArray<float> MorphTargetMaxPositionDeltas;
	MorphTargetMaxPositionDeltas.AddZeroed(NumCurves);

	if (MorphTargetSource == nullptr)
	{
		return MorphTargetMaxPositionDeltas;
	}

	for (int32 CurveIndex = 0; CurveIndex < NumCurves; ++CurveIndex)
	{
		float MaxDeltaPosition = 0.0f;

		const FFloatCurve& Curve = AnimSeq.RawCurveData.FloatCurves[CurveIndex];
		UMorphTarget* Target = MorphTargetSource->FindMorphTarget(Curve.Name.DisplayName);
		if (Target != nullptr)
		{
			// This curve drives a morph target, find the largest displacement it can have
			const int32 LODIndex = 0;
			int32 NumDeltas = 0;
			const FMorphTargetDelta* Deltas = Target->GetMorphTargetDelta(LODIndex, NumDeltas);
			for (int32 DeltaIndex = 0; DeltaIndex < NumDeltas; ++DeltaIndex)
			{
				const FMorphTargetDelta& Delta = Deltas[DeltaIndex];
				MaxDeltaPosition = FMath::Max(MaxDeltaPosition, Delta.PositionDelta.Size());
			}
		}

		MorphTargetMaxPositionDeltas[CurveIndex] = MaxDeltaPosition;
	}

	return MorphTargetMaxPositionDeltas;
}

bool UAnimCurveCompressionCodec_ACL::Compress(const FCompressibleAnimData& AnimSeq, FAnimCurveCompressionResult& OutResult)
{
	const TArray<float> MorphTargetMaxPositionDeltas = GetMorphTargetMaxPositionDeltas(AnimSeq, MorphTargetSource);

	const int32 NumCurves = AnimSeq.RawCurveData.FloatCurves.Num();
	if (NumCurves == 0)
	{
		// Nothing to compress
		OutResult.CompressedBytes.Empty(0);
		OutResult.Codec = this;
		return true;
	}

	const int32 NumSamples = AnimSeq.NumFrames;
	const float SequenceLength = AnimSeq.SequenceLength;

	const bool bIsStaticPose = NumSamples <= 1 || SequenceLength < 0.0001f;
	const float SampleRate = bIsStaticPose ? 30.0f : (float(NumSamples - 1) / SequenceLength);
	const float InvSampleRate = 1.0f / SampleRate;

	acl::track_array_float1f Tracks(ACLAllocatorImpl, NumCurves);

	for (int32 CurveIndex = 0; CurveIndex < NumCurves; ++CurveIndex)
	{
		const FFloatCurve& Curve = AnimSeq.RawCurveData.FloatCurves[CurveIndex];
		const float MaxPositionDelta = MorphTargetMaxPositionDeltas[CurveIndex];

		// If our curve drives a morph target, we use a different precision value with world space units.
		// This is much easier to tune and control: 0.1mm precision is clear.
		// In order to this this, we must convert that precision value into a value that makes sense for the curve
		// since the animated blend weight doesn't have any units: it's a scaling factor.
		// The morph target math is like this for every vertex: result vtx = ref vtx + (target vtx - ref vtx) * blend weight
		// (target vtx - ref vtx) is our deformation difference (or delta) and we scale it between 0.0 and 1.0 with our blend weight.
		// At 0.0, the resulting vertex is 100% the reference vertex.
		// At 1.0, the resulting vertex is 100% the target vertex.
		// This can thus be re-written as follow: result vtx = ref vtx + vtx delta * blend weight
		// From this, it follows that any error we introduce into the blend weight will impact the delta linearly.
		// If our delta measures 1 meter, an error of 10% translates into 0.1 meter.
		// If our delta measures 1 cm, an error of 10% translates into 0.1 cm.
		// Thus, for a given error quantity, a larger delta means a larger resulting difference from the original value.
		// If the delta is zero, any error is irrelevant as it will have no measurable impact.
		// By dividing the precision value we want with the delta length, we can control how much precision our blend weight needs.
		// If we want 0.01 cm precision and our largest vertex displacement is 3 cm, the blend weight precision needs to be:
		// 0.01 cm / 3.00 cm = 0.0033 (with the units cancelling out just like we need)
		// Another way to think about it is that every 0.0033 increment of the blend weight results in an increment of 0.01 cm
		// when our displacement delta is 3 cm.
		// 0.01 cm / 50.00 cm = 0.0002 (if our delta increases, we need to retain more blend weight precision)
		// 0.01 cm / 1.00 cm = 0.01
		// Each blend weight curve will drive a different target position for many vertices and this way, we can specify
		// a single value for the world space precision we want to achieve for every vertex and every blend weight curve
		// will end up with the precision value it needs.
		//
		// If our curve doesn't drive a morph target, we use the supplied CurvePrecision instead.

		const float Precision = MaxPositionDelta > 0.0f ? (MorphTargetPositionPrecision / MaxPositionDelta) : CurvePrecision;

		acl::track_desc_scalarf Desc;
		Desc.output_index = CurveIndex;
		Desc.precision = Precision;

		acl::track_float1f Track = acl::track_float1f::make_reserve(Desc, ACLAllocatorImpl, NumSamples, SampleRate);
		for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
		{
			const float SampleTime = FMath::Clamp(SampleIndex * InvSampleRate, 0.0f, SequenceLength);
			const float SampleValue = Curve.FloatCurve.Eval(SampleTime);

			Track[SampleIndex] = SampleValue;
		}

		Tracks[CurveIndex] = MoveTemp(Track);
	}

	acl::compression_settings Settings;

	acl::compressed_tracks* CompressedTracks = nullptr;
	acl::output_stats Stats;
	const acl::error_result CompressionResult = acl::compress_track_list(ACLAllocatorImpl, Tracks, Settings, CompressedTracks, Stats);

	if (CompressionResult.any())
	{
		UE_LOG(LogAnimationCompression, Warning, TEXT("ACL failed to compress curves: %s"), ANSI_TO_TCHAR(CompressionResult.c_str()));
		return false;
	}

	checkSlow(CompressedTracks->is_valid(true).empty());

	const uint32 CompressedDataSize = CompressedTracks->get_size();

	OutResult.CompressedBytes.Empty(CompressedDataSize);
	OutResult.CompressedBytes.AddUninitialized(CompressedDataSize);
	FMemory::Memcpy(OutResult.CompressedBytes.GetData(), CompressedTracks, CompressedDataSize);

	OutResult.Codec = this;

#if !NO_LOGGING
	{
		acl::decompression_context<acl::debug_scalar_decompression_settings> Context;
		Context.initialize(*CompressedTracks);
		const acl::track_error Error = acl::calculate_compression_error(ACLAllocatorImpl, Tracks, Context);

		UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Curves compressed size: %u bytes"), CompressedDataSize);
		UE_LOG(LogAnimationCompression, Verbose, TEXT("ACL Curves error: %.4f (curve %u @ %.3f)"), Error.error, Error.index, Error.sample_time);
	}
#endif

	ACLAllocatorImpl.deallocate(CompressedTracks, CompressedDataSize);
	return true;
}
#endif // WITH_EDITORONLY_DATA

struct UE4CurveDecompressionSettings final : public acl::decompression_settings
{
	static constexpr bool is_track_type_supported(acl::track_type8 type) { return type == acl::track_type8::float1f; }

	// Only support our latest version
	static constexpr acl::compressed_tracks_version16 version_supported() { return acl::compressed_tracks_version16::latest; }

#if UE_BUILD_SHIPPING
	// Shipping builds do not need safety checks, by then the game has been tested enough
	// Only data corruption could cause a safety check to fail
	// We keep this disabled regardless because it is generally better to output a T-pose than to have a
	// potential crash. Corruption can happen and it would be unfortunate if a demo or playtest failed
	// as a result of a crash that we can otherwise recover from.
	//static constexpr bool skip_initialize_safety_checks() { return true; }
#endif
};

struct UE4CurveWriter final : public acl::track_writer
{
	const TArray<FSmartName>& CompressedCurveNames;
	FBlendedCurve& Curves;

	UE4CurveWriter(const TArray<FSmartName>& CompressedCurveNames_, FBlendedCurve& Curves_)
		: CompressedCurveNames(CompressedCurveNames_)
		, Curves(Curves_)
	{
	}

	void write_float1(uint32_t TrackIndex, rtm::scalarf_arg0 Value)
	{
		const FSmartName& CurveName = CompressedCurveNames[TrackIndex];
		if (Curves.IsEnabled(CurveName.UID))
		{
			Curves.Set(CurveName.UID, rtm::scalar_cast(Value));
		}
	}
};

void UAnimCurveCompressionCodec_ACL::DecompressCurves(const FCompressedAnimSequence& AnimSeq, FBlendedCurve& Curves, float CurrentTime) const
{
	const TArray<FSmartName>& CompressedCurveNames = AnimSeq.CompressedCurveNames;
	const int32 NumCurves = CompressedCurveNames.Num();

	if (NumCurves == 0)
	{
		return;
	}

	const acl::compressed_tracks* CompressedTracks = acl::make_compressed_tracks(AnimSeq.CompressedCurveByteStream.GetData());
	check(CompressedTracks != nullptr && CompressedTracks->is_valid(false).empty());

	acl::decompression_context<UE4CurveDecompressionSettings> Context;
	Context.initialize(*CompressedTracks);
	Context.seek(CurrentTime, acl::sample_rounding_policy::none);

	UE4CurveWriter TrackWriter(CompressedCurveNames, Curves);
	Context.decompress_tracks(TrackWriter);
}

struct UE4ScalarCurveWriter final : public acl::track_writer
{
	float SampleValue;

	UE4ScalarCurveWriter()
		: SampleValue(0.0f)
	{
	}

	void write_float1(uint32_t /*TrackIndex*/, rtm::scalarf_arg0 Value)
	{
		SampleValue = rtm::scalar_cast(Value);
	}
};

float UAnimCurveCompressionCodec_ACL::DecompressCurve(const FCompressedAnimSequence& AnimSeq, SmartName::UID_Type CurveUID, float CurrentTime) const
{
	const TArray<FSmartName>& CompressedCurveNames = AnimSeq.CompressedCurveNames;
	const int32 NumCurves = CompressedCurveNames.Num();

	if (NumCurves == 0)
	{
		return 0.0f;
	}

	const acl::compressed_tracks* CompressedTracks = acl::make_compressed_tracks(AnimSeq.CompressedCurveByteStream.GetData());
	check(CompressedTracks != nullptr && CompressedTracks->is_valid(false).empty());

	acl::decompression_context<UE4CurveDecompressionSettings> Context;
	Context.initialize(*CompressedTracks);
	Context.seek(CurrentTime, acl::sample_rounding_policy::none);

	int32 TrackIndex = -1;
	for (int32 CurveIndex = 0; CurveIndex < NumCurves; ++CurveIndex)
	{
		if (CompressedCurveNames[CurveIndex].UID == CurveUID)
		{
			TrackIndex = CurveIndex;
			break;
		}
	}

	if (TrackIndex < 0)
	{
		return 0.0f;	// Track not found
	}

	UE4ScalarCurveWriter TrackWriter;
	Context.decompress_track(TrackIndex, TrackWriter);

	return TrackWriter.SampleValue;
}
