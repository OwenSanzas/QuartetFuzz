#include <array>
#include <bitset>
#include <chrono>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <expected>
#include <filesystem>
#include <glaze/glaze.hpp>
#include <glaze/beve.hpp>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

struct nested
{
   int n = 0;
   std::string label{};
   std::vector<double> data{};
};

struct my_struct
{
   int i = 287;
   double d = 3.14;
   std::string hello = "Hello World";
   std::array<uint64_t, 3> arr = {1, 2, 3};
   std::optional<std::string> maybe{};
   std::map<std::string, int> dict{};
   std::vector<nested> kids{};
   std::variant<int, std::string, double> alt{};
};

enum class color : int32_t { red = 0, green = 1, blue = 2, mystery = 12345 };

struct small_obj
{
   int x = 7;
   std::string y = "obj";
};

struct rich_struct
{
   bool flag = false;
   char c = 'x';
   int8_t i8 = 0;
   uint16_t u16 = 0;
   int64_t i64 = 0;
   double d = 3.14;
   float f = 1.0f;
   color col = color::red;

   std::string hello = "Hello World";
   std::array<uint64_t, 3> arr = {1, 2, 3};
   std::vector<nested> kids{};
   std::deque<int> dq{};
   std::list<int> lst{};
   std::set<int> st{};
   std::unordered_map<std::string, int> umap{};
   std::map<std::string, int> dict{};
   std::pair<std::string, int> pr{"k", 1};
   std::tuple<int, double, std::string> tup{1, 2.0, "t"};

   std::optional<std::string> maybe{};
   std::unique_ptr<int> uptr{};
   std::shared_ptr<double> sptr{};

   std::variant<std::monostate, int, std::string, double, small_obj> alt{};

   std::complex<double> cplx{1.0, -1.0};
};

struct wrapped_id
{
   uint64_t value{};
};
template <>
struct glz::meta<wrapped_id>
{
   static constexpr auto value = &wrapped_id::value;
};

struct vec3
{
   double x{1};
   double y{2};
   double z{3};
};
template <>
struct glz::meta<vec3>
{
   static constexpr std::string_view name = "vec3";
   static constexpr auto value = glz::array(&vec3::x, &vec3::y, &vec3::z);
};

struct flags_t
{
   bool a{};
   bool b{};
   bool c{};
};
template <>
struct glz::meta<flags_t>
{
   using T = flags_t;
   static constexpr auto value = glz::flags("a", &T::a, "b", &T::b, "c", &T::c);
};

struct extra_struct
{
   std::bitset<16> bits{};
   std::chrono::milliseconds dur{};
   std::chrono::steady_clock::time_point steady_tp{};
   std::expected<int, std::string> exp{};
   glz::raw_json raw{};
   glz::text txt{};
   vec3 v3{};
   wrapped_id wid{};
   flags_t flg{};
};

struct OptionA
{
   std::string tag{};
   int a{};
   std::string extra{"ea"};
};
struct OptionB
{
   std::string tag{};
   int a{};
   double extra{1.0};
};

using untagged_multi_obj_variant = std::variant<OptionA, OptionB, int, std::monostate>;

using tagged_variant = std::variant<OptionA, OptionB>;
template <>
struct glz::meta<tagged_variant>
{
   static constexpr std::string_view tag = "tag";
   static constexpr std::array<std::string_view, 2> ids{"A", "B"};
};

struct adj_payload_a
{
   int x{};
};
struct adj_payload_b
{
   std::string y{};
};
using adjacent_variant = std::variant<adj_payload_a, adj_payload_b>;
template <>
struct glz::meta<adjacent_variant>
{
   static constexpr std::string_view tag = "type";
   static constexpr std::string_view content = "data";
   static constexpr std::array<std::string_view, 2> ids{"a", "b"};
};

struct variant_struct
{
   untagged_multi_obj_variant u_alt{};
   tagged_variant t_alt{OptionA{}};
   adjacent_variant adj_alt{adj_payload_a{}};
   std::map<int, int> int_keyed_map{};
   std::unordered_map<int, std::string> int_keyed_umap{};
   std::filesystem::path path{};

   std::vector<std::pair<std::string, int>> kv_pairs{};
};

struct aligned_opts : glz::opts
{
   bool aligned_arrays = true;
};
inline constexpr aligned_opts aligned_beve_opts{glz::opts{.format = glz::BEVE}};

