#pragma once

#include <stdint.h>

class RenderStreamLink
{
public:
    enum SenderPixelFormat : uint32_t
    {
        // No sampling with alpha
        FMT_BGRA = 0x00000000,
        FMT_RGBA,

        // No sampling with padding
        FMT_BGRX,
        FMT_RGBX,

        // sampling
        FMT_UYVY_422,

        // special
        FMT_NDI_UYVY_422_A, // NDI specific yuv422 format with alpha lines appended to bottom of frame

        // rivermax
        FMT_UC_YUV422_10BIT,
        FMT_UC_YUV422_12BIT,
        FMT_UC_RGB_10BIT,
        FMT_UC_RGB_12BIT,
        FMT_UC_RGBA_10BIT,
        FMT_UC_RGBA_12BIT,
    };

    enum SenderFrameType
    {
        RS_FRAMETYPE_HOST_MEMORY,
        RS_FRAMETYPE_DX11_TEXTURE,
        RS_FRAMETYPE_DX12_TEXTURE
    };

    enum RS_ERROR
    {
        RS_ERROR_SUCCESS = 0,

        // Core is not initialised
        RS_NOT_INITIALISED,

        // Core is already initialised
        RS_ERROR_ALREADYINITIALISED,

        // Given handle is invalid
        RS_ERROR_INVALIDHANDLE,

        // Maximum number of frame senders have been created
        RS_MAXSENDERSREACHED,

        RS_ERROR_BADSTREAMTYPE,

        RS_ERROR_NOTFOUND,

        RS_ERROR_INCORRECTSCHEMA,

        RS_ERROR_UNSPECIFIED
    };

    // Bitmask flags
    enum FRAMEDATA_FLAGS
    {
        FRAMEDATA_NO_FLAGS = 0,
        FRAMEDATA_RESET = 1
    };

    typedef uint64_t StreamHandle;
    typedef uint64_t AssetHandle;
    typedef uint64_t CameraHandle;
    typedef void(*logger_t)(const char*);

#pragma pack(push, 4)
    typedef struct
    {
        StreamHandle id;
        CameraHandle cameraHandle;
        float x, y, z;
        float rx, ry, rz;
        float focalLength;
        float sensorX, sensorY;
        float cx, cy;
        float nearZ, farZ;
    } CameraData;

    typedef struct
    {
        double tTracked;
        double localTime;
        double localTimeDelta;
        unsigned int frameRateNumerator;
        unsigned int frameRateDenominator;
        uint32_t flags;
        uint32_t scene;
    } FrameData;

    typedef struct
    {
        double tTracked;
        CameraData camera;
    } CameraResponseData;
#pragma pack(pop)

#define RENDER_STREAM_VERSION_MAJOR 1
#define RENDER_STREAM_VERSION_MINOR 7

    static RenderStreamLink& instance();

private:
    RenderStreamLink();
    ~RenderStreamLink();

private:
    typedef void rs_getVersionFn(int* versionMajor, int* versionMinor);

    typedef void (*logger_t)(const char*);

    typedef void rs_registerLoggingFuncFn(logger_t);
    typedef void rs_registerErrorLoggingFuncFn(logger_t);
    typedef void rs_registerVerboseLoggingFuncFn(logger_t);

    typedef void rs_unregisterLoggingFuncFn();
    typedef void rs_unregisterErrorLoggingFuncFn();
    typedef void rs_unregisterVerboseLoggingFuncFn();

    typedef RS_ERROR rs_initFn();
    typedef RS_ERROR rs_shutdownFn();
    typedef RS_ERROR rs_createAssetFn(const char* name, AssetHandle* assetHandle);
    typedef RS_ERROR rs_destroyAssetFn(AssetHandle* assetHandle);
    typedef RS_ERROR rs_setSchemaFn(AssetHandle assetHandle, const char* jsonSchema);
    typedef RS_ERROR rs_createStreamFn(AssetHandle assetHandle, const char* name, StreamHandle* handle);
    typedef RS_ERROR rs_createUCStreamFn(AssetHandle assetHandle, const char* name, int width, int height, SenderPixelFormat senderFmt, int framerateNumerator, int framerateDenominator, void* pDeviceD3D11, bool opencl, StreamHandle* handle);
    typedef RS_ERROR rs_destroyStreamFn(AssetHandle assetHandle, StreamHandle* handle);
    typedef RS_ERROR rs_sendFrameFn(AssetHandle assetHandle, StreamHandle handle, SenderFrameType frameType, void* data, int width, int height, SenderPixelFormat senderFmt, void* metaData);
    typedef RS_ERROR rs_awaitFrameDataFn(/*Out*/AssetHandle * assetHandle, int timeoutMs, /*Out*/FrameData * data);
    typedef RS_ERROR rs_getFrameParametersFn(AssetHandle assetHandle, uint64_t schemaHash, /*Out*/void* outParameterData, size_t outParameterDataSize); 
    typedef RS_ERROR rs_getFrameCameraFn(AssetHandle assetHandle, StreamHandle streamHandle, /*Out*/CameraData* outCameraData);

public:
    bool isAvailable();

    bool loadExplicit();
    bool unloadExplicit();

public: // d3renderstream.h API, but loaded dynamically.
    rs_getVersionFn* rs_getVersion = nullptr;

    rs_registerLoggingFuncFn* rs_registerLoggingFunc = nullptr;
    rs_registerErrorLoggingFuncFn* rs_registerErrorLoggingFunc = nullptr;
    rs_registerVerboseLoggingFuncFn* rs_registerVerboseLoggingFunc = nullptr;

    rs_unregisterLoggingFuncFn* rs_unregisterLoggingFunc = nullptr;
    rs_unregisterErrorLoggingFuncFn* rs_unregisterErrorLoggingFunc = nullptr;
    rs_unregisterVerboseLoggingFuncFn* rs_unregisterVerboseLoggingFunc = nullptr;

    rs_initFn* rs_init = nullptr;
    rs_createAssetFn* rs_createAsset = nullptr;
    rs_destroyAssetFn* rs_destroyAsset = nullptr;
    rs_setSchemaFn* rs_setSchema = nullptr;
    rs_shutdownFn* rs_shutdown = nullptr;
    rs_createStreamFn* rs_createStream = nullptr;
    rs_createUCStreamFn* rs_createUCStream = nullptr;
    rs_destroyStreamFn* rs_destroyStream = nullptr;
    rs_sendFrameFn* rs_sendFrame = nullptr;
    rs_awaitFrameDataFn* rs_awaitFrameData = nullptr;
    rs_getFrameParametersFn* rs_getFrameParameters = nullptr;
    rs_getFrameCameraFn* rs_getFrameCamera = nullptr;

private:
    bool m_loaded = false;
    void* m_dll = nullptr;
};
