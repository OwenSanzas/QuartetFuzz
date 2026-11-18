#include "config.h"

#include <vips/vips.h>
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
	if (size < header_len + 1)
		return 0;

	uint8_t b_interesting = data[0];
	uint8_t b_premult = data[1];
	uint8_t b_ix = data[2];
	uint8_t b_iy = data[3];
	uint8_t b_w = data[4];
	uint8_t b_h = data[5];

	const uint8_t *img_data = data + header_len;
	size_t img_size = size - header_len;

	VipsImage *image;
	if (!(image = vips_image_new_from_buffer(img_data, img_size, "", nullptr)))
		return 0;

	if (image->Xsize > 2000 ||
		image->Ysize > 2000 ||
		image->Bands > 8 ||
		(gint64) image->Xsize * image->Ysize * image->Bands > 20000000) {
		g_object_unref(image);
		return 0;
	}

	if (image->Xsize <= 0 || image->Ysize <= 0) {
		g_object_unref(image);
		return 0;
	}

	int interesting = b_interesting % VIPS_INTERESTING_LAST;

	int width = 1 + (int) (b_w % (unsigned) image->Xsize);
	int height = 1 + (int) (b_h % (unsigned) image->Ysize);

	int interesting_x = (int) b_ix;
	int interesting_y = (int) b_iy;

	gboolean premultiplied = (b_premult & 1) ? TRUE : FALSE;

	VipsImage *out;
	int ret = vips_smartcrop(image, &out, width, height,
		"interesting", (VipsInteresting) interesting,
		"interesting_x", interesting_x,
		"interesting_y", interesting_y,
		"premultiplied", premultiplied,
		nullptr);

	if (ret) {
		g_object_unref(image);
		return 0;
	}

	int xoff = vips_image_get_xoffset(out);
	int yoff = vips_image_get_yoffset(out);
	(void) xoff;
	(void) yoff;

	double avg = 0.0;
	if (!vips_avg(out, &avg, nullptr)) {
		(void) avg;
	}

	if (out->Xsize > 0 && out->Ysize > 0) {
		VipsImage *sub;
		int ew = out->Xsize > 1 ? out->Xsize - 1 : out->Xsize;
		int eh = out->Ysize > 1 ? out->Ysize - 1 : out->Ysize;
		if (ew > 0 && eh > 0 &&
			!vips_extract_area(out, &sub, 0, 0, ew, eh, nullptr)) {
			g_object_unref(sub);
		}
	}

	g_object_unref(out);
	g_object_unref(image);

	return 0;
}
