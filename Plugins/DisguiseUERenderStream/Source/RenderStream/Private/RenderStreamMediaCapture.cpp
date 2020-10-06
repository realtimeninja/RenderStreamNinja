// Fill out your copyright notice in the Description page of Project Settings.

#include "RenderStreamMediaCapture.h"

#include <string>
#include <sstream>

//#include <cuda_d3d11_interop.h>

#include "RenderStreamMediaOutput.h"

#include "Engine/Public/EngineUtils.h"
#include "Engine/Public/HardwareInfo.h"

#include "Camera/CameraActor.h"
#include "Core/Public/Misc/CoreDelegates.h"

#include "Camera/CameraComponent.h"
#include "CinematicCamera/Public/CineCameraActor.h"
#include "CinematicCamera/Public/CineCameraComponent.h"

#include <d3d11.h>
#include "D3D11RHI/Public/D3D11State.h"
#include "D3D11RHI/Public/D3D11Resources.h"
#include "D3D11RHI/Public/D3D11Util.h"

// The include order of these dx12 headers is very brittle.
#include <d3dx12.h>
#include "Windows/WindowsD3D12DiskCache.h"
#include "Windows/WindowsD3D12PipelineState.h"
class FD3D12CommandAllocator;
#include "D3D12PipelineState.h"
#include "D3D12Texture.h"
#include "D3D12RHI/Public/D3D12RHI.h"
#include "D3D12RHI/Public/D3D12State.h"
#include "D3D12RHI/Public/D3D12Resources.h"
#include "D3D12RHI/Public/D3D12Util.h"

#include "RHI/Public/RHICommandList.h"

#include "PipelineStateCache.h"
#include "RendererInterface.h"
#include "RenderUtils.h"
#include "RHIStaticStates.h"
#include "MediaShaders.h"

#include "RenderCore/Public/ShaderParameterMacros.h"
#include "RenderCore/Public/ShaderParameterUtils.h"
#include "Core/Public/Misc/ConfigCacheIni.h"


namespace
{
    HRESULT DX12CreateSharedRenderTarget2D(ID3D12Device * device,
        uint64_t width,
        uint32_t height, // matching the struct
        EPixelFormat format,
        const FRHIResourceCreateInfo& info,
        ID3D12Resource** outTexture,
        const TCHAR* Name)
    {

        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            (DXGI_FORMAT)GPixelFormats[format].PlatformFormat,
            width,
            height,
            1,  // Array size
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);  // Add misc flags later


        D3D12_CLEAR_VALUE ClearValue = CD3DX12_CLEAR_VALUE(desc.Format, info.ClearValueBinding.Value.Color);
        const D3D12_HEAP_PROPERTIES HeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        TRefCountPtr<ID3D12Resource> pResource;
        D3D12_HEAP_FLAGS HeapFlags = D3D12_HEAP_FLAG_NONE;
        if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)
        {
            HeapFlags |= D3D12_HEAP_FLAG_SHARED;
        }

        const HRESULT hr = device->CreateCommittedResource(&HeapProps, HeapFlags, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &ClearValue, IID_PPV_ARGS(outTexture));
        if (hr == 0)
        {
            const HRESULT hr2 = (*outTexture)->SetName(Name);
            if (hr2 != 0)
            {
                UE_LOG(LogRenderStream, Error, TEXT("Failed to set name on new dx12 texture."));
                return false;
            }
        }
        else
        {
            UE_LOG(LogRenderStream, Error, TEXT("Failed create a DX12 texture."));
            return false;
        }
        return true;
    }

}


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



bool isUCFormat(ERenderStreamMediaOutputFormat fmt)
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


    case ERenderStreamMediaOutputFormat::YUV422_10b: return RenderStreamLink::SenderPixelFormat::FMT_UC_YUV422_10BIT;
    case ERenderStreamMediaOutputFormat::YUV422_12b: return RenderStreamLink::SenderPixelFormat::FMT_UC_YUV422_12BIT;
    case ERenderStreamMediaOutputFormat::RGB_10b: return RenderStreamLink::SenderPixelFormat::FMT_UC_RGB_10BIT;
    case ERenderStreamMediaOutputFormat::RGB_12b: return RenderStreamLink::SenderPixelFormat::FMT_UC_RGB_12BIT;
    case ERenderStreamMediaOutputFormat::RGBA_10b: return RenderStreamLink::SenderPixelFormat::FMT_UC_RGBA_10BIT;
    case ERenderStreamMediaOutputFormat::RGBA_12b: return RenderStreamLink::SenderPixelFormat::FMT_UC_RGBA_12BIT;


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
    return isUCFormat(Output->m_outputFormat);
}

