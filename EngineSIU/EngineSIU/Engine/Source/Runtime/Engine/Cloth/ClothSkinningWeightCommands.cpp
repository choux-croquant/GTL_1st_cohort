/**
 * Cloth Skinning Weight Generator - Console Commands
 * Debug and configuration commands for cloth skinning weight generation
 */

#include "ClothSkinningWeightGenerator.h"
#include "Engine/UserInterface/Console.h"
#include "Engine/ClothAsset.h"

// Global default skinning parameters
static FClothSkinningParams GDefaultClothSkinningParams;

// Visualization flags
static bool GVisualizeSkinningWeights = false;
static bool GVisualizeBVHStructure = false;
static bool GShowSkinningStats = false;

/**
 * Console command: Set skinning weight generation method
 * Usage: cloth.skinning.method [barycentric|inverse]
 */
void ConsoleCommand_SetSkinningMethod(const TArray<FString>& Args)
{
    if (Args.Num() < 1)
    {
        UE_LOG(ELogLevel::Display, TEXT("Usage: cloth.skinning.method [barycentric|inverse]"));
        UE_LOG(ELogLevel::Display, TEXT("Current method: %s"), 
               GDefaultClothSkinningParams.WeightingMethod == EClothWeightingMethod::Barycentric ? TEXT("barycentric") : TEXT("inverse"));
        return;
    }
    
    if (Args[0] == "barycentric" || Args[0] == "bary")
    {
        GDefaultClothSkinningParams.WeightingMethod = EClothWeightingMethod::Barycentric;
        UE_LOG(ELogLevel::Display, TEXT("Cloth skinning method set to: Barycentric"));
    }
    else if (Args[0] == "inverse" || Args[0] == "inv")
    {
        GDefaultClothSkinningParams.WeightingMethod = EClothWeightingMethod::InverseDistance;
        UE_LOG(ELogLevel::Warning, TEXT("Cloth skinning method set to: Inverse Distance (DEPRECATED)"));
    }
    else
    {
        UE_LOG(ELogLevel::Error, TEXT("Unknown method: %s. Use 'barycentric' or 'inverse'"), *Args[0]);
    }
}

/**
 * Console command: Enable/disable SAH for BVH construction
 * Usage: cloth.skinning.sah [0|1]
 */
void ConsoleCommand_SetSAH(const TArray<FString>& Args)
{
    if (Args.Num() < 1)
    {
        UE_LOG(ELogLevel::Display, TEXT("Usage: cloth.skinning.sah [0|1]"));
        UE_LOG(ELogLevel::Display, TEXT("Current SAH: %s"), 
               GDefaultClothSkinningParams.bUseSAH ? TEXT("enabled") : TEXT("disabled"));
        return;
    }
    
    int32 value = FCString::Atoi(*Args[0]);
    GDefaultClothSkinningParams.bUseSAH = (value != 0);
    
    UE_LOG(ELogLevel::Display, TEXT("SAH for BVH construction: %s"), 
           GDefaultClothSkinningParams.bUseSAH ? TEXT("enabled") : TEXT("disabled"));
}

/**
 * Console command: Set BVH max leaf triangles
 * Usage: cloth.skinning.bvh.leafsize [value]
 */
void ConsoleCommand_SetBVHLeafSize(const TArray<FString>& Args)
{
    if (Args.Num() < 1)
    {
        UE_LOG(ELogLevel::Display, TEXT("Usage: cloth.skinning.bvh.leafsize [value]"));
        UE_LOG(ELogLevel::Display, TEXT("Current leaf size: %d"), 
               GDefaultClothSkinningParams.BVHMaxLeafTriangles);
        return;
    }
    
    int32 value = FCString::Atoi(*Args[0]);
    if (value < 1 || value > 64)
    {
        UE_LOG(ELogLevel::Error, TEXT("Leaf size must be between 1 and 64"));
        return;
    }
    
    GDefaultClothSkinningParams.BVHMaxLeafTriangles = value;
    UE_LOG(ELogLevel::Display, TEXT("BVH max leaf triangles set to: %d"), value);
}

