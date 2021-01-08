#pragma once

// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "Commandlets/Commandlet.h"
#include "ACLStatsDumpCommandlet.generated.h"

/*
 * This commandlet is used to extract and dump animation compression statistics.
 *
 * It supports the following arguments: -acl=<path> -stats=<path> -MasterTolerance=<tolerance>
 *
 *   acl: This is the path to the input directory that contains the ACL SJSON animation clips.
 *   stats: This is the path to the output directory that will contain the extracted SJSON statistics.
 *   MasterTolerance: This is the master tolerance used by the UE4 Automatic compression algorithm. Defaults to 0.1cm.
 */
UCLASS()
class UACLStatsDumpCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

public:
	virtual int32 Main(const FString& Params) override;

	FString ACLRawDir;
	FString OutputDir;

	bool PerformExhaustiveDump;
	bool PerformCompression;
	bool PerformClipExtraction;
	bool TryAutomaticCompression;
	bool TryACLCompression;
	bool TryKeyReductionRetarget;
	bool TryKeyReduction;
	bool ResumeTask;
	bool SkipAdditiveClips;

	class UAnimBoneCompressionSettings* AutoCompressionSettings;
	class UAnimBoneCompressionSettings* ACLCompressionSettings;
	class UAnimBoneCompressionSettings* KeyReductionCompressionSettings;
	class UAnimBoneCompressionCodec_ACL* ACLCodec;
	class UAnimCompress_RemoveLinearKeys* KeyReductionCodec;
};