struct opts_saa : glz::opts
{
   bool structs_as_arrays = true;
};
inline constexpr opts_saa beve_saa_opts{glz::opts{.format = glz::BEVE}};

struct opts_limits : glz::opts
{
   bool skip_null_members_on_read = true;
   bool shrink_to_fit = true;
   size_t max_string_length = 8;
   size_t max_array_size = 8;
   size_t max_map_size = 8;
};
inline constexpr opts_limits beve_limits_opts{glz::opts{.format = glz::BEVE}};

inline constexpr glz::opts beve_strict_opts{
   .format = glz::BEVE, .error_on_unknown_keys = false, .error_on_missing_keys = true, .partial_read = true};

struct hidden_writer
{
   int x = 0;
   std::string secret = "s";
};

struct hidden_reader
{
   int x = 0;
   std::string secret{};
};
template <>
struct glz::meta<hidden_reader>
{
   using T = hidden_reader;
   static constexpr auto value = glz::object(&T::x, "secret", glz::hide{&T::secret});
};

struct includer_struct
{
   std::string str = "Hello";
   int i = 55;
};
template <>
struct glz::meta<includer_struct>
{
   using T = includer_struct;
   static constexpr auto value = glz::object("include", glz::file_include{}, "str", &T::str, "i", &T::i);
};

template <class T>
struct value_only_optional
{
   bool present{};
   T val{};
   bool has_value() const { return present; }
   T& value() { return val; }
   const T& value() const { return val; }
   void emplace()
   {
      present = true;
      val = T{};
   }
};

struct nvt_struct
{
   value_only_optional<int> v{};
};

namespace
{
   struct cursor
   {
      const uint8_t* p;
      size_t n;
      size_t i = 0;
      uint8_t next() { return i < n ? p[i++] : 0; }
   };

   uint16_t next16(cursor& c)
   {
      const uint16_t lo = c.next();
      const uint16_t hi = c.next();
      return static_cast<uint16_t>(lo | (hi << 8));
   }

   rich_struct make_seed(cursor& c)
   {
      rich_struct v{};
      v.flag = c.next() & 1;
      v.c = static_cast<char>(c.next());
      v.i8 = static_cast<int8_t>(c.next());
      v.u16 = static_cast<uint16_t>(c.next()) | (static_cast<uint16_t>(c.next()) << 8);
      v.i64 = static_cast<int64_t>(c.next());
      v.d = 3.14 * (c.next() % 7);
      v.f = 1.5f * (c.next() % 5);
      v.col = static_cast<color>(c.next() % 4);

      v.hello.assign(1 + (next16(c) % 64), static_cast<char>('a' + (c.next() % 26)));

      v.kids.resize(next16(c) % 70);
      for (auto& k : v.kids) {
         k.n = c.next();
         k.label = "k";
         k.data = {1.0, 2.0};
      }
      v.dq = {1, 2, static_cast<int>(c.next())};
      v.lst = {static_cast<int>(c.next())};
      v.st = {1, 2, static_cast<int>(c.next())};
      v.umap["a"] = c.next();
      v.dict["b"] = c.next();
      v.pr = {"key", static_cast<int>(c.next())};
      v.tup = {static_cast<int>(c.next()), 1.0 * c.next(), "tup"};

      if (c.next() & 1) v.maybe = "present";
      if (c.next() & 1) v.uptr = std::make_unique<int>(c.next());
      if (c.next() & 1) v.sptr = std::make_shared<double>(c.next());

      switch (c.next() % 5) {
      case 0:
         v.alt = std::monostate{};
         break;
      case 1:
         v.alt = static_cast<int>(c.next());
         break;
      case 2:
         v.alt = std::string("alt");
         break;
      case 3:
         v.alt = 2.5 * c.next();
         break;
      default:
         v.alt = small_obj{static_cast<int>(c.next()), "o"};
         break;
      }

      v.cplx = {1.0 * c.next(), -1.0 * c.next()};
      return v;
   }

   extra_struct make_extra_seed(cursor& c)
   {
      extra_struct v{};
      uint16_t bits_val = static_cast<uint16_t>(c.next()) | (static_cast<uint16_t>(c.next()) << 8);
      v.bits = std::bitset<16>(bits_val);
      v.dur = std::chrono::milliseconds(static_cast<int64_t>(c.next()) * 1000 + c.next());
      v.steady_tp =
         std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(static_cast<int64_t>(c.next())));

