// Include engine headers FIRST (before CUDA headers)
#include "Engine/UserInterface/Console.h"
#include "CUDADXInterop.h"

// Windows/D3D headers
#include <d3d11.h>
#include <dxgi.h>

// CUDA headers come last (after engine headers have defined TCHAR)
// Undefine TCHAR before including CUDA headers to avoid redefinition
#ifdef CUDA_ENABLED
#ifdef TCHAR
#undef TCHAR
#endif
#include <cuda_runtime.h>
#include <cuda_d3d11_interop.h>
#endif

FCUDADXInterop::FCUDADXInterop()
    : bInitialized(false), DeviceID(-1), CudaStream(nullptr), D3DDevice(nullptr)
{
}

FCUDADXInterop::~FCUDADXInterop()
{
    Release();
}

bool FCUDADXInterop::Initialize(ID3D11Device *InD3DDevice)
{
    if (bInitialized)
    {
        return true;
    }

    if (!InD3DDevice)
    {
        UE_LOG(ELogLevel::Error, TEXT("FCUDADXInterop::Initialize - Invalid D3D device"));
        return false;
    }

#ifndef CUDA_ENABLED
    UE_LOG(ELogLevel::Error, TEXT("FCUDADXInterop::Initialize - CUDA not enabled in build"));
    return false;
#else

    D3DDevice = InD3DDevice;

    // Get CUDA devices compatible with this D3D11 device
    unsigned int cudaDeviceCount = 0;
    int cudaDevices[16];
    cudaError_t error = cudaD3D11GetDevices(&cudaDeviceCount, cudaDevices, 16, InD3DDevice, cudaD3D11DeviceListAll);

    if (!CheckCudaError(error, "cudaD3D11GetDevices") || cudaDeviceCount == 0)
    {
        UE_LOG(ELogLevel::Error, TEXT("FCUDADXInterop::Initialize - No CUDA devices found for D3D11 adapter"));
        return false;
    }

    // Use the first compatible CUDA device
    DeviceID = cudaDevices[0];
    error = cudaSetDevice(DeviceID);
    if (!CheckCudaError(error, "cudaSetDevice"))
    {
        return false;
    }

    // Get device properties for logging
    cudaDeviceProp deviceProp;
    error = cudaGetDeviceProperties(&deviceProp, DeviceID);
    if (CheckCudaError(error, "cudaGetDeviceProperties"))
    {
        UE_LOG(ELogLevel::Display, TEXT("FCUDADXInterop::Initialize - Using CUDA device %d (Compute %d.%d)"),
               DeviceID, deviceProp.major, deviceProp.minor);
    }

    // Create CUDA stream for async operations
    error = cudaStreamCreate(&CudaStream);
    if (!CheckCudaError(error, "cudaStreamCreate"))
    {
        return false;
    }

    bInitialized = true;
    UE_LOG(ELogLevel::Display, TEXT("FCUDADXInterop::Initialize - CUDA-DX11 interop initialized successfully"));
    return true;

#endif // CUDA_ENABLED
}

void FCUDADXInterop::Release()
{
    if (!bInitialized)
    {
        return;
    }

#ifdef CUDA_ENABLED
    if (CudaStream)
    {
        cudaStreamDestroy(CudaStream);
        CudaStream = nullptr;
    }

    // Note: We don't reset the CUDA device here as it may be shared
    // with other parts of the application
#endif

    D3DDevice = nullptr;
    DeviceID = -1;
    bInitialized = false;

    UE_LOG(ELogLevel::Display, TEXT("FCUDADXInterop::Release - CUDA-DX11 interop released"));
}

