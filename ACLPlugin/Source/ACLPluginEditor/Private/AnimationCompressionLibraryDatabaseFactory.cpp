// Copyright 2020 Nicholas Frechette. All Rights Reserved.

#include "AnimationCompressionLibraryDatabaseFactory.h"
#include "AnimationCompressionLibraryDatabase.h"

#if (ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1)
#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimationCompressionLibraryDatabaseFactory)
#endif

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

