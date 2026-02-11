# Cloth Attachment Editor - Architecture & Implementation Plan

## Overview

Design and implement a vertex-level cloth attachment editor that allows artists to:
- Select multiple vertices visually in the editor
- Attach them to Actors or Skeletal Mesh Bones
- Modify attachments at runtime during PIE
- Preserve attachment data when duplicating for PIE mode

## Current System Analysis

### Existing Attachment Infrastructure

#### Data Structures ([`ClothSimulationData.h`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSimulationData.h))

```cpp
enum class EClothAttachmentType : uint8
{
    WorldPosition,   // Static world position
    SkeletalBone,    // Follow bone transform
    ActorTransform   // Follow actor transform
};

struct FClothAttachmentData
{
    uint32 ClothVertexIndex;
    EClothAttachmentType Type;
    
    // Driver references (NEW - already implemented)
    USceneComponent* DriverComponent;
    AActor* DriverActor;
    
    // For skeletal mesh attachment
    FName BoneName;
    int32 BoneIndex;
    FTransform LocalOffset;
    
    // For world/actor attachment
    FVector WorldPosition;
    
    // Constraint properties
    float Stiffness = 1.0f;
    bool bIsKinematic = true;
    float AttachDistance = 0.0f;  // LRA support
};
```

#### Storage ([`ClothAsset.h`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Engine/ClothAsset.h))

```cpp
class UClothAsset : public UObject
{
    // Attachments stored in asset
    TArray<FClothAttachmentData> AttachmentsData;
    TArray<uint32> AttachmentIndices;  // Vertex indices
    
    // Serialization already implemented
    virtual void SerializeAsset(FArchive& Ar) override;
};
```

#### Runtime ([`ClothComponent.h`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.h))

```cpp
class UClothComponent : public USceneComponent
{
    // Cached attachment data
    TArray<FClothAttachmentData> Attachments;
    
    // Existing API
    void AttachToComponent(USceneComponent* Parent, FName SocketName);
    void AttachToSkeletalMesh(USkeletalMeshComponent* SkelMesh, const TArray<FName>& BoneNames);
};
```

### Gaps to Address

1. **No visual vertex selection** - Need editor UI for picking vertices
2. **No multi-select support** - Can only attach one vertex at a time programmatically
3. **No visual feedback** - Selected vertices not highlighted in viewport
4. **No editor panel** - No ImGui interface for attachment management
5. **PIE duplication unclear** - Need to ensure attachments copy correctly
6. **Runtime modification limited** - Need API to change targets/positions during PIE

---

## Architecture Design

### System Components

```
┌─────────────────────────────────────────────────────────────┐
│                    Cloth Attachment System                   │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────────┐         ┌──────────────────┐          │
│  │ ClothAsset       │◄────────│ Attachment       │          │
│  │ (Data Storage)   │         │ Authoring Data   │          │
│  └──────────────────┘         └──────────────────┘          │
│           │                            │                     │
│           │                            │                     │
│           ▼                            ▼                     │
│  ┌──────────────────┐         ┌──────────────────┐          │
│  │ ClothComponent   │◄────────│ Attachment       │          │
│  │ (Runtime)        │         │ Editor Panel     │          │
│  └──────────────────┘         │ (ImGui)          │          │
│           │                   └──────────────────┘          │
│           │                            │                     │
│           ▼                            ▼                     │
│  ┌──────────────────┐         ┌──────────────────┐          │
│  │ Attachment       │         │ Vertex Selection │          │
│  │ Solver/Update    │         │ Visualization    │          │
│  └──────────────────┘         │ (EditorRender)   │          │
│                                └──────────────────┘          │
└─────────────────────────────────────────────────────────────┘
```

---

## Feature 1: Vertex Selection System

### Data Structure

