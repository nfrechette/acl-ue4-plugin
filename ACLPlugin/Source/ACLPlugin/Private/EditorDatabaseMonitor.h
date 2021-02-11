#pragma once

// Copyright 2021 Nicholas Frechette. All Rights Reserved.

#if WITH_EDITORONLY_DATA

#include "CoreMinimal.h"
#include "AnimationCompressionLibraryDatabase.h"

/** A central database monitor that ensures database instances have their mappings up to date. */
namespace EditorDatabaseMonitor
{
	void RegisterMonitor();
	void UnregisterMonitor();

	void MarkDirty(UAnimationCompressionLibraryDatabase* Database);
}

#endif
