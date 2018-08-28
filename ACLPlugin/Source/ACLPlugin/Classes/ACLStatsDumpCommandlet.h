#pragma once

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

#if WITH_EDITOR
	FString ACLRawDir;
	FString UE4StatDir;

	bool PerformExhaustiveDump;
	bool TryAutomaticCompression;
	bool TryACLCompression;

	float MasterTolerance;

	class UAnimCompress_Automatic* AutoCompressor;
	class UAnimCompress_ACL* ACLCompressor;

	const class UEnum* AnimFormatEnum;
#endif
};
