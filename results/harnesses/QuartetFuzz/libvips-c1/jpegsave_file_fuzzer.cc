#include "config.h"

#include <vips/vips.h>

#include <stdint.h>
#include <string.h>

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
	const size_t header_len = 8;
	if (size < header_len + 4)
		return 0;

	uint8_t b0 = data[0];
	uint8_t b1 = data[1];
	uint8_t b2 = data[2];
	uint8_t b3 = data[3];
	uint8_t b4 = data[4];
	uint8_t b5 = data[5];
	uint8_t b6 = data[6];
	uint8_t b7 = data[7];

	const uint8_t *body = data + header_len;
	size_t body_len = size - header_len;

	VipsImage *image = nullptr;

	if (vips_jpegload_buffer((void *) body, body_len, &image,
			"autorotate", (gboolean) (b4 & 1),
			nullptr)) {
		image = nullptr;
	}

	if (!image) {
		if (!(image = vips_image_new_from_buffer(body, body_len, "", nullptr)))
			return 0;
	}

	if (image->Xsize <= 0 ||
		image->Ysize <= 0 ||
		(gint64) image->Xsize * (gint64) image->Ysize > 4000000 ||
		image->Bands > 4 ||
		image->Bands < 1) {
		g_object_unref(image);
		return 0;
	}

	int q = 1 + (b0 % 100);
	gboolean optimize_coding = (b1 & 1) != 0;
	gboolean interlace = (b1 & 2) != 0;
	gboolean trellis_quant = (b1 & 4) != 0;
	gboolean overshoot_deringing = (b1 & 8) != 0;
	gboolean optimize_scans = (b1 & 16) != 0;
	int quant_table = b2 % 9;
	int restart_interval = b3;

	VipsForeignSubsample subsample_mode;
	switch (b5 % 3) {
	case 0:
		subsample_mode = VIPS_FOREIGN_SUBSAMPLE_AUTO;
		break;
	case 1:
		subsample_mode = VIPS_FOREIGN_SUBSAMPLE_ON;
		break;
	default:
		subsample_mode = VIPS_FOREIGN_SUBSAMPLE_OFF;
		break;
	}

	const char *profile = (b6 & 1) ? "none" : nullptr;

	void *buf = nullptr;
	size_t len = 0;

	int rc;
	if (profile)
		rc = vips_jpegsave_buffer(image, &buf, &len,
			"Q", q,
			"optimize_coding", optimize_coding,
			"interlace", interlace,
			"trellis_quant", trellis_quant,
			"overshoot_deringing", overshoot_deringing,
			"optimize_scans", optimize_scans,
			"quant_table", quant_table,
			"restart_interval", restart_interval,
			"subsample_mode", subsample_mode,
			"profile", profile,
			"strip", (gboolean) (b7 & 1),
			nullptr);
	else
		rc = vips_jpegsave_buffer(image, &buf, &len,
			"Q", q,
			"optimize_coding", optimize_coding,
			"interlace", interlace,
			"trellis_quant", trellis_quant,
			"overshoot_deringing", overshoot_deringing,
			"optimize_scans", optimize_scans,
			"quant_table", quant_table,
			"restart_interval", restart_interval,
			"subsample_mode", subsample_mode,
			"strip", (gboolean) (b7 & 1),
			nullptr);

	if (rc) {
		g_object_unref(image);
		return 0;
	}

	VipsImage *reloaded = nullptr;
	if (!vips_jpegload_buffer(buf, len, &reloaded, nullptr)) {
		double avg;
		(void) vips_avg(reloaded, &avg, nullptr);
		g_object_unref(reloaded);
	}

	g_free(buf);
	g_object_unref(image);

	return 0;
}
