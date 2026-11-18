#include <hunspell/hunspell.hxx>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {

  if (size < 3)
    return 0;

  size_t wordlen = data[0];
  size_t word2len = data[1];
  uint8_t ctrl = data[2];
  data += 3;
  size -= 3;

  if (wordlen > size)
    wordlen = size;
  std::string word(reinterpret_cast<const char*>(data), wordlen);
  data += wordlen;
  size -= wordlen;

  if (word2len > size)
    word2len = size;
  std::string word2(reinterpret_cast<const char*>(data), word2len);
  data += word2len;
  size -= word2len;

  size_t affsize = size / 2;
  size_t dicsize = size - affsize;

  {
    std::ofstream aff("/tmp/morphfuzzer.aff",
                       std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
    aff.write(reinterpret_cast<const char*>(data), affsize);
  }
  {
    std::ofstream dic("/tmp/morphfuzzer.dic",
                       std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
    dic.write(reinterpret_cast<const char*>(data + affsize), dicsize);
  }

  Hunspell dict("/tmp/morphfuzzer.aff", "/tmp/morphfuzzer.dic");

  if (ctrl & 0x80)
    dict.add_dic("/tmp/morphfuzzer.dic");

  std::vector<std::string> analysis = dict.analyze(word);

  std::vector<std::string> stems_from_morph = dict.stem(analysis);

  std::vector<std::string> stems = dict.stem(word);

  std::vector<std::string> word2_analysis = dict.analyze(word2);
  std::vector<std::string> generated = dict.generate(word, word2);
  std::vector<std::string> generated_from_desc = dict.generate(word, word2_analysis);

  volatile size_t checksum = 0;
  const std::vector<std::string>* result_sets[] = {
      &analysis, &stems_from_morph, &stems, &word2_analysis, &generated,
      &generated_from_desc};
  for (const auto* v : result_sets) {
    for (const auto& s : *v) {
      for (char c : s)
        checksum += static_cast<unsigned char>(c);
    }
  }
  (void)checksum;

  return 0;
}

