#include "GpuTimingManager.h"


FGPUTimingManager::FGPUTimingManager() = default;

bool FGPUTimingManager::Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, uint32 InNumBufferedFrames)
{
    if (bInitialized) return true;
    if (!pDevice || !pContext || InNumBufferedFrames < 2) return false;

    Device = pDevice;
    Context = pContext;
    NumBufferedFrames = InNumBufferedFrames;

    // We need query pairs (start/end) + one disjoint query per frame buffer slot.
    // Let's pre-allocate a fixed number of pairs per frame for simplicity.
    FrameQueries.Reserve(NumBufferedFrames * MAX_QUERIES_PER_FRAME);
    for (uint32 i = 0; i < NumBufferedFrames * MAX_QUERIES_PER_FRAME; ++i)
    {
        FrameQueries.Add(FFrameQueries());
    }

    D3D11_QUERY_DESC QueryDesc = {};
    QueryDesc.MiscFlags = 0;

    for (uint32 i = 0; i < NumBufferedFrames; ++i)
    {
        // Create one Disjoint query per frame buffer slot
        QueryDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        HRESULT hr = Device->CreateQuery(&QueryDesc, FrameQueries[i * MAX_QUERIES_PER_FRAME].DisjointQuery.GetAddressOf());
        if (FAILED(hr)) { /* LOG_ERROR("Failed to create Disjoint Query"); */ return false; }

        // Create timestamp query pairs for measurements within the frame
        QueryDesc.Query = D3D11_QUERY_TIMESTAMP;
        for (uint32 j = 0; j < MAX_QUERIES_PER_FRAME; ++j)
        {
            const uint32 QueryIndex = i * MAX_QUERIES_PER_FRAME + j;
            // Only the first slot in a frame buffer needs the disjoint query object stored
            if (j > 0) FrameQueries[QueryIndex].DisjointQuery = FrameQueries[i * MAX_QUERIES_PER_FRAME].DisjointQuery;

            hr = Device->CreateQuery(&QueryDesc, FrameQueries[QueryIndex].StartQuery.GetAddressOf());
            if (FAILED(hr)) { /* LOG_ERROR("Failed to create Start Query"); */ return false; }

            hr = Device->CreateQuery(&QueryDesc, FrameQueries[QueryIndex].EndQuery.GetAddressOf());
            if (FAILED(hr)) { /* LOG_ERROR("Failed to create End Query"); */ return false; }

            FrameQueries[QueryIndex].bQueryIssued = false;
            FrameQueries[QueryIndex].StatId = TStatId(); // Reset StatId
        }
    }

    CurrentFrameIndex = 0;
    ResultsFrameIndex = (CurrentFrameIndex + 1) % NumBufferedFrames; // Start retrieving from the oldest buffer
    bInitialized = true;
    // LOG_INFO("FGpuTimingManager Initialized.");
    return true;
}

void FGPUTimingManager::BeginFrame()
{
    if (!bInitialized)
    {
        return;
    }

    LatestResults.Empty(); // Clear Previous Frame's Results

    // Retrieve results from the oldest frame buffer
    RetrieveResults();

    // Reset query usage count for the new frame
    CurrentQueryIndexInFrame = 0;

    // Begin the disjoint query for the current frame
    const uint32 BaseQueryIndex = CurrentFrameIndex * MAX_QUERIES_PER_FRAME;
    Context->Begin(FrameQueries[BaseQueryIndex].DisjointQuery.Get());

    FrameCounter++;
}

void FGPUTimingManager::StartTimestamp(const TStatId& StatId)
{
    if (!bInitialized || CurrentQueryIndexInFrame >= MAX_QUERIES_PER_FRAME)
    {
        // LOG_WARN("GPU Timer: Max queries per frame exceeded or not initialized.");
        return;
    }

    const uint32 QueryIndex = CurrentFrameIndex * MAX_QUERIES_PER_FRAME + CurrentQueryIndexInFrame;

    // Issue the end command for the *start* timestamp query
    Context->End(FrameQueries[QueryIndex].StartQuery.Get());
    FrameQueries[QueryIndex].StatId = StatId; // Associate StatId with this query slot
    FrameQueries[QueryIndex].bQueryIssued = true;
}

void FGPUTimingManager::StopTimestamp(const TStatId& StatId)
{
    if (!bInitialized || CurrentQueryIndexInFrame >= MAX_QUERIES_PER_FRAME)
    {
        // LOG_WARN("GPU Timer: Stop called without Start or max queries exceeded.");
        return;
    }

    // Find the matching Start query (simplistic: assumes it's the current index)
    // A stack would be needed here for proper nesting.
    const uint32 QueryIndex = CurrentFrameIndex * MAX_QUERIES_PER_FRAME + CurrentQueryIndexInFrame;

    if (FrameQueries[QueryIndex].StatId != StatId || !FrameQueries[QueryIndex].bQueryIssued)
    {
        // LOG_ERROR("GPU Timer: StopTimestamp called with mismatching StatId or without StartTimestamp.");
        // Or handle more gracefully depending on desired behavior
        return; // Don't issue End query if Start wasn't properly issued for this StatId/index
    }

    // Issue the end command for the *end* timestamp query
    Context->End(FrameQueries[QueryIndex].EndQuery.Get());

    // Move to the next available query pair for this frame
    CurrentQueryIndexInFrame++;
}


