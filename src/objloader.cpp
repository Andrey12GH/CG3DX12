#include "parcer.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>

using namespace DirectX;

namespace
{
    constexpr float ModelSize = 5.0f;

    struct VertexKey
    {
        int position = -1;
        int texcoord = -1;
        int normal = -1;

        bool operator==(const VertexKey&) const = default;
    };

    struct VertexKeyHash
    {
        size_t operator()(const VertexKey& key) const noexcept
        {
            size_t value = std::hash<int>{}(key.position);
            value ^= std::hash<int>{}(key.texcoord) + 0x9e3779b9 + (value << 6) + (value >> 2);
            value ^= std::hash<int>{}(key.normal) + 0x9e3779b9 + (value << 6) + (value >> 2);
            return value;
        }
    };

    std::string Trim(std::string value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};

        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    int ResolveIndex(int value, size_t itemCount)
    {
        if (value > 0)
            return value - 1;
        if (value < 0)
            return static_cast<int>(itemCount) + value;
        return -1;
    }

    bool ParseFaceVertex(
        std::string_view token,
        size_t positionCount,
        size_t texcoordCount,
        size_t normalCount,
        VertexKey& key)
    {
        try
        {
            const size_t firstSlash = token.find('/');
            const size_t secondSlash = firstSlash == std::string_view::npos
                ? std::string_view::npos
                : token.find('/', firstSlash + 1);

            key.position = ResolveIndex(
                std::stoi(std::string(token.substr(0, firstSlash))), positionCount);

            if (firstSlash != std::string_view::npos)
            {
                const size_t texcoordLength = secondSlash == std::string_view::npos
                    ? token.size() - firstSlash - 1
                    : secondSlash - firstSlash - 1;
                const std::string text(token.substr(firstSlash + 1, texcoordLength));
                if (!text.empty())
                    key.texcoord = ResolveIndex(std::stoi(text), texcoordCount);
            }

            if (secondSlash != std::string_view::npos)
            {
                const std::string text(token.substr(secondSlash + 1));
                if (!text.empty())
                    key.normal = ResolveIndex(std::stoi(text), normalCount);
            }
        }
        catch (...)
        {
            return false;
        }

        return key.position >= 0 && key.position < static_cast<int>(positionCount);
    }

    XMFLOAT4 FallbackColor(const std::string& name)
    {
        const uint32_t hash = static_cast<uint32_t>(std::hash<std::string>{}(name));
        const auto channel = [hash](int shift)
        {
            return 0.45f + static_cast<float>((hash >> shift) & 0xff) / 255.0f * 0.45f;
        };
        return XMFLOAT4(channel(0), channel(8), channel(16), 1.0f);
    }

    uint32_t FindOrCreateMaterial(
        const std::string& name,
        std::vector<ObjMaterial>& materials,
        std::unordered_map<std::string, uint32_t>& materialLookup)
    {
        const auto found = materialLookup.find(name);
        if (found != materialLookup.end())
            return found->second;

        ObjMaterial material;
        material.name = name;
        material.diffuseColor = FallbackColor(name);

        const uint32_t index = static_cast<uint32_t>(materials.size());
        materials.push_back(std::move(material));
        materialLookup[name] = index;
        return index;
    }

    void LoadMTL(
        const std::filesystem::path& path,
        std::vector<ObjMaterial>& materials,
        std::unordered_map<std::string, uint32_t>& materialLookup)
    {
        std::ifstream file(path);
        if (!file)
            return;

        ObjMaterial* current = nullptr;
        std::string line;
        while (std::getline(file, line))
        {
            const size_t comment = line.find('#');
            if (comment != std::string::npos)
                line.resize(comment);
            line = Trim(std::move(line));
            if (line.empty())
                continue;

            std::istringstream input(line);
            std::string command;
            input >> command;

            if (command == "newmtl")
            {
                std::string name;
                std::getline(input, name);
                name = Trim(std::move(name));
                const uint32_t index = FindOrCreateMaterial(name, materials, materialLookup);
                current = &materials[index];
                current->diffuseColor = XMFLOAT4(1, 1, 1, 1);
            }
            else if (current && command == "Kd")
            {
                input >> current->diffuseColor.x
                    >> current->diffuseColor.y
                    >> current->diffuseColor.z;
            }
            else if (current && command == "d")
            {
                input >> current->diffuseColor.w;
            }
            else if (current && command == "Tr")
            {
                float transparency = 0.0f;
                input >> transparency;
                current->diffuseColor.w = 1.0f - transparency;
            }
            else if (current && command == "map_Kd")
            {
                // The last token is the image path. This also accepts common
                // options such as "-s 1 1 1 texture.png".
                std::string token;
                std::string finalToken;
                while (input >> token)
                    finalToken = token;

                if (!finalToken.empty())
                {
                    if (finalToken.size() >= 2 && finalToken.front() == '"' && finalToken.back() == '"')
                        finalToken = finalToken.substr(1, finalToken.size() - 2);
                    current->diffuseTexturePath =
                        (path.parent_path() / finalToken).lexically_normal().string();
                }
            }
        }
    }

