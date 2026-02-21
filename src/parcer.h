#pragma once
#include <vector>
#include <string>
#include <DirectXMath.h>

struct Vertex;

bool LoadOBJ(
    const std::string& filename,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices
);
