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
#include <acl/math/quat_32.h>
#include <acl/math/vector4_32.h>
#include <acl/math/transform_32.h>

/** The ACL allocator implementation simply forwards to the default heap allocator. */
class ACLAllocator final : public acl::IAllocator
{
public:
	virtual void* allocate(size_t size, size_t alignment = acl::IAllocator::k_default_alignment)
	{
		return GMalloc->Malloc(size, alignment);
	}

	virtual void deallocate(void* ptr, size_t size)
	{
		GMalloc->Free(ptr);
	}
};

inline acl::Vector4_32 VectorCast(const FVector& Input) { return acl::vector_set(Input.X, Input.Y, Input.Z); }
inline FVector VectorCast(const acl::Vector4_32& Input) { return FVector(acl::vector_get_x(Input), acl::vector_get_y(Input), acl::vector_get_z(Input)); }
inline acl::Quat_32 QuatCast(const FQuat& Input) { return acl::quat_set(Input.X, Input.Y, Input.Z, Input.W); }
inline FQuat QuatCast(const acl::Quat_32& Input) { return FQuat(acl::quat_get_x(Input), acl::quat_get_y(Input), acl::quat_get_z(Input), acl::quat_get_w(Input)); }
inline acl::Transform_32 TransformCast(const FTransform& Input) { return acl::transform_set(QuatCast(Input.GetRotation()), VectorCast(Input.GetTranslation()), VectorCast(Input.GetScale3D())); }
inline FTransform TransformCast(const acl::Transform_32& Input) { return FTransform(QuatCast(Input.rotation), VectorCast(Input.translation), VectorCast(Input.scale)); }

#if WITH_EDITOR
#include "AnimBoneCompressionCodec_ACLBase.h"

#include <acl/compression/skeleton.h>
#include <acl/compression/animation_clip.h>
#include <acl/compression/compression_level.h>

acl::RotationFormat8 GetRotationFormat(ACLRotationFormat Format);
acl::VectorFormat8 GetVectorFormat(ACLVectorFormat Format);
acl::CompressionLevel8 GetCompressionLevel(ACLCompressionLevel Level);

TUniquePtr<acl::RigidSkeleton> BuildACLSkeleton(ACLAllocator& AllocatorImpl, const FCompressibleAnimData& CompressibleAnimData, float DefaultVirtualVertexDistance, float SafeVirtualVertexDistance);
TUniquePtr<acl::AnimationClip> BuildACLClip(ACLAllocator& AllocatorImpl, const FCompressibleAnimData& CompressibleAnimData, const acl::RigidSkeleton& ACLSkeleton, bool bBuildAdditiveBase);
#endif // WITH_EDITOR
