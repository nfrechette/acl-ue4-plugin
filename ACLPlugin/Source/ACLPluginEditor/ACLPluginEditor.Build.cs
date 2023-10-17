// Copyright 2020 Nicholas Frechette. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class ACLPluginEditor : ModuleRules
	{
		public ACLPluginEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			CppStandard = CppStandardVersion.Cpp17;

			// Replace with PCHUsageMode.UseExplicitOrSharedPCHs when this plugin can compile with cpp20
			PCHUsage = PCHUsageMode.NoPCHs;

			string ACLSDKDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "../ThirdParty"));

			//OptimizeCode = CodeOptimization.Never;
			//bUseUnity = false;

			PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
			PublicIncludePaths.Add(Path.Combine(ACLSDKDir, "acl/external/sjson-cpp/includes"));

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

			PrivateDefinitions.Add("ACL_USE_SJSON");
		}
	}
}
