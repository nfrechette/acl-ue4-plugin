// Copyright 2020 Nicholas Frechette. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "AnimationCompressionLibraryDatabaseFactory.generated.h"

UCLASS(HideCategories = Object, MinimalAPI)
class UAnimationCompressionLibraryDatabaseFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	//~ Begin UFactory Interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	//~ Begin UFactory Interface
};
