#pragma once

// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Runtime/Launch/Resources/Version.h"

#if DO_GUARD_SLOW
	// ACL has a lot of asserts, only enabled in Debug
	// This decision has been made because the extensive asserts add considerable overhead and most
	// developers use a Development configuration for the editor. The ACL library runs extensive
	// unit and regression tests on a very large number of clips which minimizes the risk of
	// having a legitimate assert fire.
	#define ACL_ON_ASSERT_CUSTOM
	#define ACL_ASSERT(expression, format, ...) checkf(expression, TEXT(format), #__VA_ARGS__)
#endif

// Enable popcount usage on PS4.
// We cannot use PLATFORM_ENABLE_POPCNT_INTRINSIC as that might be unsafe.
// UE uses that macro to determine if the GCC intrinsic is present, not if the instruction
// is supported.
// UE 5.1 removed the PS4 define and we can no longer rely on it
#if (ENGINE_MAJOR_VERSION <= 5 && ENGINE_MINOR_VERSION <= 0) && PLATFORM_PS4
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

/** RTM <-> UE conversion utilities and aliases. */
#if ENGINE_MAJOR_VERSION >= 5
inline FQuat RTM_SIMD_CALL UEQuatCast(const FQuat4f& Input) { return FQuat(Input.X, Input.Y, Input.Z, Input.W); }
inline FVector RTM_SIMD_CALL UEVector3Cast(const FVector3f& Input) { return FVector(Input.X, Input.Y, Input.Z); }
inline FVector RTM_SIMD_CALL UEVector3Cast(const FVector3d& Input) { return FVector(Input.X, Input.Y, Input.Z); }

inline rtm::vector4f RTM_SIMD_CALL UEVector3ToACL(const FVector3f& Input) { return rtm::vector_set(Input.X, Input.Y, Input.Z); }
inline rtm::vector4f RTM_SIMD_CALL UEVector3ToACL(const FVector& Input) { return rtm::vector_cast(rtm::vector_set(Input.X, Input.Y, Input.Z)); }
inline FVector3f RTM_SIMD_CALL ACLVector3ToUE(rtm::vector4f_arg0 Input) { return FVector3f(rtm::vector_get_x(Input), rtm::vector_get_y(Input), rtm::vector_get_z(Input)); }

inline rtm::quatf RTM_SIMD_CALL UEQuatToACL(const FQuat4f& Input) { return rtm::quat_set(Input.X, Input.Y, Input.Z, Input.W); }
inline rtm::quatf RTM_SIMD_CALL UEQuatToACL(const FQuat& Input) { return rtm::quat_cast(rtm::quat_set(Input.X, Input.Y, Input.Z, Input.W)); }
inline FQuat4f RTM_SIMD_CALL ACLQuatToUE(rtm::quatf_arg0 Input) { return FQuat4f(rtm::quat_get_x(Input), rtm::quat_get_y(Input), rtm::quat_get_z(Input), rtm::quat_get_w(Input)); }

inline FTransform RTM_SIMD_CALL ACLTransformToUE(rtm::qvvf_arg0 Input) { return FTransform(UEQuatCast(ACLQuatToUE(Input.rotation)), UEVector3Cast(ACLVector3ToUE(Input.translation)), UEVector3Cast(ACLVector3ToUE(Input.scale))); }

using FRawAnimTrackQuat = FQuat4f;
using FRawAnimTrackVector3 = FVector3f;
#else
inline FQuat RTM_SIMD_CALL UEQuatCast(const FQuat& Input) { return Input; }
inline FVector RTM_SIMD_CALL UEVector3Cast(const FVector& Input) { return Input; }

inline rtm::vector4f RTM_SIMD_CALL UEVector3ToACL(const FVector& Input) { return rtm::vector_set(Input.X, Input.Y, Input.Z); }
inline FVector RTM_SIMD_CALL ACLVector3ToUE(rtm::vector4f_arg0 Input) { return FVector(rtm::vector_get_x(Input), rtm::vector_get_y(Input), rtm::vector_get_z(Input)); }

inline rtm::quatf RTM_SIMD_CALL UEQuatToACL(const FQuat& Input) { return rtm::quat_set(Input.X, Input.Y, Input.Z, Input.W); }
inline FQuat RTM_SIMD_CALL ACLQuatToUE(rtm::quatf_arg0 Input) { return FQuat(rtm::quat_get_x(Input), rtm::quat_get_y(Input), rtm::quat_get_z(Input), rtm::quat_get_w(Input)); }

inline FTransform RTM_SIMD_CALL ACLTransformToUE(rtm::qvvf_arg0 Input) { return FTransform(ACLQuatToUE(Input.rotation), ACLVector3ToUE(Input.translation), ACLVector3ToUE(Input.scale)); }

using FRawAnimTrackQuat = FQuat;
using FRawAnimTrackVector3 = FVector;
#endif

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

struct UE4DebugDecompressionSettings : public acl::debug_transform_decompression_settings
{
	// Only support our latest version
	static constexpr acl::compressed_tracks_version16 version_supported() { return acl::compressed_tracks_version16::latest; }
};

// Same as debug settings for now since everything is allowed
using UE4CustomDecompressionSettings = UE4DebugDecompressionSettings;

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

struct UE4DebugDBDecompressionSettings final : public UE4DebugDecompressionSettings
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

struct FCompressibleAnimData;
class UAnimSequence;

ACLPLUGIN_API acl::rotation_format8 GetRotationFormat(ACLRotationFormat Format);
ACLPLUGIN_API acl::vector_format8 GetVectorFormat(ACLVectorFormat Format);
ACLPLUGIN_API acl::compression_level8 GetCompressionLevel(ACLCompressionLevel Level);

ACLPLUGIN_API acl::track_array_qvvf BuildACLTransformTrackArray(ACLAllocator& AllocatorImpl, const FCompressibleAnimData& CompressibleAnimData, float DefaultVirtualVertexDistance, float SafeVirtualVertexDistance, bool bBuildAdditiveBase);

/** Compatibility utilities */
ACLPLUGIN_API uint32 GetNumSamples(const FCompressibleAnimData& CompressibleAnimData);
ACLPLUGIN_API float GetSequenceLength(const UAnimSequence& AnimSeq);
#endif // WITH_EDITOR
