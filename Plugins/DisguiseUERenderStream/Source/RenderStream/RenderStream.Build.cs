// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RenderStream : ModuleRules
{
	public RenderStream(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange (new string [] { "RenderStream/Private" });
		//PublicAdditionalLibraries.AddRange(new string[] { "C:\\code\\d3_deps\\.conan\\data\\CUDA\\10.2\\d3\\stable\\package\\ca33edce272a279b24f87dc0d4cf5bbdcffbc187\\lib\\x64\\cudart.lib" });

		PublicDependencyModuleNames.AddRange (new string[] { "Core", "Sockets", "Networking", "MediaIOCore", "MediaUtils", "InputCore", "UMG" });
		PrivateDependencyModuleNames.AddRange (new string[] { "CoreUObject", "Engine", "Slate", "SlateCore", "CinematicCamera", "RHI", "D3D11RHI", "RenderCore", "Projects", "Json", "JsonUtilities" });
		
		DynamicallyLoadedModuleNames.AddRange (new string[] {});
	}
}
