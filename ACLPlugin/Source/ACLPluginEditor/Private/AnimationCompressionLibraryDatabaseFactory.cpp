// Copyright 2020 Nicholas Frechette. All Rights Reserved.

#include "AnimationCompressionLibraryDatabaseFactory.h"
#include "AnimationCompressionLibraryDatabase.h"

UAnimationCompressionLibraryDatabaseFactory::UAnimationCompressionLibraryDatabaseFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	SupportedClass = UAnimationCompressionLibraryDatabase::StaticClass();
}

UObject* UAnimationCompressionLibraryDatabaseFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UAnimationCompressionLibraryDatabase>(InParent, Class, Name, Flags);
}
