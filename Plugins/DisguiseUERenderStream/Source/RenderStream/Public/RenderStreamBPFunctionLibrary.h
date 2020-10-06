#pragma once

#include "Runtime/Engine/Classes/Kismet/BlueprintFunctionLibrary.h"

#include "RenderStreamBPFunctionLibrary.generated.h"

class URenderStreamMediaCapture;
class URenderStreamMediaOutput;
class ACameraActor;

class USceneComponent;
class UCameraComponent;

UCLASS()
class URenderStreamBPFunctionLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_UCLASS_BODY()

    //~~~~~~~~~~~~~~~~~~
    // 	Start Capture
    //~~~~~~~~~~~~~~~~~~
    /**  Starts Capture using the given Camera for tracking and view if desired
    *
    * @param MediaOutput - RenderStreamMediaOutput asset to use
    * @param Camera - CameraActor or subclass of CameraActor to use for view and tracking
    * @param SetNewViewTarget - true if the CameraActor provided will be the new main view target, otherwise captue will use whatever the current main view is
    * @return RenderStreamMediaCapture created or none on failure
    */
    UFUNCTION(BlueprintCallable, Category = "DisguiseRenderStream")
    static URenderStreamMediaCapture* StartCapture(URenderStreamMediaOutput* MediaOutput, ACameraActor* Camera, bool SetNewViewTarget = true);

    //~~~~~~~~~~~~~~~~~~
    // 	Start Capture
    //~~~~~~~~~~~~~~~~~~
    /**  Starts Capture using the given Components for tracking and the actor of the camera component for view if desired
    *
    * Main viewport will be set to the viewport of the given camera
    * @param MediaOutput - RenderStreamMediaOutput asset to use
    * @param LocationReceiver - SceneComponent to receive tracking position data, none means no position tracking
    * @param RotationReceiver - SceneCOmponent to receive tracking rotational data, none means no rotation tracking
    * @param CameraDataReceiver - CameraComponent or subclass of to use for view and receive camera data, the atachment actor of this component is the target of the view
    * @param SetNewViewTarget - true if the Actor that the CameraComponent provided is attached to will be the new main view target, otherwise captue will use whatever the current main view is
    * @return RenderStreamMediaCapture created or none on failure
    */
    UFUNCTION(BlueprintCallable, Category = "DisguiseRenderStream")
    static URenderStreamMediaCapture* StartCaptureWithComponents(URenderStreamMediaOutput* MediaOutput, USceneComponent* LocationReceiver, USceneComponent* RotationReceiver, UCameraComponent* CameraDataReceiver, bool SetNewViewTarget = true);

    //~~~~~~~~~~~~~~~~~~
    // 	Start Capture
    //~~~~~~~~~~~~~~~~~~
    /**  Stop Capturing with the given RenderStreamMediaCapture
    *
    * Ensures any pending frames are processed before stopping
    * @param MediaCapture - RenderStreamMediaCapture object that is currently capturing
    */
    UFUNCTION(BlueprintCallable, Category = "DisguiseRenderStream")
    static void StopCapture(URenderStreamMediaCapture* MediaCapture);
};