#include <AMReX_PArena.H>
#include <AMReX_GpuDevice.H>
#include <AMReX_GpuElixir.H>
#include <AMReX_MemPool.H>

#ifdef AMREX_USE_OMP
#include <omp.h>
#endif

namespace amrex {

PArena::PArena (Long release_threshold)
{
#ifdef AMREX_GPU_STREAM_ALLOC_SUPPORT
    if (Gpu::Device::memoryPoolsSupported()) {
        AMREX_HIP_OR_CUDA(
            AMREX_HIP_SAFE_CALL(hipDeviceGetMemPool(&m_pool, Gpu::Device::deviceId()));,
            AMREX_CUDA_SAFE_CALL(cudaDeviceGetMemPool(&m_pool, Gpu::Device::deviceId()));
        )
        AMREX_HIP_OR_CUDA(
            AMREX_HIP_SAFE_CALL(hipMemPoolGetAttribute(m_pool, hipMemPoolAttrReleaseThreshold,
                                                       &m_old_release_threshold));,
            AMREX_CUDA_SAFE_CALL(cudaMemPoolGetAttribute(m_pool, cudaMemPoolAttrReleaseThreshold,
                                                         &m_old_release_threshold));
        )
        AMREX_HIP_OR_CUDA(
            uint64_t value = release_threshold;,
            cuuint64_t value = release_threshold;
        )
        AMREX_HIP_OR_CUDA(
            AMREX_HIP_SAFE_CALL(hipMemPoolSetAttribute(m_pool, hipMemPoolAttrReleaseThreshold, &value));,
            AMREX_CUDA_SAFE_CALL(cudaMemPoolSetAttribute(m_pool, cudaMemPoolAttrReleaseThreshold, &value));
        )
    }
#endif
    amrex::ignore_unused(release_threshold);
}

PArena::~PArena () // NOLINT(modernize-use-equals-default)
{
#ifdef AMREX_GPU_STREAM_ALLOC_SUPPORT
    if (Gpu::Device::memoryPoolsSupported()) {
        AMREX_HIP_OR_CUDA(
            AMREX_HIP_SAFE_CALL(hipMemPoolSetAttribute(m_pool, hipMemPoolAttrReleaseThreshold,
                                                        &m_old_release_threshold));,
            AMREX_CUDA_SAFE_CALL(cudaMemPoolSetAttribute(m_pool, cudaMemPoolAttrReleaseThreshold,
                                                        &m_old_release_threshold));
        )
    }
#endif
}

void*
PArena::alloc (std::size_t nbytes)
{
#if defined(AMREX_USE_GPU)

#if defined(AMREX_GPU_STREAM_ALLOC_SUPPORT)
    if (Gpu::Device::memoryPoolsSupported()) {
        void* p;
        AMREX_HIP_OR_CUDA(
            AMREX_HIP_SAFE_CALL(hipMallocAsync(&p, nbytes, m_pool, Gpu::gpuStream()));,
            AMREX_CUDA_SAFE_CALL(cudaMallocAsync(&p, nbytes, m_pool, Gpu::gpuStream()));
        )
        m_profiler.profile_alloc(p, nbytes);
        return p;
    } else
#endif
    {
        return The_Arena()->alloc(nbytes);
    }

#elif defined(AMREX_USE_OMP)

    if (omp_in_parallel()) {
        return amrex_mempool_alloc(nbytes);
    } else {
        return The_Arena()->alloc(nbytes);
    }

#else

    return The_Arena()->alloc(nbytes);

#endif
}

void
PArena::free (void* p)
{
    if (p == nullptr) { return; }

#if defined(AMREX_USE_GPU)

#if defined (AMREX_GPU_STREAM_ALLOC_SUPPORT)
    if (Gpu::Device::memoryPoolsSupported()) {
        m_profiler.profile_free(p);
        AMREX_HIP_OR_CUDA(
            AMREX_HIP_SAFE_CALL(hipFreeAsync(p, Gpu::gpuStream()));,
            AMREX_CUDA_SAFE_CALL(cudaFreeAsync(p, Gpu::gpuStream()));
        )
    } else
#endif
    {
        Elixir eli(p, The_Arena());
    }

#elif defined(AMREX_USE_OMP)

    if (omp_in_parallel()) {
        amrex_mempool_free(p);
    } else {
        The_Arena()->free(p);
    }

#else

    The_Arena()->free(p);

#endif
}

bool
PArena::isDeviceAccessible () const
{
#ifdef AMREX_USE_GPU
#ifdef AMREX_GPU_STREAM_ALLOC_SUPPORT
    if (Gpu::Device::memoryPoolsSupported()) {
        return true;
    } else
#endif
    {
        return The_Arena()->isDeviceAccessible();
    }
#else
    return false;
#endif
}

bool
PArena::isHostAccessible () const
{
#ifdef AMREX_USE_GPU
#if defined (AMREX_GPU_STREAM_ALLOC_SUPPORT)
    if (Gpu::Device::memoryPoolsSupported()) {
        return false; // cudaMallocAsync allocates device memory
    } else
#endif
    {
        return The_Arena()->isHostAccessible();
    }
#else
    return true;
#endif
}

bool
PArena::isManaged () const
{
#ifdef AMREX_USE_GPU
#if defined(AMREX_GPU_STREAM_ALLOC_SUPPORT)
    if (Gpu::Device::memoryPoolsSupported()) {
        return false; // cudaMallocAsync allocates device memory
    } else
#endif
    {
        return The_Arena()->isManaged();
    }
#else
    return false;
#endif
}

bool
PArena::isDevice () const
{
#ifdef AMREX_USE_GPU
#if defined (AMREX_GPU_STREAM_ALLOC_SUPPORT)
    if (Gpu::Device::memoryPoolsSupported()) {
        return true; // cudaMallocAsync allocates device memory
    } else
#endif
    {
        return The_Arena()->isDevice();
    }
#else
    return false;
#endif
}

bool
PArena::isPinned () const
{
#ifdef AMREX_USE_GPU
#if defined (AMREX_GPU_STREAM_ALLOC_SUPPORT)
    if (Gpu::Device::memoryPoolsSupported()) {
        return false; // cudaMallocAsync allocates device memory
    } else
#endif
    {
        return The_Arena()->isPinned();
    }
#else
    return false;
#endif
}

}
