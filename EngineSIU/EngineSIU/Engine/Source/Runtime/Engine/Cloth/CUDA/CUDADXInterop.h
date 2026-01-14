#pragma once

#ifdef CUDA_ENABLED
#include <cuda_runtime.h>
#include <cuda_d3d11_interop.h>
#else
// Forward declarations when CUDA is not available
struct cudaGraphicsResource;
typedef struct CUstream_st *cudaStream_t;
typedef int cudaError_t;
#define cudaSuccess 0
#endif

/**
 * FCUDADXInterop
 *
 * Manages interoperability between CUDA and DirectX 11.
 * Handles buffer registration, resource mapping, and synchronization
 * to enable zero-copy data sharing between D3D11 rendering and CUDA compute.
 *
 * Key Features:
 * - Automatic CUDA device selection based on D3D11 adapter
 * - D3D11 buffer registration with CUDA
 * - Resource mapping/unmapping for CUDA access
 * - Stream management for async operations
 */
class FCUDADXInterop
{
public:
    FCUDADXInterop();
    ~FCUDADXInterop();

    /**
     * Initialize CUDA interop with the given D3D11 device.
     * Selects a compatible CUDA device and creates a CUDA stream.
     *
     * @param InD3DDevice - The D3D11 device to interop with
     * @return true if initialization succeeded, false otherwise
     */
    bool Initialize(ID3D11Device *InD3DDevice);

    /**
     * Release all CUDA resources and clean up.
     */
    void Release();

    /**
     * Register a D3D11 buffer for CUDA access.
     * The buffer must have D3D11_RESOURCE_MISC_SHARED flag.
     *
     * @param D3DBuffer - The D3D11 buffer to register
     * @param OutCudaResource - Receives the CUDA graphics resource handle
     * @return true if registration succeeded, false otherwise
     */
    bool RegisterD3DBuffer(ID3D11Buffer *D3DBuffer, cudaGraphicsResource **OutCudaResource);

    /**
     * Unregister a previously registered CUDA graphics resource.
     *
     * @param Resource - The CUDA graphics resource to unregister
     */
    void UnregisterResource(cudaGraphicsResource *Resource);

    /**
     * Map CUDA graphics resources for CUDA access.
     * After mapping, use GetMappedPointer to get device pointers.
     *
     * @param Resources - Array of CUDA graphics resources to map
     * @param Count - Number of resources in the array
     * @param Stream - CUDA stream for the operation (default: internal stream)
     * @return true if mapping succeeded, false otherwise
     */
    bool MapResources(cudaGraphicsResource **Resources, int Count, cudaStream_t Stream = 0);

    /**
     * Unmap CUDA graphics resources, returning control to D3D11.
     *
     * @param Resources - Array of CUDA graphics resources to unmap
     * @param Count - Number of resources in the array
     * @param Stream - CUDA stream for the operation (default: internal stream)
     */
    void UnmapResources(cudaGraphicsResource **Resources, int Count, cudaStream_t Stream = 0);

    /**
     * Get a CUDA device pointer from a mapped graphics resource.
     * Resource must be mapped before calling this function.
     *
     * @param Resource - The mapped CUDA graphics resource
     * @param OutDevPtr - Receives the CUDA device pointer
     * @param OutSize - Receives the buffer size in bytes
     * @return true if successful, false otherwise
     */
    bool GetMappedPointer(cudaGraphicsResource *Resource, void **OutDevPtr, size_t *OutSize);

    /**
     * Get the internal CUDA stream for async operations.
     *
     * @return The CUDA stream handle
     */
    cudaStream_t GetStream() const { return CudaStream; }

    /**
     * Synchronize the CUDA stream, blocking until all operations complete.
     */
    void SynchronizeStream();

    /**
     * Check if the interop system is initialized.
     *
     * @return true if initialized, false otherwise
     */
    bool IsInitialized() const { return bInitialized; }

    /**
     * Get the CUDA device ID being used.
     *
     * @return The CUDA device ID, or -1 if not initialized
     */
    int GetDeviceID() const { return DeviceID; }

private:
    bool bInitialized;
    int DeviceID;
    cudaStream_t CudaStream;
    ID3D11Device *D3DDevice;

    // Helper to check and log CUDA errors
    bool CheckCudaError(cudaError_t Error, const char *Operation);
};
