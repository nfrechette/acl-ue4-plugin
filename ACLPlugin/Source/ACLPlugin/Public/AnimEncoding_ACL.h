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

#include "CoreMinimal.h"
#include "AnimEncoding.h"

//////////////////////////////////////////////////////////////////////////
// For performance reasons, the codec is split into 3:
//    default: This uses the default and recommended settings
//    safe: This uses safe settings in case the error is too high with the default settings
//    debug: This has every feature enabled and supported, mainly used for debugging
//////////////////////////////////////////////////////////////////////////

/** The ACL codec base class runtime implementation. */
class AEFACLCompressionCodec_Base : public AnimEncoding
{
public:
	virtual void ByteSwapIn(UAnimSequence& Seq, FMemoryReader& MemoryReader) override;
	virtual void ByteSwapOut(UAnimSequence& Seq, TArray<uint8>& SerializedData, bool ForceByteSwapping) override;
};

/** The ACL default codec runtime implementation. */
class AEFACLCompressionCodec_Default final : public AEFACLCompressionCodec_Base
{
public:
	virtual void GetBoneAtom(FTransform& OutAtom, const UAnimSequence& Seq, int32 TrackIndex, float Time) override;

#if USE_ANIMATION_CODEC_BATCH_SOLVER
	virtual void GetPoseRotations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time) override;
	virtual void GetPoseTranslations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time) override;
	virtual void GetPoseScales(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time) override;
#endif
};

/** The ACL safe codec runtime implementation. */
class AEFACLCompressionCodec_Safe final : public AEFACLCompressionCodec_Base
{
public:
	virtual void GetBoneAtom(FTransform& OutAtom, const UAnimSequence& Seq, int32 TrackIndex, float Time) override;

#if USE_ANIMATION_CODEC_BATCH_SOLVER
	virtual void GetPoseRotations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time) override;
	virtual void GetPoseTranslations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time) override;
	virtual void GetPoseScales(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time) override;
#endif
};

/** The ACL custom codec runtime implementation. */
class AEFACLCompressionCodec_Custom final : public AEFACLCompressionCodec_Base
{
public:
	virtual void GetBoneAtom(FTransform& OutAtom, const UAnimSequence& Seq, int32 TrackIndex, float Time) override;

#if USE_ANIMATION_CODEC_BATCH_SOLVER
	virtual void GetPoseRotations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time) override;
	virtual void GetPoseTranslations(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time) override;
	virtual void GetPoseScales(FTransformArray& Atoms, const BoneTrackArray& DesiredPairs, const UAnimSequence& Seq, float Time) override;
#endif
};
