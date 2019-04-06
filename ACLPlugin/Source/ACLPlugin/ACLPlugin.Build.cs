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

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class ACLPlugin : ModuleRules
	{
		public ACLPlugin(ReadOnlyTargetRules Target) : base(Target)
		{
			string ACLSDKDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty"));

			PublicIncludePaths.Add(Path.Combine(ACLSDKDir, "acl/includes"));

			PrivateIncludePaths.Add("ACLPlugin/Private");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
				}
			);

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("UnrealEd");

				PublicIncludePaths.Add(Path.Combine(ACLSDKDir, "acl/external/sjson-cpp/includes"));
			}

			if (Target.Platform == UnrealTargetPlatform.Linux)
			{
				// There appears to be a bug when cross-compiling Linux under Windows where the clang tool-chain used
				// isn't fully C++11 compliant. The standard specifies that when the 'cinttypes' header is included
				// the format macros are always defined unlike C which requires the following macro to be defined first.
				// This fix should be required for UE 4.20 and earlier versions.
				PrivateDefinitions.Add("__STDC_FORMAT_MACROS");
			}
		}
	}
}
