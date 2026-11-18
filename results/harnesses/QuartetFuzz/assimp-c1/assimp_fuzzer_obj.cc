#include <cstdint>
#include <cstring>
#include <string>

#include <assimp/cimport.h>
#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>

using namespace Assimp;

static const char *const kHints[] = {
    "",     "obj",  "fbx",  "gltf", "glb",  "dae",  "ply",
    "3ds",  "stl",  "blend","ifc",  "3mf",  "amf",  "ase",
    "x",    "md5mesh",
};
static const size_t kNumHints = sizeof(kHints) / sizeof(kHints[0]);

static void WalkNode(const aiNode *node, int depth) {
    if (node == nullptr || depth > 64) {
        return;
    }
    volatile unsigned int sink = node->mNumMeshes + node->mNumChildren;
    (void)sink;
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        WalkNode(node->mChildren[i], depth + 1);
    }
}

static void WalkScene(const aiScene *scene) {
    if (scene == nullptr) {
        return;
    }
    WalkNode(scene->mRootNode, 0);
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh *mesh = scene->mMeshes[m];
        if (mesh == nullptr) {
            continue;
        }
        volatile unsigned int v = mesh->mNumVertices + mesh->mNumFaces +
                                   mesh->mNumBones + mesh->mNumAnimMeshes;
        (void)v;
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace &face = mesh->mFaces[f];
            volatile unsigned int nidx = face.mNumIndices;
            (void)nidx;
        }
    }
    for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
        const aiAnimation *anim = scene->mAnimations[a];
        if (anim == nullptr) {
            continue;
        }
        volatile unsigned int ch = anim->mNumChannels;
        (void)ch;
    }
}

static unsigned int SanitizeFlags(unsigned int flags) {
    if ((flags & aiProcess_GenNormals) && (flags & aiProcess_GenSmoothNormals)) {
        flags &= ~aiProcess_GenSmoothNormals;
    }
    if ((flags & aiProcess_OptimizeGraph) && (flags & aiProcess_PreTransformVertices)) {
        flags &= ~aiProcess_OptimizeGraph;
    }
    return flags;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t dataSize) {

    if (dataSize > 1024 * 1024) {
        return 0;
    }

    if (dataSize < 9) {
        return 0;
    }

    const size_t ctrlBytes = 7;
    const size_t modelSize = dataSize - ctrlBytes;
    const uint8_t *ctrl = data + modelSize;

    const uint8_t hintSel = ctrl[0];
    const uint32_t flagsBits = static_cast<uint32_t>(ctrl[1]) |
                               (static_cast<uint32_t>(ctrl[2]) << 8);
    const uint8_t postFlagsSel = ctrl[3];
    const uint8_t rvcSel = ctrl[4];
    const uint8_t scaleSel = ctrl[5];
    const uint8_t exportSel = ctrl[6];

    const char *hint = kHints[hintSel % kNumHints];

    static const unsigned int kAllSteps[] = {
        aiProcess_CalcTangentSpace,
        aiProcess_JoinIdenticalVertices,
        aiProcess_MakeLeftHanded,
        aiProcess_Triangulate,
        aiProcess_RemoveComponent,
        aiProcess_GenNormals,
        aiProcess_GenSmoothNormals,
        aiProcess_SplitLargeMeshes,
        aiProcess_PreTransformVertices,
        aiProcess_LimitBoneWeights,
        aiProcess_ValidateDataStructure,
        aiProcess_ImproveCacheLocality,
        aiProcess_SortByPType,
        aiProcess_FindDegenerates,
        aiProcess_FindInvalidData,
        aiProcess_GenUVCoords,
    };
    static const size_t kNumAllSteps = sizeof(kAllSteps) / sizeof(kAllSteps[0]);

    unsigned int flags = 0;
    for (size_t i = 0; i < kNumAllSteps; ++i) {
        if (flagsBits & (1u << i)) {
            flags |= kAllSteps[i];
        }
    }
    flags = SanitizeFlags(flags);

    static const unsigned int kPostSteps[] = {
        aiProcess_OptimizeMeshes,
        aiProcess_OptimizeGraph,
        aiProcess_FixInfacingNormals,
        aiProcess_TransformUVCoords,
        aiProcess_FindInstances,
        aiProcess_Debone,
        aiProcess_SplitByBoneCount,
        aiProcess_GenBoundingBoxes,
    };
    static const size_t kNumPostSteps = sizeof(kPostSteps) / sizeof(kPostSteps[0]);

    unsigned int postFlags = 0;
    for (size_t i = 0; i < kNumPostSteps; ++i) {
        if (postFlagsSel & (1u << i)) {
            postFlags |= kPostSteps[i];
        }
    }
    postFlags = SanitizeFlags(postFlags);

    Importer importer;

    importer.SetPropertyFloat(AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY,
                               static_cast<float>((scaleSel % 64) + 1) * 0.5f);
    importer.SetPropertyInteger(AI_CONFIG_PP_SBBC_MAX_BONES, (rvcSel % 64) + 1);

    unsigned int rvc = 0;
    if (rvcSel & 0x01) rvc |= aiComponent_NORMALS;
    if (rvcSel & 0x02) rvc |= aiComponent_TANGENTS_AND_BITANGENTS;
    if (rvcSel & 0x04) rvc |= aiComponent_COLORS;
    if (rvcSel & 0x08) rvc |= aiComponent_TEXCOORDS;
    if (rvcSel & 0x10) rvc |= aiComponent_BONEWEIGHTS;
    if (rvcSel & 0x20) rvc |= aiComponent_ANIMATIONS;
    if (rvcSel & 0x40) rvc |= aiComponent_TEXTURES;
    if (rvcSel & 0x80) rvc |= aiComponent_LIGHTS;
    importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, static_cast<int>(rvc));

    const aiScene *scene = importer.ReadFileFromMemory(data, modelSize, flags, hint);
    if (scene == nullptr) {
        return 0;
    }

    scene = importer.ApplyPostProcessing(postFlags);
    if (scene == nullptr) {
        return 0;
    }

    WalkScene(scene);

    const aiExportDataBlob *roundTripBlob = nullptr;
    std::string roundTripFormatId;
    {
        Exporter exporter;
        const size_t formatCount = exporter.GetExportFormatCount();
        if (formatCount > 0) {
            const size_t formatIdx = exportSel % formatCount;
            const aiExportFormatDesc *desc = exporter.GetExportFormatDescription(formatIdx);
            if (desc != nullptr && desc->id != nullptr) {
                roundTripFormatId = desc->id;
                roundTripBlob = exporter.ExportToBlob(scene, desc->id);

                if (roundTripBlob != nullptr && roundTripBlob->data != nullptr &&
                    roundTripBlob->size > 0) {

                    Importer reimporter;
                    const aiScene *reimported = reimporter.ReadFileFromMemory(
                            roundTripBlob->data, roundTripBlob->size, 0,
                            roundTripFormatId.c_str());
                    WalkScene(reimported);
                }
            }
        }
        exporter.ExportToBlob(scene, "obj");
        exporter.ExportToBlob(scene, "collada");
    }

    if ((ctrl[0] & 0x80) != 0) {
        aiScene *owned = importer.GetOrphanedScene();
        delete owned;
    }

    return 0;
}
