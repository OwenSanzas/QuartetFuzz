#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <fstream>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

static const char *kHints[] = {
    "",
    "obj",
    "fbx",
    "dae",
    "3ds",
    "ply",
    "stl",
    "gltf",
    "glb",
    "blend",
    "x",
    "md5mesh",
    "assbin",
    "off",
    "ifc",
    "xml",
};

static void TryWriteDummyFile(const uint8_t *Data, size_t Size) {

    std::ofstream ofs("./dummy_file", std::ios::binary | std::ios::trunc);
    if (ofs.is_open()) {
        ofs.write(reinterpret_cast<const char *>(Data), static_cast<std::streamsize>(Size));
        ofs.close();
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    uint8_t hintSelector = Data[0];
    size_t numHints = sizeof(kHints) / sizeof(kHints[0]);
    const char *hint = kHints[hintSelector % numHints];

    uint32_t flags = 0;
    if (Size > 1) {
        uint8_t flagByte = Data[1];

        if (flagByte & 0x01) flags |= aiProcess_Triangulate;
        if (flagByte & 0x02) flags |= aiProcess_GenNormals;
        if (flagByte & 0x04) flags |= aiProcess_JoinIdenticalVertices;
        if (flagByte & 0x08) flags |= aiProcess_CalcTangentSpace;
        if (flagByte & 0x10) flags |= aiProcess_ValidateDataStructure;
        if (flagByte & 0x20) flags |= aiProcess_SplitLargeMeshes;
        if (flagByte & 0x40) flags |= aiProcess_OptimizeMeshes;
        if (flagByte & 0x80) flags |= aiProcess_FlipUVs;
    }

    const uint8_t *bufferData = Data;
    size_t bufferSize = Size;
    if (Size > 2) {
        bufferData = Data + 2;
        bufferSize = Size - 2;
    }

    TryWriteDummyFile(Data, Size);

    try {
        Assimp::Importer importer;

        const aiScene *scene = importer.ReadFileFromMemory(
            bufferData, bufferSize, flags, hint);

        if (scene != nullptr) {

            volatile unsigned int meshCount = scene->mNumMeshes;
            volatile unsigned int matCount = scene->mNumMaterials;
            volatile unsigned int animCount = scene->mNumAnimations;
            volatile unsigned int texCount = scene->mNumTextures;
            volatile unsigned int lightCount = scene->mNumLights;
            volatile unsigned int camCount = scene->mNumCameras;
            (void)meshCount;
            (void)matCount;
            (void)animCount;
            (void)texCount;
            (void)lightCount;
            (void)camCount;

            if (scene->mRootNode != nullptr) {
                volatile unsigned int childCount = scene->mRootNode->mNumChildren;
                (void)childCount;
            }

            for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
                aiMesh *mesh = scene->mMeshes[i];
                if (mesh != nullptr) {
                    volatile unsigned int vCount = mesh->mNumVertices;
                    volatile unsigned int fCount = mesh->mNumFaces;
                    (void)vCount;
                    (void)fCount;
                }
            }
        }

        Assimp::Importer importer2;
        const aiScene *scene2 = importer2.ReadFileFromMemory(
            bufferData, bufferSize, 0, "");
        if (scene2 != nullptr) {
            volatile unsigned int meshCount2 = scene2->mNumMeshes;
            (void)meshCount2;
        }

        if (Size > 3) {
            uint8_t hintSelector2 = Data[2];
            const char *hint2 = kHints[hintSelector2 % numHints];
            uint32_t flags2 = 0;
            uint8_t flagByte2 = Data[3];
            if (flagByte2 & 0x01) flags2 |= aiProcess_FindDegenerates;
            if (flagByte2 & 0x02) flags2 |= aiProcess_FindInvalidData;
            if (flagByte2 & 0x04) flags2 |= aiProcess_FixInfacingNormals;
            if (flagByte2 & 0x08) flags2 |= aiProcess_GenUVCoords;
            if (flagByte2 & 0x10) flags2 |= aiProcess_LimitBoneWeights;
            if (flagByte2 & 0x20) flags2 |= aiProcess_RemoveComponent;
            if (flagByte2 & 0x40) flags2 |= aiProcess_SortByPType;
            if (flagByte2 & 0x80) flags2 |= aiProcess_TransformUVCoords;

            Assimp::Importer importer3;
            const aiScene *scene3 = importer3.ReadFileFromMemory(
                bufferData, bufferSize, flags2, hint2);
            if (scene3 != nullptr) {
                volatile unsigned int meshCount3 = scene3->mNumMeshes;
                (void)meshCount3;
            }
        }
    } catch (const std::exception &) {

    } catch (...) {

    }

    return 0;
}