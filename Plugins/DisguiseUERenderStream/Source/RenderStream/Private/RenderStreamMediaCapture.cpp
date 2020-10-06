// Fill out your copyright notice in the Description page of Project Settings.
#include "RenderStreamMediaCapture.h"

#include <string>
#include <sstream>


//#include <cuda_d3d11_interop.h>

#include "RenderStreamMediaOutput.h"

#include "Engine/Public/EngineUtils.h"

#include "Camera/CameraActor.h"
#include "Core/Public/Misc/CoreDelegates.h"

#include "Camera/CameraComponent.h"
#include "CinematicCamera/Public/CineCameraActor.h"
#include "CinematicCamera/Public/CineCameraComponent.h"
#include <d3d11.h>
#include "D3D11RHI/Public/D3D11State.h"
#include "D3D11RHI/Public/D3D11Resources.h"
#include "D3D11RHI/Public/D3D11Util.h"
#include "RHI/Public/RHICommandList.h"


#include "PipelineStateCache.h"
#include "RendererInterface.h"
#include "RenderUtils.h"
#include "RHIStaticStates.h"
#include "MediaShaders.h"

#include "RenderCore/Public/ShaderParameterMacros.h"
#include "RenderCore/Public/ShaderParameterUtils.h"
#include "Core/Public/Misc/ConfigCacheIni.h"

static EUnit getGlobalUnitEnum()
{
    // Unreal defaults to centimeters so we might as well do the same
    EUnit ret = EUnit::Centimeters;

    FString ValueReceived;
    if (!GConfig->GetString(TEXT("/Script/UnrealEd.EditorProjectAppearanceSettings"), TEXT("DistanceUnits"), ValueReceived, GEditorIni))
        return ret;

    TOptional<EUnit> currentUnit = FUnitConversion::UnitFromString(*ValueReceived);
    if (currentUnit.IsSet())
        ret = currentUnit.GetValue();

    return ret;
}


class RSResizeCopy
    : public FGlobalShader
{
    DECLARE_EXPORTED_SHADER_TYPE(RSResizeCopy, Global, /* RenderStream */);
public:

    static bool ShouldCache(EShaderPlatform Platform)
    {
        return true;
    }

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
    }

    RSResizeCopy() { }

    RSResizeCopy(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
        : FGlobalShader(Initializer)
    { }


    void SetParameters(FRHICommandList& RHICmdList, TRefCountPtr<FRHITexture2D> RGBTexture, const FIntPoint& OutputDimensions);
};