void FGPUTimingManager::EndFrame()
{
    if (!bInitialized) return;

    // End the disjoint query for the current frame
    const uint32 BaseQueryIndex = CurrentFrameIndex * MAX_QUERIES_PER_FRAME;
    Context->End(FrameQueries[BaseQueryIndex].DisjointQuery.Get());

    // Advance frame indices
    CurrentFrameIndex = (CurrentFrameIndex + 1) % NumBufferedFrames;
    ResultsFrameIndex = (CurrentFrameIndex + 1) % NumBufferedFrames; // Results are always N-1 frames behind current
}

void FGPUTimingManager::RetrieveResults()
{
    if (!bInitialized) return;

    // Try to get data from the results frame index
    const uint32 BaseResultIndex = ResultsFrameIndex * MAX_QUERIES_PER_FRAME;

    // Check disjoint query first for the entire frame's results
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT DisjointData;
    HRESULT hr = Context->GetData(FrameQueries[BaseResultIndex].DisjointQuery.Get(), &DisjointData, sizeof(DisjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH);

    if (hr == S_OK)
    {
        // Disjoint query data is ready
        for (uint32 i = 0; i < MAX_QUERIES_PER_FRAME; ++i)
        {
            const uint32 QueryIndex = BaseResultIndex + i;
            if (!FrameQueries[QueryIndex].bQueryIssued) continue; // Skip if not used

            double ResultMS = -2.0; // Default to "Data not ready"

            if (DisjointData.Disjoint)
            {
                ResultMS = -1.0; // Disjoint event
            }
            else
            {
                UINT64 StartTime = 0;
                HRESULT hrStart = Context->GetData(FrameQueries[QueryIndex].StartQuery.Get(), &StartTime, sizeof(StartTime), D3D11_ASYNC_GETDATA_DONOTFLUSH);

                UINT64 EndTime = 0;
                HRESULT hrEnd = Context->GetData(FrameQueries[QueryIndex].EndQuery.Get(), &EndTime, sizeof(EndTime), D3D11_ASYNC_GETDATA_DONOTFLUSH);

                if (hrStart == S_OK && hrEnd == S_OK)
                {
                    if (EndTime >= StartTime)
                    {
                        const UINT64 Delta = EndTime - StartTime;
                        ResultMS = (static_cast<double>(Delta) / static_cast<double>(DisjointData.Frequency)) * 1000.0;
                    }
                    else
                    {
                        ResultMS = -4.0; // Timestamp rollover or error
                    }
                }
                // else: hrStart or hrEnd returned S_FALSE or an error, keep resultMs = -2.0
            }

            // Store or report the result
            const TStatId& statId = FrameQueries[QueryIndex].StatId;
            if (statId.GetName() != NAME_None) // Check if StatId is valid
            {
                LatestResults[statId.GetName()] = { ResultMS, FrameCounter };
            }

            // Reset the query slot for reuse
            FrameQueries[QueryIndex].bQueryIssued = false;
            FrameQueries[QueryIndex].StatId = TStatId();
        }
    }
    // else if hrDisjoint == S_FALSE: Data not ready yet, do nothing this frame.
     else if (hr == S_FALSE)
    {
        // 데이터가 아직 준비되지 않아도, 쿼리를 재사용하기 전에
        // 최소 한 번은 GetData로 결과를 버려 경고를 막는다.
        Context->GetData(
            FrameQueries[BaseResultIndex].DisjointQuery.Get(),
            nullptr, 0, 0
        );

        for (uint32 i = 0; i < MAX_QUERIES_PER_FRAME; ++i)
        {
            const uint32 QueryIndex = BaseResultIndex + i;
            if (!FrameQueries[QueryIndex].bQueryIssued)
                continue;

            Context->GetData(
                FrameQueries[QueryIndex].StartQuery.Get(),
                nullptr, 0, 0
            );
            Context->GetData(
                FrameQueries[QueryIndex].EndQuery.Get(),
                nullptr, 0, 0
            );

            FrameQueries[QueryIndex].bQueryIssued = false;
            FrameQueries[QueryIndex].StatId = TStatId();
        }
    }
    // else: Error occurred during GetData for disjoint query.
}

double FGPUTimingManager::GetElapsedTimeMs(const TStatId& StatId) const
{
    if (!bInitialized) return -3.0; // Not initialized

    auto it = LatestResults.Find(StatId.GetName());
    if (it != nullptr)
    {
        // Optional: Check freshness based on m_frameCounter vs it->second.LastUpdateFrame
        return it->ElapsedTimeMs;
    }
    return -3.0; // No data recorded for this StatId yet
}
