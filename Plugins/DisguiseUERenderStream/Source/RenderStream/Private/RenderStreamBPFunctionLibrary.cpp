#include "RenderStreamBPFunctionLibrary.h"
#include "CoreUObject/Public/UObject/UObjectGlobals.h"

#include "RenderStreamMediaOutput.h"
#include "RenderStreamMediaCapture.h"

#include "Camera/CameraActor.h"
#include "CinematicCamera/Public/CineCameraActor.h"

#include "Camera/CameraComponent.h"
#include "CinematicCamera/Public/CineCameraComponent.h"

#include "Kismet/GameplayStatics.h"

URenderStreamBPFunctionLibrary::URenderStreamBPFunctionLibrary(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}

/*static*/ URenderStreamMediaCapture* URenderStreamBPFunctionLibrary::StartCapture(URenderStreamMediaOutput* MediaOutput, ACameraActor* Camera, bool SetNewViewTarget)
{
    
    USceneComponent* Loc = nullptr;
    USceneComponent* Rot = nullptr;
    UCameraComponent* Cam = nullptr;
    if (Camera)
    {
        Loc = Camera->K2_GetRootComponent();
        Rot = Loc;
        Cam = Camera->GetCameraComponent();
    }

    return StartCaptureWithComponents(MediaOutput, Loc, Rot, Cam, SetNewViewTarget);
}

/*static*/ URenderStreamMediaCapture* URenderStreamBPFunctionLibrary::StartCaptureWithComponents(URenderStreamMediaOutput* MediaOutput, USceneComponent* LocationReceiver, USceneComponent* RotationReceiver, UCameraComponent* CameraDataReceiver, bool SetNewViewTarget)
{
    if (!MediaOutput)
        return nullptr;

    URenderStreamMediaCapture* Capture = nullptr;
    Capture = CastChecked<URenderStreamMediaCapture>(MediaOutput->CreateMediaCapture());
    if (!Capture)
        return nullptr;

    AActor* Root = CameraDataReceiver ? CameraDataReceiver->GetOwner() : nullptr;
    if (Root && SetNewViewTarget)
    {
        APlayerController* Controller = UGameplayStatics::GetPlayerController(Root, 0);
        Controller->SetViewTargetWithBlend(Root);
    }

    Capture->SetReceivingComponentsCamera(LocationReceiver, RotationReceiver, CameraDataReceiver);

    FMediaCaptureOptions Options;
    Options.bResizeSourceBuffer = true;

    Capture->CaptureActiveSceneViewport(Options);
    return Capture;
}

/*static*/ void URenderStreamBPFunctionLibrary::StopCapture(URenderStreamMediaCapture* MediaCapture)
{
    if (MediaCapture)
        MediaCapture->StopCapture(true);
}