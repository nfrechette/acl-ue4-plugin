#pragma once

// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

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
#include <acl/decompression/decompress.h>

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

extern ACLPLUGIN_API ACLAllocator ACLAllocatorImpl;

/** RTM <-> UE4 conversion utilities */
inline rtm::vector4f RTM_SIMD_CALL VectorCast(const FVector& Input) { return rtm::vector_set(Input.X, Input.Y, Input.Z); }
inline FVector RTM_SIMD_CALL VectorCast(rtm::vector4f_arg0 Input) { return FVector(rtm::vector_get_x(Input), rtm::vector_get_y(Input), rtm::vector_get_z(Input)); }
inline rtm::quatf RTM_SIMD_CALL QuatCast(const FQuat& Input) { return rtm::quat_set(Input.X, Input.Y, Input.Z, Input.W); }
inline FQuat RTM_SIMD_CALL QuatCast(rtm::quatf_arg0 Input) { return FQuat(rtm::quat_get_x(Input), rtm::quat_get_y(Input), rtm::quat_get_z(Input), rtm::quat_get_w(Input)); }
inline rtm::qvvf RTM_SIMD_CALL TransformCast(const FTransform& Input) { return rtm::qvv_set(QuatCast(Input.GetRotation()), VectorCast(Input.GetTranslation()), VectorCast(Input.GetScale3D())); }
inline FTransform RTM_SIMD_CALL TransformCast(rtm::qvvf_arg0 Input) { return FTransform(QuatCast(Input.rotation), VectorCast(Input.translation), VectorCast(Input.scale)); }

/** The decompression settings used by ACL */
struct UE4DefaultDecompressionSettings : public acl::default_transform_decompression_settings
{
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

using UE4CustomDecompressionSettings = acl::debug_transform_decompression_settings;

struct UE4SafeDecompressionSettings final : public UE4DefaultDecompressionSettings
{
	static constexpr bool is_rotation_format_supported(acl::rotation_format8 format) { return format == acl::rotation_format8::quatf_full; }
	static constexpr acl::rotation_format8 get_rotation_format(acl::rotation_format8 /*format*/) { return acl::rotation_format8::quatf_full; }
};

struct UE4DefaultDatabaseSettings final : public acl::default_database_settings
{
	// Only support our latest version
	static constexpr acl::compressed_tracks_version16 version_supported() { return acl::compressed_tracks_version16::latest; }
};

struct UE4DefaultDBDecompressionSettings final : public UE4DefaultDecompressionSettings
{
	using database_settings_type = UE4DefaultDatabaseSettings;
};

using UE4DebugDatabaseSettings = acl::debug_database_settings;

struct UE4DebugDBDecompressionSettings final : public acl::debug_transform_decompression_settings
{
	using database_settings_type = UE4DebugDatabaseSettings;
};

/** UE4 equivalents for some ACL enums */
/** An enum for ACL rotation formats. */
UENUM()
enum ACLRotationFormat
{
	ACLRF_Quat_128 UMETA(DisplayName = "Quat Full Bit Rate"),
	ACLRF_QuatDropW_96 UMETA(DisplayName = "Quat Drop W Full Bit Rate"),
	ACLRF_QuatDropW_Variable UMETA(DisplayName = "Quat Drop W Variable Bit Rate"),
};

/** An enum for ACL Vector3 formats. */
UENUM()
enum ACLVectorFormat
{
	ACLVF_Vector3_96 UMETA(DisplayName = "Vector3 Full Bit Rate"),
	ACLVF_Vector3_Variable UMETA(DisplayName = "Vector3 Variable Bit Rate"),
};

/** An enum for ACL compression levels. */
UENUM()
enum ACLCompressionLevel
{
	ACLCL_Lowest UMETA(DisplayName = "Lowest"),
	ACLCL_Low UMETA(DisplayName = "Low"),
	ACLCL_Medium UMETA(DisplayName = "Medium"),
	ACLCL_High UMETA(DisplayName = "High"),
	ACLCL_Highest UMETA(DisplayName = "Highest"),
};

/** Editor only utilities */
#if WITH_EDITOR
#include <acl/compression/track_array.h>
#include <acl/compression/compression_level.h>

ACLPLUGIN_API acl::rotation_format8 GetRotationFormat(ACLRotationFormat Format);
ACLPLUGIN_API acl::vector_format8 GetVectorFormat(ACLVectorFormat Format);
ACLPLUGIN_API acl::compression_level8 GetCompressionLevel(ACLCompressionLevel Level);

ACLPLUGIN_API acl::track_array_qvvf BuildACLTransformTrackArray(ACLAllocator& AllocatorImpl, const FCompressibleAnimData& CompressibleAnimData, float DefaultVirtualVertexDistance, float SafeVirtualVertexDistance, bool bBuildAdditiveBase);
#endif // WITH_EDITOR
