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
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>

#include "hunspell.hxx"

static void write_dummy_files() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    std::ofstream aff_file("./dummy.aff");
    if (aff_file.is_open()) {
        aff_file << "SET UTF-8\n";
        aff_file << "TRY esianrtolcdugmphbyfvkwz\n";
        aff_file << "PFX A Y 1\n";
        aff_file << "PFX A 0 re .\n";
        aff_file << "SFX B Y 1\n";
        aff_file << "SFX B 0 ing .\n";
        aff_file.close();
    }

    std::ofstream dic_file("./dummy.dic");
    if (dic_file.is_open()) {
        dic_file << "3\n";
        dic_file << "hello\n";
        dic_file << "world/AB\n";
        dic_file << "test/A\n";
        dic_file.close();
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    write_dummy_files();

    std::string word(reinterpret_cast<const char*>(Data), Size);

    std::string c_word = word;
    for (auto &c : c_word) {
        if (c == '\0') c = ' ';
    }

    try {
        Hunspell hunspell("./dummy.aff", "./dummy.dic");

        std::vector<std::string> result = hunspell.analyze(word);
        (void)result;

        char **slst = nullptr;
        int count = hunspell.analyze(&slst, c_word.c_str());
        if (slst != nullptr) {
            for (int i = 0; i < count; i++) {
                if (slst[i]) {
                    free(slst[i]);
                }
            }
            free(slst);
        }

        std::vector<std::string> empty_result = hunspell.analyze(std::string());
        (void)empty_result;

        char **slst2 = nullptr;
        int count2 = hunspell.analyze(&slst2, "");
        if (slst2 != nullptr) {
            for (int i = 0; i < count2; i++) {
                if (slst2[i]) {
                    free(slst2[i]);
                }
            }
            free(slst2);
        }

    } catch (...) {

    }

    return 0;
}