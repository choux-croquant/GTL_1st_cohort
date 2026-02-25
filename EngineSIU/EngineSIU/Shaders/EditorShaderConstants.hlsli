#define MAX_NUM_CAPSULE  100
#define MAX_NUM_BOX      100
#define MAX_NUM_SPHERE   100
#define MAX_NUM_TORUS    100

struct BoxData
{
    row_major matrix WorldMatrix;
    
    float3 Extent;
    float Padding2;
};

cbuffer ConstantBufferDebugAABB : register(b11)
{
    BoxData DataBox[MAX_NUM_BOX];
}

struct SphereData
{
    float3 Position;
    float Radius;
};

cbuffer ConstantBufferDebugSphere : register(b11)
{
    SphereData DataSphere[MAX_NUM_SPHERE];
}

struct ConeData
{
    float3 ApexPosition;
    float Radius;

    float3 Direction;
    float Angle;
};

cbuffer ConstantBufferDebugCone : register(b11)
{
    ConeData DataCone[100];
}

cbuffer ConstantBufferDebugGrid : register(b11)
{
    row_major matrix InverseViewProj;
}

cbuffer ConstantBufferDebugIcon : register(b11)
{
    float3 IconPosition;
    float IconScale;
}

struct ArrowData
{
    float3 Position;
    float  Scale;
    float3 Direction;
    float  ScaleZ;
};

cbuffer ConstantBufferDebugArrow : register(b11)
{
    ArrowData DataArrow[100];
}

struct CapsuleData
{
    row_major matrix WorldMatrix;
    float HalfHeight;
    float Radius;
};

cbuffer ConstantBufferDebugCapsule : register(b11)
{
    CapsuleData DataCapsule[MAX_NUM_CAPSULE];
}

struct TorusData
{
    row_major matrix WorldMatrix;
    float3 Axis;
    float MajorRadius;
    float MinorRadius;
    float3 Padding;
};

cbuffer ConstantBufferDebugTorus : register(b11)
{
    TorusData DataTorus[MAX_NUM_TORUS];
}
