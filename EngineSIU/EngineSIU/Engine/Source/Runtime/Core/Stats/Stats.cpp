#include "Stats.h"
#include "WindowsPlatformTime.h"
#include "GpuTimingManager.h"
#include "ProfilerStatsManager.h"

FScopeCycleCounter::FScopeCycleCounter(TStatId StatId)
    : StartCycles(FPlatformTime::Cycles64())
    , UsedStatId(StatId)
{
}

FScopeCycleCounter::~FScopeCycleCounter()
{
    Finish();
}

uint64 FScopeCycleCounter::Finish()
{
    const uint64 EndCycles = FPlatformTime::Cycles64();
    const uint64 CycleDiff = EndCycles - StartCycles;

    FProfilerStatsManager::AddCpuStat(UsedStatId, FPlatformTime::ToMilliseconds(CycleDiff));

    return CycleDiff;
}

FGPUScopeCycleCounter::FGPUScopeCycleCounter(const TStatId& StatId, FGPUTimingManager& GPUTimingManager)
    : GPUTimingManager(GPUTimingManager),
    StatId(StatId),
    bStarted(false)
{
    if (GPUTimingManager.IsInitialized())
    {
        GPUTimingManager.StartTimestamp(StatId);
        bStarted = true; // Assume StartTimestamp handles internal errors/limits
    }
}

FGPUScopeCycleCounter::~FGPUScopeCycleCounter()
{
    if (bStarted && GPUTimingManager.IsInitialized())
    {
        GPUTimingManager.StopTimestamp(StatId);
    }
}
