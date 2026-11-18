#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "glaze/glaze.hpp"
#include "glaze/json/jmespath.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
   FuzzedDataProvider stream(data, size);

   const uint8_t path_selector = stream.ConsumeIntegral<uint8_t>();

   std::string buffer = stream.ConsumeRemainingBytesAsString();

   glz::generic value{};
   glz::error_ctx ec{};

   switch (path_selector % 8) {
      case 0: {
         ec = glz::read_jmespath<"family.father.first_name">(value, buffer);
         break;
      }
      case 1: {
         ec = glz::read_jmespath<"family.mother.last_name">(value, buffer);
         break;
      }
      case 2: {
         ec = glz::read_jmespath<"family.father.age">(value, buffer);
         break;
      }
      case 3: {
         ec = glz::read_jmespath<"address">(value, buffer);
         break;
      }
      case 4: {
         ec = glz::read_jmespath<"family.children[0]">(value, buffer);
         break;
      }
      case 5: {
         ec = glz::read_jmespath<"[0:5]">(value, buffer);
         break;
      }
      case 6: {
         ec = glz::read_jmespath<"[::-1]">(value, buffer);
         break;
      }
      default: {
         ec = glz::read_jmespath<"a.b.c[1][2]">(value, buffer);
         break;
      }
   }

   if (!ec) {

      [[maybe_unused]] auto s = value.size();
   }

   return 0;
}