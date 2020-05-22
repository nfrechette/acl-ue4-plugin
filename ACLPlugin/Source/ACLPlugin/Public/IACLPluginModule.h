#pragma once

// Copyright 2018 Nicholas Frechette. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

/** The main ACL plugin module interface. */
class IACLPlugin : public IModuleInterface
{
public:
	static inline IACLPlugin& Get()
	{
		return FModuleManager::LoadModuleChecked<IACLPlugin>("ACLPlugin");
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("ACLPlugin");
	}
};
