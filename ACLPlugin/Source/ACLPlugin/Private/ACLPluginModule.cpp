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

#include "CoreMinimal.h"
#include "IACLPluginModule.h"
#include "Modules/ModuleManager.h"

#if !UE_BUILD_SHIPPING
#include "AnimationCompression.h"
#include "Animation/AnimBoneCompressionCodec.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCurveCompressionCodec.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#endif

class FACLPlugin final : public IACLPlugin
{
private:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

#if !UE_BUILD_SHIPPING
	// Console commands
	void ListCodecs(const TArray<FString>& Args);
	void ListAnimSequences(const TArray<FString>& Args);

	TArray<IConsoleObject*> ConsoleCommands;
#endif
};

IMPLEMENT_MODULE(FACLPlugin, ACLPlugin)

//////////////////////////////////////////////////////////////////////////

#if !UE_BUILD_SHIPPING
template<class ClassType>
static TArray<ClassType*> GetObjectInstancesSorted()
{
	TArray<ClassType*> Results;

	for (TObjectIterator<ClassType> It; It; ++It)
	{
		Results.Add(*It);
	}

	struct FCompareObjectNames
	{
		FORCEINLINE bool operator()(const ClassType& Lhs, const ClassType& Rhs) const
		{
			return Lhs.GetPathName().Compare(Rhs.GetPathName()) < 0;
		}
	};
	Results.Sort(FCompareObjectNames());

	return Results;
}

static double BytesToKB(SIZE_T NumBytes)
{
	return (double)NumBytes / 1024.0;
}

static double BytesToMB(SIZE_T NumBytes)
{
	return (double)NumBytes / (1024.0 * 1024.0);
}

void FACLPlugin::ListCodecs(const TArray<FString>& Args)
{
	// Turn off log times to make diffing easier
	TGuardValue<ELogTimes::Type> DisableLogTimes(GPrintLogTimes, ELogTimes::None);

	// Make sure to log everything
	const ELogVerbosity::Type OldVerbosity = LogAnimationCompression.GetVerbosity();
	LogAnimationCompression.SetVerbosity(ELogVerbosity::All);

	const TArray<UAnimBoneCompressionSettings*> BoneSettings = GetObjectInstancesSorted<UAnimBoneCompressionSettings>();
	const TArray<UAnimBoneCompressionCodec*> BoneCodecs = GetObjectInstancesSorted<UAnimBoneCompressionCodec>();
	const TArray<UAnimCurveCompressionSettings*> CurveSettings = GetObjectInstancesSorted<UAnimCurveCompressionSettings>();
	const TArray<UAnimCurveCompressionCodec*> CurveCodecs = GetObjectInstancesSorted<UAnimCurveCompressionCodec>();
	const TArray<UAnimSequence*> AnimSequences = GetObjectInstancesSorted<UAnimSequence>();

	UE_LOG(LogAnimationCompression, Log, TEXT("===== Bone Compression Setting Assets ====="));
	for (const UAnimBoneCompressionSettings* Settings : BoneSettings)
	{
		int32 NumReferences = 0;
		SIZE_T TotalSize = 0;
		for (const UAnimSequence* AnimSeq : AnimSequences)
		{
			if (AnimSeq->BoneCompressionSettings == Settings)
			{
				NumReferences++;
				TotalSize += AnimSeq->CompressedData.CompressedTrackToSkeletonMapTable.GetAllocatedSize();
				if (AnimSeq->CompressedData.CompressedDataStructure)
				{
					TotalSize += AnimSeq->CompressedData.CompressedDataStructure->GetApproxCompressedSize();
				}
			}
		}

		UE_LOG(LogAnimationCompression, Log, TEXT("%s referenced by %d anim sequences (%.2f MB)"), *Settings->GetPathName(), NumReferences, BytesToMB(TotalSize));
	}

	UE_LOG(LogAnimationCompression, Log, TEXT("===== Bone Compression Codecs ====="));
	for (const UAnimBoneCompressionCodec* Codec : BoneCodecs)
	{
		int32 NumReferences = 0;
		SIZE_T TotalSize = 0;
		for (const UAnimSequence* AnimSeq : AnimSequences)
		{
			if (AnimSeq->CompressedData.BoneCompressionCodec == Codec)
			{
				NumReferences++;
				TotalSize += AnimSeq->CompressedData.CompressedTrackToSkeletonMapTable.GetAllocatedSize();
				if (AnimSeq->CompressedData.CompressedDataStructure)
				{
					TotalSize += AnimSeq->CompressedData.CompressedDataStructure->GetApproxCompressedSize();
				}
			}
		}

		if (Codec->Description.IsEmpty())
		{
			UE_LOG(LogAnimationCompression, Log, TEXT("%s referenced by %d anim sequences (%.2f MB)"), *Codec->GetPathName(), NumReferences, BytesToMB(TotalSize));
		}
		else
		{
			UE_LOG(LogAnimationCompression, Log, TEXT("%s (%s) referenced by %d anim sequences (%.2f MB)"), *Codec->GetPathName(), *Codec->Description, NumReferences, BytesToMB(TotalSize));
		}
	}

	UE_LOG(LogAnimationCompression, Log, TEXT("===== Curve Compression Setting Assets ====="));
	for (const UAnimCurveCompressionSettings* Settings : CurveSettings)
	{
		int32 NumReferences = 0;
		SIZE_T TotalSize = 0;
		for (const UAnimSequence* AnimSeq : AnimSequences)
		{
			if (AnimSeq->CurveCompressionSettings == Settings)
			{
				NumReferences++;
				TotalSize += AnimSeq->CompressedData.CompressedCurveNames.GetAllocatedSize();
				TotalSize += AnimSeq->CompressedData.CompressedCurveByteStream.GetAllocatedSize();
			}
		}

		UE_LOG(LogAnimationCompression, Log, TEXT("%s referenced by %d anim sequences (%.2f MB)"), *Settings->GetPathName(), NumReferences, BytesToMB(TotalSize));
	}

	UE_LOG(LogAnimationCompression, Log, TEXT("===== Curve Compression Codecs ====="));
	for (const UAnimCurveCompressionCodec* Codec : CurveCodecs)
	{
		int32 NumReferences = 0;
		SIZE_T TotalSize = 0;
		for (const UAnimSequence* AnimSeq : AnimSequences)
		{
			if (AnimSeq->CompressedData.CurveCompressionCodec == Codec)
			{
				NumReferences++;
				TotalSize += AnimSeq->CompressedData.CompressedCurveNames.GetAllocatedSize();
				TotalSize += AnimSeq->CompressedData.CompressedCurveByteStream.GetAllocatedSize();
			}
		}

		UE_LOG(LogAnimationCompression, Log, TEXT("%s referenced by %d anim sequences (%.2f MB)"), *Codec->GetPathName(), NumReferences, BytesToMB(TotalSize));
	}

	LogAnimationCompression.SetVerbosity(OldVerbosity);
}

