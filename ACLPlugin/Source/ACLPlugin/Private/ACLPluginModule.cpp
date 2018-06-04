////////////////////////////////////////////////////////////////////////////////
// The MIT License (MIT)
//
// Copyright (c) 2018 Nicholas Frechette
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
////////////////////////////////////////////////////////////////////////////////

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IACLPluginModule.h"
#include "AnimEncoding.h"
#include "Animation/AnimEncodingRegistry.h"
#include "AnimEncoding_ACL.h"

class FACLPlugin final : public IACLPlugin
{
private:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FACLPlugin, ACLPlugin)

static const FName NAME_ACLDefaultCodec("ACLDefault");
static const FName NAME_ACLDebugCodec("ACLDebug");
static const FName NAME_ACLSafetyFallbackCodec("ACLSafetyFallback");

// Function that hooks up the proper interface links for the ACL codec implementation
static void ACLSetInterfaceLinks(UAnimSequence& AnimSeq)
{
	static AEFACLCompressionCodec StaticCodec;

	AnimSeq.RotationCodec = &StaticCodec;
	AnimSeq.TranslationCodec = &StaticCodec;
	AnimSeq.ScaleCodec = &StaticCodec;
}

void FACLPlugin::StartupModule()
{
	FAnimEncodingRegistry::Get().RegisterEncoding(NAME_ACLDefaultCodec, &ACLSetInterfaceLinks);
	FAnimEncodingRegistry::Get().RegisterEncoding(NAME_ACLDebugCodec, &ACLSetInterfaceLinks);
	FAnimEncodingRegistry::Get().RegisterEncoding(NAME_ACLSafetyFallbackCodec, &ACLSetInterfaceLinks);
}

void FACLPlugin::ShutdownModule()
{
	FAnimEncodingRegistry::Get().UnregisterEncoding(NAME_ACLDefaultCodec);
	FAnimEncodingRegistry::Get().UnregisterEncoding(NAME_ACLDebugCodec);
	FAnimEncodingRegistry::Get().UnregisterEncoding(NAME_ACLSafetyFallbackCodec);
}
