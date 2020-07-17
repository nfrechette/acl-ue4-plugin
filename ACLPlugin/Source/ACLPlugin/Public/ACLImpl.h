#pragma once

// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "CoreMinimal.h"

#if DO_GUARD_SLOW
	// ACL has a lot of asserts, only enabled in Debug
	// This decision has been made because the extensive asserts add considerable overhead and most
	// developers use a Development configuration for the editor. The ACL library runs extensive
	// unit and regression tests on a very large number of clips which minimizes the risk of
	// having a legitimate assert fire.
	#define ACL_ON_ASSERT_CUSTOM
	#define ACL_ASSERT(expression, format, ...) checkf(expression, TEXT(format), #__VA_ARGS__)
#endif

#if PLATFORM_PS4
	// Enable usage of popcount instruction
	#define ACL_USE_POPCOUNT
#endif

#include <acl/core/error.h>
#include <acl/core/iallocator.h>

#include <rtm/quatf.h>
#include <rtm/vector4f.h>
#include <rtm/qvvf.h>

/** The ACL allocator implementation simply forwards to the default heap allocator. */
class ACLAllocator final : public acl::iallocator
{
public:
	virtual void* allocate(size_t size, size_t alignment = acl::iallocator::k_default_alignment)
	{
		return GMalloc->Malloc(size, alignment);
	}

	virtual void deallocate(void* ptr, size_t size)
	{
		GMalloc->Free(ptr);
	}
};

inline rtm::vector4f RTM_SIMD_CALL VectorCast(const FVector& Input) { return rtm::vector_set(Input.X, Input.Y, Input.Z); }
inline FVector RTM_SIMD_CALL VectorCast(rtm::vector4f_arg0 Input) { return FVector(rtm::vector_get_x(Input), rtm::vector_get_y(Input), rtm::vector_get_z(Input)); }
inline rtm::quatf RTM_SIMD_CALL QuatCast(const FQuat& Input) { return rtm::quat_set(Input.X, Input.Y, Input.Z, Input.W); }
inline FQuat RTM_SIMD_CALL QuatCast(rtm::quatf_arg0 Input) { return FQuat(rtm::quat_get_x(Input), rtm::quat_get_y(Input), rtm::quat_get_z(Input), rtm::quat_get_w(Input)); }
inline rtm::qvvf RTM_SIMD_CALL TransformCast(const FTransform& Input) { return rtm::qvv_set(QuatCast(Input.GetRotation()), VectorCast(Input.GetTranslation()), VectorCast(Input.GetScale3D())); }
inline FTransform RTM_SIMD_CALL TransformCast(rtm::qvvf_arg0 Input) { return FTransform(QuatCast(Input.rotation), VectorCast(Input.translation), VectorCast(Input.scale)); }

#if WITH_EDITOR
#include "AnimBoneCompressionCodec_ACLBase.h"

#include <acl/compression/track_array.h>
#include <acl/compression/compression_level.h>

acl::rotation_format8 GetRotationFormat(ACLRotationFormat Format);
acl::vector_format8 GetVectorFormat(ACLVectorFormat Format);
acl::compression_level8 GetCompressionLevel(ACLCompressionLevel Level);

acl::track_array_qvvf BuildACLTransformTrackArray(ACLAllocator& AllocatorImpl, const FCompressibleAnimData& CompressibleAnimData, float DefaultVirtualVertexDistance, float SafeVirtualVertexDistance, bool bBuildAdditiveBase);
#endif // WITH_EDITOR