/**
 * Console command: Set max search distance
 * Usage: cloth.skinning.maxdistance [value]
 */
void ConsoleCommand_SetMaxSearchDistance(const TArray<FString>& Args)
{
    if (Args.Num() < 1)
    {
        UE_LOG(ELogLevel::Display, TEXT("Usage: cloth.skinning.maxdistance [value]"));
        UE_LOG(ELogLevel::Display, TEXT("Current max search distance: %.2f"), 
               GDefaultClothSkinningParams.MaxSearchDistance);
        return;
    }
    
    float value = FCString::Atof(*Args[0]);
    if (value <= 0.0f)
    {
        UE_LOG(ELogLevel::Error, TEXT("Max search distance must be positive"));
        return;
    }
    
    GDefaultClothSkinningParams.MaxSearchDistance = value;
    UE_LOG(ELogLevel::Display, TEXT("Max search distance set to: %.2f"), value);
}

/**
 * Console command: Enable/disable triangle caching
 * Usage: cloth.skinning.cache [0|1]
 */
void ConsoleCommand_SetTriangleCache(const TArray<FString>& Args)
{
    if (Args.Num() < 1)
    {
        UE_LOG(ELogLevel::Display, TEXT("Usage: cloth.skinning.cache [0|1]"));
        UE_LOG(ELogLevel::Display, TEXT("Current triangle caching: %s"), 
               GDefaultClothSkinningParams.bCacheTriangleData ? TEXT("enabled") : TEXT("disabled"));
        return;
    }
    
    int32 value = FCString::Atoi(*Args[0]);
    GDefaultClothSkinningParams.bCacheTriangleData = (value != 0);
    
    UE_LOG(ELogLevel::Display, TEXT("Triangle caching: %s"), 
           GDefaultClothSkinningParams.bCacheTriangleData ? TEXT("enabled") : TEXT("disabled"));
}

/**
 * Console command: Enable/disable multi-threading
 * Usage: cloth.skinning.multithread [0|1]
 */
void ConsoleCommand_SetMultiThreading(const TArray<FString>& Args)
{
    if (Args.Num() < 1)
    {
        UE_LOG(ELogLevel::Display, TEXT("Usage: cloth.skinning.multithread [0|1]"));
        UE_LOG(ELogLevel::Display, TEXT("Current multi-threading: %s"), 
               GDefaultClothSkinningParams.bEnableMultiThreading ? TEXT("enabled") : TEXT("disabled"));
        return;
    }
    
    int32 value = FCString::Atoi(*Args[0]);
    GDefaultClothSkinningParams.bEnableMultiThreading = (value != 0);
    
    UE_LOG(ELogLevel::Display, TEXT("Multi-threading: %s"), 
           GDefaultClothSkinningParams.bEnableMultiThreading ? TEXT("enabled") : TEXT("disabled"));
}

/**
 * Console command: Toggle skinning weight visualization
 * Usage: cloth.skinning.visualize
 */
void ConsoleCommand_VisualizeSkinningWeights(const TArray<FString>& Args)
{
    GVisualizeSkinningWeights = !GVisualizeSkinningWeights;
    UE_LOG(ELogLevel::Display, TEXT("Skinning weight visualization: %s"), 
           GVisualizeSkinningWeights ? TEXT("enabled") : TEXT("disabled"));
}

/**
 * Console command: Toggle BVH structure visualization
 * Usage: cloth.skinning.visualize.bvh
 */
void ConsoleCommand_VisualizeBVH(const TArray<FString>& Args)
{
    GVisualizeBVHStructure = !GVisualizeBVHStructure;
    UE_LOG(ELogLevel::Display, TEXT("BVH structure visualization: %s"), 
           GVisualizeBVHStructure ? TEXT("enabled") : TEXT("disabled"));
}