void FACLPlugin::ListAnimSequences(const TArray<FString>& Args)
{
	// Turn off log times to make diffing easier
	TGuardValue<ELogTimes::Type> DisableLogTimes(GPrintLogTimes, ELogTimes::None);

	// Make sure to log everything
	const ELogVerbosity::Type OldVerbosity = LogAnimationCompression.GetVerbosity();
	LogAnimationCompression.SetVerbosity(ELogVerbosity::All);

	const TArray<UAnimSequence*> AnimSequences = GetObjectInstancesSorted<UAnimSequence>();

	SIZE_T BoneDataTotalSize = 0;
	SIZE_T CurveDataTotalSize = 0;

	UE_LOG(LogAnimationCompression, Log, TEXT("===== Anim Sequence Assets ====="));
	for (const UAnimSequence* AnimSeq : AnimSequences)
	{
		UE_LOG(LogAnimationCompression, Log, TEXT("%s ..."), *AnimSeq->GetPathName());

		if (AnimSeq->CompressedData.BoneCompressionCodec->Description.IsEmpty())
		{
			UE_LOG(LogAnimationCompression, Log, TEXT("    uses bone codec %s"), *AnimSeq->CompressedData.BoneCompressionCodec->GetPathName());
		}
		else
		{
			UE_LOG(LogAnimationCompression, Log, TEXT("    uses bone codec %s (%s)"), *AnimSeq->CompressedData.BoneCompressionCodec->GetPathName(), *AnimSeq->CompressedData.BoneCompressionCodec->Description);
		}

		SIZE_T BoneDataSize = AnimSeq->CompressedData.CompressedTrackToSkeletonMapTable.GetAllocatedSize();
		if (AnimSeq->CompressedData.CompressedDataStructure)
		{
			BoneDataSize += AnimSeq->CompressedData.CompressedDataStructure->GetApproxCompressedSize();
		}
		UE_LOG(LogAnimationCompression, Log, TEXT("    has %.2f KB of bone data"), BytesToKB(BoneDataSize));

#if WITH_EDITORONLY_DATA
		if (AnimSeq->CompressedData.CompressedDataStructure)
		{
			UE_LOG(LogAnimationCompression, Log, TEXT("    has a bone error of %.4f cm"), AnimSeq->CompressedData.CompressedDataStructure->BoneCompressionErrorStats.MaxError);
		}
#endif

		UE_LOG(LogAnimationCompression, Log, TEXT("    uses curve codec %s"), *AnimSeq->CompressedData.CurveCompressionCodec->GetPathName());

		SIZE_T CurveDataSize = AnimSeq->CompressedData.CompressedCurveNames.GetAllocatedSize();
		CurveDataSize += AnimSeq->CompressedData.CompressedCurveByteStream.GetAllocatedSize();
		UE_LOG(LogAnimationCompression, Log, TEXT("    has %.2f KB of curve data"), BytesToKB(CurveDataSize));

		BoneDataTotalSize += BoneDataSize;
		CurveDataTotalSize += CurveDataSize;
	}

	UE_LOG(LogAnimationCompression, Log, TEXT("Total bone data size: %.2f MB"), BytesToMB(BoneDataTotalSize));
	UE_LOG(LogAnimationCompression, Log, TEXT("Total curve data size: %.2f MB"), BytesToMB(CurveDataTotalSize));

	LogAnimationCompression.SetVerbosity(OldVerbosity);
}
#endif

void FACLPlugin::StartupModule()
{
#if !UE_BUILD_SHIPPING
	if (!IsRunningCommandlet())
	{
		ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("ACL.ListCodecs"),
			TEXT("Dumps statistics about animation codecs to the log."),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FACLPlugin::ListCodecs),
			ECVF_Default
		));

		ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("ACL.ListAnimSequences"),
			TEXT("Dumps statistics about animation sequences to the log."),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FACLPlugin::ListAnimSequences),
			ECVF_Default
		));
	}
#endif
}

void FACLPlugin::ShutdownModule()
{
#if !UE_BUILD_SHIPPING
	for (IConsoleObject* Cmd : ConsoleCommands)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Cmd);
	}
	ConsoleCommands.Empty();
#endif
}
