// Copyright 2021 Nicholas Frechette. All Rights Reserved.

#if WITH_EDITORONLY_DATA
#include "EditorDatabaseMonitor.h"

#include "UObject/WeakObjectPtrTemplates.h"

namespace EditorDatabaseMonitor
{
	static FDelegateHandle MonitorTickerHandle;
	static TArray<TWeakObjectPtr<UAnimationCompressionLibraryDatabase>> DirtyDatabases;

	static bool MonitorTicker(float DeltaTime)
	{
		if (DirtyDatabases.Num() != 0)
		{
			// Iterate over our dirty databases and refresh any stale mappings we might have
			for (TWeakObjectPtr<UAnimationCompressionLibraryDatabase>& DatabasePtr : DirtyDatabases)
			{
				if (UAnimationCompressionLibraryDatabase* Database = DatabasePtr.Get())
				{
					Database->UpdateReferencingAnimSequenceList();
				}
			}

			// Reset everything
			DirtyDatabases.Empty(0);
		}

		const bool bFireTickerAgainAfterDelay = true;
		return bFireTickerAgainAfterDelay;
	}

	void RegisterMonitor()
	{
		if (MonitorTickerHandle.IsValid())
		{
			return;	// Already registered
		}

		// Tick every 300ms
		const float TickerDelay = 0.3F;

		MonitorTickerHandle = FTicker::GetCoreTicker().AddTicker(TEXT("ACLEditorDatabaseMonitor"), TickerDelay, MonitorTicker);
	}

	void UnregisterMonitor()
	{
		if (MonitorTickerHandle.IsValid())
		{
			FTicker::GetCoreTicker().RemoveTicker(MonitorTickerHandle);
			MonitorTickerHandle.Reset();
		}
	}

	void MarkDirty(UAnimationCompressionLibraryDatabase* Database)
	{
		// Must execute on the main thread, not thread safe
		check(IsInGameThread());

		if (Database == nullptr)
		{
			return;	// Nothing to do
		}

		// Add our database, we'll process it later
		DirtyDatabases.AddUnique(Database);
	}
}

#endif
