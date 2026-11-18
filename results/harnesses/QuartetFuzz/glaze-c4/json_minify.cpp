#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <glaze/glaze.hpp>

using minify_json_fn_t = std::string (*)(std::vector<char>&);
static minify_json_fn_t volatile g_minify_json =
   &glz::minify_json<glz::opts{}, std::vector<char>>;

inline constexpr glz::opts non_null_terminated_opts{.null_terminated = false};
static minify_json_fn_t volatile g_minify_json_nnt =
   &glz::minify_json<non_null_terminated_opts, std::vector<char>>;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
   if (size == 0) {
      return 0;
   }

   const uint8_t selector = data[0];
   const uint8_t* payload = data + 1;
   const size_t payload_size = size - 1;

   std::vector<char> buffer(payload, payload + payload_size);

   std::string minified = (selector & 2u) ? g_minify_json_nnt(buffer) : g_minify_json(buffer);

   {
      glz::generic json{};
      auto ec = glz::read_json(json, minified);
      if (!ec) {
         [[maybe_unused]] auto s = json.size();
      }
   }
   {
      glz::generic json{};
      auto ec = glz::read<glz::opts{.minified = true}>(json, minified);
      if (!ec) {
         [[maybe_unused]] auto s = json.size();
      }
   }

   {
      std::vector<char> buffer_for_out(payload, payload + payload_size);
      std::string out;
      glz::minify_json(buffer_for_out, out);
      glz::generic json3{};
      auto ec3 = glz::read_json(json3, out);
      if (!ec3) {
         [[maybe_unused]] auto s3 = json3.size();
      }
   }

   if (selector & 1u) {
      std::vector<char> buffer2(payload, payload + payload_size);
      std::string minified_c = glz::minify_jsonc(buffer2);

      glz::generic json2{};
      auto ec2 = glz::read_jsonc(json2, minified_c);
      if (!ec2) {
         [[maybe_unused]] auto s2 = json2.size();
      }
   }

   return 0;
}
