// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MediaIOCore/Public/MediaOutput.h"

#include "RenderStream.h"
#include "RenderStreamTimecodeProvider.h"

#include "RenderStreamMediaOutput.generated.h"

class UMediaCapture;

UENUM()
enum class ERenderStreamMediaOutputFormat
{
	//RGBA	UMETA(DisplayName = "RGBA 8bit"),
	BGRA	UMETA(DisplayName = "BGRA 8bit (NDI)"),
	YUV422	UMETA(DisplayName = "YUV 4:2:2 8bit (NDI)"),

	YUV422_10b	UMETA(DisplayName = "YUV 4:2:2 10bit (UC)"),
	YUV422_12b	UMETA(DisplayName = "YUV 4:2:2 12bit (UC)"),
	RGB_10b		UMETA(DisplayName = "RGB 10bit (UC)"),
	RGB_12b		UMETA(DisplayName = "RGB 12bit (UC)"),
	RGBA_10b	UMETA(DisplayName = "RGBA 10bit (UC)"),
	RGBA_12b	UMETA(DisplayName = "RGBA 12bit (UC)")
};

UENUM()
enum class ERenderStreamAlphaType
{
	NO_ACTION	UMETA(Display = "No Action"),
	SET_ONE		UMETA(DisplayName = "Set to 1"),
	INVERT		UMETA(DisplayName = "Invert Alpha"),
};

/**
 * 
 */
UCLASS(BlueprintType, ClassGroup = (Disguise))
class RENDERSTREAM_API URenderStreamMediaOutput : public UMediaOutput
{
	GENERATED_BODY()

public:
	
	UPROPERTY (BlueprintReadWrite, meta = (DisplayName = "Override Size"), Category = "DisguiseRenderStream")
	bool m_overrideSize;

	UPROPERTY (BlueprintReadWrite, EditAnywhere, meta = (EditCondition = "m_overrideSize", DisplayName = "Desired Size"), Category = "DisguiseRenderStream")
	FIntPoint m_desiredSize;

	UPROPERTY (BlueprintReadWrite, EditAnywhere, meta = (DisplayName = "Output Format"), Category = "DisguiseRenderStream")
	ERenderStreamMediaOutputFormat m_outputFormat;

	// If set, when receiving the stream, this object is populated with the timecode distributed from disguise
	UPROPERTY(EditAnywhere, Category = "Timecode", meta = (DisplayName = "Associated Timecode"))
	URenderStreamTimecodeProvider *m_timecode;

	// Sets the operation performed on alpha channel when alpha is provided
	UPROPERTY (BlueprintReadWrite, EditAnywhere, meta = (DisplayName = "Alpha Action"), Category = "DisguiseRenderStream")
	ERenderStreamAlphaType m_alphatype;

	UPROPERTY (EditAnywhere, Category = "RenderStream Uncompressed", meta = (DisplayName = "UC Framerate (numerator)"))
	uint32 m_framerateNumerator;

	UPROPERTY(EditAnywhere, Category = "RenderStream Uncompressed", meta = (DisplayName = "UC Framerate (denominator)"))
	uint32 m_framerateDenominator;

	UPROPERTY(EditAnywhere, Category = "RenderStream Uncompressed", meta = (DisplayName = "UC Use OpenCL"))
	bool m_opencl = false;


public:
	URenderStreamMediaOutput ();

	ERenderStreamMediaOutputFormat OutputFormat () { return m_outputFormat; }

	// Begin UMediaOutput
	bool Validate (FString& OutFailureReason) const override;
	FIntPoint GetRequestedSize () const override;
	EPixelFormat GetRequestedPixelFormat () const override;
	EMediaCaptureConversionOperation GetConversionOperation (EMediaCaptureSourceType InSourceType) const override;

protected:
	UMediaCapture* CreateMediaCaptureImpl () override;
	// End UMediaOutput
};
