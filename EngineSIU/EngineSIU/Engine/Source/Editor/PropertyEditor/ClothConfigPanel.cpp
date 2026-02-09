#include "ClothConfigPanel.h"
#include "Engine/Engine.h"
#include "Cloth/ClothPhysicsManager.h"
#include "Cloth/ClothWorld.h"
#include "Cloth/ClothSimulationData.h"
#include "Cloth/ClothBatchManager.h"
#include "Cloth/ClothBatchedSolver.h"
#include "UnrealEd/ImGuiWidget.h"
#include "imgui/imgui.h"

ClothConfigPanel::ClothConfigPanel()
{
    // Support PIE mode only for runtime tuning
    SetSupportedWorldTypes(EWorldTypeBitFlag::PIE);
}

void ClothConfigPanel::Render()
{
    if (!bShowPanel)
    {
        return;
    }

    // Get the active ClothWorld
    if (!GEngine || !GEngine->ClothPhysicsManager || !GEngine->ActiveWorld)
    {
        return;
    }

    FClothWorld *ClothWorld = GEngine->ClothPhysicsManager->GetClothWorld(GEngine->ActiveWorld);
    if (!ClothWorld || !ClothWorld->IsInitialized())
    {
        return;
    }

    // Calculate panel position and size (similar to PropertyEditorPanel)
    float PanelWidth = (Width) * 0.2f - 5.0f;
    float PanelHeight = 400.0f; // Fixed height for cloth config
    float PanelPosX = 5.0f;
    float PanelPosY = 35.0f;

    ImGui::SetNextWindowPos(ImVec2(PanelPosX, PanelPosY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(PanelWidth, PanelHeight), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_None;

    if (ImGui::Begin("Cloth Configuration", &bShowPanel, WindowFlags))
    {
        ImGui::Text("Cloth Simulation Solver Config");
        ImGui::Separator();

        RenderClothConfig();
    }
    ImGui::End();
}

void ClothConfigPanel::RenderClothConfig()
{
    if (!GEngine || !GEngine->ClothPhysicsManager || !GEngine->ActiveWorld)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "ClothPhysicsManager not available");
        return;
    }

    FClothWorld *ClothWorld = GEngine->ClothPhysicsManager->GetClothWorld(GEngine->ActiveWorld);
    if (!ClothWorld || !ClothWorld->IsInitialized())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No active ClothWorld");
        return;
    }

    // Get batch manager for LOD_0 (most detailed LOD)
    FClothBatchManager *BatchManager = ClothWorld->GetBatchManager(EClothLODLevel::LOD_0);
    if (!BatchManager)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No active cloth instances");
        return;
    }

    // Get the solver from the batch manager
    FClothBatchedSolver *Solver = BatchManager->GetSolver();
    if (!Solver || !Solver->IsInitialized())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Cloth solver not initialized");
        return;
    }

    // Get a copy of the current config
    // We'll modify this and apply it back if any changes are made
    FClothConfig Config = Solver->GetConfig();
    bool bConfigChanged = false;

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));

    // Global Simulation Settings
    if (ImGui::TreeNodeEx("Global Settings", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
    {
        FVector Gravity = Config.Gravity;
        bConfigChanged |= FImGuiWidget::DrawVec3Control("Gravity", Gravity, 0, 100);
        Config.Gravity = Gravity;

        //bConfigChanged |= ImGui::SliderFloat("Mass", &Config.Mass, 0.1f, 10.0f, "%.2f");
        bConfigChanged |= ImGui::SliderFloat("Damping", &Config.Damping, 0.0f, 10.0f, "%.3f");

        ImGui::TreePop();
    }

    // Constraint Stiffness
    if (ImGui::TreeNodeEx("Constraint Stiffness", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
    {
        bConfigChanged |= ImGui::SliderFloat("Stretch Stiffness", &Config.StretchStiffness, 0.001f, 1.0f, "%.3f");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Controls fabric stretching resistance");
        }

        bConfigChanged |= ImGui::SliderFloat("Bend Stiffness", &Config.BendStiffness, 0.0f, 10.0f, "%.3f");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Controls fabric bending/folding resistance");
        }

       /* bConfigChanged |= ImGui::SliderFloat("Attach Stiffness", &Config.AttachStiffness, 0.0f, 1.0f, "%.3f");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Controls attachment point stiffness");
        }

        bConfigChanged |= ImGui::SliderFloat("Long Range Stretchiness", &Config.LongRangeStretchiness, 1.0f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Long-range attachment slack multiplier");
        }*/

        ImGui::TreePop();
    }

    // Solver Settings
    if (ImGui::TreeNodeEx("Solver Settings", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
    {
        bConfigChanged |= ImGui::SliderInt("Iterations", &Config.NumIterations, 1, 10);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Constraint solver iterations per substep");
        }

        bConfigChanged |= ImGui::SliderInt("Substeps", &Config.NumSubsteps, 1, 10);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Simulation substeps per frame");
        }

        /*bConfigChanged |= ImGui::SliderInt("Max Substeps/Frame", &Config.MaxSubstepsPerFrame, 1, 20);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Safety limit to prevent performance issues");
        }*/

        bConfigChanged |= ImGui::SliderFloat("Max Speed", &Config.MaxSpeed, 100.0f, 5000.0f, "%.0f cm/s");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Velocity clamping for stability");
        }

        bConfigChanged |= ImGui::SliderFloat("Relaxation Factor", &Config.RelaxationFactor, 0.5f, 1.5f, "%.2f");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Jacobi solver convergence control");
        }

        /*bool bUseXPBD = Config.bUseXPBD;
        if (ImGui::Checkbox("Use XPBD", &bUseXPBD))
        {
            Config.bUseXPBD = bUseXPBD;
            bConfigChanged = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Extended Position-Based Dynamics (more stable)");
        }*/

        ImGui::TreePop();
    }

    // Wind and Forces
    /*if (ImGui::TreeNodeEx("Wind & Forces", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
    {
        bConfigChanged |= ImGui::SliderFloat("Air Drag", &Config.AirDrag, 0.0f, 5.0f, "%.2f");
        bConfigChanged |= ImGui::SliderFloat("Wind Strength", &Config.WindStrength, 0.0f, 10.0f, "%.2f");

        ImGui::TreePop();
    }*/

    // Collision Settings
    if (ImGui::TreeNodeEx("Collision", ImGuiTreeNodeFlags_Framed))
    {
        bool bSelfCollision = Config.bEnableSelfCollision;
        if (ImGui::Checkbox("Enable Self Collision", &bSelfCollision))
        {
            Config.bEnableSelfCollision = bSelfCollision;
            bConfigChanged = true;
        }
        bConfigChanged |= ImGui::SliderFloat("Collision Thickness", &Config.CollisionThickness, 0.001f, 10.0f, "%.3f");
        bConfigChanged |= ImGui::SliderFloat("Friction", &Config.CollisionFriction, 0.0f, 10.0f, "%.3f");
        bConfigChanged |= ImGui::SliderFloat("Self Collision Radius", &Config.SelfCollisionRadius, 0.001f, 2.0f, "%.3f");
        bConfigChanged |= ImGui::SliderFloat("Self Collision Stiffness", &Config.SelfCollisionStiffness, 0.001f, 1.0f, "%.3f");

        ImGui::TreePop();
    }

    ImGui::PopStyleColor();

    // Apply changes if any were made
    if (bConfigChanged)
    {
        Solver->SetConfig(Config);
    }

    // Display statistics
    ImGui::Separator();
    if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_Framed))
    {
        int32 NumInstances = ClothWorld->GetNumInstancesInLOD(EClothLODLevel::LOD_0);
        ImGui::Text("Active Instances: %d", NumInstances);
        ImGui::Text("Total Particles: %u", Solver->GetUsedParticleCount());
        ImGui::Text("Total Constraints: %u", Solver->GetUsedConstraintCount() + Solver->GetUsedBendConstraintCount() + Solver->GetUsedAttachmentCount());

        EClothSystemMode SystemMode = ClothWorld->GetSystemMode();
        const char *ModeStr = (SystemMode == EClothSystemMode::Batched) ? "Batched" : "Legacy";
        ImGui::Text("System Mode: %s", ModeStr);

        ImGui::TreePop();
    }

    // Reset to defaults button
    ImGui::Separator();
    if (ImGui::Button("Reset to Defaults", ImVec2(-1, 0)))
    {
        FClothConfig DefaultConfig;
        Solver->SetConfig(DefaultConfig);
    }
}

void ClothConfigPanel::OnResize(HWND hWnd)
{
    RECT ClientRect;
    GetClientRect(hWnd, &ClientRect);
    Width = static_cast<float>(ClientRect.right - ClientRect.left);
    Height = static_cast<float>(ClientRect.bottom - ClientRect.top);
}