bool URenderStreamMediaCapture::CreateSenderHandle()
{
    URenderStreamMediaOutput* Output = CastChecked<URenderStreamMediaOutput>(MediaOutput);

    m_module = FRenderStreamModule::Get();

    m_fmt = GetSendingFormat(Output->OutputFormat());

    m_useUC = isUCFormat(Output->m_outputFormat);

    // name selection priority goes Camera > Location > Rotation
    USceneComponent* BestComp = m_cameraDataReceiver.Get();
    if (!BestComp)
        BestComp = m_locationReceiver.Get();
    if (!BestComp)
        BestComp = m_rotationReceiver.Get();
    const AActor* NameSupplier = BestComp ? BestComp->GetOwner() : nullptr;
    m_streamName = NameSupplier ? (NameSupplier->GetName()) : TEXT("Default");

    UpdateSchema();

    if (m_useUC) {
        auto point = Output->m_desiredSize;
        FRHIResourceCreateInfo info{ FClearValueBinding::Green };

        // Full float format required to match interop logic in d3. Buffer is chosen to be large enough to support all Unreal back buffer formats.
        constexpr auto format = EPixelFormat::PF_A32B32G32R32F;

        auto toggle = FHardwareInfo::GetHardwareInfo(NAME_RHI);
        if (toggle == "D3D12")
        {
            // unreal won't let us make a texture with the shared flag that isn't 8bit BGRA, so we have to handle it ourselves
            auto rhi12 = static_cast<FD3D12DynamicRHI*>(GDynamicRHI);
            auto dx12device = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
            ID3D12Resource* outTex = nullptr;
            if (!DX12CreateSharedRenderTarget2D(dx12device, point.X, point.Y, format, info, &outTex, L"DUERS_Target"))
            {
                UE_LOG(LogRenderStream, Error, TEXT("Failed to create DX12 render target."));
                m_module->m_status.setOutputStatus("Error: Failed create a DX12 render target.", RSSTATUS_RED);
                return false;
            }
            
            m_bufTexture = rhi12->RHICreateTexture2DFromResource(format, ETextureCreateFlags::TexCreate_Shared | ETextureCreateFlags::TexCreate_RenderTargetable, FClearValueBinding::Green, outTex);
        }
        else if (toggle == "D3D11")
        {
            m_bufTexture = RHICreateTexture2D(point.X, point.Y, format, 1, 1, ETextureCreateFlags::TexCreate_RenderTargetable, info);
        }
        else
        {
            UE_LOG(LogRenderStream, Error, TEXT("RHI backend not supported for uncompressed RenderStream."));
            return false;
        }
        
        FRenderCommandFence fence;
        ENQUEUE_RENDER_COMMAND(FRenderStreamGetDevice)(

        [&, point](FRHICommandListImmediate& RHICmdList)
        {
            ID3D11Device* pDeviceD3D11 = reinterpret_cast<ID3D11Device*>(RHICmdList.GetNativeDevice());
            if (RenderStreamLink::instance().rs_createUCStream(m_module->m_assetHandle, TCHAR_TO_ANSI(*m_streamName), point[0], point[1], m_fmt, Output->m_framerateNumerator, Output->m_framerateDenominator, pDeviceD3D11, Output->m_opencl, &m_streamHandle) != 0)
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
    // Always update response data
    m_frameResponseData.tTracked = frameData.tTracked;
    m_frameResponseData.camera = cameraData;

    if (cameraData.cameraHandle == 0)
        return;

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
            FRHITexture2D* tex2d2 = m_bufTexture->GetTexture2D();
            auto point2 = tex2d2->GetSizeXY();
            void* resource = m_bufTexture->GetTexture2D()->GetNativeResource();

            auto toggle = FHardwareInfo::GetHardwareInfo(NAME_RHI);

            RenderStreamLink::SenderFrameType senderFrameType = RenderStreamLink::SenderFrameType::RS_FRAMETYPE_DX11_TEXTURE;
            if (toggle == "D3D11")
            {
                senderFrameType = RenderStreamLink::SenderFrameType::RS_FRAMETYPE_DX11_TEXTURE;
            }
            else if (toggle == "D3D12")
            {
                senderFrameType = RenderStreamLink::SenderFrameType::RS_FRAMETYPE_DX12_TEXTURE;
            }
            else 
            {
                UE_LOG(LogRenderStream, Error, TEXT("RenderStream tried to send frame with unsupported RHI backend."));
                return;
            }

            if (resource) 
            {
                RenderStreamLink::instance().rs_sendFrame(m_module->m_assetHandle, m_streamHandle, senderFrameType, resource, point2.X, point2.Y, m_fmt, &FrameData->frameData);
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

    RenderStreamLink::instance().rs_sendFrame(m_module->m_assetHandle, m_streamHandle, RenderStreamLink::SenderFrameType::RS_FRAMETYPE_HOST_MEMORY, InBuffer, frameWidth, frameHeight, m_fmt, &FrameData->frameData);
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
