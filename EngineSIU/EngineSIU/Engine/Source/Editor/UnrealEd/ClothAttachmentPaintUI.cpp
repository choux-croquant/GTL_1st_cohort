/**
 * Cloth Attachment Paint UI Implementation
 * ImGui-based UI panel for cloth attachment editing
 * Simple, immediately usable interface for artists
 */

#include "ClothAttachmentPaintUI.h"
#include "Components/ClothMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Classes/Engine/ClothAsset.h"
#include "ThirdParty/ImGui/include/ImGui/imgui.h"
#include <cstring>

FClothAttachmentPaintUI::FClothAttachmentPaintUI()
    : ClothComponent(nullptr)
    , SkeletalMeshComponent(nullptr)
    , bIsOpen(true)
    , ZThreshold(0.9f)
    , SelectedVertexCount(0)
    , SelectedBoneIndex(0)
    , KinematicWeight(1.0f)
    , AttachmentStiffness(1.0f)
    , AttachmentDistance(0.0f)
{
    std::memset(BoneNameBuffer, 0, sizeof(BoneNameBuffer));
    std::memset(StatusMessage, 0, sizeof(StatusMessage));
    std::strcpy(StatusMessage, "Ready");
}

FClothAttachmentPaintUI::~FClothAttachmentPaintUI()
{
}

void FClothAttachmentPaintUI::SetClothComponent(UClothMeshComponent* InClothComponent)
{
    ClothComponent = InClothComponent;
    if (ClothComponent)
    {
        std::strcpy(StatusMessage, "Cloth component set");
    }
}

void FClothAttachmentPaintUI::SetSkeletalMeshComponent(USkeletalMeshComponent* InSkelMeshComponent)
{
    SkeletalMeshComponent = InSkelMeshComponent;
    if (SkeletalMeshComponent)
    {
        PopulateBoneList();
        std::strcpy(StatusMessage, "Skeletal mesh component set - bones loaded");
    }
}

void FClothAttachmentPaintUI::RenderUI()
{
    if (!bIsOpen)
        return;

    ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Cloth Attachment Paint Editor", &bIsOpen))
    {
        RenderHeader();
        ImGui::Separator();
        
        RenderSelectionSection();
        ImGui::Separator();
        
        RenderBoneAssignmentSection();
        ImGui::Separator();
        
        RenderAttachmentListSection();
        ImGui::Separator();
        
        RenderActionsSection();
    }
    ImGui::End();
}

void FClothAttachmentPaintUI::RenderHeader()
{
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Cloth Attachment Paint Editor");
    ImGui::Text("Edit cloth-to-bone attachments");
    ImGui::Spacing();

    // Component status
    if (ClothComponent)
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Cloth Component: %s", ClothComponent->GetName().c_str());
        
        if (ClothComponent->GetClothAsset())
        {
            int32 VertexCount = ClothComponent->GetClothAsset()->RestPositions.Num();
            int32 AttachmentCount = ClothComponent->GetClothAsset()->AttachmentPaintData.Num();
            ImGui::Text("Vertices: %d | Attachments: %d", VertexCount, AttachmentCount);
        }
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No cloth component selected");
    }

    if (SkeletalMeshComponent)
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Skeletal Mesh: %s", SkeletalMeshComponent->GetName().c_str());
        ImGui::Text("Available Bones: %d", AvailableBones.Num());
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No skeletal mesh selected");
    }

    // Status message
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.0f, 1.0f), "Status: %s", StatusMessage);
}

