#include <stddef.h>
#include <stdint.h>

#include <string>

#include "flatbuffers/idl.h"
#include "test_assert.h"
#include "test_init.h"

OneTimeTestInit OneTimeTestInit::one_time_init_;

namespace {

static constexpr size_t kMinInputLength = 1;
static constexpr size_t kMaxInputLength = 16384;

static constexpr uint8_t flags_strict_json = 0x80;
static constexpr uint8_t flags_skip_unexpected_fields_in_json = 0x40;
static constexpr uint8_t flags_allow_non_utf8 = 0x20;
static constexpr uint8_t flags_natural_utf8 = 0x10;
static constexpr uint8_t flags_size_prefixed = 0x08;
static constexpr uint8_t flags_require_json_eof = 0x04;
static constexpr uint8_t flags_output_default_scalars = 0x02;
static constexpr uint8_t flags_json_nested_legacy = 0x01;

static constexpr uint8_t flags2_root_is_weapon = 0x01;
static constexpr uint8_t flags2_try_flexbuffer = 0x02;

static const char* kSchema = R"FBS(
namespace Game;

enum Color:byte { Red = 0, Green, Blue }

struct Vec3 {
  x:float;
  y:float;
  z:float;
}

table Weapon {
  name:string;
  damage:short;
}

union Equipped { Weapon }

table Monster {
  pos:Vec3;
  mana:short = 150;
  hp:short = 100;
  name:string;
  color:Color = Blue;
  inventory:[ubyte];
  friendly:bool = false;
  testarrayoftables:[Monster];
  equipped:Equipped;
  nested:[ubyte] (nested_flatbuffer: "Monster");
  flex:[ubyte] (flexbuffer);
}

root_type Monster;
file_identifier "MONS";
)FBS";

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {

  if (size < 3) return 0;
  const uint8_t flags = data[0];
  const uint8_t flags2 = data[1];
  data += 2;
  size -= 2;

  const std::string original(reinterpret_cast<const char*>(data), size);
  auto input = std::string(original.c_str());
  if (input.size() < kMinInputLength || input.size() > kMaxInputLength)
    return 0;

  flatbuffers::IDLOptions opts;
  opts.strict_json = (flags & flags_strict_json) != 0;
  opts.skip_unexpected_fields_in_json =
      (flags & flags_skip_unexpected_fields_in_json) != 0;
  opts.allow_non_utf8 = (flags & flags_allow_non_utf8) != 0;
  opts.natural_utf8 = (flags & flags_natural_utf8) != 0;
  opts.size_prefixed = (flags & flags_size_prefixed) != 0;
  opts.require_json_eof = (flags & flags_require_json_eof) != 0;
  opts.output_default_scalars_in_json =
      (flags & flags_output_default_scalars) != 0;
  opts.json_nested_legacy_flatbuffers =
      (flags & flags_json_nested_legacy) != 0;

  flatbuffers::Parser parser(opts);

  if (!parser.Parse(kSchema)) {
    return 0;
  }

  if (flags2 & flags2_root_is_weapon) {
    parser.SetRootType("Weapon");
  }

  if (parser.ParseJson(input.c_str())) {

    const uint8_t* buf = parser.builder_.GetBufferPointer();
    const size_t buf_size = parser.builder_.GetSize();

    const flatbuffers::Table* root =
        opts.size_prefixed
            ? flatbuffers::GetSizePrefixedRoot<flatbuffers::Table>(buf)
            : flatbuffers::GetRoot<flatbuffers::Table>(buf);
    const uint8_t* root_buf = reinterpret_cast<const uint8_t*>(root);

    flatbuffers::Verifier verifier(buf, buf_size);
    if (verifier.VerifyTableStart(root_buf) &&
        root->VerifyTableStart(verifier)) {
      std::string jsongen;

      const char* genres = GenText(parser, buf, &jsongen);
      (void)genres;

      if (parser.root_struct_def_) {
        auto& field_defs = parser.root_struct_def_->fields;
        for (auto it = field_defs.vec.begin(); it != field_defs.vec.end();
            ++it) {
          auto* field_def = *it;
          auto offset = field_def->value.offset;
          volatile bool present =
              root->CheckField(static_cast<flatbuffers::voffset_t>(offset));
          (void)present;
        }
      }

      volatile std::ptrdiff_t consumed = parser.BytesConsumed();
      (void)consumed;
    }
  } else {

    volatile size_t errlen = parser.error_.size();
    (void)errlen;
  }

  if (flags2 & flags2_try_flexbuffer) {
    flatbuffers::IDLOptions flex_opts;
    flex_opts.use_flexbuffers = true;
    flatbuffers::Parser flex_parser(flex_opts);
    if (flex_parser.Parse(input.c_str())) {
      volatile size_t flexlen = flex_parser.builder_.GetSize();
      (void)flexlen;
    }
  }

  return 0;
}
