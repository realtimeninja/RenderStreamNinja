// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MediaIOCore/Public/MediaCapture.h"
#include "Misc/Timecode.h"
#include "Math/UnitConversion.h"

#include "RenderStream.h"
#include "RenderStreamLink.h"

#include "RenderStreamMediaCapture.generated.h"

/**
 * 
 */
UCLASS(ClassGroup = (Disguise))
class RENDERSTREAM_API URenderStreamMediaCapture : public UMediaCapture
{
    GENERATED_BODY ()

public:

    void SetReceivingComponentsCamera(class USceneComponent* LocationComponent, class USceneComponent* RotationComponent, class UCameraComponent* Camera);

    RenderStreamLink::StreamHandle streamHandle() const { return m_streamHandle; }
    void ApplyCameraData(const RenderStreamLink::FrameData& frameData, const RenderStreamLink::CameraData& cameraData);

    UWorld* SchemaWorld() const;
    UFUNCTION(BlueprintCallable, Category = "Callback")
    void UpdateSchema() const; // Callback for lazy level load

private:
    TWeakObjectPtr<USceneComponent> m_locationReceiver;
    TWeakObjectPtr<USceneComponent> m_rotationReceiver;
    TWeakObjectPtr<UCameraComponent> m_cameraDataReceiver;

    FString m_streamName;
    RenderStreamLink::StreamHandle m_streamHandle = 0;
	RenderStreamLink::SenderPixelFormat m_fmt;

    EUnit m_unitScale;

    RenderStreamLink::CameraResponseData m_frameResponseData;

    FTextureRHIRef m_bufTexture;

    bool m_printSuccess = false;

    bool m_useHQ = false;

    FRenderStreamModule* m_module = nullptr;

private:
    bool CreateSenderHandle ();

    // Begin UMediaCapture
protected:
    struct FRenderStreamUserData : FMediaCaptureUserData
    {
        RenderStreamLink::CameraResponseData frameData;
    };

    void OnCustomCapture_RenderingThread(FRHICommandListImmediate & RHICmdList, const FCaptureBaseData & InBaseData, TSharedPtr < FMediaCaptureUserData , ESPMode::ThreadSafe > InUserData, FTexture2DRHIRef InSourceTexture, FTextureRHIRef TargetableTexture, FResolveParams & ResolveParams, FVector2D CropU, FVector2D CropV) override;
    void OnRHITextureCaptured_RenderingThread(const FCaptureBaseData & InBaseData, TSharedPtr < FMediaCaptureUserData , ESPMode::ThreadSafe > InUserData, FTextureRHIRef InTexture) override;
    void OnFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, void* InBuffer, int32 Width, int32 Height) override;
    
    bool CaptureSceneViewportImpl (TSharedPtr<FSceneViewport>& InSceneViewport) override;
    bool CaptureRenderTargetImpl (UTextureRenderTarget2D* InRenderTarget) override;
    void StopCaptureImpl (bool bAllowPendingFrameToBeProcess) override;

    bool ShouldCaptureRHITexture() const override;

    TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> GetCaptureFrameUserData_GameThread () override;
    // End UMediaCapture

    bool ReadyCapture ();
};