```cpp
// New file: ClothAttachmentAuthoringData.h
struct FClothVertexSelectionState
{
    TSet<uint32> SelectedVertices;           // Currently selected vertex indices
    TMap<uint32, FClothAttachmentData> PendingAttachments;  // Staged attachments
    
    bool bIsSelecting = false;               // Selection mode active
    bool bMultiSelectMode = false;           // Allow multiple selection
    
    void AddVertex(uint32 VertexIndex);
    void RemoveVertex(uint32 VertexIndex);
    void ClearSelection();
    bool IsVertexSelected(uint32 VertexIndex) const;
};

// Stored per ClothAsset during authoring
class FClothAttachmentAuthoringData
{
public:
    FClothVertexSelectionState SelectionState;
    
    // Attachment staging area (before committing to asset)
    TArray<FClothAttachmentData> StagedAttachments;
    
    // Undo/redo support
    TArray<FClothAttachmentData> UndoStack;
    
    void CommitAttachments(UClothAsset* Asset);
    void RevertAttachments();
};
```

### Vertex Picking

```cpp
// Extension to EditorViewportClient or ClothComponent
class FClothVertexPicker
{
public:
    // Ray-cast against cloth mesh to find nearest vertex
    bool PickVertex(const FRay& Ray, UClothAsset* Asset, uint32& OutVertexIndex, float& OutDistance);
    
    // Get vertices within screen-space rectangle (for box select)
    void PickVerticesInRect(const FBox2D& ScreenRect, UClothAsset* Asset, TArray<uint32>& OutVertices);
    
private:
    // Helper: Project vertex to screen space
    FVector2D ProjectVertexToScreen(const FVector& WorldPos, const FSceneView* View);
    
    // Helper: Ray-sphere intersection for vertex picking
    bool RaySphereIntersect(const FRay& Ray, const FVector& SphereCenter, float Radius, float& OutDistance);
};
```

---

## Feature 2: Visual Feedback System

### Vertex Highlighting Shader

```hlsl
// New file: Shaders/Cloth/ClothAttachmentVisualization.hlsl

cbuffer AttachmentVisualizationConstants : register(b11)
{
    uint SelectedVertexCount;
    uint AttachedVertexCount;
    float HighlightPulseTime;  // For animated highlight
    float Padding;
};

StructuredBuffer<uint> SelectedVertexIndices : register(t10);
StructuredBuffer<uint> AttachedVertexIndices : register(t11);

// Vertex shader: Expand selected vertices slightly
float4 HighlightVertexVS(uint VertexID : SV_VertexID) : SV_POSITION
{
    // Check if vertex is selected
    bool isSelected = false;
    for (uint i = 0; i < SelectedVertexCount; ++i)
    {
        if (SelectedVertexIndices[i] == VertexID)
        {
            isSelected = true;
            break;
        }
    }
    
    // Offset selected vertices outward along normal
    float3 offset = isSelected ? Normal * 0.5 : 0.0;
    float4 worldPos = mul(float4(Position + offset, 1.0), WorldMatrix);
    return mul(worldPos, ViewProjectionMatrix);
}

// Pixel shader: Color selected vertices
float4 HighlightVertexPS(float4 pos : SV_POSITION) : SV_Target
{
    // Animated pulse effect
    float pulse = 0.5 + 0.5 * sin(HighlightPulseTime * 3.14159);
    
    // Selected = Yellow, Attached = Green
    float3 color = lerp(float3(1, 1, 0), float3(1, 1, 1), pulse);
    return float4(color, 0.8);  // Semi-transparent
}
```

### Rendering Integration