/* FRGBConvertPS shader
 *****************************************************************************/

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(RSResizeCopyUB, )
SHADER_PARAMETER(FVector2D, UVScale)
SHADER_PARAMETER_TEXTURE(Texture2D, Texture)
SHADER_PARAMETER_SAMPLER(SamplerState, Sampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(RSResizeCopyUB, "RSResizeCopyUB");
IMPLEMENT_SHADER_TYPE(, RSResizeCopy, TEXT("/DisguiseUERenderStream/Private/copy.usf"), TEXT("RSCopyPS"), SF_Pixel);

void RSResizeCopy::SetParameters(FRHICommandList& CommandList, TRefCountPtr<FRHITexture2D> RGBTexture, const FIntPoint& OutputDimensions)
{
    RSResizeCopyUB UB;
    {
        UB.Sampler = TStaticSamplerState<SF_Point>::GetRHI();
        UB.Texture = RGBTexture;
        UB.UVScale = FVector2D((float)OutputDimensions.X / (float)RGBTexture->GetSizeX(), (float)OutputDimensions.Y / (float)RGBTexture->GetSizeY());
    }

    TUniformBufferRef<RSResizeCopyUB> Data = TUniformBufferRef<RSResizeCopyUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
    SetUniformBufferParameter(CommandList, CommandList.GetBoundPixelShader(), GetUniformBufferParameter<RSResizeCopyUB>(), Data);
}



bool isHQFormat(ERenderStreamMediaOutputFormat fmt)
{

    switch (fmt)
    {
    case ERenderStreamMediaOutputFormat::YUV422_10b:
    case ERenderStreamMediaOutputFormat::YUV422_12b:
    case ERenderStreamMediaOutputFormat::RGB_10b:
    case ERenderStreamMediaOutputFormat::RGB_12b:
    case ERenderStreamMediaOutputFormat::RGBA_10b:
    case ERenderStreamMediaOutputFormat::RGBA_12b:
        return true;


    default: return false;
    }
}

RenderStreamLink::SenderPixelFormat GetSendingFormat(ERenderStreamMediaOutputFormat fmt)
{
    switch (fmt)
    {
    case ERenderStreamMediaOutputFormat::BGRA: return RenderStreamLink::SenderPixelFormat::FMT_BGRA;
        //case ERenderStreamMediaOutputFormat::RGBA: return RenderStreamLink::FourCC::RGBA;
    case ERenderStreamMediaOutputFormat::YUV422: return RenderStreamLink::SenderPixelFormat::FMT_UYVY_422;


    case ERenderStreamMediaOutputFormat::YUV422_10b: return RenderStreamLink::SenderPixelFormat::FMT_HQ_YUV422_10BIT;
    case ERenderStreamMediaOutputFormat::YUV422_12b: return RenderStreamLink::SenderPixelFormat::FMT_HQ_YUV422_12BIT;
    case ERenderStreamMediaOutputFormat::RGB_10b: return RenderStreamLink::SenderPixelFormat::FMT_HQ_RGB_10BIT;
    case ERenderStreamMediaOutputFormat::RGB_12b: return RenderStreamLink::SenderPixelFormat::FMT_HQ_RGB_12BIT;
    case ERenderStreamMediaOutputFormat::RGBA_10b: return RenderStreamLink::SenderPixelFormat::FMT_HQ_RGBA_10BIT;
    case ERenderStreamMediaOutputFormat::RGBA_12b: return RenderStreamLink::SenderPixelFormat::FMT_HQ_RGBA_12BIT;


    default: return RenderStreamLink::SenderPixelFormat::FMT_RGBA;
    }
}

float WidthMultiplier(RenderStreamLink::SenderPixelFormat fourcc)
{
    switch (fourcc)
    {
    case RenderStreamLink::SenderPixelFormat::FMT_NDI_UYVY_422_A:
    case RenderStreamLink::SenderPixelFormat::FMT_UYVY_422: return 2.f;
    default: return 1.f;
    }
}

float HeightMultiplier(RenderStreamLink::SenderPixelFormat fourcc)
{
    switch (fourcc)
    {
    case RenderStreamLink::SenderPixelFormat::FMT_NDI_UYVY_422_A: return 1.5f;
    default: return 1.f;
    }
}

void URenderStreamMediaCapture::SetReceivingComponentsCamera(USceneComponent* LocationComponent, USceneComponent* RotationComponent, UCameraComponent* Camera)
{
    m_locationReceiver = MakeWeakObjectPtr(LocationComponent);
    m_rotationReceiver = MakeWeakObjectPtr(RotationComponent);
    m_cameraDataReceiver = MakeWeakObjectPtr(Camera);
}

bool URenderStreamMediaCapture::ShouldCaptureRHITexture() const {
    URenderStreamMediaOutput* Output = CastChecked<URenderStreamMediaOutput>(MediaOutput);
    return isHQFormat(Output->m_outputFormat);
}

bool URenderStreamMediaCapture::CreateSenderHandle()
{
    URenderStreamMediaOutput* Output = CastChecked<URenderStreamMediaOutput>(MediaOutput);

    m_module = FRenderStreamModule::Get();

    m_fmt = GetSendingFormat(Output->OutputFormat());

    m_useHQ = ShouldCaptureRHITexture();

    // name selection priority goes Camera > Location > Rotation
    USceneComponent* BestComp = m_cameraDataReceiver.Get();
    if (!BestComp)
        BestComp = m_locationReceiver.Get();
    if (!BestComp)
        BestComp = m_rotationReceiver.Get();
    const AActor* NameSupplier = BestComp ? BestComp->GetOwner() : nullptr;
    m_streamName = NameSupplier ? (NameSupplier->GetName()) : TEXT("Default");

    UpdateSchema();

    if (m_useHQ) {
        auto point = Output->m_desiredSize;
        FRHIResourceCreateInfo info{ FClearValueBinding::Green };
        m_bufTexture = RHICreateTexture2D(point.X, point.Y, EPixelFormat::PF_FloatRGBA, 1, 1, ETextureCreateFlags::TexCreate_RenderTargetable, info);
        
        FRenderCommandFence fence;
        ENQUEUE_RENDER_COMMAND(FRenderStreamGetDevice)(

        [&, point](FRHICommandListImmediate& RHICmdList)
        {
            ID3D11Device* pDeviceD3D11 = reinterpret_cast<ID3D11Device*>(RHICmdList.GetNativeDevice());
            if (RenderStreamLink::instance().rs_createHQStream(m_module->m_assetHandle, TCHAR_TO_ANSI(*m_streamName), point[0], point[1], m_fmt, Output->m_framerateNumerator, Output->m_framerateDenominator, pDeviceD3D11, Output->m_opencl, &m_streamHandle) != 0)
            {
                m_streamHandle = 0;
            }
        });
        fence.BeginFence();
        fence.Wait();
        if (m_streamHandle == 0) {
            UE_LOG(LogRenderStream, Error, TEXT("Unable to create uncompressed stream"));
            m_module->m_status.setOutputStatus("Error: Unable to create uncompressed stream", RSSTATUS_RED);
            return false;
        }
        UE_LOG(LogRenderStream, Log, TEXT("Created uncompressed stream '%s'"), *m_streamName);
        m_module->m_status.setOutputStatus("Connected to uncompressed stream", RSSTATUS_GREEN);
    }
    else {
        if (RenderStreamLink::instance().rs_createStream(m_module->m_assetHandle, TCHAR_TO_ANSI(*m_streamName), &m_streamHandle) != 0)
        {
            m_streamHandle = 0;
            UE_LOG(LogRenderStream, Error, TEXT("Unable to create NDI stream '%s'"), *m_streamName);
            m_module->m_status.setOutputStatus("Error: Unable to create NDI stream", RSSTATUS_RED);
            return false;
        }
        UE_LOG(LogRenderStream, Log, TEXT("Created NDI stream '%s'"), *m_streamName);
        m_module->m_status.setOutputStatus("Connected to NDI stream", RSSTATUS_GREEN);
    }

    m_module->AddActiveCapture(this);

    return true;
}

void URenderStreamMediaCapture::ApplyCameraData(const RenderStreamLink::FrameData& frameData, const RenderStreamLink::CameraData& cameraData)
{
    URenderStreamMediaOutput* Output = CastChecked<URenderStreamMediaOutput>(MediaOutput);

    m_frameResponseData.tTracked = frameData.tTracked;
    m_frameResponseData.camera = cameraData;

    UCameraComponent* Camera = m_cameraDataReceiver.Get();
    if (UCineCameraComponent* CineCamera = dynamic_cast<UCineCameraComponent*>(Camera))
    {
        CineCamera->Filmback.SensorWidth = cameraData.sensorX;
        CineCamera->Filmback.SensorHeight = cameraData.sensorY;

        CineCamera->CurrentFocalLength = cameraData.focalLength;
    }
    else if (Camera)
    {
        float throwRatioH = cameraData.focalLength / cameraData.sensorX;
        float fovH = 2.f * FMath::Atan(0.5f / throwRatioH);
        Camera->SetFieldOfView(fovH * 180.f / PI);
    }

    USceneComponent* LocationComp = m_locationReceiver.Get();
    USceneComponent* RotationComp = m_rotationReceiver.Get();
    if (RotationComp)
    {
        float _pitch = cameraData.rx;
        float _yaw = cameraData.ry;
        float _roll = cameraData.rz;
        FQuat rotationQuat = FQuat::MakeFromEuler(FVector(_roll, _pitch, _yaw));
        RotationComp->SetRelativeRotation(rotationQuat);
    }
    if (LocationComp)
    {
        FVector pos;
        pos.X = FUnitConversion::Convert(float(cameraData.z), EUnit::Meters, m_unitScale);
        pos.Y = FUnitConversion::Convert(float(cameraData.x), EUnit::Meters, m_unitScale);
        pos.Z = FUnitConversion::Convert(float(cameraData.y), EUnit::Meters, m_unitScale);
        LocationComp->SetRelativeLocation(pos);
    }
}

UWorld* URenderStreamMediaCapture::SchemaWorld() const
{
    USceneComponent* BestComp = m_cameraDataReceiver.Get();
    if (!BestComp)
        BestComp = m_locationReceiver.Get();
    if (!BestComp)
        BestComp = m_rotationReceiver.Get();

    UWorld* World = BestComp && BestComp->GetOwner() ? BestComp->GetOwner()->GetWorld() : nullptr;
    return World;
}

void URenderStreamMediaCapture::UpdateSchema() const
{
    const UWorld* World = SchemaWorld();
    if (m_module && World)
        m_module->LoadSchemas(*World);
}

void URenderStreamMediaCapture::OnCustomCapture_RenderingThread(
    FRHICommandListImmediate& RHICmdList,
    const FCaptureBaseData& InBaseData,
    TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData,
    FTexture2DRHIRef InSourceTexture,
    FTextureRHIRef TargetableTexture,
    FResolveParams& ResolveParams,
    FVector2D CropU,
    FVector2D CropV)
{
    FRHITexture2D* tex2d = InSourceTexture->GetTexture2D();
    auto point = tex2d->GetSizeXY();
    auto format = tex2d->GetFormat();
    
    if (!m_printSuccess) {
        UE_LOG(LogRenderStream, Warning, TEXT("RenderStream got GPU tex, w: %d h: %d f: %d"), point.X, point.Y, format);
        m_printSuccess = true;
    }

    {
        // convert the source with a draw call
        FGraphicsPipelineStateInitializer GraphicsPSOInit;
        FRHITexture* RenderTarget = m_bufTexture.GetReference();
        FRHIRenderPassInfo RPInfo(RenderTarget, ERenderTargetActions::DontLoad_Store);
        RHICmdList.BeginRenderPass(RPInfo, TEXT("MediaCapture"));

        RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

        GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
        GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
        GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI();
        GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

        // configure media shaders
        auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
        TShaderMapRef<FMediaShadersVS> VertexShader(ShaderMap);

        GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GMediaVertexDeclaration.VertexDeclarationRHI;
        GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();

        TShaderMapRef<RSResizeCopy> ConvertShader(ShaderMap);
        GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ConvertShader.GetPixelShader();
        SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
        auto streamTexSize = m_bufTexture->GetTexture2D()->GetSizeXY();
        ConvertShader->SetParameters(RHICmdList, InSourceTexture, point);

        // draw full size quad into render target
        float ULeft = 0.0f;
        float URight = 1.0f;
        float VTop = 0.0f;
        float VBottom = 1.0f;
        FVertexBufferRHIRef VertexBuffer = CreateTempMediaVertexBuffer(ULeft, URight, VTop, VBottom);
        RHICmdList.SetStreamSource(0, VertexBuffer, 0);

        // set viewport to RT size
        RHICmdList.SetViewport(0, 0, 0.0f, streamTexSize.X, streamTexSize.Y, 1.0f);
        RHICmdList.DrawPrimitive(0, 2, 1);
        RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, m_bufTexture);

        RHICmdList.EndRenderPass();

        TSharedPtr<FRenderStreamUserData, ESPMode::ThreadSafe> FrameData = StaticCastSharedPtr<FRenderStreamUserData>(InUserData);

        RHICmdList.EnqueueLambda([this, FrameData](FRHICommandListImmediate& RHICmdList) {

            FD3D11TextureBase* tex = GetD3D11TextureFromRHITexture(m_bufTexture);
            FRHITexture2D* tex2d2 = m_bufTexture->GetTexture2D();
            auto point2 = tex2d2->GetSizeXY();
            ID3D11Resource* resource = tex->GetResource();

            if (resource) {
                RenderStreamLink::instance().rs_sendFrame(m_module->m_assetHandle, m_streamHandle, resource, point2.X, point2.Y, m_fmt, &FrameData->frameData);
            }
        });
    }
}