/**
 * Console command: Show skinning statistics
 * Usage: cloth.skinning.stats
 */
void ConsoleCommand_ShowSkinningStats(const TArray<FString>& Args)
{
    GShowSkinningStats = !GShowSkinningStats;
    UE_LOG(ELogLevel::Display, TEXT("Skinning statistics display: %s"), 
           GShowSkinningStats ? TEXT("enabled") : TEXT("disabled"));
}

/**
 * Console command: Print current skinning parameters
 * Usage: cloth.skinning.params
 */
void ConsoleCommand_PrintSkinningParams(const TArray<FString>& Args)
{
    UE_LOG(ELogLevel::Display, TEXT("=== Cloth Skinning Parameters ==="));
    UE_LOG(ELogLevel::Display, TEXT("Method: %s"), 
           GDefaultClothSkinningParams.WeightingMethod == EClothWeightingMethod::Barycentric ? TEXT("Barycentric") : TEXT("Inverse Distance"));
    UE_LOG(ELogLevel::Display, TEXT("Max Influences: %d"), GDefaultClothSkinningParams.MaxInfluences);
    UE_LOG(ELogLevel::Display, TEXT("Normalize Weights: %s"), GDefaultClothSkinningParams.bNormalizeWeights ? TEXT("true") : TEXT("false"));
    
    if (GDefaultClothSkinningParams.WeightingMethod == EClothWeightingMethod::Barycentric)
    {
        UE_LOG(ELogLevel::Display, TEXT("--- Barycentric Parameters ---"));
        UE_LOG(ELogLevel::Display, TEXT("Max Search Distance: %.2f"), GDefaultClothSkinningParams.MaxSearchDistance);
        UE_LOG(ELogLevel::Display, TEXT("Use Closest Point Projection: %s"), GDefaultClothSkinningParams.bUseClosestPointProjection ? TEXT("true") : TEXT("false"));
        UE_LOG(ELogLevel::Display, TEXT("Fallback to Inverse Distance: %s"), GDefaultClothSkinningParams.bFallbackToInverseDistance ? TEXT("true") : TEXT("false"));
        UE_LOG(ELogLevel::Display, TEXT("BVH Max Leaf Triangles: %d"), GDefaultClothSkinningParams.BVHMaxLeafTriangles);
        UE_LOG(ELogLevel::Display, TEXT("Use SAH: %s"), GDefaultClothSkinningParams.bUseSAH ? TEXT("true") : TEXT("false"));
        UE_LOG(ELogLevel::Display, TEXT("Triangle Caching: %s"), GDefaultClothSkinningParams.bCacheTriangleData ? TEXT("true") : TEXT("false"));
        UE_LOG(ELogLevel::Display, TEXT("Multi-threading: %s"), GDefaultClothSkinningParams.bEnableMultiThreading ? TEXT("true") : TEXT("false"));
    }
    else
    {
        UE_LOG(ELogLevel::Display, TEXT("--- Inverse Distance Parameters ---"));
        UE_LOG(ELogLevel::Display, TEXT("Max Distance: %.2f"), GDefaultClothSkinningParams.MaxDistance);
        UE_LOG(ELogLevel::Display, TEXT("Use Inverse Distance Weighting: %s"), GDefaultClothSkinningParams.bUseInverseDistanceWeighting ? TEXT("true") : TEXT("false"));
        UE_LOG(ELogLevel::Display, TEXT("Weight Power: %.2f"), GDefaultClothSkinningParams.WeightPower);
    }
}

/**
 * Console command: Set quality preset
 * Usage: cloth.skinning.preset [fast|balanced|quality]
 */