```cpp
// Extension to EditorRenderPass.cpp
void FEditorRenderPass::RenderClothAttachmentVisualization()
{
    // Only render in Editor mode when cloth asset is being edited
    if (GEngine->ActiveWorld->WorldType != EWorldType::Editor)
        return;
    
    // Get active cloth asset being edited
    UClothAsset* EditingAsset = GetActiveClothAssetForEditing();
    if (!EditingAsset)
        return;
    
    FClothAttachmentAuthoringData* AuthoringData = GetAuthoringData(EditingAsset);
    if (!AuthoringData)
        return;
    
    // Render selected vertices as spheres
    RenderSelectedVertices(AuthoringData->SelectionState.SelectedVertices, FLinearColor::Yellow);
    
    // Render attached vertices as spheres
    TSet<uint32> AttachedVertices;
    for (const FClothAttachmentData& Attachment : EditingAsset->AttachmentsData)
    {
        AttachedVertices.Add(Attachment.ClothVertexIndex);
    }
    RenderAttachedVertices(AttachedVertices, FLinearColor::Green);
    
    // Render attachment lines (vertex -> target)
    RenderAttachmentLines(EditingAsset);
}

void FEditorRenderPass::RenderSelectedVertices(const TSet<uint32>& Vertices, const FLinearColor& Color)
{
    // Similar to RenderSphereInstanced, but for vertex positions
    TArray<FConstantBufferDebugSphere> Spheres;
    for (uint32 VertexIndex : Vertices)
    {
        FVector WorldPos = GetVertexWorldPosition(VertexIndex);
        Spheres.Add({WorldPos, 2.0f});  // 2cm radius spheres
    }
    
    // Batch render all spheres
    RenderSpheresInstanced(Spheres, Color);
}

void FEditorRenderPass::RenderAttachmentLines(UClothAsset* Asset)
{
    // Draw lines from attached vertices to their targets
    for (const FClothAttachmentData& Attachment : Asset->AttachmentsData)
    {
        FVector VertexPos = GetVertexWorldPosition(Attachment.ClothVertexIndex);
        FVector TargetPos = GetAttachmentTargetPosition(Attachment);
        
        DrawDebugLine(VertexPos, TargetPos, FLinearColor::Cyan, 2.0f);
    }
}
```

---

## Feature 3: Attachment Editor Panel (ImGui)

### Panel Design

```cpp
// New file: Engine/Source/Editor/PropertyEditor/ClothAttachmentEditorPanel.h

class ClothAttachmentEditorPanel : public UEditorPanel
{
public:
    ClothAttachmentEditorPanel();
    virtual void Render() override;
    
private:
    void RenderVertexSelectionSection();
    void RenderAttachmentListSection();
    void RenderAttachmentPropertiesSection();
    void RenderToolbarSection();
    
    // Selection tools
    void BeginVertexSelection();
    void EndVertexSelection();
    void ClearSelection();
    
    // Attachment operations
    void CreateAttachment(EClothAttachmentType Type);
    void DeleteSelectedAttachments();
    void ModifyAttachmentTarget(uint32 AttachmentIndex);
    
    // UI state
    UClothAsset* CurrentAsset = nullptr;
    FClothAttachmentAuthoringData* AuthoringData = nullptr;
    int32 SelectedAttachmentIndex = -1;
    
    // Attachment creation state
    EClothAttachmentType PendingAttachmentType = EClothAttachmentType::WorldPosition;
    AActor* SelectedTargetActor = nullptr;
    USceneComponent* SelectedTargetComponent = nullptr;
    FName SelectedBoneName;
};
```

### Panel Implementation

