#pragma once

// Copyright 2021 Nicholas Frechette. All Rights Reserved.

#include "AnimationCompression.h"
#include "CoreMinimal.h"
#include "HAL/UnrealMemory.h"

#include "ACLImpl.h"

#include <acl/core/compressed_database.h>
#include <acl/decompression/database/database_streamer.h>


/** A simple UE4 preview streamer. Everything is assumed to be in memory and no real streaming is done. */
class UE4DatabasePreviewStreamer final : public acl::database_streamer
{
public:
	UE4DatabasePreviewStreamer(const acl::compressed_database& CompressedDatabase, const TArray<uint8>& BulkData_)
		: database_streamer(Requests, acl::k_num_database_tiers)
	{
		const uint32 BulkDataMediumSize = CompressedDatabase.get_bulk_data_size(acl::quality_tier::medium_importance);

		const uint8* BulkDataMedium = BulkData_.GetData();
		BulkData[0] = BulkDataMedium;
		BulkData[1] = acl::align_to(BulkDataMedium + BulkDataMediumSize, acl::k_database_bulk_data_alignment);
	}

	virtual bool is_initialized() const override { return true; }

	virtual const uint8_t* get_bulk_data(acl::quality_tier Tier) const override
	{
		checkf(Tier != acl::quality_tier::highest_importance, TEXT("Unexpected quality tier"));
		const uint32 TierIndex = uint32(Tier) - 1;
		return BulkData[TierIndex];
	}

	virtual void stream_in(uint32_t Offset, uint32_t Size, bool CanAllocateBulkData, acl::quality_tier Tier, acl::streaming_request_id RequestID) override
	{
		UE_LOG(LogAnimationCompression, Log, TEXT("ACL database bulk data is streaming in!"));
		complete(RequestID);
	}

	virtual void stream_out(uint32_t Offset, uint32_t Size, bool CanDeallocateBulkData, acl::quality_tier Tier, acl::streaming_request_id RequestID) override
	{
		UE_LOG(LogAnimationCompression, Log, TEXT("ACL database bulk data is streaming out!"));
		complete(RequestID);
	}

private:
	UE4DatabasePreviewStreamer(const UE4DatabasePreviewStreamer&) = delete;
	UE4DatabasePreviewStreamer& operator=(const UE4DatabasePreviewStreamer&) = delete;

	const uint8* BulkData[acl::k_num_database_tiers];

	acl::streaming_request Requests[acl::k_num_database_tiers];	// One request per tier is enough
};