void ConsoleCommand_SetQualityPreset(const TArray<FString>& Args)
{
    if (Args.Num() < 1)
    {
        UE_LOG(ELogLevel::Display, TEXT("Usage: cloth.skinning.preset [fast|balanced|quality]"));
        return;
    }
    
    if (Args[0] == "fast")
    {
        GDefaultClothSkinningParams.WeightingMethod = EClothWeightingMethod::Barycentric;
        GDefaultClothSkinningParams.BVHMaxLeafTriangles = 16;
        GDefaultClothSkinningParams.bUseSAH = false;
        GDefaultClothSkinningParams.bCacheTriangleData = false;
        GDefaultClothSkinningParams.bEnableMultiThreading = true;
        UE_LOG(ELogLevel::Display, TEXT("Preset 'fast' applied: Quick BVH build, multi-threading enabled"));
    }
    else if (Args[0] == "balanced")
    {
        GDefaultClothSkinningParams.WeightingMethod = EClothWeightingMethod::Barycentric;
        GDefaultClothSkinningParams.BVHMaxLeafTriangles = 8;
        GDefaultClothSkinningParams.bUseSAH = false;
        GDefaultClothSkinningParams.bCacheTriangleData = true;
        GDefaultClothSkinningParams.bEnableMultiThreading = false;
        UE_LOG(ELogLevel::Display, TEXT("Preset 'balanced' applied: Standard BVH, triangle caching"));
    }
    else if (Args[0] == "quality")
    {
        GDefaultClothSkinningParams.WeightingMethod = EClothWeightingMethod::Barycentric;
        GDefaultClothSkinningParams.BVHMaxLeafTriangles = 4;
        GDefaultClothSkinningParams.bUseSAH = true;
        GDefaultClothSkinningParams.bCacheTriangleData = true;
        GDefaultClothSkinningParams.bEnableMultiThreading = false;
        UE_LOG(ELogLevel::Display, TEXT("Preset 'quality' applied: SAH-optimized BVH, full caching"));
    }
    else
    {
        UE_LOG(ELogLevel::Error, TEXT("Unknown preset: %s. Use 'fast', 'balanced', or 'quality'"), *Args[0]);
    }
}

/**
 * Register all cloth skinning console commands
 */
void RegisterClothSkinningCommands()
{
    // Method selection
    Console.RegisterCommand("cloth.skinning.method", ConsoleCommand_SetSkinningMethod);
    
    // Optimization settings
    Console.RegisterCommand("cloth.skinning.sah", ConsoleCommand_SetSAH);
    Console.RegisterCommand("cloth.skinning.bvh.leafsize", ConsoleCommand_SetBVHLeafSize);
    Console.RegisterCommand("cloth.skinning.maxdistance", ConsoleCommand_SetMaxSearchDistance);
    Console.RegisterCommand("cloth.skinning.cache", ConsoleCommand_SetTriangleCache);
    Console.RegisterCommand("cloth.skinning.multithread", ConsoleCommand_SetMultiThreading);
    
    // Visualization
    Console.RegisterCommand("cloth.skinning.visualize", ConsoleCommand_VisualizeSkinningWeights);
    Console.RegisterCommand("cloth.skinning.visualize.bvh", ConsoleCommand_VisualizeBVH);
    Console.RegisterCommand("cloth.skinning.stats", ConsoleCommand_ShowSkinningStats);
    
    // Utility
    Console.RegisterCommand("cloth.skinning.params", ConsoleCommand_PrintSkinningParams);
    Console.RegisterCommand("cloth.skinning.preset", ConsoleCommand_SetQualityPreset);
    
    UE_LOG(ELogLevel::Display, TEXT("Cloth skinning console commands registered"));
}

/**
 * Get global default skinning parameters
 */
FClothSkinningParams& GetDefaultClothSkinningParams()
{
    return GDefaultClothSkinningParams;
}

/**
 * Check if skinning weight visualization is enabled
 */
bool IsVisualizingSkinningWeights()
{
    return GVisualizeSkinningWeights;
}

/**
 * Check if BVH visualization is enabled
 */
bool IsVisualizingBVH()
{
    return GVisualizeBVHStructure;
}

/**
 * Check if skinning stats should be shown
 */
bool ShouldShowSkinningStats()
{
    return GShowSkinningStats;
}
