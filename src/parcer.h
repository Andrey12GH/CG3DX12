#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <string>
#include <vector>

struct Vertex
{
    DirectX::XMFLOAT3 position{};
    DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
    DirectX::XMFLOAT2 texcoord{};
};

struct ObjMaterial
{
    std::string name;
    DirectX::XMFLOAT4 diffuseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::string diffuseTexturePath;
    uint32_t textureSrvIndex = 0;
};

struct ObjSubset
{
    uint32_t startIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
};

struct ObjModel
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<ObjMaterial> materials;
    std::vector<ObjSubset> subsets;
};

// Loads geometry, UV coordinates, normals, mtllib/usemtl assignments and
// diffuse (Kd/map_Kd) material properties from an OBJ/MTL pair.
bool LoadOBJ(const std::string& filename, ObjModel& outModel);
