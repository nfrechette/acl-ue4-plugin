// Copyright 2020 Nicholas Frechette. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class ACLPluginEditor : ModuleRules
	{
		public ACLPluginEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			string ACLSDKDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "../ThirdParty"));

			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
			//OptimizeCode = CodeOptimization.Never;
			//bUseUnity = false;

			PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
			PublicIncludePaths.Add(Path.Combine(ACLSDKDir, "acl/external/sjson-cpp/includes"));

			PrivateIncludePaths.Add("ACLPluginEditor/Private");

			PublicDependencyModuleNames.Add("ACLPlugin");
			PublicDependencyModuleNames.Add("Core");
			PublicDependencyModuleNames.Add("CoreUObject");
			PublicDependencyModuleNames.Add("Engine");

			if (Target.Version.MajorVersion >= 5)
			{
				PublicDependencyModuleNames.Add("AnimationDataController");
			}

			PrivateDependencyModuleNames.Add("EditorStyle");
			PrivateDependencyModuleNames.Add("Slate");
			PrivateDependencyModuleNames.Add("SlateCore");
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