```cpp
// ClothAttachmentEditorPanel.cpp

void ClothAttachmentEditorPanel::Render()
{
    if (!ImGui::Begin("Cloth Attachment Editor", &bShowPanel))
    {
        ImGui::End();
        return;
    }
    
    // Asset selection
    if (ImGui::BeginCombo("Cloth Asset", CurrentAsset ? *CurrentAsset->GetName() : "None"))
    {
        for (UClothAsset* Asset : GetAllClothAssets())
        {
            if (ImGui::Selectable(*Asset->GetName(), Asset == CurrentAsset))
            {
                CurrentAsset = Asset;
                AuthoringData = GetOrCreateAuthoringData(Asset);
            }
        }
        ImGui::EndCombo();
    }
    
    if (!CurrentAsset)
    {
        ImGui::Text("Select a cloth asset to edit attachments");
        ImGui::End();
        return;
    }
    
    ImGui::Separator();
    
    // Toolbar
    RenderToolbarSection();
    
    ImGui::Separator();
    
    // Main content (split view)
    ImGui::BeginChild("LeftPane", ImVec2(300, 0), true);
    RenderVertexSelectionSection();
    RenderAttachmentListSection();
    ImGui::EndChild();
    
    ImGui::SameLine();
    
    ImGui::BeginChild("RightPane", ImVec2(0, 0), true);
    RenderAttachmentPropertiesSection();
    ImGui::EndChild();
    
    ImGui::End();
}

void ClothAttachmentEditorPanel::RenderToolbarSection()
{
    // Selection mode toggle
    if (ImGui::Button(AuthoringData->SelectionState.bIsSelecting ? "Stop Selecting" : "Select Vertices"))
    {
        if (AuthoringData->SelectionState.bIsSelecting)
            EndVertexSelection();
        else
            BeginVertexSelection();
    }
    
    ImGui::SameLine();
    ImGui::Checkbox("Multi-Select", &AuthoringData->SelectionState.bMultiSelectMode);
    
    ImGui::SameLine();
    if (ImGui::Button("Clear Selection"))
    {
        ClearSelection();
    }
    
    ImGui::Separator();
    
    // Attachment creation buttons
    ImGui::Text("Create Attachment:");
    ImGui::SameLine();
    
    if (ImGui::Button("World Position"))
        CreateAttachment(EClothAttachmentType::WorldPosition);
    
    ImGui::SameLine();
    if (ImGui::Button("Actor"))
        CreateAttachment(EClothAttachmentType::ActorTransform);
    
    ImGui::SameLine();
    if (ImGui::Button("Bone"))
        CreateAttachment(EClothAttachmentType::SkeletalBone);
}

void ClothAttachmentEditorPanel::RenderVertexSelectionSection()
{
    ImGui::Text("Selected Vertices: %d", AuthoringData->SelectionState.SelectedVertices.Num());
    
    if (ImGui::BeginListBox("##SelectedVertices", ImVec2(-1, 150)))
    {
        for (uint32 VertexIndex : AuthoringData->SelectionState.SelectedVertices)
        {
            FString Label = FString::Printf(TEXT("Vertex %d"), VertexIndex);
            if (ImGui::Selectable(*Label))
            {
                // Focus camera on vertex
                FocusCameraOnVertex(VertexIndex);
            }
        }
        ImGui::EndListBox();
    }
}

void ClothAttachmentEditorPanel::RenderAttachmentListSection()
{
    ImGui::Separator();
    ImGui::Text("Attachments: %d", CurrentAsset->AttachmentsData.Num());
    
    if (ImGui::BeginListBox("##Attachments", ImVec2(-1, 200)))
    {
        for (int32 i = 0; i < CurrentAsset->AttachmentsData.Num(); ++i)
        {
            const FClothAttachmentData& Attachment = CurrentAsset->AttachmentsData[i];
            
            FString Label = FString::Printf(TEXT("Vertex %d -> %s"), 
                Attachment.ClothVertexIndex,
                *GetAttachmentTargetName(Attachment));
            
            if (ImGui::Selectable(*Label, SelectedAttachmentIndex == i))
            {
                SelectedAttachmentIndex = i;
            }
            
            // Right-click context menu
            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Delete"))
                {
                    CurrentAsset->AttachmentsData.RemoveAt(i);
                    SelectedAttachmentIndex = -1;
                }
                if (ImGui::MenuItem("Focus Vertex"))
                {
                    FocusCameraOnVertex(Attachment.ClothVertexIndex);
                }
                ImGui::EndPopup();
            }
        }
        ImGui::EndListBox();
    }
}

void ClothAttachmentEditorPanel::RenderAttachmentPropertiesSection()
{
    if (SelectedAttachmentIndex < 0 || SelectedAttachmentIndex >= CurrentAsset->AttachmentsData.Num())
    {
        ImGui::Text("Select an attachment to edit properties");
        return;
    }
    
    FClothAttachmentData& Attachment = CurrentAsset->AttachmentsData[SelectedAttachmentIndex];
    
    ImGui::Text("Attachment Properties");
    ImGui::Separator();
    
    // Vertex index (read-only)
    ImGui::Text("Vertex Index: %d", Attachment.ClothVertexIndex);
    
    // Attachment type
    const char* TypeNames[] = {"World Position", "Skeletal Bone", "Actor Transform"};
    int CurrentType = (int)Attachment.Type;
    if (ImGui::Combo("Type", &CurrentType, TypeNames, 3))
    {
        Attachment.Type = (EClothAttachmentType)CurrentType;
    }
    
    // Type-specific properties
    switch (Attachment.Type)
    {
    case EClothAttachmentType::WorldPosition:
        ImGui::DragFloat3("Position", &Attachment.WorldPosition.X, 0.1f);
        break;
        
    case EClothAttachmentType::SkeletalBone:
        // Bone selection
        if (ImGui::BeginCombo("Bone", *Attachment.BoneName.ToString()))
        {
            // List bones from selected skeletal mesh
            if (USkeletalMeshComponent* SkelMesh = GetSelectedSkeletalMesh())
            {
                const FReferenceSkeleton& RefSkel = SkelMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
                for (int32 BoneIdx = 0; BoneIdx < RefSkel.GetNum(); ++BoneIdx)
                {
                    FName BoneName = RefSkel.GetBoneName(BoneIdx);
                    if (ImGui::Selectable(*BoneName.ToString(), Attachment.BoneName == BoneName))
                    {
                        Attachment.BoneName = BoneName;
                        Attachment.BoneIndex = BoneIdx;
                    }
                }
            }
            ImGui::EndCombo();
        }
        
        // Local offset
        ImGui::DragFloat3("Local Offset", &Attachment.LocalOffset.GetTranslation().X, 0.1f);
        break;
        
    case EClothAttachmentType::ActorTransform:
        // Actor selection
        ImGui::Text("Target Actor: %s", Attachment.DriverActor ? *Attachment.DriverActor->GetName() : "None");
        if (ImGui::Button("Pick Actor"))
        {
            // Open actor picker
            BeginActorPicking();
        }
        break;
    }
    
    ImGui::Separator();
    
    // Constraint properties
    ImGui::SliderFloat("Stiffness", &Attachment.Stiffness, 0.0f, 1.0f);
    ImGui::Checkbox("Kinematic", &Attachment.bIsKinematic);
    ImGui::DragFloat("Attach Distance", &Attachment.AttachDistance, 0.1f, 0.0f, 100.0f);
    
    ImGui::Separator();
    
    // Actions
    if (ImGui::Button("Apply Changes"))
    {
        // Mark asset as dirty
        CurrentAsset->MarkPackageDirty();
    }
}

void ClothAttachmentEditorPanel::CreateAttachment(EClothAttachmentType Type)
{
    if (AuthoringData->SelectionState.SelectedVertices.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No vertices selected"));
        return;
    }
    
    // Create attachment for each selected vertex
    for (uint32 VertexIndex : AuthoringData->SelectionState.SelectedVertices)
    {
        FClothAttachmentData NewAttachment;
        NewAttachment.ClothVertexIndex = VertexIndex;
        NewAttachment.Type = Type;
        
        // Set default values based on type
        switch (Type)
        {
        case EClothAttachmentType::WorldPosition:
            NewAttachment.WorldPosition = GetVertexWorldPosition(VertexIndex);
            break;
            
        case EClothAttachmentType::SkeletalBone:
            // Will be configured in properties panel
            break;
            
        case EClothAttachmentType::ActorTransform:
            // Will be configured in properties panel
            break;
        }
        
        CurrentAsset->AttachmentsData.Add(NewAttachment);
    }
    
    // Clear selection after creating attachments
    ClearSelection();
}
```

