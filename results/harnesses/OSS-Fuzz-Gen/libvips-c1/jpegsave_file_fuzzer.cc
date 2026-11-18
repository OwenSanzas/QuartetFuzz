#include "config.h"

#include <vips/vips.h>

#include <fuzzer/FuzzedDataProvider.h>

#include <algorithm>
#include <cstdint>
#include <vector>

extern "C" int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
	if (VIPS_INIT(*argv[0]))
		return -1;

	vips_concurrency_set(1);
	return 0;
}

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	FuzzedDataProvider provider(data, size);

	int q = provider.ConsumeIntegralInRange<int>(1, 100);
	gboolean optimize_coding = provider.ConsumeBool();
	gboolean interlace = provider.ConsumeBool();
	gboolean trellis_quant = provider.ConsumeBool();
	gboolean overshoot_deringing = provider.ConsumeBool();
	gboolean optimize_scans = provider.ConsumeBool();
	int restart_interval = provider.ConsumeIntegralInRange<int>(0, 100);
	int quant_table = provider.ConsumeIntegralInRange<int>(0, 8);
	int subsample_mode = provider.ConsumeIntegralInRange<int>(0, 2);
	int keep = provider.ConsumeIntegralInRange<int>(0, 63);
	int page_height = provider.ConsumeIntegralInRange<int>(0, 200);
	gboolean use_synthetic = provider.ConsumeBool();

	static const char *profiles[] = { "none", "cmyk", "p3", "sGrey", "sRGB" };
	const char *profile = profiles[provider.ConsumeIntegralInRange<int>(0, 4)];

	double bg[4];
	for (double &v : bg)
		v = provider.ConsumeIntegralInRange<int>(0, 255);
	VipsArrayDouble *background = vips_array_double_new(bg, 4);

	int syn_w = provider.ConsumeIntegralInRange<int>(1, 64);
	int syn_h = provider.ConsumeIntegralInRange<int>(1, 64);

	int syn_bands = provider.ConsumeIntegralInRange<int>(1, 8);
	int syn_interp = provider.ConsumeIntegralInRange<int>(0, 2);

	static const char *res_units[] = { "in", "cm", "none" };
	const char *res_unit = res_units[provider.ConsumeIntegralInRange<int>(0, 2)];
	double xres = provider.ConsumeFloatingPointInRange<double>(0.1, 500.0);
	double yres = provider.ConsumeFloatingPointInRange<double>(0.1, 500.0);

	gboolean use_16bit = provider.ConsumeBool();

	gboolean strip_legacy = provider.ConsumeBool();
	gboolean no_subsample_legacy = provider.ConsumeBool();

	gboolean force_oversized_exif = provider.ConsumeBool();
	gboolean force_oversized_xmp = provider.ConsumeBool();
	gboolean force_oversized_iptc = provider.ConsumeBool();

	size_t needed = (size_t) syn_w * syn_h * syn_bands;
	size_t reserve_for_image = use_synthetic ? needed : (size_t) 64;
	size_t total_remaining = provider.remaining_bytes();
	size_t available_for_metadata = total_remaining > reserve_for_image
		? total_remaining - reserve_for_image
		: 0;
	size_t per_field_budget = available_for_metadata / 4;

	auto take_blob = [&](size_t max_len) {
		size_t cap = std::min(max_len, per_field_budget);
		size_t len = provider.ConsumeIntegralInRange<size_t>(0, cap);
		return provider.ConsumeBytes<uint8_t>(len);
	};

	std::vector<uint8_t> exif_data = take_blob(4096);
	if (force_oversized_exif)
		exif_data.resize(70000, 0xAA);

	std::vector<uint8_t> xmp_data = take_blob(4096);
	if (force_oversized_xmp)
		xmp_data.resize(61000, 0xBB);

	std::vector<uint8_t> iptc_data = take_blob(4096);
	if (force_oversized_iptc)
		iptc_data.resize(70000, 0xCC);

	std::vector<uint8_t> icc_data = take_blob(2048);

	std::vector<uint8_t> image_data = provider.ConsumeRemainingBytes<uint8_t>();

	VipsImage *image = nullptr;

	if (!use_synthetic) {
		image = vips_image_new_from_buffer(
			image_data.data(), image_data.size(), "", nullptr);
	}

	if (!image) {

		if (image_data.size() < needed || needed == 0) {
			vips_area_unref((VipsArea *) background);
			return 0;
		}
		image = vips_image_new_from_memory_copy(
			image_data.data(), needed, syn_w, syn_h, syn_bands,
			VIPS_FORMAT_UCHAR);
		if (image) {
			VipsInterpretation interp = VIPS_INTERPRETATION_B_W;
			if (syn_interp == 1)
				interp = VIPS_INTERPRETATION_sRGB;
			else if (syn_interp == 2 && syn_bands == 4)
				interp = VIPS_INTERPRETATION_CMYK;

			VipsImage *tagged;
			if (!vips_copy(image, &tagged, "interpretation", interp, NULL)) {
				g_object_unref(image);
				image = tagged;
			}
		}
	}

	if (!image) {
		vips_area_unref((VipsArea *) background);
		return 0;
	}

	if (image->Xsize > 100 || image->Ysize > 100) {
		VipsImage *small;
		if (!vips_thumbnail_image(image, &small, 100,
				"height", 100, NULL)) {
			g_object_unref(image);
			image = small;
		}
	}

	if (image->Xsize > 100 || image->Ysize > 100 || image->Bands > 8) {
		g_object_unref(image);
		vips_area_unref((VipsArea *) background);
		return 0;
	}

	if (use_16bit) {
		VipsImage *shorty;
		if (!vips_cast(image, &shorty, VIPS_FORMAT_USHORT, NULL)) {
			g_object_unref(image);
			image = shorty;
		}
	}

	if (!exif_data.empty())
		vips_image_set_blob_copy(image, VIPS_META_EXIF_NAME,
			exif_data.data(), exif_data.size());
	if (!xmp_data.empty())
		vips_image_set_blob_copy(image, VIPS_META_XMP_NAME,
			xmp_data.data(), xmp_data.size());
	if (!iptc_data.empty())
		vips_image_set_blob_copy(image, VIPS_META_IPTC_NAME,
			iptc_data.data(), iptc_data.size());
	if (!icc_data.empty())
		vips_image_set_blob_copy(image, VIPS_META_ICC_NAME,
			icc_data.data(), icc_data.size());

	vips_image_set_string(image, VIPS_META_RESOLUTION_UNIT, res_unit);
	image->Xres = xres;
	image->Yres = yres;

	vips_image_set_string(image, VIPS_META_IMAGEDESCRIPTION, "fuzz description");
	uint8_t dummy_gainmap = 1;
	vips_image_set_blob_copy(image, "gainmap-dummy", &dummy_gainmap, 1);

	void *buf = nullptr;
	size_t len = 0;

	if (!vips_jpegsave_buffer(image, &buf, &len,
			"Q", q,
			"optimize_coding", optimize_coding,
			"interlace", interlace,
			"trellis_quant", trellis_quant,
			"overshoot_deringing", overshoot_deringing,
			"optimize_scans", optimize_scans,
			"restart_interval", restart_interval,
			"quant_table", quant_table,
			"subsample_mode", subsample_mode,
			"keep", keep,
			"page_height", page_height,
			"profile", profile,
			"background", background,
			nullptr)) {
		g_free(buf);
		buf = nullptr;
	}

	if (!vips_jpegsave_buffer(image, &buf, &len,
			"Q", q,
			"optimize_coding", optimize_coding,
			"interlace", interlace,
			"trellis_quant", trellis_quant,
			"overshoot_deringing", overshoot_deringing,
			"optimize_scans", optimize_scans,
			"restart_interval", restart_interval,
			"quant_table", quant_table,
			"no_subsample", no_subsample_legacy,
			"strip", strip_legacy,
			"page_height", page_height,
			"background", background,
			nullptr)) {
		g_free(buf);
		buf = nullptr;
	}

	g_object_unref(image);
	vips_area_unref((VipsArea *) background);

	return 0;
}