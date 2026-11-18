#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <glaze/glaze.hpp>

namespace {
struct opts_no_array_newlines : glz::opts {
   bool new_lines_in_arrays = false;
};
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
   if (size < 1) return 0;

   const uint8_t selector = data[0];
   const uint8_t* json_data = data + 1;
   const size_t json_size = size - 1;

   std::string in(reinterpret_cast<const char*>(json_data), json_size);

   std::string pretty = glz::prettify_json(in);

   {
      glz::generic json{};
      auto ec = glz::read_json(json, pretty);
      if (!ec) {
         [[maybe_unused]] auto s = json.size();
      }
   }

   {
      std::string minified = glz::minify_json(pretty);
      [[maybe_unused]] auto s = minified.size();
   }

   switch (selector % 4) {
      case 0: {

         std::string prettyc = glz::prettify_jsonc(in);
         std::string minifiedc = glz::minify_jsonc(prettyc);
         [[maybe_unused]] auto s = minifiedc.size();
         break;
      }
      case 1: {

         std::vector<char> vbuf(in.begin(), in.end());
         static constexpr glz::opts vopts{.null_terminated = false};
         std::string pretty2 = glz::prettify_json<vopts>(vbuf);
         [[maybe_unused]] auto s = pretty2.size();
         break;
      }
      case 2: {

         std::string pretty3 = glz::prettify_json<opts_no_array_newlines{}>(in);
         [[maybe_unused]] auto s = pretty3.size();
         break;
      }
      case 3: {

         std::string out;
         glz::prettify_json(in, out);
         [[maybe_unused]] auto s = out.size();
         break;
      }
   }

   return 0;
}
