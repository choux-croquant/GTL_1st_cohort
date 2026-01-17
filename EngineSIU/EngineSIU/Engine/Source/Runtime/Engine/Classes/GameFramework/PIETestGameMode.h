#pragma once
#include "GameMode.h"

/**
 * PIETestGameMode - A specialized GameMode for Play-In-Editor testing
 *
 * This GameMode is automatically used when entering PIE mode and provides:
 * - Free-fly camera controls for testing
 * - Extensible debug functionality
 * - Cloth simulation and other gameplay feature testing
 */
class APieTestGameMode : public AGameMode
{
    DECLARE_CLASS(APieTestGameMode, AGameMode)

public:
    APieTestGameMode();
    virtual ~APieTestGameMode() override = default;

    virtual void PostSpawnInitialize() override;
    virtual void InitGame() override;
    virtual void StartMatch() override;
    virtual void Tick(float DeltaTime) override;

    // Debug helpers that can be extended
    virtual void OnDebugAction1();
    virtual void OnDebugAction2();
    virtual void OnDebugAction3();

protected:
    bool bFreeFlyMode = true;
    float TimeInPIE = 0.0f;
};
