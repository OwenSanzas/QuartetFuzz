#include <cstddef>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <array>

#include <glaze/cbor.hpp>
#include <glaze/glaze.hpp>

struct CborNested
{
   int n{};
   std::string s{};
   std::vector<double> vals{};
};

struct CborAlt
{
   int a{};
   std::string b{};
};

struct CborMain
{
   int i{};
   double d{};
   std::string str{};
   std::array<uint64_t, 4> arr{};
   std::optional<std::string> opt{};
   std::map<std::string, int> m{};
   std::vector<CborNested> vec{};
   std::variant<int, std::string, double, CborAlt> var{};
   std::chrono::system_clock::time_point tp{};
   glz::epoch_seconds epoch{};
};

struct CborAltMap
{
   std::map<std::string, std::vector<int>> mm{};
};

struct CborAltVec
{
   std::vector<CborNested> v{};
};

struct CborAltVariant
{
   std::variant<int, std::string, CborNested> var{};
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
   if (size < 2) {
      return 0;
   }

   const uint8_t selector = data[size - 1];

   const std::string buffer(reinterpret_cast<const char*>(data), size);

   CborMain main_value{};
   auto ec = glz::read_cbor(main_value, buffer);

   if (!ec) {

      std::string cbor_out{};
      [[maybe_unused]] auto write_ec = glz::write_cbor(main_value, cbor_out);

      std::string json_out{};
      [[maybe_unused]] auto json_ec = glz::write_json(main_value, json_out);
   }
   else {

      [[maybe_unused]] auto msg1 = glz::format_error(ec);
      [[maybe_unused]] auto msg2 = glz::format_error(ec, buffer);
   }

   {
      auto restored = glz::read_cbor<CborMain>(buffer);
      if (restored) {
         std::string json_out{};
         [[maybe_unused]] auto json_ec = glz::write_json(restored.value(), json_out);
      }
      else {
         [[maybe_unused]] auto msg = glz::format_error(restored, buffer);
      }
   }

   {
      glz::generic generic_value{};
      auto gec = glz::read_cbor(generic_value, buffer);
      if (!gec) {
         std::string json_out{};
         [[maybe_unused]] auto json_ec = glz::write_json(generic_value, json_out);
         [[maybe_unused]] bool is_arr = glz::is_array(generic_value);
         [[maybe_unused]] bool is_obj = glz::is_object(generic_value);
      }
      else {
         [[maybe_unused]] auto msg = glz::format_error(gec, buffer);
      }
   }

   {
      std::vector<char> mutable_buffer(data, data + size);
      mutable_buffer.push_back('\0');

      std::vector<char> logical_buffer(mutable_buffer.begin(), mutable_buffer.begin() + static_cast<long>(size));
      CborMain main_value2{};
      auto ec2 = glz::read_cbor(main_value2, logical_buffer);
      if (!ec2) {
         std::string json_out{};
         [[maybe_unused]] auto json_ec = glz::write_json(main_value2, json_out);
      }
   }

   switch (selector % 4) {
   case 0: {
      CborAltMap alt{};
      auto aec = glz::read_cbor(alt, buffer);
      if (!aec) {
         std::string json_out{};
         [[maybe_unused]] auto json_ec = glz::write_json(alt, json_out);
      }
      break;
   }
   case 1: {
      CborAltVec alt{};
      auto aec = glz::read_cbor(alt, buffer);
      if (!aec) {
         std::string json_out{};
         [[maybe_unused]] auto json_ec = glz::write_json(alt, json_out);
      }
      break;
   }
   case 2: {
      CborAltVariant alt{};
      auto aec = glz::read_cbor(alt, buffer);
      if (!aec) {
         std::string json_out{};
         [[maybe_unused]] auto json_ec = glz::write_json(alt, json_out);
      }
      break;
   }
   case 3: {

      if (!ec) {
         std::string out{};
         auto wec = glz::write_cbor(main_value, out);
         if (!wec) {
            auto restored = glz::read_cbor<CborMain>(out);
            if (restored) {
               std::string json_out{};
               [[maybe_unused]] auto json_ec = glz::write_json(restored.value(), json_out);
            }
         }
      }
      break;
   }
   }

   return 0;
}