---

## Feature 4: PIE Duplication Support

### Problem

When entering PIE mode, the engine duplicates the world and all actors. Cloth attachments that reference actors/components need to be remapped to the PIE duplicates.

### Solution: Duplication Callback

```cpp
// Extension to ClothComponent.cpp

void UClothComponent::PostDuplicate(bool bDuplicateForPIE)
{
    Super::PostDuplicate(bDuplicateForPIE);
    
    if (bDuplicateForPIE)
    {
        // Remap attachment driver references to PIE duplicates
        RemapAttachmentDriversForPIE();
    }
}

void UClothComponent::RemapAttachmentDriversForPIE()
{
    for (FClothAttachmentData& Attachment : Attachments)
    {
        // Remap DriverActor
        if (Attachment.DriverActor)
        {
            AActor* PIEDuplicate = FindPIEDuplicate(Attachment.DriverActor);
            if (PIEDuplicate)
            {
                Attachment.DriverActor = PIEDuplicate;
            }
        }
        
        // Remap DriverComponent
        if (Attachment.DriverComponent)
        {
            USceneComponent* PIEDuplicate = FindPIEDuplicate(Attachment.DriverComponent);
            if (PIEDuplicate)
            {
                Attachment.DriverComponent = PIEDuplicate;
            }
        }
    }
}

template<typename T>
T* UClothComponent::FindPIEDuplicate(T* OriginalObject)
{
    // Use engine's PIE duplication map
    UWorld* PIEWorld = GetWorld();
    if (!PIEWorld || PIEWorld->WorldType != EWorldType::PIE)
        return nullptr;
    
    // Find duplicate in PIE world
    FString OriginalPath = OriginalObject->GetPathName();
    FString PIEPath = PIEWorld->RemapCommonPIEPath(OriginalPath);
    
    return Cast<T>(StaticFindObject(T::StaticClass(), nullptr, *PIEPath));
}
```