void URenderStreamMediaCapture::OnRHITextureCaptured_RenderingThread(const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, FTextureRHIRef InTexture) {

}

void URenderStreamMediaCapture::OnFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, void* InBuffer, int32 Width, int32 Height)
{
    if (m_streamHandle == 0)
        return;

    int frameWidth = Width * WidthMultiplier(m_fmt);
    int frameHeight = Height * HeightMultiplier(m_fmt);

    TSharedPtr<FRenderStreamUserData, ESPMode::ThreadSafe> FrameData = StaticCastSharedPtr<FRenderStreamUserData>(InUserData);

    RenderStreamLink::instance().rs_sendFrame(m_module->m_assetHandle, m_streamHandle, InBuffer, frameWidth, frameHeight, m_fmt, &FrameData->frameData);
}


bool URenderStreamMediaCapture::CaptureSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport)
{
    FSceneViewport* Viewport = InSceneViewport.Get();

    if (!ReadyCapture())
    {
        UE_LOG(LogRenderStream, Error, TEXT("Unable to start scene viewport capture."));
        m_module->m_status.setOutputStatus("Error: Unable to start scene viewport capture", RSSTATUS_RED);
        return false;
    }

    SetState(EMediaCaptureState::Capturing);
    return true;
}


bool URenderStreamMediaCapture::CaptureRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget)
{
    if (!ReadyCapture())
    {
        UE_LOG(LogRenderStream, Error, TEXT("Unable to start render target capture."));
        return false;
    }

    SetState(EMediaCaptureState::Capturing);
    return true;
}