    void NormalizeGeometry(std::vector<Vertex>& vertices)
    {
        if (vertices.empty())
            return;

        XMFLOAT3 minPoint = vertices.front().position;
        XMFLOAT3 maxPoint = vertices.front().position;
        for (const Vertex& vertex : vertices)
        {
            minPoint.x = std::min(minPoint.x, vertex.position.x);
            minPoint.y = std::min(minPoint.y, vertex.position.y);
            minPoint.z = std::min(minPoint.z, vertex.position.z);
            maxPoint.x = std::max(maxPoint.x, vertex.position.x);
            maxPoint.y = std::max(maxPoint.y, vertex.position.y);
            maxPoint.z = std::max(maxPoint.z, vertex.position.z);
        }

        const XMFLOAT3 center{
            (minPoint.x + maxPoint.x) * 0.5f,
            (minPoint.y + maxPoint.y) * 0.5f,
            (minPoint.z + maxPoint.z) * 0.5f
        };
        const float largestExtent = std::max({
            maxPoint.x - minPoint.x,
            maxPoint.y - minPoint.y,
            maxPoint.z - minPoint.z
        });

        if (largestExtent <= 0.0f)
            return;

        const float scale = ModelSize / largestExtent;
        for (Vertex& vertex : vertices)
        {
            vertex.position.x = (vertex.position.x - center.x) * scale;
            vertex.position.y = (vertex.position.y - center.y) * scale;
            vertex.position.z = (vertex.position.z - center.z) * scale;
        }
    }
}

bool LoadOBJ(const std::string& filename, ObjModel& outModel)
{
    outModel = {};

    const std::filesystem::path objPath(filename);
    std::ifstream file(objPath);
    if (!file)
        return false;

    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT3> normals;
    std::vector<XMFLOAT2> texcoords;
    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexLookup;
    std::unordered_map<std::string, uint32_t> materialLookup;

    positions.reserve(500000);
    normals.reserve(500000);
    texcoords.reserve(500000);
    outModel.vertices.reserve(500000);
    outModel.indices.reserve(1000000);

    const uint32_t defaultMaterial = FindOrCreateMaterial(
        "default", outModel.materials, materialLookup);
    uint32_t currentMaterial = defaultMaterial;
    uint32_t subsetStart = 0;

    const auto finishSubset = [&]()
    {
        const uint32_t currentIndex = static_cast<uint32_t>(outModel.indices.size());
        if (currentIndex > subsetStart)
        {
            outModel.subsets.push_back({ subsetStart, currentIndex - subsetStart, currentMaterial });
            subsetStart = currentIndex;
        }
    };

    std::string line;
    while (std::getline(file, line))
    {
        const size_t comment = line.find('#');
        if (comment != std::string::npos)
            line.resize(comment);
        line = Trim(std::move(line));
        if (line.empty())
            continue;

        std::istringstream input(line);
        std::string command;
        input >> command;

        if (command == "v")
        {
            XMFLOAT3 position;
            if (input >> position.x >> position.y >> position.z)
                positions.push_back(position);
        }
        else if (command == "vn")
        {
            XMFLOAT3 normal;
            if (input >> normal.x >> normal.y >> normal.z)
                normals.push_back(normal);
        }
        else if (command == "vt")
        {
            XMFLOAT2 texcoord;
            if (input >> texcoord.x >> texcoord.y)
            {
                // WIC images use a top-left origin; OBJ V grows from the bottom.
                texcoord.y = 1.0f - texcoord.y;
                texcoords.push_back(texcoord);
            }
        }
        else if (command == "mtllib")
        {
            std::string libraryName;
            std::getline(input, libraryName);
            libraryName = Trim(std::move(libraryName));
            if (!libraryName.empty())
                LoadMTL(objPath.parent_path() / libraryName, outModel.materials, materialLookup);
        }
        else if (command == "usemtl")
        {
            finishSubset();
            std::string materialName;
            std::getline(input, materialName);
            materialName = Trim(std::move(materialName));
            currentMaterial = FindOrCreateMaterial(materialName, outModel.materials, materialLookup);
        }
        else if (command == "f")
        {
            std::vector<VertexKey> face;
            std::string token;
            while (input >> token)
            {
                VertexKey key;
                if (ParseFaceVertex(token, positions.size(), texcoords.size(), normals.size(), key))
                    face.push_back(key);
            }

            if (face.size() < 3)
                continue;

            const auto getVertexIndex = [&](const VertexKey& key)
            {
                const auto found = vertexLookup.find(key);
                if (found != vertexLookup.end())
                    return found->second;

                Vertex vertex;
                vertex.position = positions[key.position];
                if (key.normal >= 0 && key.normal < static_cast<int>(normals.size()))
                    vertex.normal = normals[key.normal];
                if (key.texcoord >= 0 && key.texcoord < static_cast<int>(texcoords.size()))
                    vertex.texcoord = texcoords[key.texcoord];

                const uint32_t index = static_cast<uint32_t>(outModel.vertices.size());
                outModel.vertices.push_back(vertex);
                vertexLookup.emplace(key, index);
                return index;
            };

            // Fan triangulation supports both triangles and polygonal faces.
            for (size_t i = 1; i + 1 < face.size(); ++i)
            {
                outModel.indices.push_back(getVertexIndex(face[0]));
                outModel.indices.push_back(getVertexIndex(face[i]));
                outModel.indices.push_back(getVertexIndex(face[i + 1]));
            }
        }
    }

    finishSubset();
    if (outModel.vertices.empty() || outModel.indices.empty())
        return false;

    NormalizeGeometry(outModel.vertices);
    return true;
}
