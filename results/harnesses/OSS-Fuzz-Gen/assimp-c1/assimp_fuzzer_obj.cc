#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <assimp/cimport.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace Assimp;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

    if (size > 1024 * 1024) {
        return 0;
    }

    FuzzedDataProvider stream(data, size);

    const unsigned int flags = stream.ConsumeIntegral<uint32_t>();

    const std::string hint =
        stream.ConsumeRandomLengthString(8);

    const std::vector<uint8_t> buffer = stream.ConsumeRemainingBytes<uint8_t>();

    Importer importer;
    const aiScene *scene = importer.ReadFileFromMemory(
        buffer.data(), buffer.size(), flags, hint.c_str());

    if (scene == nullptr) {
        return 0;
    }

    return 0;
}