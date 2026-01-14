#include <cuda_runtime.h>

__global__ void TestKernel()
{
    // Minimal test kernel
}

extern "C" bool TestCUDASetup()
{
    int deviceCount = 0;
    cudaError_t error = cudaGetDeviceCount(&deviceCount);
    return error == cudaSuccess && deviceCount > 0;
}
