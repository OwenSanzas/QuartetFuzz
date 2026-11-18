#include <fuzzer/FuzzedDataProvider.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "flatbuffers/idl.h"
#include "test_init.h"

OneTimeTestInit OneTimeTestInit::one_time_init_;

namespace {

constexpr size_t kMaxInputLength = 16384;

const char* kSchema =
    "enum Color:byte { Red = 0, Green, Blue }\n"
    "struct Vec3 (force_align: 8) {\n"
    "  x:float;\n"
    "  y:float;\n"
    "  z:float;\n"
    "  arr:[int:3];\n"
    "}\n"
    "struct Ability {\n"
    "  id:uint (key);\n"
    "  distance:uint;\n"
    "}\n"
    "table Weapon {\n"
    "  name:string;\n"
    "  damage:short;\n"
    "}\n"
    "table Stat {\n"
    "  id:string;\n"
    "  val:long;\n"
    "  count:ushort (key);\n"
    "}\n"
    "union Any { Monster, Weapon, Vec3, TextUnion:string }\n"
    "table Monster {\n"
    "  pos:Vec3;\n"
    "  mana:short = 150;\n"
    "  hp:short = 100;\n"
    "  name:string (key);\n"
    "  inventory:[ubyte];\n"
    "  color:Color = Blue;\n"
    "  tags:[string];\n"
    "  testbool:bool;\n"
    "  testf:float;\n"
    "  testi:int;\n"
    "  testd:double;\n"
    "  testlong:long;\n"
    "  testulong:ulong;\n"
    "  testarrayoftables:[Monster];\n"
    "  testarrayofsortedstruct:[Ability];\n"
    "  scalar_key_sorted_tables:[Stat];\n"
    "  test_union:Any;\n"
    "  test_union_vec:[Any];\n"
    "  testhash:int (hash:\"fnv1a_32\");\n"
    "  testhashu32:uint (hash:\"fnv1_32\");\n"
    "  testhashlong:long (hash:\"fnv1_64\");\n"
    "  testhashulong:ulong (hash:\"fnv1a_64\");\n"
    "  testnested:[ubyte] (nested_flatbuffer: \"Monster\");\n"
    "  testflex:[ubyte] (flexbuffer);\n"
    "  shared_name:string (shared);\n"
    "  req_name:string (required);\n"
    "  vec64_field:[int] (vector64);\n"
    "  opt_int:int = null;\n"
    "}\n"
    "root_type Monster;\n";

std::string QuoteAlnum(FuzzedDataProvider& stream, size_t max_len = 12) {
  size_t len = stream.ConsumeIntegralInRange<size_t>(0, max_len);
  std::string s = "\"";
  for (size_t i = 0; i < len; ++i) {
    s += static_cast<char>('a' + (stream.ConsumeIntegral<uint8_t>() % 26));
  }
  s += "\"";
  return s;
}

std::string Int32(FuzzedDataProvider& stream) {
  return std::to_string(stream.ConsumeIntegral<int32_t>());
}

std::string UInt32(FuzzedDataProvider& stream) {
  return std::to_string(stream.ConsumeIntegral<uint32_t>());
}

std::string Int16(FuzzedDataProvider& stream) {
  return std::to_string(stream.ConsumeIntegral<int16_t>());
}

std::string UInt16(FuzzedDataProvider& stream) {
  return std::to_string(stream.ConsumeIntegral<uint16_t>());
}

std::string Int64Str(FuzzedDataProvider& stream) {
  return std::to_string(stream.ConsumeIntegral<int64_t>());
}

std::string UInt64Str(FuzzedDataProvider& stream) {
  return std::to_string(stream.ConsumeIntegral<uint64_t>());
}

std::string Flt(FuzzedDataProvider& stream) {
  float f = stream.ConsumeFloatingPoint<float>();
  if (!std::isfinite(f)) f = 0.0f;
  return std::to_string(f);
}

std::string Vec3Json(FuzzedDataProvider& stream) {
  return "{\"x\":" + Flt(stream) + ",\"y\":" + Flt(stream) + ",\"z\":" +
         Flt(stream) + ",\"arr\":[" + Int32(stream) + "," + Int32(stream) +
         "," + Int32(stream) + "]}";
}

std::string WeaponJson(FuzzedDataProvider& stream) {
  return "{\"name\":" + QuoteAlnum(stream) + ",\"damage\":" +
         Int16(stream) + "}";
}

void PickUnionVariant(FuzzedDataProvider& stream, std::string* type_name,
                      std::string* value_json) {
  int choice = stream.ConsumeIntegralInRange<int>(0, 2);
  switch (choice) {
    case 0:
      *type_name = "Weapon";
      *value_json = WeaponJson(stream);
      break;
    case 1:
      *type_name = "Vec3";
      *value_json = Vec3Json(stream);
      break;
    default:
      *type_name = "TextUnion";
      *value_json = QuoteAlnum(stream);
      break;
  }
}

std::string BuildTemplatedJson(FuzzedDataProvider& stream) {
  std::vector<std::string> fields;

  fields.push_back("\"pos\":" + Vec3Json(stream));
  fields.push_back("\"mana\":" + Int16(stream));
  fields.push_back("\"hp\":" + Int16(stream));
  fields.push_back("\"name\":" + QuoteAlnum(stream));
  fields.push_back("\"req_name\":" + QuoteAlnum(stream));
  fields.push_back("\"shared_name\":" + QuoteAlnum(stream));

  {
    int c = stream.ConsumeIntegralInRange<int>(0, 2);
    const char* colors[] = { "Red", "Green", "Blue" };
    fields.push_back(std::string("\"color\":\"") + colors[c] + "\"");
  }

  fields.push_back(std::string("\"testbool\":") +
                   (stream.ConsumeBool() ? "true" : "false"));
  fields.push_back("\"testf\":" + Flt(stream));
  fields.push_back("\"testi\":" + Int32(stream));
  fields.push_back("\"testd\":" + Flt(stream));
  fields.push_back("\"testlong\":" + Int64Str(stream));
  fields.push_back("\"testulong\":" + UInt64Str(stream));

  if (stream.ConsumeBool()) {
    fields.push_back("\"opt_int\":null");
  } else {
    fields.push_back("\"opt_int\":" + Int32(stream));
  }

  fields.push_back("\"testhash\":" + QuoteAlnum(stream));
  fields.push_back("\"testhashu32\":" + QuoteAlnum(stream));
  fields.push_back("\"testhashlong\":" + QuoteAlnum(stream));
  fields.push_back("\"testhashulong\":" + QuoteAlnum(stream));

  fields.push_back("\"inventory\":[1,2,3]");
  fields.push_back("\"tags\":[" + QuoteAlnum(stream) + "," +
                   QuoteAlnum(stream) + "]");
  fields.push_back("\"vec64_field\":[" + Int32(stream) + "," +
                   Int32(stream) + "]");

  {
    int n = stream.ConsumeIntegralInRange<int>(0, 4);
    std::string arr = "\"scalar_key_sorted_tables\":[";
    for (int i = 0; i < n; ++i) {
      if (i) arr += ",";
      arr += "{\"id\":" + QuoteAlnum(stream) + ",\"val\":" +
             Int64Str(stream) + ",\"count\":" + UInt16(stream) + "}";
    }
    arr += "]";
    fields.push_back(arr);
  }

  fields.push_back("\"testarrayoftables\":[]");

  {
    int n = stream.ConsumeIntegralInRange<int>(0, 3);
    std::string arr = "\"testarrayofsortedstruct\":[";
    for (int i = 0; i < n; ++i) {
      if (i) arr += ",";
      arr += "{\"id\":" + UInt32(stream) + ",\"distance\":" +
             UInt32(stream) + "}";
    }
    arr += "]";
    fields.push_back(arr);
  }

  {
    std::string type_name, value_json;
    PickUnionVariant(stream, &type_name, &value_json);
    fields.push_back("\"test_union_type\":\"" + type_name + "\"");
    fields.push_back("\"test_union\":" + value_json);
  }

  {
    int n = stream.ConsumeIntegralInRange<int>(0, 3);
    std::vector<std::string> types, values;
    for (int i = 0; i < n; ++i) {
      std::string t, v;
      PickUnionVariant(stream, &t, &v);
      types.push_back(t);
      values.push_back(v);
    }
    std::string tarr = "\"test_union_vec_type\":[";
    for (size_t i = 0; i < types.size(); ++i) {
      if (i) tarr += ",";
      tarr += "\"" + types[i] + "\"";
    }
    tarr += "]";
    fields.push_back(tarr);

    std::string varr = "\"test_union_vec\":[";
    for (size_t i = 0; i < values.size(); ++i) {
      if (i) varr += ",";
      varr += values[i];
    }
    varr += "]";
    fields.push_back(varr);
  }

  fields.push_back("\"testnested\":[" +
                   std::to_string(stream.ConsumeIntegral<uint8_t>()) + "," +
                   std::to_string(stream.ConsumeIntegral<uint8_t>()) + "]");

  fields.push_back("\"testflex\":{\"k\":" + Int32(stream) + ",\"arr\":[" +
                   Int32(stream) + "," + Int32(stream) + "]}");

  std::string json = "{";
  for (size_t i = 0; i < fields.size(); ++i) {
    if (i) json += ",";
    json += fields[i];
  }
  json += "}";
  return json;
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {

  static flatbuffers::Parser parser;
  static const bool schema_loaded = [] {

    parser.opts.lang_to_generate = flatbuffers::IDLOptions::kCpp;
    const bool ok = parser.Parse(kSchema);
    if (!ok) {

      fprintf(stderr, "FATAL: kSchema failed to parse: %s\n",
              parser.error_.c_str());
      abort();
    }
    return ok;
  }();
  if (!schema_loaded) {

    return 0;
  }

  FuzzedDataProvider stream(data, size);

  const uint8_t flags = stream.ConsumeIntegral<uint8_t>();

  constexpr uint8_t flags_strict_json = 0x80;
  constexpr uint8_t flags_skip_unexpected_fields_in_json = 0x40;
  constexpr uint8_t flags_allow_non_utf8 = 0x20;
  constexpr uint8_t flags_size_prefixed = 0x10;
  constexpr uint8_t flags_require_json_eof = 0x08;
  constexpr uint8_t flags_json_nested_legacy = 0x04;

  flatbuffers::IDLOptions opts;
  opts.strict_json = (flags & flags_strict_json) != 0;
  opts.skip_unexpected_fields_in_json =
      (flags & flags_skip_unexpected_fields_in_json) != 0;
  opts.allow_non_utf8 = (flags & flags_allow_non_utf8) != 0;

  opts.size_prefixed = (flags & flags_size_prefixed) != 0;

  opts.require_json_eof = (flags & flags_require_json_eof) != 0;

  opts.json_nested_legacy_flatbuffers =
      (flags & flags_json_nested_legacy) != 0;

  opts.lang_to_generate = flatbuffers::IDLOptions::kCpp;
  parser.opts = opts;

  const bool use_template = stream.ConsumeBool();
  std::string json;
  if (use_template) {
    json = BuildTemplatedJson(stream);
  } else {

    json = stream.ConsumeRemainingBytesAsString();
  }

  if (json.empty() || json.size() > kMaxInputLength) {
    return 0;
  }

  parser.ParseJson(json.c_str());

  return 0;
}