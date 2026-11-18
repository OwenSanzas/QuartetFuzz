#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <glaze/glaze.hpp>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>
#include <fuzzer/FuzzedDataProvider.h>

struct opts_comments : glz::opts
{
   bool comments = true;
};

struct opts_no_array_newlines : glz::opts
{
   bool new_lines_in_arrays = false;
};

struct opts_tabs : glz::opts
{
   char indentation_char = '\t';
   uint8_t indentation_width = 1;
};

struct opts_comments_no_array_newlines : glz::opts
{
   bool comments = true;
   bool new_lines_in_arrays = false;
};

struct opts_tabs_no_array_newlines : glz::opts
{
   char indentation_char = '\t';
   uint8_t indentation_width = 2;
   bool new_lines_in_arrays = false;
};

struct opts_padded : glz::opts
{
   uint32_t internal = uint32_t(glz::opts_internal::is_padded);
};

struct safely_padded_buffer
{
   static constexpr size_t kExtraPadding = 16;
   std::vector<char> storage;
   size_t logical_size{};

   const char* data() const { return storage.data(); }
   size_t size() const { return logical_size; }
   bool empty() const { return logical_size == 0; }
};

static safely_padded_buffer make_padded_buffer(const std::string& content)
{
   safely_padded_buffer pb;
   pb.logical_size = content.size();
   pb.storage.assign(pb.logical_size + safely_padded_buffer::kExtraPadding, '\0');
   std::copy(content.begin(), content.end(), pb.storage.begin());
   return pb;
}

using PrettifyVecFn = std::string (*)(const std::vector<char>&);
using PrettifyStrFn = std::string (*)(const std::string&);

static volatile PrettifyVecFn g_prettify_vec_fn =
    &glz::prettify_json<glz::opts{}, std::vector<char>>;
static volatile PrettifyStrFn g_prettify_str_fn =
    &glz::prettify_json<glz::opts{}, std::string>;

static volatile PrettifyVecFn g_prettify_vec_comments_fn =
    &glz::prettify_json<opts_comments{}, std::vector<char>>;
static volatile PrettifyVecFn g_prettify_vec_no_arr_nl_fn =
    &glz::prettify_json<opts_no_array_newlines{}, std::vector<char>>;
static volatile PrettifyVecFn g_prettify_vec_tabs_fn =
    &glz::prettify_json<opts_tabs{}, std::vector<char>>;
static volatile PrettifyVecFn g_prettify_vec_comments_no_arr_nl_fn =
    &glz::prettify_json<opts_comments_no_array_newlines{}, std::vector<char>>;
static volatile PrettifyVecFn g_prettify_vec_tabs_no_arr_nl_fn =
    &glz::prettify_json<opts_tabs_no_array_newlines{}, std::vector<char>>;

static volatile PrettifyStrFn g_prettify_str_comments_fn =
    &glz::prettify_json<opts_comments{}, std::string>;
static volatile PrettifyStrFn g_prettify_str_no_arr_nl_fn =
    &glz::prettify_json<opts_no_array_newlines{}, std::string>;
static volatile PrettifyStrFn g_prettify_str_tabs_fn =
    &glz::prettify_json<opts_tabs{}, std::string>;
static volatile PrettifyStrFn g_prettify_str_comments_no_arr_nl_fn =
    &glz::prettify_json<opts_comments_no_array_newlines{}, std::string>;
static volatile PrettifyStrFn g_prettify_str_tabs_no_arr_nl_fn =
    &glz::prettify_json<opts_tabs_no_array_newlines{}, std::string>;

