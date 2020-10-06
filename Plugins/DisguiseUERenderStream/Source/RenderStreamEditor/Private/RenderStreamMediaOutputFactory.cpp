// Fill out your copyright notice in the Description page of Project Settings.


#include "RenderStreamMediaOutputFactory.h"
#include "RenderStream/Public/RenderStreamMediaOutput.h"
#include "AssetTypeCategories.h"

URenderStreamMediaOutputFactory::URenderStreamMediaOutputFactory (const FObjectInitializer& ObjectInitializer)
	: Super (ObjectInitializer)
{
	SupportedClass = URenderStreamMediaOutput::StaticClass ();
	bCreateNew = true;
	bEditAfterNew = true;
}


/* UFactory overrides
 *****************************************************************************/

UObject* URenderStreamMediaOutputFactory::FactoryCreateNew (UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<URenderStreamMediaOutput> (InParent, InClass, InName, Flags);
}


uint32 URenderStreamMediaOutputFactory::GetMenuCategories () const
{
	return EAssetTypeCategories::Media;
}


bool URenderStreamMediaOutputFactory::ShouldShowInNewMenu () const
{
	return true;
}