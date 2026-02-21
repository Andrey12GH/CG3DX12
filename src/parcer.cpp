#include "parcer.h"
#include "dx12renderer.h"

#include <fstream>
#include <vector>
#include <string>
#include <algorithm>

using namespace DirectX;

bool LoadOBJ(
    const std::string& filename,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    std::ifstream file(filename);
    if (!file.is_open())
        return false;

    constexpr float OBJ_SCALE = 5.0f;

    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT3> normals;

    positions.reserve(500000);
    normals.reserve(500000);
    outVertices.reserve(500000);
    outIndices.reserve(1000000);

    std::string line;

    while (std::getline(file, line))
    {
        // ===== vertex position =====
        if (line.rfind("v ", 0) == 0)
        {
            XMFLOAT3 p;
            sscanf_s(line.c_str(), "v %f %f %f", &p.x, &p.y, &p.z);

            p.x *= OBJ_SCALE;
            p.y *= OBJ_SCALE;
            p.z *= OBJ_SCALE;

            positions.push_back(p);
        }
        // ===== vertex normal =====
        else if (line.rfind("vn ", 0) == 0)
        {
            XMFLOAT3 n;
            sscanf_s(line.c_str(), "vn %f %f %f", &n.x, &n.y, &n.z);
            normals.push_back(n);
        }
        // ===== face =====
        else if (line.rfind("f ", 0) == 0)
        {
            int pi[3]{}, ni[3]{};

            // ÔÓ‰‰ÂÊÍ‡: v//n Ë v/vt/n
            int matched = sscanf_s(
                line.c_str(),
                "f %d//%d %d//%d %d//%d",
                &pi[0], &ni[0],
                &pi[1], &ni[1],
                &pi[2], &ni[2]
            );

            if (matched != 6)
            {
                matched = sscanf_s(
                    line.c_str(),
                    "f %d/%*d/%d %d/%*d/%d %d/%*d/%d",
                    &pi[0], &ni[0],
                    &pi[1], &ni[1],
                    &pi[2], &ni[2]
                );
            }

            if (matched != 6)
                continue;

            for (int i = 0; i < 3; i++)
            {
                int posIndex = pi[i] - 1;
                int normIndex = ni[i] - 1;

                if (posIndex < 0 || posIndex >= (int)positions.size())
                    continue;

                Vertex v{};
                v.position = positions[posIndex];

                if (!normals.empty() &&
                    normIndex >= 0 &&
                    normIndex < (int)normals.size())
                {
                    v.normal = normals[normIndex];
                }
                else
                {
                    v.normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
                }

                v.color = XMFLOAT4(1, 1, 1, 1);

                outVertices.push_back(v);
                outIndices.push_back((uint32_t)(outVertices.size() - 1));
            }
        }
    }

    if (outVertices.empty())
        return false;

    // ===== ÕŒ–Ã¿À»«¿÷»ﬂ ¬ [-1;1] =====
    XMFLOAT3 minP = outVertices[0].position;
    XMFLOAT3 maxP = outVertices[0].position;

    for (const auto& v : outVertices)
    {
        minP.x = min(minP.x, v.position.x);
        minP.y = min(minP.y, v.position.y);
        minP.z = min(minP.z, v.position.z);

        maxP.x = max(maxP.x, v.position.x);
        maxP.y = max(maxP.y, v.position.y);
        maxP.z = max(maxP.z, v.position.z);
    }

    XMFLOAT3 center =
    {
        (minP.x + maxP.x) * 0.5f,
        (minP.y + maxP.y) * 0.5f,
        (minP.z + maxP.z) * 0.5f
    };

    float maxExtent = max(
        maxP.x - minP.x,
        max(
            maxP.y - minP.y,
            maxP.z - minP.z
        )
    );

    if (maxExtent > 0.0f)
    {
        float scale = OBJ_SCALE / maxExtent;

        for (auto& v : outVertices)
        {
            v.position.x = (v.position.x - center.x) * scale;
            v.position.y = (v.position.y - center.y) * scale;
            v.position.z = (v.position.z - center.z) * scale;
        }
    }

    return true;
}
