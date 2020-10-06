// Fill out your copyright notice in the Description page of Project Settings.


#include "RenderStreamMediaOutput.h"

#include "RenderStreamMediaCapture.h"
#include "Core/Public/Modules/ModuleManager.h"

#include "Engine/Classes/Engine/RendererSettings.h"

URenderStreamMediaOutput::URenderStreamMediaOutput ()
	: Super(), m_overrideSize(true), m_desiredSize(1920, 1080), m_outputFormat(ERenderStreamMediaOutputFormat::BGRA), m_alphatype(ERenderStreamAlphaType::INVERT)
    , m_framerateNumerator(60), m_framerateDenominator(1)
{
}

bool URenderStreamMediaOutput::Validate(FString& OutFailureReason) const
{
	if (!Super::Validate (OutFailureReason))
	{
		return false;
	}

	if (m_overrideSize && (m_desiredSize.X == 0 || m_desiredSize.Y == 0))
	{
		OutFailureReason = FString::Printf (TEXT ("Can't validate MediaOutput '%s'. The desired size contains 0 length dimension(s)."), *GetName ());
		return false;
	}

	return true;
}

FIntPoint URenderStreamMediaOutput::GetRequestedSize () const
{
	//RHICmdList.GetViewportBackBuffer(InCapturingSceneViewport->GetViewportRHI());

    if (m_overrideSize)
    {
        return m_desiredSize;
    }

	return UMediaOutput::RequestCaptureSourceSize;
}


EPixelFormat URenderStreamMediaOutput::GetRequestedPixelFormat () const
{
	switch (m_outputFormat)
	{
	case ERenderStreamMediaOutputFormat::BGRA: 
	case ERenderStreamMediaOutputFormat::YUV422:
        return PF_B8G8R8A8;

	default:
        return EDefaultBackBufferPixelFormat::Convert2PixelFormat(GetDefault<URendererSettings>()->DefaultBackBufferPixelFormat);
	}
}

EMediaCaptureConversionOperation URenderStreamMediaOutput::GetConversionOperation (EMediaCaptureSourceType InSourceType) const
{
	switch (m_outputFormat)
	{
	case ERenderStreamMediaOutputFormat::YUV422: return EMediaCaptureConversionOperation::RGBA8_TO_YUV_8BIT;

	case ERenderStreamMediaOutputFormat::BGRA:
	//case ERenderStreamMediaOutputFormat::RGBA:
		switch (m_alphatype) 
		{
		case ERenderStreamAlphaType::INVERT: return EMediaCaptureConversionOperation::INVERT_ALPHA;
		case ERenderStreamAlphaType::SET_ONE: return EMediaCaptureConversionOperation::SET_ALPHA_ONE;
		default:
			return EMediaCaptureConversionOperation::NONE;
		}
	default:
		return EMediaCaptureConversionOperation::CUSTOM;
	}
}


UMediaCapture* URenderStreamMediaOutput::CreateMediaCaptureImpl ()
{
	URenderStreamMediaCapture* Result = NewObject<URenderStreamMediaCapture> ();
	if (Result)
	{
		Result->SetMediaOutput (this);
	}
	return Result;
}

