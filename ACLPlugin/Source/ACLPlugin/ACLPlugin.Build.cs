// Copyright 2018 Nicholas Frechette. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class ACLPlugin : ModuleRules
	{
		public ACLPlugin(ReadOnlyTargetRules Target) : base(Target)
		{
			string ACLSDKDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "../ThirdParty"));

			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
			PublicIncludePaths.Add(Path.Combine(ACLSDKDir, "acl/includes"));
			PublicIncludePaths.Add(Path.Combine(ACLSDKDir, "acl/external/rtm/includes"));

			PrivateIncludePaths.Add("ACLPlugin/Private");

			PublicDependencyModuleNames.Add("Core");
			PublicDependencyModuleNames.Add("CoreUObject");
			PublicDependencyModuleNames.Add("Engine");

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("DesktopPlatform");
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
