// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "RenderStreamMediaOutputFactory.generated.h"

/**
 * 
 */
UCLASS()
class RENDERSTREAMEDITOR_API URenderStreamMediaOutputFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

public:
	virtual UObject* FactoryCreateNew (UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual uint32 GetMenuCategories () const override;
	virtual bool ShouldShowInNewMenu () const override;
};
