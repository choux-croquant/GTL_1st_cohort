#pragma once

#include "UnrealEd/EditorPanel.h"

/**
 * ClothConfigPanel - ImGui-based runtime cloth configuration editor
 * Allows editing FClothConfig values during PIE for rapid iteration
 */
class ClothConfigPanel : public UEditorPanel
{
public:
    ClothConfigPanel();

    virtual void Render() override;
    virtual void OnResize(HWND hWnd) override;

private:
    void RenderClothConfig();

    // UI state
    float Width = 0.0f;
    float Height = 0.0f;
    bool bShowPanel = true;
};