bool FCUDADXInterop::RegisterD3DBuffer(ID3D11Buffer *D3DBuffer, cudaGraphicsResource **OutCudaResource)
{
    if (!bInitialized)
    {
        UE_LOG(ELogLevel::Error, TEXT("FCUDADXInterop::RegisterD3DBuffer - Not initialized"));
        return false;
    }

    if (!D3DBuffer || !OutCudaResource)
    {
        UE_LOG(ELogLevel::Error, TEXT("FCUDADXInterop::RegisterD3DBuffer - Invalid parameters"));
        return false;
    }

#ifndef CUDA_ENABLED
    return false;
#else
    cudaError_t error = cudaGraphicsD3D11RegisterResource(
        OutCudaResource,
        D3DBuffer,
        cudaGraphicsRegisterFlagsNone);

    if (!CheckCudaError(error, "cudaGraphicsD3D11RegisterResource"))
    {
        UE_LOG(ELogLevel::Error, TEXT("FCUDADXInterop::RegisterD3DBuffer - Failed to register buffer"));
        return false;
    }

    return true;
#endif
}

void FCUDADXInterop::UnregisterResource(cudaGraphicsResource *Resource)
{
    if (!bInitialized || !Resource)
    {
        return;
    }

#ifdef CUDA_ENABLED
    cudaError_t error = cudaGraphicsUnregisterResource(Resource);
    CheckCudaError(error, "cudaGraphicsUnregisterResource");
#endif
}

bool FCUDADXInterop::MapResources(cudaGraphicsResource **Resources, int Count, cudaStream_t Stream)
{
    if (!bInitialized)
    {
        UE_LOG(ELogLevel::Error, TEXT("FCUDADXInterop::MapResources - Not initialized"));
        return false;
    }

    if (!Resources || Count <= 0)
    {
        UE_LOG(ELogLevel::Error, TEXT("FCUDADXInterop::MapResources - Invalid parameters"));
        return false;
    }

#ifndef CUDA_ENABLED
    return false;
#else
    // Use internal stream if none provided
    cudaStream_t streamToUse = (Stream == 0) ? CudaStream : Stream;

    cudaError_t error = cudaGraphicsMapResources(Count, Resources, streamToUse);

    if (!CheckCudaError(error, "cudaGraphicsMapResources"))
    {
        UE_LOG(ELogLevel::Error, TEXT("FCUDADXInterop::MapResources - Failed to map %d resources"), Count);
        return false;
    }

    return true;
#endif
}

void FCUDADXInterop::UnmapResources(cudaGraphicsResource **Resources, int Count, cudaStream_t Stream)
{
    if (!bInitialized || !Resources || Count <= 0)
    {
        return;
    }

#ifdef CUDA_ENABLED
    // Use internal stream if none provided
    cudaStream_t streamToUse = (Stream == 0) ? CudaStream : Stream;

    cudaError_t error = cudaGraphicsUnmapResources(Count, Resources, streamToUse);
    CheckCudaError(error, "cudaGraphicsUnmapResources");
#endif
}

bool FCUDADXInterop::GetMappedPointer(cudaGraphicsResource *Resource, void **OutDevPtr, size_t *OutSize)
{
    if (!bInitialized)
    {
        UE_LOG(ELogLevel::Error, TEXT("FCUDADXInterop::GetMappedPointer - Not initialized"));
        return false;
    }

    if (!Resource || !OutDevPtr || !OutSize)
    {
        UE_LOG(ELogLevel::Error, TEXT("FCUDADXInterop::GetMappedPointer - Invalid parameters"));
        return false;
    }

#ifndef CUDA_ENABLED
    return false;
#else
    cudaError_t error = cudaGraphicsResourceGetMappedPointer(OutDevPtr, OutSize, Resource);

    if (!CheckCudaError(error, "cudaGraphicsResourceGetMappedPointer"))
    {
        UE_LOG(ELogLevel::Error, TEXT("FCUDADXInterop::GetMappedPointer - Failed to get mapped pointer"));
        return false;
    }

    return true;
#endif
}

void FCUDADXInterop::SynchronizeStream()
{
    if (!bInitialized)
    {
        return;
    }

#ifdef CUDA_ENABLED
    cudaError_t error = cudaStreamSynchronize(CudaStream);
    CheckCudaError(error, "cudaStreamSynchronize");
#endif
}

bool FCUDADXInterop::CheckCudaError(cudaError_t Error, const char *Operation)
{
#ifdef CUDA_ENABLED
    if (Error != cudaSuccess)
    {
        const char *errorStr = cudaGetErrorString(Error);
        return false;
    }
#endif
    return true;
}