void FClothAttachmentPaintUI::RenderSelectionSection()
{
    if (ImGui::CollapsingHeader("1. Vertex Selection", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        // Z-Threshold selection
        ImGui::Text("Auto-Select by Z-Threshold:");
        ImGui::SliderFloat("Z Threshold", &ZThreshold, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        if (ImGui::Button("?##ZThresholdHelp"))
        {
            ImGui::SetTooltip("0.9 = top 10%%, 0.8 = top 20%%, etc.");
        }

        if (ImGui::Button("Auto-Select Top Vertices", ImVec2(-1, 0)))
        {
            if (ClothComponent)
            {
                ClothComponent->AttachmentZThreshold = ZThreshold;
                ClothComponent->AutoSelectTopVertices();
                SelectedVertexCount = ClothComponent->SelectedVertexIndices.Num();
                std::sprintf(StatusMessage, "Selected %d vertices", SelectedVertexCount);
            }
            else
            {
                std::strcpy(StatusMessage, "ERROR: No cloth component");
            }
        }

        ImGui::Spacing();
        ImGui::Text("Selected Vertices: %d", SelectedVertexCount);

        if (ImGui::Button("Clear Selection", ImVec2(-1, 0)))
        {
            if (ClothComponent)
            {
                ClothComponent->SelectedVertexIndices.Empty();
                SelectedVertexCount = 0;
                std::strcpy(StatusMessage, "Selection cleared");
            }
        }

        ImGui::Unindent();
    }
}

void FClothAttachmentPaintUI::RenderBoneAssignmentSection()
{
    if (ImGui::CollapsingHeader("2. Bone Assignment", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        // Bone selection
        ImGui::Text("Target Bone:");
        
        if (AvailableBones.Num() > 0)
        {
            // Dropdown with bone names
            if (ImGui::BeginCombo("##BoneCombo", AvailableBones[SelectedBoneIndex].ToString().c_str()))
            {
                for (int32 i = 0; i < AvailableBones.Num(); ++i)
                {
                    bool bIsSelected = (SelectedBoneIndex == i);
                    if (ImGui::Selectable(AvailableBones[i].ToString().c_str(), bIsSelected))
                    {
                        SelectedBoneIndex = i;
                        std::strcpy(BoneNameBuffer, AvailableBones[i].ToString().c_str());
                    }
                    if (bIsSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No bones available - set skeletal mesh first");
        }

        ImGui::Spacing();

        // Attachment parameters
        ImGui::Text("Attachment Parameters:");
        ImGui::SliderFloat("Kinematic Weight", &KinematicWeight, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        if (ImGui::Button("?##WeightHelp"))
        {
            ImGui::SetTooltip("0.0 = free, 1.0 = fully pinned");
        }

        ImGui::SliderFloat("Stiffness", &AttachmentStiffness, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        if (ImGui::Button("?##StiffnessHelp"))
        {
            ImGui::SetTooltip("0.0 = soft, 1.0 = hard constraint");
        }

        ImGui::InputFloat("Attach Distance", &AttachmentDistance, 0.0f, 100.0f, "%.1f");
        ImGui::SameLine();
        if (ImGui::Button("?##DistanceHelp"))
        {
            ImGui::SetTooltip("0.0 = kinematic (no stretch), >0 = LRA with max stretch distance");
        }

        ImGui::Spacing();

        // Assign button
        if (ImGui::Button("Assign Bone to Selection", ImVec2(-1, 0)))
        {
            if (!ClothComponent)
            {
                std::strcpy(StatusMessage, "ERROR: No cloth component");
            }
            else if (SelectedVertexCount == 0)
            {
                std::strcpy(StatusMessage, "ERROR: No vertices selected");
            }
            else if (AvailableBones.Num() == 0)
            {
                std::strcpy(StatusMessage, "ERROR: No bones available");
            }
            else
            {
                // Set parameters
                ClothComponent->TargetBoneName = AvailableBones[SelectedBoneIndex];
                ClothComponent->KinematicWeight = KinematicWeight;
                ClothComponent->AttachmentStiffness = AttachmentStiffness;
                ClothComponent->AttachmentDistance = AttachmentDistance;

                // Assign bone
                ClothComponent->AssignBoneToSelection();

                std::sprintf(StatusMessage, "Assigned bone '%s' to %d vertices",
                    AvailableBones[SelectedBoneIndex].ToString().c_str(), SelectedVertexCount);
            }
        }

        ImGui::Unindent();
    }
}

void FClothAttachmentPaintUI::RenderAttachmentListSection()
{
    if (ImGui::CollapsingHeader("3. Current Attachments"))
    {
        ImGui::Indent();

        if (!ClothComponent || !ClothComponent->GetClothAsset())
        {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No cloth asset");
            ImGui::Unindent();
            return;
        }

        UClothAsset* ClothAsset = ClothComponent->GetClothAsset();
        const TArray<FClothAttachmentPaintData>& PaintData = ClothAsset->AttachmentPaintData;

        if (PaintData.Num() == 0)
        {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No attachments defined");
        }
        else
        {
            ImGui::Text("Total Attachments: %d", PaintData.Num());
            ImGui::Spacing();

            // Table header
            if (ImGui::BeginTable("AttachmentTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Vertex", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Bone", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Stiffness", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableHeadersRow();

                // Show first 20 attachments (for performance)
                int32 DisplayCount = FMath::Min(PaintData.Num(), 20);
                for (int32 i = 0; i < DisplayCount; ++i)
                {
                    const FClothAttachmentPaintData& Data = PaintData[i];

                    ImGui::TableNextRow();
                    
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", Data.SimVertexIndex);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", Data.BoneName.ToString().c_str());

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.2f", Data.KinematicWeight);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.2f", Data.Stiffness);

                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextColored(
                        Data.bIsActive ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                        Data.bIsActive ? "Yes" : "No"
                    );
                }

                if (PaintData.Num() > 20)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "... and %d more", PaintData.Num() - 20);
                }

                ImGui::EndTable();
            }
        }

        ImGui::Unindent();
    }
}

void FClothAttachmentPaintUI::RenderActionsSection()
{
    if (ImGui::CollapsingHeader("4. Actions", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        // Apply button
        if (ImGui::Button("Apply to Asset", ImVec2(-1, 0)))
        {
            if (ClothComponent)
            {
                ClothComponent->ApplyAttachmentPaintDataToAsset();
                std::strcpy(StatusMessage, "Applied to asset - data will persist");
            }
            else
            {
                std::strcpy(StatusMessage, "ERROR: No cloth component");
            }
        }

        // Clear all button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Clear All Attachments", ImVec2(-1, 0)))
        {
            if (ClothComponent)
            {
                ClothComponent->ClearAttachmentPaintData();
                SelectedVertexCount = 0;
                std::strcpy(StatusMessage, "All attachments cleared");
            }
            else
            {
                std::strcpy(StatusMessage, "ERROR: No cloth component");
            }
        }
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Help text
        ImGui::TextWrapped("Workflow: 1) Select vertices, 2) Choose bone, 3) Assign bone, 4) Apply to asset, 5) Test in PIE");

        ImGui::Unindent();
    }
}

// ===== Helper Functions =====

void FClothAttachmentPaintUI::PopulateBoneList()
{
    AvailableBones.Empty();

    if (!SkeletalMeshComponent)
        return;

    // Get bone names from skeletal mesh
    // Note: This is a simplified version - actual implementation depends on SkeletalMeshComponent API
    // For now, add common Mixamo bones as examples
    AvailableBones.Add(FName(TEXT("mixamorig:Hips")));
    AvailableBones.Add(FName(TEXT("mixamorig:Spine")));
    AvailableBones.Add(FName(TEXT("mixamorig:Spine1")));
    AvailableBones.Add(FName(TEXT("mixamorig:Spine2")));
    AvailableBones.Add(FName(TEXT("mixamorig:Neck")));
    AvailableBones.Add(FName(TEXT("mixamorig:Head")));
    AvailableBones.Add(FName(TEXT("mixamorig:LeftShoulder")));
    AvailableBones.Add(FName(TEXT("mixamorig:RightShoulder")));
    AvailableBones.Add(FName(TEXT("mixamorig:LeftArm")));
    AvailableBones.Add(FName(TEXT("mixamorig:RightArm")));

    // TODO: Replace with actual bone enumeration from SkeletalMeshComponent
    // Example: SkeletalMeshComponent->GetBoneNames(AvailableBones);

    if (AvailableBones.Num() > 0)
    {
        std::strcpy(BoneNameBuffer, AvailableBones[0].ToString().c_str());
    }
}

TArray<FName> FClothAttachmentPaintUI::GetAvailableBones() const
{
    return AvailableBones;
}
