// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;


public class RenderStream : ModuleRules
{
	public RenderStream(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange (new string [] { "RenderStream/Private", 
			Path.Combine(EngineDirectory, "Source/Runtime/D3D12RHI/Private"), 
			Path.Combine(EngineDirectory, "Source/Runtime/D3D12RHI/Public"),
			Path.Combine(EngineDirectory, "Source/ThirdParty/Windows/D3DX12/Include") }); 
		
		PublicDependencyModuleNames.AddRange (new string[] { "Core", "Sockets", "Networking", "MediaIOCore", "MediaUtils", "InputCore", "UMG" });
		PrivateDependencyModuleNames.AddRange (new string[] { "CoreUObject", "Engine", "Slate", "SlateCore", "CinematicCamera", "RHI", "D3D11RHI", "D3D12RHI", "RenderCore", "Projects", "Json", "JsonUtilities" });
		
		DynamicallyLoadedModuleNames.AddRange (new string[] {});
	}
}