### Serialization

```cpp
// Already implemented in ClothSimulationData.h
inline FArchive& operator<<(FArchive& Ar, FClothAttachmentData& A)
{
    Ar << A.ClothVertexIndex;
    
    uint8 TypeAsByte = static_cast<uint8>(A.Type);
    Ar << TypeAsByte;
    if (Ar.IsLoading())
        A.Type = static_cast<EClothAttachmentType>(TypeAsByte);
    
    // NOTE: DriverComponent and DriverActor are NOT serialized
    // They are runtime references that get resolved from BoneName/WorldPosition
    
    Ar << A.BoneName;
    Ar << A.BoneIndex;
    Ar << A.LocalOffset;
    Ar << A.WorldPosition;
    Ar << A.Stiffness;
    Ar << A.bIsKinematic;
    Ar << A.AttachDistance;
    
    return Ar;
}
```

---

## Feature 5: Runtime Modification API

### API Design

```cpp
// Extension to ClothComponent.h

class UClothComponent : public USceneComponent
{
public:
    // Runtime attachment modification (PIE mode)
    
    /** Change attachment target for a specific vertex */
    void SetAttachmentTarget(uint32 VertexIndex, AActor* NewTarget);
    void SetAttachmentTarget(uint32 VertexIndex, USceneComponent* NewTarget, FName BoneName = NAME_None);
    
    /** Change attachment position (for WorldPosition type) */
    void SetAttachmentPosition(uint32 VertexIndex, const FVector& NewPosition);
    
    /** Change attachment properties */
    void SetAttachmentStiffness(uint32 VertexIndex, float NewStiffness);
    void SetAttachmentDistance(uint32 VertexIndex, float NewDistance);
    
    /** Remove attachment */
    void RemoveAttachment(uint32 VertexIndex);
    
    /** Get attachment data for a vertex */
    FClothAttachmentData* FindAttachment(uint32 VertexIndex);
    const FClothAttachmentData* FindAttachment(uint32 VertexIndex) const;
    
private:
    /** Update GPU buffers after attachment modification */
    void UpdateAttachmentBuffers();
};
```

### Implementation