static std::string build_structured_json(FuzzedDataProvider& fdp)
{
   std::string out;

   const uint8_t shape = fdp.ConsumeIntegralInRange<uint8_t>(0, 8);

   switch (shape) {
   case 0: {

      out += fdp.ConsumeBool() ? "[]" : "{}";
      out += fdp.ConsumeBool() ? R"(,"a":[1,[],{},2])" : R"(,"a":{"b":{},"c":[]})";
      break;
   }
   case 1: {

      out += fdp.ConsumeBool() ? "]" : "}";
      break;
   }
   case 2: {

      out += fdp.ConsumeBool() ? "{]" : "[}";
      out += R"(,"a":1)";
      break;
   }
   case 3: {

      std::string backslashes(fdp.ConsumeIntegralInRange<size_t>(0, 5), '\\');
      out += R"({"a":")" + backslashes + R"("})";
      break;
   }
   case 4: {

      static constexpr size_t boundary_lengths[] = {1, 6, 7, 8, 9, 14, 15, 16, 17, 19, 20};
      const size_t idx =
          fdp.ConsumeIntegralInRange<size_t>(0, std::size(boundary_lengths) - 1);
      const size_t n = boundary_lengths[idx];
      out += R"({"a":")" + std::string(n, 'x') + R"("})";
      break;
   }
   case 5: {

      static constexpr size_t boundary_lengths[] = {1, 6, 7, 8, 9, 14, 15, 16, 17, 19, 20};
      const size_t idx =
          fdp.ConsumeIntegralInRange<size_t>(0, std::size(boundary_lengths) - 1);
      const size_t n = boundary_lengths[idx];
      out += "/*" + std::string(n, 'y');
      if (fdp.ConsumeBool()) out += "*/";
      out += R"({"a":1})";
      break;
   }
   case 6: {

      static constexpr int boundary_depths[] = {0,  1,   63,  64,  65,
                                                 127, 128, 129, 255, 256, 257, 300};
      const int depth =
          fdp.ConsumeBool()
              ? boundary_depths[fdp.ConsumeIntegralInRange<size_t>(
                    0, std::size(boundary_depths) - 1)]
              : fdp.ConsumeIntegralInRange<int>(0, 300);
      for (int i = 0; i < depth; ++i) {
         out.push_back(fdp.ConsumeBool() ? '{' : '[');
      }

      out += R"({"a":1,"b":2,"arr":[1,2,3],"nested":{"x":true,"y":false,"z":null}})";

      if (fdp.ConsumeBool()) {
         out += "/*";
         out += fdp.ConsumeRandomLengthString(16);
         if (fdp.ConsumeBool()) out += "*/";
      }

      std::vector<char> rest = fdp.ConsumeRemainingBytes<char>();
      out.insert(out.end(), rest.begin(), rest.end());
      break;
   }
   case 7: {

      static constexpr int boundary_depths[] = {63, 64, 65, 127, 128, 129, 255, 256, 257};
      const int depth =
          boundary_depths[fdp.ConsumeIntegralInRange<size_t>(0, std::size(boundary_depths) - 1)];
      for (int i = 0; i < depth; ++i) {
         out.push_back(fdp.ConsumeBool() ? '{' : '[');
      }
      std::string backslashes(fdp.ConsumeIntegralInRange<size_t>(0, 5), '\\');
      out += R"({"a":")" + backslashes + R"("})";
      break;
   }
   default: {

      std::string prefix = fdp.ConsumeRandomLengthString(8);
      out += prefix;
      out.push_back(fdp.ConsumeBool() ? '[' : '{');
      break;
   }
   }

   return out;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size)
{

   const uint8_t combo_mask = Size > 0 ? Data[0] : 0;

   std::vector<char> buffer{Data, Data + Size};

   [[maybe_unused]] std::string pretty_direct = glz::prettify_json(buffer);

   PrettifyVecFn vec_fn = g_prettify_vec_fn;
   [[maybe_unused]] std::string pretty_via_ptr = vec_fn(buffer);

   buffer.push_back('\0');
   [[maybe_unused]] std::string pretty_terminated = glz::prettify_json(buffer);
   [[maybe_unused]] std::string pretty_terminated_via_ptr = vec_fn(buffer);

   std::string str_input(reinterpret_cast<const char*>(Data), Size);
   [[maybe_unused]] std::string pretty_from_string = glz::prettify_json(str_input);

   PrettifyStrFn str_fn = g_prettify_str_fn;
   [[maybe_unused]] std::string pretty_from_string_via_ptr = str_fn(str_input);

   std::string_view sv_input(reinterpret_cast<const char*>(Data), Size);
   [[maybe_unused]] std::string pretty_from_sv = glz::prettify_json(sv_input);

   {
      std::array<char, 16> arr_buf{};
      const size_t n = Size < arr_buf.size() ? Size : arr_buf.size();
      if (n > 0) {
         std::memcpy(arr_buf.data(), Data, n);
      }
      [[maybe_unused]] std::string pretty_from_array = glz::prettify_json(arr_buf);
   }

   {
      std::array<char, 0> empty_arr_buf{};
      [[maybe_unused]] std::string pretty_empty_array = glz::prettify_json(empty_arr_buf);
   }

   if (combo_mask & 0x01) {

      [[maybe_unused]] std::string p1 = glz::prettify_json<opts_comments{}>(buffer);
      [[maybe_unused]] std::string p2 = glz::prettify_json<opts_comments{}>(str_input);
      PrettifyVecFn fn = g_prettify_vec_comments_fn;
      [[maybe_unused]] std::string p1p = fn(buffer);
      PrettifyStrFn fn2 = g_prettify_str_comments_fn;
      [[maybe_unused]] std::string p2p = fn2(str_input);
   }

   if (combo_mask & 0x02) {

      [[maybe_unused]] std::string p3 = glz::prettify_json<opts_no_array_newlines{}>(buffer);
      [[maybe_unused]] std::string p4 = glz::prettify_json<opts_no_array_newlines{}>(str_input);
      PrettifyVecFn fn = g_prettify_vec_no_arr_nl_fn;
      [[maybe_unused]] std::string p3p = fn(buffer);
      PrettifyStrFn fn2 = g_prettify_str_no_arr_nl_fn;
      [[maybe_unused]] std::string p4p = fn2(str_input);
   }

   if (combo_mask & 0x04) {

      [[maybe_unused]] std::string p5 = glz::prettify_json<opts_tabs{}>(buffer);
      [[maybe_unused]] std::string p6 = glz::prettify_json<opts_tabs{}>(str_input);
      PrettifyVecFn fn = g_prettify_vec_tabs_fn;
      [[maybe_unused]] std::string p5p = fn(buffer);
      PrettifyStrFn fn2 = g_prettify_str_tabs_fn;
      [[maybe_unused]] std::string p6p = fn2(str_input);
   }

   if (combo_mask & 0x08) {

      [[maybe_unused]] std::string p7 =
          glz::prettify_json<opts_comments_no_array_newlines{}>(buffer);
      [[maybe_unused]] std::string p8 =
          glz::prettify_json<opts_comments_no_array_newlines{}>(str_input);
      PrettifyVecFn fn = g_prettify_vec_comments_no_arr_nl_fn;
      [[maybe_unused]] std::string p7p = fn(buffer);
      PrettifyStrFn fn2 = g_prettify_str_comments_no_arr_nl_fn;
      [[maybe_unused]] std::string p8p = fn2(str_input);
   }

   if (combo_mask & 0x10) {

      [[maybe_unused]] std::string p9 =
          glz::prettify_json<opts_tabs_no_array_newlines{}>(buffer);
      [[maybe_unused]] std::string p10 =
          glz::prettify_json<opts_tabs_no_array_newlines{}>(str_input);
      PrettifyVecFn fn = g_prettify_vec_tabs_no_arr_nl_fn;
      [[maybe_unused]] std::string p9p = fn(buffer);
      PrettifyStrFn fn2 = g_prettify_str_tabs_no_arr_nl_fn;
      [[maybe_unused]] std::string p10p = fn2(str_input);
   }

   FuzzedDataProvider fdp(Data, Size);
   std::string structured = build_structured_json(fdp);

   std::vector<char> sbuf(structured.begin(), structured.end());

   std::vector<char> sbuf_notnul = sbuf;

   [[maybe_unused]] std::string r1 = glz::prettify_json(sbuf_notnul);
   [[maybe_unused]] std::string r1_comments = glz::prettify_json<opts_comments{}>(sbuf_notnul);
   [[maybe_unused]] std::string r1_tabs = glz::prettify_json<opts_tabs{}>(sbuf_notnul);
   sbuf.push_back('\0');
   [[maybe_unused]] std::string r2 = glz::prettify_json(sbuf);

   [[maybe_unused]] std::string r3 = glz::prettify_json(structured);

   [[maybe_unused]] std::string r4 = glz::prettify_json<opts_comments{}>(structured);
   [[maybe_unused]] std::string r5 = glz::prettify_json<opts_no_array_newlines{}>(structured);
   [[maybe_unused]] std::string r6 = glz::prettify_json<opts_tabs{}>(structured);
   [[maybe_unused]] std::string r7 =
       glz::prettify_json<opts_comments_no_array_newlines{}>(structured);
   [[maybe_unused]] std::string r8 =
       glz::prettify_json<opts_tabs_no_array_newlines{}>(structured);

   [[maybe_unused]] std::string r9 = glz::prettify_json<opts_comments{}>(sbuf);
   [[maybe_unused]] std::string r10 = glz::prettify_json<opts_no_array_newlines{}>(sbuf);
   [[maybe_unused]] std::string r11 = glz::prettify_json<opts_tabs{}>(sbuf);
   [[maybe_unused]] std::string r12 =
       glz::prettify_json<opts_comments_no_array_newlines{}>(sbuf);
   [[maybe_unused]] std::string r13 =
       glz::prettify_json<opts_tabs_no_array_newlines{}>(sbuf);

   {
      std::array<char, 16> arr_buf2{};
      const size_t n2 = structured.size() < arr_buf2.size() ? structured.size() : arr_buf2.size();
      if (n2 > 0) {
         std::memcpy(arr_buf2.data(), structured.data(), n2);
      }
      [[maybe_unused]] std::string pretty_from_array2 = glz::prettify_json(arr_buf2);
      [[maybe_unused]] std::string pretty_from_array2_tabs =
          glz::prettify_json<opts_tabs{}>(arr_buf2);
      [[maybe_unused]] std::string pretty_from_array2_comments =
          glz::prettify_json<opts_comments{}>(arr_buf2);
   }

   std::string_view sv_structured(structured);
   [[maybe_unused]] std::string pretty_from_sv2 = glz::prettify_json(sv_structured);
   [[maybe_unused]] std::string pretty_from_sv2_comments =
       glz::prettify_json<opts_comments{}>(sv_structured);
   [[maybe_unused]] std::string pretty_from_sv2_tabs =
       glz::prettify_json<opts_tabs{}>(sv_structured);
   [[maybe_unused]] std::string pretty_from_sv2_no_arr_nl =
       glz::prettify_json<opts_no_array_newlines{}>(sv_structured);

   {
      safely_padded_buffer pb_structured = make_padded_buffer(structured);
      [[maybe_unused]] std::string rp1 = glz::prettify_json<opts_padded{}>(pb_structured);

      safely_padded_buffer pb_raw = make_padded_buffer(str_input);
      [[maybe_unused]] std::string rp2 = glz::prettify_json<opts_padded{}>(pb_raw);
   }

   return 0;
}