bool URenderStreamMediaCapture::ReadyCapture()
{
    if (!RenderStreamLink::instance().isAvailable())
    {
        UE_LOG(LogRenderStream, Error, TEXT("Unable to start capture - RenderStream link is not active - install the correct version of d3."));
        m_module = FRenderStreamModule::Get();
        return false;
    }

    if (m_streamHandle == 0 && !CreateSenderHandle())
        return false;

    m_unitScale = getGlobalUnitEnum();

    return true;
}

void URenderStreamMediaCapture::StopCaptureImpl(bool bAllowPendingFrameToBeProcess)
{
    if (m_streamHandle != 0)
    {
        m_module->RemoveActiveCapture(this);
        m_module->m_status.setOutputStatus("Disconnected from stream", RSSTATUS_ORANGE);
        RenderStreamLink::instance().rs_destroyStream(m_module->m_assetHandle, &m_streamHandle);
    }
    UE_LOG(LogRenderStream, Log, TEXT("Destroyed stream '%s'"), *m_streamName);
}

TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> URenderStreamMediaCapture::GetCaptureFrameUserData_GameThread()
{
    TSharedPtr<FRenderStreamUserData, ESPMode::ThreadSafe> newData = MakeShared<FRenderStreamUserData, ESPMode::ThreadSafe>();
    newData->frameData = m_frameResponseData;
    return newData;
}