      if (c.next() & 1) {
         v.exp = static_cast<int>(c.next());
      }
      else {
         v.exp = std::unexpected(std::string("err"));
      }

      v.raw = std::string(R"({"k":)") + std::to_string(c.next()) + "}";
      v.txt.str.assign(1 + (c.next() % 6), static_cast<char>('a' + (c.next() % 26)));

      v.v3.x = 1.0 * c.next();
      v.v3.y = 1.0 * c.next();
      v.v3.z = 1.0 * c.next();

      v.wid.value = c.next();

      v.flg.a = c.next() & 1;
      v.flg.b = c.next() & 1;
      v.flg.c = c.next() & 1;

      return v;
   }

   variant_struct make_variant_seed(cursor& c)
   {
      variant_struct v{};

      switch (c.next() % 4) {
      case 0:
         v.u_alt = OptionA{"", static_cast<int>(c.next()), "ua"};
         break;
      case 1:
         v.u_alt = OptionB{"", static_cast<int>(c.next()), 1.0 * c.next()};
         break;
      case 2:
         v.u_alt = static_cast<int>(c.next());
         break;
      default:
         v.u_alt = std::monostate{};
         break;
      }

      if (c.next() & 1) {
         v.t_alt = OptionA{"", static_cast<int>(c.next()), "ta"};
      }
      else {
         v.t_alt = OptionB{"", static_cast<int>(c.next()), 2.0 * c.next()};
      }

      if (c.next() & 1) {
         v.adj_alt = adj_payload_a{static_cast<int>(c.next())};
      }
      else {
         v.adj_alt = adj_payload_b{std::string("adjv")};
      }

      const auto map_sz = c.next() % 5;
      for (uint8_t k = 0; k < map_sz; ++k) {
         v.int_keyed_map[static_cast<int>(c.next())] = static_cast<int>(c.next());
      }
      const auto umap_sz = c.next() % 5;
      for (uint8_t k = 0; k < umap_sz; ++k) {
         v.int_keyed_umap[static_cast<int>(c.next())] = "v";
      }

      v.path = std::filesystem::path(std::string("p") + std::to_string(c.next()));

      const auto kv_sz = c.next() % 5;
      for (uint8_t k = 0; k < kv_sz; ++k) {
         v.kv_pairs.emplace_back(std::string("k") + std::to_string(c.next()), static_cast<int>(c.next()));
      }

      return v;
   }

   glz::generic make_generic_seed(cursor& c)
   {
      glz::generic g{};
      g["num"] = 1.0 * c.next();
      g["str"] = std::string("gs") + std::to_string(c.next());
      g["flag"] = (c.next() & 1) != 0;
      g["none"] = nullptr;

      glz::generic::array_t arr{};
      const auto arr_len = c.next() % 4;
      for (uint8_t k = 0; k < arr_len; ++k) {
         glz::generic elem{};
         elem = 1.0 * c.next();
         arr.push_back(elem);
      }
      g["arr"] = arr;

      g["nested"] = glz::generic::object_t{};
      g["nested"]["x"] = 1.0 * c.next();
      g["nested"]["y"] = std::string("n");

      return g;
   }

   void mutate_cyclic(std::string& buf, const uint8_t* mutate_bytes, size_t mutate_len)
   {
      if (buf.empty() || mutate_len == 0) return;
      for (size_t k = 0; k < mutate_len; ++k) {
         const size_t idx = k % buf.size();
         buf[idx] ^= static_cast<char>(mutate_bytes[k]);
      }
   }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size)
{

   std::vector<char> buffer{Data, Data + Size};

   const uint8_t heavy_selector = Size > 0 ? static_cast<uint8_t>(Data[0] % 5) : 0;

   {
      my_struct value{};
      using reader_fn_t = glz::error_ctx (*)(my_struct&, std::vector<char>&);
      static volatile reader_fn_t read_beve_typed = &glz::read_beve<my_struct&, std::vector<char>&>;
      reader_fn_t fn = read_beve_typed;
      [[maybe_unused]] glz::error_ctx ec = fn(value, buffer);
   }

   {
      my_struct value{};
      buffer.push_back('\0');
      [[maybe_unused]] glz::error_ctx ec = glz::read_beve(value, buffer);
   }

   {
      glz::generic value{};
      using generic_reader_fn_t = glz::error_ctx (*)(glz::generic&, std::vector<char>&);
      static volatile generic_reader_fn_t read_beve_generic =
          &glz::read_beve<glz::generic&, std::vector<char>&>;
      generic_reader_fn_t fn = read_beve_generic;
      [[maybe_unused]] glz::error_ctx ec = fn(value, buffer);
   }

   {
      rich_struct value{};
      [[maybe_unused]] glz::error_ctx ec = glz::read_beve(value, buffer);
   }

   {
      extra_struct value{};
      [[maybe_unused]] glz::error_ctx ec = glz::read_beve(value, buffer);
   }

   {
      variant_struct value{};
      [[maybe_unused]] glz::error_ctx ec = glz::read_beve(value, buffer);
   }

   {
      std::span<const uint8_t> sp8{};
      [[maybe_unused]] glz::error_ctx ec1 = glz::read_beve(sp8, buffer);
      std::span<const double> spd{};
      [[maybe_unused]] glz::error_ctx ec2 = glz::read_beve(spd, buffer);
   }

   {
      std::vector<std::pair<std::string, int>> kv{};
      [[maybe_unused]] glz::error_ctx ec1 = glz::read_beve(kv, buffer);

      hidden_reader hr{};
      [[maybe_unused]] glz::error_ctx ec2 = glz::read_beve(hr, buffer);

      includer_struct is{};
      [[maybe_unused]] glz::error_ctx ec3 = glz::read_beve(is, buffer);

      nvt_struct nvt{};
      [[maybe_unused]] glz::error_ctx ec4 = glz::read_beve(nvt, buffer);
   }

   {
      std::vector<uint8_t> v8(Data, Data + Size);
      std::string buf8{};
      if (!glz::write_beve(v8, buf8)) {
         std::string mutated8 = buf8;
         mutate_cyclic(mutated8, Data, Size);
         std::vector<char> vec8(mutated8.begin(), mutated8.end());
         std::span<const uint8_t> out8{};
         [[maybe_unused]] glz::error_ctx ec = glz::read_beve(out8, vec8);

         std::vector<char> clean8(buf8.begin(), buf8.end());
         std::span<const uint8_t> out8b{};
         [[maybe_unused]] glz::error_ctx ecb = glz::read_beve(out8b, clean8);
      }
   }

   {
      std::vector<double> vd;
      for (size_t i = 0; i + 8 <= Size; i += 8) {
         double d;
         std::memcpy(&d, Data + i, 8);
         vd.push_back(d);
      }
      std::string bufd{};
      if (!glz::write<aligned_beve_opts>(vd, bufd)) {
         std::string mutatedd = bufd;
         mutate_cyclic(mutatedd, Data, Size);
         std::vector<char> vecd(mutatedd.begin(), mutatedd.end());
         std::span<const double> outd{};
         [[maybe_unused]] glz::error_ctx ec = glz::read_beve(outd, vecd);

         std::vector<char> cleand(bufd.begin(), bufd.end());
         std::span<const double> outdb{};
         [[maybe_unused]] glz::error_ctx ecb = glz::read_beve(outdb, cleand);
      }
   }

   {
      cursor cur{Data, Size};
      hidden_writer hw{};
      hw.x = static_cast<int>(cur.next());
      hw.secret = "fuzzed-secret-" + std::to_string(cur.next());

      std::string buf{};
      if (!glz::write_beve(hw, buf)) {
         std::vector<char> vec(buf.begin(), buf.end());
         hidden_reader hr{};
         [[maybe_unused]] glz::error_ctx ec = glz::read_beve(hr, vec);
      }
   }

   {
      cursor cur{Data, Size};
      includer_struct seed{};
      seed.str = "fuzzed";
      seed.i = static_cast<int>(cur.next());

      std::string buf{};
      if (!glz::write_beve(seed, buf)) {
         std::string mutated = buf;
         mutate_cyclic(mutated, Data, Size);
         std::vector<char> vec(mutated.begin(), mutated.end());
         includer_struct out{};
         [[maybe_unused]] glz::error_ctx ec = glz::read_beve(out, vec);

         std::vector<char> clean(buf.begin(), buf.end());
         includer_struct out2{};
         [[maybe_unused]] glz::error_ctx ec2 = glz::read_beve(out2, clean);
      }
   }

   {
      cursor cur{Data, Size};
      nvt_struct seed{};
      seed.v.present = (cur.next() & 1) != 0;
      seed.v.val = static_cast<int>(cur.next());

      std::string buf{};
      if (!glz::write_beve(seed, buf)) {
         std::vector<char> vec(buf.begin(), buf.end());
         nvt_struct out{};
         out.v.present = (cur.next() & 1) != 0;
         [[maybe_unused]] glz::error_ctx ec = glz::read_beve(out, vec);
      }
   }

   if (heavy_selector == 0) {
      cursor cur{Data, Size};
      rich_struct seed = make_seed(cur);

      std::string valid_buf{};
      if (!glz::write_beve(seed, valid_buf)) {
         const size_t consumed = cur.i;
         const size_t remaining = (consumed < Size) ? (Size - consumed) : 0;

         std::string mutated_str = valid_buf;
         mutate_cyclic(mutated_str, Data + consumed, remaining);

         std::vector<char> mutated(mutated_str.begin(), mutated_str.end());
         rich_struct out{};
         [[maybe_unused]] glz::error_ctx ec = glz::read_beve(out, mutated);

         std::vector<char> clean(valid_buf.begin(), valid_buf.end());
         rich_struct out2{};
         [[maybe_unused]] glz::error_ctx ec2 = glz::read_beve(out2, clean);
      }
   }

   if (heavy_selector == 1) {
      cursor cur{Data, Size};
      extra_struct seed = make_extra_seed(cur);

      std::string valid_buf{};
      if (!glz::write_beve(seed, valid_buf)) {
         const size_t consumed = cur.i;
         const size_t remaining = (consumed < Size) ? (Size - consumed) : 0;

         std::string mutated_str = valid_buf;
         mutate_cyclic(mutated_str, Data + consumed, remaining);

         std::vector<char> mutated(mutated_str.begin(), mutated_str.end());
         extra_struct out{};
         [[maybe_unused]] glz::error_ctx ec = glz::read_beve(out, mutated);

         std::vector<char> clean(valid_buf.begin(), valid_buf.end());
         extra_struct out2{};
         [[maybe_unused]] glz::error_ctx ec2 = glz::read_beve(out2, clean);
      }
   }

   if (heavy_selector == 2) {
      cursor cur{Data, Size};
      glz::generic gseed = make_generic_seed(cur);

      std::string valid_buf{};
      if (!glz::write_beve(gseed, valid_buf)) {
         const size_t consumed = cur.i;
         const size_t remaining = (consumed < Size) ? (Size - consumed) : 0;

         std::string mutated_str = valid_buf;
         mutate_cyclic(mutated_str, Data + consumed, remaining);

         std::vector<char> mutated(mutated_str.begin(), mutated_str.end());
         glz::generic gout{};
         [[maybe_unused]] glz::error_ctx ec = glz::read_beve(gout, mutated);

         std::vector<char> clean(valid_buf.begin(), valid_buf.end());
         glz::generic gout2{};
         [[maybe_unused]] glz::error_ctx ec2 = glz::read_beve(gout2, clean);
      }
   }

   if (heavy_selector == 3) {
      cursor cur{Data, Size};
      variant_struct seed = make_variant_seed(cur);

      std::string valid_buf{};
      if (!glz::write_beve(seed, valid_buf)) {
         const size_t consumed = cur.i;
         const size_t remaining = (consumed < Size) ? (Size - consumed) : 0;

         std::string mutated_str = valid_buf;
         mutate_cyclic(mutated_str, Data + consumed, remaining);

         std::vector<char> mutated(mutated_str.begin(), mutated_str.end());
         variant_struct out{};
         [[maybe_unused]] glz::error_ctx ec = glz::read_beve(out, mutated);

         std::vector<char> clean(valid_buf.begin(), valid_buf.end());
         variant_struct out2{};
         [[maybe_unused]] glz::error_ctx ec2 = glz::read_beve(out2, clean);
      }
   }

   {
      using legacy_variant = std::variant<int32_t, double>;

      {
         const uint8_t choice = (Size > 0 ? Data[0] : 0) % 2;
         std::string content_buf{};
         bool write_ok = false;
         if (choice == 0) {
            int32_t v = (Size > 1) ? static_cast<int32_t>(Data[1]) : 42;
            write_ok = !glz::write_beve(v, content_buf);
         }
         else {
            double v = (Size > 1) ? static_cast<double>(Data[1]) : 3.14;
            write_ok = !glz::write_beve(v, content_buf);
         }
         if (write_ok) {
            std::vector<char> legacy{};
            legacy.push_back(static_cast<char>(0x0E));
            legacy.push_back(static_cast<char>(choice << 2));
            legacy.insert(legacy.end(), content_buf.begin(), content_buf.end());

            legacy_variant lv{};
            [[maybe_unused]] glz::error_ctx ec = glz::read_beve(lv, legacy);
         }
      }

      {
         std::vector<char> legacy{};
         legacy.push_back(static_cast<char>(0x0E));
         legacy.push_back(static_cast<char>((Size > 0 ? Data[0] : 0) << 2));
         legacy.insert(legacy.end(), buffer.begin(), buffer.end());

         legacy_variant lv{};
         [[maybe_unused]] glz::error_ctx ec = glz::read_beve(lv, legacy);
      }
   }

   if (heavy_selector == 4) {
      glz::generic deep{};
      glz::generic* cur = &deep;
      for (int depth = 0; depth < 300; ++depth) {
         cur = &((*cur)["n"]);
      }
      *cur = 1.0;

      std::string deep_buf{};
      if (!glz::write_beve(deep, deep_buf)) {
         std::vector<char> deep_vec(deep_buf.begin(), deep_buf.end());
         glz::generic out{};
         [[maybe_unused]] glz::error_ctx ec = glz::read_beve(out, deep_vec);

         std::string mutated_deep = deep_buf;
         mutate_cyclic(mutated_deep, Data, Size);
         std::vector<char> mutated_deep_vec(mutated_deep.begin(), mutated_deep.end());
         glz::generic out2{};
         [[maybe_unused]] glz::error_ctx ec2 = glz::read_beve(out2, mutated_deep_vec);
      }
   }

   {

      rich_struct v1{};
      [[maybe_unused]] auto ec1 = glz::read<beve_saa_opts>(v1, buffer);
      rich_struct v2{};
      [[maybe_unused]] auto ec2 = glz::read_beve_untagged(v2, buffer);
      rich_struct v3{};
      [[maybe_unused]] auto ec3 = glz::read<beve_limits_opts>(v3, buffer);
      rich_struct v4{};
      [[maybe_unused]] auto ec4 = glz::read<beve_strict_opts>(v4, buffer);

      cursor cur{Data, Size};
      rich_struct seed = make_seed(cur);

      std::string saa_buf{};
      if (!glz::write_beve_untagged(seed, saa_buf)) {
         std::vector<char> vec(saa_buf.begin(), saa_buf.end());
         rich_struct out{};
         [[maybe_unused]] auto ec5 = glz::read<beve_saa_opts>(out, vec);
         rich_struct out2{};
         [[maybe_unused]] auto ec6 = glz::read_beve_untagged(out2, vec);

         std::string mutated_saa = saa_buf;
         mutate_cyclic(mutated_saa, Data, Size);
         std::vector<char> mvec_saa(mutated_saa.begin(), mutated_saa.end());
         rich_struct out3{};
         [[maybe_unused]] auto ec7 = glz::read<beve_saa_opts>(out3, mvec_saa);
      }

      std::string vb{};
      if (!glz::write_beve(seed, vb)) {
         std::vector<char> vec(vb.begin(), vb.end());
         rich_struct out4{};
         [[maybe_unused]] auto ec8 = glz::read<beve_strict_opts>(out4, vec);
         rich_struct out5{};
         [[maybe_unused]] auto ec9 = glz::read<beve_limits_opts>(out5, vec);

         std::string mutated = vb;
         const size_t consumed = cur.i;
         mutate_cyclic(mutated, Data + consumed, (consumed < Size) ? (Size - consumed) : 0);
         std::vector<char> mvec(mutated.begin(), mutated.end());
         rich_struct out6{};
         [[maybe_unused]] auto ec10 = glz::read<beve_strict_opts>(out6, mvec);
         rich_struct out7{};
         [[maybe_unused]] auto ec11 = glz::read<beve_limits_opts>(out7, mvec);
      }
   }

   return 0;
}