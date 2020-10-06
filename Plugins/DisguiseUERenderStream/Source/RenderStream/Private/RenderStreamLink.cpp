#include "RenderStreamLink.h"

#if defined WIN32 || defined WIN64
#define WINDOWS
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef WINDOWS
#include "Core/Public/Windows/AllowWindowsPlatformTypes.h"
#include <WinError.h>
#include <Winreg.h>
#include <libloaderapi.h>
#endif 

/* static */ RenderStreamLink& RenderStreamLink::instance()
{
    static RenderStreamLink r;
    return r;
}

RenderStreamLink::RenderStreamLink()
{
    loadExplicit();
}

RenderStreamLink::~RenderStreamLink()
{
    unloadExplicit();
}

bool RenderStreamLink::isAvailable()
{
    return m_dll && m_loaded;
}

bool RenderStreamLink::loadExplicit()
{
    if (isAvailable())
        return true;

#ifdef WINDOWS

    //#define USE_HARD_PATH
#ifndef USE_HARD_PATH
    HKEY hKey;
    HRESULT hResult = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\d3 Technologies\\d3 Production Suite", 0, KEY_READ, &hKey);
    if (FAILED(hResult))
    {
        UE_LOG(LogRenderStream, Error, TEXT("Failed to open d3 production suite registry key."));
        return false;
    }

    FString valueName("exe path");
    TCHAR buffer[512];
    DWORD bufferSize = sizeof(buffer);
    hResult = RegQueryValueExW(hKey, *valueName, 0, nullptr, reinterpret_cast<LPBYTE>(buffer), &bufferSize);
    if (FAILED(hResult))
    {
        UE_LOG(LogRenderStream, Error, TEXT("Failed to query exe path registry value."));
        return false;
    }


    FString exePath(buffer);
#else
    FString exePath = "c:\\code\\d3dev2\\fbuild\\x64-msvc12.0-toolset120-Release-full\\d3\\build\\msvc\\d3.exe";
#endif
    int32 index;
    exePath.FindLastChar('\\', index);
    if (index != exePath.Len() - 1)
        exePath = exePath.Left(index + 1);
    FString dllName("d3renderstream.dll");

    if (!FPaths::FileExists(exePath + dllName))
    {
        UE_LOG(LogRenderStream, Error, TEXT("%s not found in %s."), *dllName, *exePath);
        return false;
    }

    m_dll = LoadLibraryEx(*(exePath + dllName), NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (m_dll == nullptr)
    {
        UE_LOG(LogRenderStream, Error, TEXT("Failed to load %s."), *exePath);
        return false;
    }

#define LOAD_FN(FUNC_NAME) \
    FUNC_NAME = (FUNC_NAME ## Fn*)FPlatformProcess::GetDllExport(m_dll, TEXT(#FUNC_NAME)); \
    if (!FUNC_NAME) { \
        UE_LOG(LogRenderStream, Error, TEXT("Failed to get function " #FUNC_NAME " from DLL.")); \
        m_loaded = false; \
        return false; \
    }

    LOAD_FN(rs_getVersion);
    LOAD_FN(rs_init);
    LOAD_FN(rs_shutdown);

    LOAD_FN(rs_registerLoggingFunc);
    LOAD_FN(rs_registerErrorLoggingFunc);
    LOAD_FN(rs_registerVerboseLoggingFunc);

    LOAD_FN(rs_unregisterLoggingFunc);
    LOAD_FN(rs_unregisterErrorLoggingFunc);
    LOAD_FN(rs_unregisterVerboseLoggingFunc);

    LOAD_FN(rs_createAsset);
    LOAD_FN(rs_destroyAsset);

    LOAD_FN(rs_setSchema);

    LOAD_FN(rs_createStream);
    LOAD_FN(rs_createUCStream);
    LOAD_FN(rs_destroyStream);

    LOAD_FN(rs_sendFrame);
    LOAD_FN(rs_awaitFrameData);
    LOAD_FN(rs_getFrameParameters);
    LOAD_FN(rs_getFrameCamera);

    m_loaded = true;

    rs_registerLoggingFunc(&log_default);
    rs_registerErrorLoggingFunc(&log_error);
    rs_registerVerboseLoggingFunc(&log_verbose);

#endif

    return isAvailable();
}

bool RenderStreamLink::unloadExplicit()
{
    if (rs_shutdown)
        rs_shutdown();
#ifdef WINDOWS
    if (m_dll)
        FreeLibrary((HMODULE)m_dll);
    m_dll = nullptr;
#endif
    return m_dll == nullptr;
}