```cpp
// ClothComponent.cpp

void UClothComponent::SetAttachmentTarget(uint32 VertexIndex, AActor* NewTarget)
{
    FClothAttachmentData* Attachment = FindAttachment(VertexIndex);
    if (!Attachment)
    {
        UE_LOG(LogCloth, Warning, TEXT("No attachment found for vertex %d"), VertexIndex);
        return;
    }
    
    Attachment->Type = EClothAttachmentType::ActorTransform;
    Attachment->DriverActor = NewTarget;
    Attachment->DriverComponent = NewTarget ? NewTarget->GetRootComponent() : nullptr;
    
    // Update GPU buffers
    UpdateAttachmentBuffers();
}

void UClothComponent::SetAttachmentTarget(uint32 VertexIndex, USceneComponent* NewTarget, FName BoneName)
{
    FClothAttachmentData* Attachment = FindAttachment(VertexIndex);
    if (!Attachment)
    {
        UE_LOG(LogCloth, Warning, TEXT("No attachment found for vertex %d"), VertexIndex);
        return;
    }
    
    if (BoneName != NAME_None)
    {
        // Skeletal bone attachment
        Attachment->Type = EClothAttachmentType::SkeletalBone;
        Attachment->BoneName = BoneName;
        
        if (USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(NewTarget))
        {
            Attachment->BoneIndex = SkelMesh->GetBoneIndex(BoneName);
        }
    }
    else
    {
        // Component attachment
        Attachment->Type = EClothAttachmentType::ActorTransform;
    }
    
    Attachment->DriverComponent = NewTarget;
    Attachment->DriverActor = NewTarget ? NewTarget->GetOwner() : nullptr;
    
    UpdateAttachmentBuffers();
}

void UClothComponent::SetAttachmentPosition(uint32 VertexIndex, const FVector& NewPosition)
{
    FClothAttachmentData* Attachment = FindAttachment(VertexIndex);
    if (!Attachment)
    {
        UE_LOG(LogCloth, Warning, TEXT("No attachment found for vertex %d"), VertexIndex);
        return;
    }
    
    Attachment->Type = EClothAttachmentType::WorldPosition;
    Attachment->WorldPosition = NewPosition;
    
    UpdateAttachmentBuffers();
}

FClothAttachmentData* UClothComponent::FindAttachment(uint32 VertexIndex)
{
    for (FClothAttachmentData& Attachment : Attachments)
    {
        if (Attachment.ClothVertexIndex == VertexIndex)
            return &Attachment;
    }
    return nullptr;
}

void UClothComponent::UpdateAttachmentBuffers()
{
    // Notify batch manager to update GPU buffers
    if (ClothInstanceHandle)
    {
        FClothBatchManager* BatchMgr = ClothInstanceHandle->GetBatchManager();
        if (BatchMgr)
        {
            BatchMgr->UpdateInstanceAttachments(ClothInstanceHandle, Attachments);
        }
    }
}
```

---

## Implementation Plan

### Phase 1: Core Infrastructure

#### Files to Create

1. **`ClothAttachmentAuthoringData.h/cpp`**
   - Location: `Engine/Source/Runtime/Engine/Cloth/`
   - Purpose: Selection state and authoring data structures
   - Dependencies: ClothSimulationData.h

2. **`ClothVertexPicker.h/cpp`**
   - Location: `Engine/Source/Runtime/Engine/Cloth/`
   - Purpose: Ray-casting and vertex selection logic
   - Dependencies: Math libraries, ClothAsset

3. **`ClothAttachmentVisualization.hlsl`**
   - Location: `Shaders/Cloth/`
   - Purpose: Vertex highlighting shader
   - Dependencies: EditorShaderConstants.hlsli

#### Files to Modify

1. **`EditorRenderPass.h/cpp`**
   - Add: `RenderClothAttachmentVisualization()`
   - Add: `RenderSelectedVertices()`
   - Add: `RenderAttachmentLines()`

2. **`ClothComponent.h/cpp`**
   - Add: Runtime modification API
   - Add: `PostDuplicate()` override for PIE
   - Add: `RemapAttachmentDriversForPIE()`

### Phase 2: Editor Panel

#### Files to Create

1. **`ClothAttachmentEditorPanel.h/cpp`**
   - Location: `Engine/Source/Editor/PropertyEditor/`
   - Purpose: ImGui-based attachment editor
   - Dependencies: EditorPanel, ClothAsset, ImGui

#### Files to Modify

1. **`UnrealEd.cpp`**
   - Register ClothAttachmentEditorPanel
   - Add to editor panel list

### Phase 3: Integration & Testing

#### Console Commands

```cpp
// For testing attachment system
CONSOLE_COMMAND(cloth.attachment.select)
{
    // Select vertex by index
    uint32 VertexIndex = FCString::Atoi(*Args[0]);
    // Add to selection...
}

CONSOLE_COMMAND(cloth.attachment.create)
{
    // Create attachment for selected vertices
    EClothAttachmentType Type = ParseAttachmentType(Args[0]);
    // Create attachments...
}

CONSOLE_COMMAND(cloth.attachment.list)
{
    // List all attachments for current cloth
    // Print to console...
}
```

### Phase 4: Documentation

1. **User Guide**: How to use the attachment editor
2. **API Reference**: Runtime modification functions
3. **Technical Guide**: Architecture and extension points

