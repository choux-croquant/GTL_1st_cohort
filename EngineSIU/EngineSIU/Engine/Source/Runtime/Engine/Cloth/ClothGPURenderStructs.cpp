/**
 * Cloth GPU Render Structures Implementation
 */

#include "ClothGPURenderStructs.h"
#include "Core/Math/Matrix.h"

void FClothInstanceConstants::SetWorldMatrix(const FMatrix& Matrix)
{
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            ClothWorldMatrix[row * 4 + col] = Matrix.M[row][col];
        }
    }
}
