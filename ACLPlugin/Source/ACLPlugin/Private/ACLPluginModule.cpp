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

static AEFACLCompressionCodec_Default StaticCodec_Default;
static AEFACLCompressionCodec_Safe StaticCodec_Safe;
static AEFACLCompressionCodec_Custom StaticCodec_Custom;

// Function that hooks up the proper interface links for the ACL codec implementations
static void ACLSetInterfaceLinks_Default(AnimationKeyFormat KeyFormat, AnimEncoding*& RotationCodec, AnimEncoding*& TranslationCodec, AnimEncoding*& ScaleCodec)
{
	RotationCodec = &StaticCodec_Default;
	TranslationCodec = &StaticCodec_Default;
	ScaleCodec = &StaticCodec_Default;
}

static void ACLSetInterfaceLinks_Safe(AnimationKeyFormat KeyFormat, AnimEncoding*& RotationCodec, AnimEncoding*& TranslationCodec, AnimEncoding*& ScaleCodec)
{
	RotationCodec = &StaticCodec_Safe;
	TranslationCodec = &StaticCodec_Safe;
	ScaleCodec = &StaticCodec_Safe;
}

static void ACLSetInterfaceLinks_Custom(AnimationKeyFormat KeyFormat, AnimEncoding*& RotationCodec, AnimEncoding*& TranslationCodec, AnimEncoding*& ScaleCodec)
{
	RotationCodec = &StaticCodec_Custom;
	TranslationCodec = &StaticCodec_Custom;
	ScaleCodec = &StaticCodec_Custom;
}

void FACLPlugin::StartupModule()
{
	FAnimEncodingRegistry::Get().RegisterEncoding(AKF_ACLDefault, &ACLSetInterfaceLinks_Default);
	FAnimEncodingRegistry::Get().RegisterEncoding(AKF_ACLCustom, &ACLSetInterfaceLinks_Custom);
	FAnimEncodingRegistry::Get().RegisterEncoding(AKF_ACLSafe, &ACLSetInterfaceLinks_Safe);
}

void FACLPlugin::ShutdownModule()
{
	FAnimEncodingRegistry::Get().UnregisterEncoding(AKF_ACLDefault);
	FAnimEncodingRegistry::Get().UnregisterEncoding(AKF_ACLCustom);
	FAnimEncodingRegistry::Get().UnregisterEncoding(AKF_ACLSafe);
}