---

## File Structure Summary

```
EngineSIU/
├── Engine/Source/
│   ├── Runtime/Engine/
│   │   ├── Cloth/
│   │   │   ├── ClothAttachmentAuthoringData.h       [NEW]
│   │   │   ├── ClothAttachmentAuthoringData.cpp     [NEW]
│   │   │   ├── ClothVertexPicker.h                  [NEW]
│   │   │   ├── ClothVertexPicker.cpp                [NEW]
│   │   │   ├── ClothSimulationData.h                [MODIFY - already has FClothAttachmentData]
│   │   │   └── ClothComponent.cpp                   [MODIFY - add runtime API]
│   │   └── Renderer/
│   │       ├── EditorRenderPass.h                   [MODIFY]
│   │       └── EditorRenderPass.cpp                 [MODIFY]
│   └── Editor/
│       └── PropertyEditor/
│           ├── ClothAttachmentEditorPanel.h         [NEW]
│           └── ClothAttachmentEditorPanel.cpp       [NEW]
└── Shaders/
    └── Cloth/
        └── ClothAttachmentVisualization.hlsl        [NEW]
```

---

## Testing Strategy

### Unit Tests

1. **Vertex Picking**
   - Test ray-cast accuracy
   - Test multi-select
   - Test box selection

2. **Attachment Creation**
   - Test all attachment types
   - Test vertex validation
   - Test duplicate prevention

3. **PIE Duplication**
   - Test actor remapping
   - Test component remapping
   - Test bone index preservation

### Integration Tests

1. **Editor Workflow**
   - Select vertices → Create attachment → Verify in list
   - Modify attachment properties → Verify changes
   - Delete attachment → Verify removal

2. **Runtime Modification**
   - Change target during PIE → Verify cloth follows new target
   - Change position → Verify cloth moves
   - Change stiffness → Verify constraint behavior

3. **Serialization**
   - Save asset → Load asset → Verify attachments preserved
   - Enter PIE → Verify attachments work
   - Exit PIE → Verify editor state restored

---

## Performance Considerations

### Vertex Selection

- **Spatial Acceleration**: Use octree or grid for fast vertex lookup
- **LOD**: Only pick from visible LOD level
- **Culling**: Skip vertices outside view frustum

### Visual Feedback

- **Instanced Rendering**: Batch all selected vertices into single draw call
- **LOD**: Reduce sphere detail for distant vertices
- **Occlusion**: Skip rendering occluded vertices

### Runtime Modification

- **Lazy Update**: Only update GPU buffers when attachments change
- **Batch Updates**: Group multiple modifications into single upload
- **Dirty Flags**: Track which instances need buffer updates

---

## Future Enhancements

### Advanced Selection

- **Paint Selection**: Brush-based vertex selection
- **Grow/Shrink Selection**: Expand selection to neighbors
- **Select by Material**: Select all vertices with specific material
- **Select by Distance**: Select vertices within distance from point

### Attachment Presets

- **Save/Load Presets**: Store common attachment configurations
- **Mirror Attachments**: Copy attachments to symmetric vertices
- **Attachment Templates**: Pre-configured setups for common use cases

### Visualization Improvements

- **Attachment Strength Heatmap**: Color vertices by stiffness
- **Motion Preview**: Animate attachment targets to preview behavior
- **Constraint Visualization**: Show distance constraints as springs

### Runtime Features

- **Attachment Blending**: Smoothly transition between targets
- **Dynamic Attachment**: Attach/detach during gameplay
- **Attachment Events**: Callbacks when attachment breaks/forms

---

## Summary

This architecture provides:

✅ **Visual vertex selection** with multi-select support  
✅ **ImGui-based editor panel** for attachment management  
✅ **Real-time visual feedback** with highlighted vertices and attachment lines  
✅ **PIE duplication support** with automatic reference remapping  
✅ **Runtime modification API** for dynamic attachment changes  
✅ **Extensible design** for future enhancements  

The system integrates seamlessly with the existing cloth infrastructure and follows established patterns from the codebase (EditorRenderPass, EditorPanel, ClothComponent).
