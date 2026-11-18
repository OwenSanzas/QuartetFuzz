#include "cctype.h"
#include "fdiskP.h"
#include "strutils.h"
#include "carefulputc.h"
#include "mangle.h"
#include "jsonwrt.h"
#include "fileutils.h"

#ifdef FUZZ_TARGET
#include "fuzz.h"
#endif

struct fdisk_scriptheader {
	struct list_head	headers;
	char			*name;
	char			*data;
};

struct fdisk_script {
	struct fdisk_table	*table;
	struct list_head	headers;
	struct fdisk_context	*cxt;

	int			refcount;
	char			*(*fn_fgets)(struct fdisk_script *, char *, size_t, FILE *);
	void			*userdata;

	size_t			nlines;
	struct fdisk_label	*label;

	unsigned long		sector_size;

	unsigned int		json : 1,
				force_label : 1,
				no_device_names  : 1;
};

static void fdisk_script_free_header(struct fdisk_scriptheader *fi)
{
	if (!fi)
		return;

	DBG_OBJ(SCRIPT, fi, ul_debug("free header %s", fi->name));
	free(fi->name);
	free(fi->data);
	list_del(&fi->headers);
	free(fi);
}

int fdisk_script_disable_devnames(struct fdisk_script *dp, int disable)
{
	if (!dp)
		return -EINVAL;

	dp->no_device_names = disable ? 1 : 0;
	return 0;
}

int fdisk_script_has_devnames(struct fdisk_script *dp)
{
	if (!dp)
		return -EINVAL;

	return dp->no_device_names ? 0 : 1;
}

struct fdisk_script *fdisk_new_script(struct fdisk_context *cxt)
{
	struct fdisk_script *dp = NULL;

	dp = calloc(1, sizeof(*dp));
	if (!dp)
		return NULL;

	DBG_OBJ(SCRIPT, dp, ul_debug("alloc"));
	dp->refcount = 1;
	dp->cxt = cxt;
	fdisk_ref_context(cxt);

	INIT_LIST_HEAD(&dp->headers);
	return dp;
}

struct fdisk_script *fdisk_new_script_from_file(struct fdisk_context *cxt,
						 const char *filename)
{
	int rc;
	FILE *f;
	struct fdisk_script *dp, *res = NULL;

	assert(cxt);
	assert(filename);

	DBG(SCRIPT, ul_debug("opening %s", filename));
	f = fopen(filename, "r");
	if (!f)
		return NULL;

	dp = fdisk_new_script(cxt);
	if (!dp)
		goto done;

	rc = fdisk_script_read_file(dp, f);
	if (rc) {
		errno = -rc;
		goto done;
	}

	res = dp;
done:
	fclose(f);
	if (!res)
		fdisk_unref_script(dp);
	else
		errno = 0;

	return res;
}

void fdisk_ref_script(struct fdisk_script *dp)
{
	if (dp)
		dp->refcount++;
}

static void fdisk_reset_script(struct fdisk_script *dp)
{
	assert(dp);

	DBG_OBJ(SCRIPT, dp, ul_debug("reset"));

	if (dp->table)
		fdisk_reset_table(dp->table);

	while (!list_empty(&dp->headers)) {
		struct fdisk_scriptheader *fi = list_entry(dp->headers.next,
						  struct fdisk_scriptheader, headers);
		fdisk_script_free_header(fi);
	}
	INIT_LIST_HEAD(&dp->headers);
}

void fdisk_unref_script(struct fdisk_script *dp)
{
	if (!dp)
		return;

	dp->refcount--;
	if (dp->refcount <= 0) {
		fdisk_reset_script(dp);
		fdisk_unref_context(dp->cxt);
		fdisk_unref_table(dp->table);
		DBG_OBJ(SCRIPT, dp, ul_debug("free script"));
		free(dp);
	}
}

int fdisk_script_set_userdata(struct fdisk_script *dp, void *data)
{
	assert(dp);
	dp->userdata = data;
	return 0;
}

void *fdisk_script_get_userdata(struct fdisk_script *dp)
{
	assert(dp);
	return dp->userdata;
}

static struct fdisk_scriptheader *script_get_header(struct fdisk_script *dp,
						     const char *name)
{
	struct list_head *p;

	list_for_each(p, &dp->headers) {
		struct fdisk_scriptheader *fi = list_entry(p, struct fdisk_scriptheader, headers);

		if (c_strcasecmp(fi->name, name) == 0)
			return fi;
	}

	return NULL;
}

const char *fdisk_script_get_header(struct fdisk_script *dp, const char *name)
{
	struct fdisk_scriptheader *fi;

	assert(dp);
	assert(name);

	fi = script_get_header(dp, name);
	return fi ? fi->data : NULL;
}

int fdisk_script_set_header(struct fdisk_script *dp,
			    const char *name,
			    const char *data)
{
	struct fdisk_scriptheader *fi;

	if (!dp || !name)
		return -EINVAL;

	fi = script_get_header(dp, name);
	if (!fi && !data)
		return 0;

	if (!data) {
		DBG_OBJ(SCRIPT, dp, ul_debug("freeing header %s", name));

		fdisk_script_free_header(fi);
		return 0;
	}

	if (!fi) {
		int rc;

		DBG_OBJ(SCRIPT, dp, ul_debug("setting new header %s='%s'", name, data));

		fi = calloc(1, sizeof(*fi));
		if (!fi)
			return -ENOMEM;
		INIT_LIST_HEAD(&fi->headers);

		rc = strdup_to_struct_member(fi, name, name);
		if (!rc)
			rc = strdup_to_struct_member(fi, data, data);
		if (rc) {
			fdisk_script_free_header(fi);
			return rc;
		}
		list_add_tail(&fi->headers, &dp->headers);
	} else {

		char *x = strdup(data);

		DBG_OBJ(SCRIPT, dp, ul_debug("update '%s' header '%s' -> '%s'", name, fi->data, data));

		if (!x)
			return -ENOMEM;
		free(fi->data);
		fi->data = x;
	}

	if (strcmp(name, "label") == 0)
		dp->label = NULL;

	return 0;
}

struct fdisk_table *fdisk_script_get_table(struct fdisk_script *dp)
{
	assert(dp);

	if (!dp->table)

		dp->table = fdisk_new_table();

	return dp->table;
}

int fdisk_script_set_table(struct fdisk_script *dp, struct fdisk_table *tb)
{
	if (!dp)
		return -EINVAL;

	fdisk_ref_table(tb);
	fdisk_unref_table(dp->table);
	dp->table = tb;

	DBG_OBJ(SCRIPT, dp, ul_debug("table replaced"));
	return 0;
}

static struct fdisk_label *script_get_label(struct fdisk_script *dp)
{
	assert(dp);
	assert(dp->cxt);

	if (!dp->label) {
		dp->label = fdisk_get_label(dp->cxt,
					fdisk_script_get_header(dp, "label"));
		DBG_OBJ(SCRIPT, dp, ul_debug("label '%s'", dp->label ? dp->label->name : ""));
	}
	return dp->label;
}

int fdisk_script_get_nlines(struct fdisk_script *dp)
{
	assert(dp);
	return dp->nlines;
}

int fdisk_script_has_force_label(struct fdisk_script *dp)
{
	assert(dp);
	return dp->force_label;
}

int fdisk_script_read_context(struct fdisk_script *dp, struct fdisk_context *cxt)
{
	struct fdisk_label *lb;
	int rc;
	char *p = NULL;
	char buf[64];

	if (!dp || (!cxt && !dp->cxt))
		return -EINVAL;

	if (!cxt)
		cxt = dp->cxt;

	DBG_OBJ(SCRIPT, dp, ul_debug("reading context into script"));
	fdisk_reset_script(dp);

	lb = fdisk_get_label(cxt, NULL);
	if (!lb)
		return -EINVAL;

	rc = fdisk_get_partitions(cxt, &dp->table);
	if (rc)
		return rc;

	rc = fdisk_script_set_header(dp, "label", fdisk_label_get_name(lb));

	if (!rc && fdisk_get_disklabel_id(cxt, &p) == 0 && p) {
		rc = fdisk_script_set_header(dp, "label-id", p);
		free(p);
	}
	if (!rc && cxt->dev_path)
		rc = fdisk_script_set_header(dp, "device", cxt->dev_path);
	if (!rc)
		rc = fdisk_script_set_header(dp, "unit", "sectors");

	if (!rc && fdisk_is_label(cxt, GPT)) {
		struct fdisk_labelitem item = FDISK_LABELITEM_INIT;

		rc = fdisk_get_disklabel_item(cxt, GPT_LABELITEM_FIRSTLBA, &item);
		if (!rc) {
			snprintf(buf, sizeof(buf), "%"PRIu64, item.data.num64);
			rc = fdisk_script_set_header(dp, "first-lba", buf);
		}

		if (!rc)
			rc = fdisk_get_disklabel_item(cxt, GPT_LABELITEM_LASTLBA, &item);
		if (!rc) {
			snprintf(buf, sizeof(buf), "%"PRIu64, item.data.num64);
			rc = fdisk_script_set_header(dp, "last-lba", buf);
		}

		if (!rc) {
			size_t n = fdisk_get_npartitions(cxt);
			if (n != FDISK_GPT_NPARTITIONS_DEFAULT) {
				snprintf(buf, sizeof(buf), "%zu", n);
				rc = fdisk_script_set_header(dp, "table-length", buf);
			}
		}
	}

	if (!rc && fdisk_get_grain_size(cxt) != 2048 * 512) {
		snprintf(buf, sizeof(buf), "%lu", fdisk_get_grain_size(cxt));
		rc = fdisk_script_set_header(dp, "grain", buf);
	}

	if (!rc) {
		snprintf(buf, sizeof(buf), "%lu", fdisk_get_sector_size(cxt));
		rc = fdisk_script_set_header(dp, "sector-size", buf);
	}

	DBG_OBJ(SCRIPT, dp, ul_debug("read context done [rc=%d]", rc));
	return rc;
}

int fdisk_script_enable_json(struct fdisk_script *dp, int json)
{
	assert(dp);

	dp->json = json;
	return 0;
}

static int write_file_json(struct fdisk_script *dp, FILE *f)
{
	struct list_head *h;
	struct fdisk_partition *pa;
	struct fdisk_iter itr;
	const char *devname = NULL;
	struct ul_jsonwrt json;

	assert(dp);
	assert(f);

	DBG_OBJ(SCRIPT, dp, ul_debug("writing json dump to file"));

	ul_jsonwrt_init(&json, f, 0, UL_JSON_PRETTY);
	ul_jsonwrt_root_open(&json);

	ul_jsonwrt_object_open(&json, "partitiontable");

	list_for_each(h, &dp->headers) {
		struct fdisk_scriptheader *fi = list_entry(h, struct fdisk_scriptheader, headers);
		const char *name = fi->name;
		int num = 0;

		if (strcmp(name, "first-lba") == 0) {
			name = "firstlba";
			num = 1;
		} else if (strcmp(name, "last-lba") == 0) {
			name = "lastlba";
			num = 1;
		} else if (strcmp(name, "sector-size") == 0) {
			name = "sectorsize";
			num = 1;
		} else if (strcmp(name, "label-id") == 0)
			name = "id";

		if (num)
			ul_jsonwrt_value_raw(&json, name, fi->data);
		else
			ul_jsonwrt_value_s(&json, name, fi->data);

		if (fdisk_script_has_devnames(dp) && strcmp(name, "device") == 0)
			devname = fi->data;
	}

	if (!dp->table || fdisk_table_is_empty(dp->table)) {
		DBG_OBJ(SCRIPT, dp, ul_debug("script table empty"));
		goto done;
	}

	DBG_OBJ(SCRIPT, dp, ul_debug("%zu entries", fdisk_table_get_nents(dp->table)));

	ul_jsonwrt_array_open(&json, "partitions");

	fdisk_reset_iter(&itr, FDISK_ITER_FORWARD);
	while (fdisk_table_next_partition(dp->table, &itr, &pa) == 0) {
		char *p = NULL;

		ul_jsonwrt_object_open(&json, NULL);
		if (devname)
			p = fdisk_partname(devname, pa->partno + 1);
		else if (asprintf(&p, "%zu", pa->partno + 1) < 0)
			return -ENOMEM;

		if (p) {
			DBG_OBJ(SCRIPT, dp, ul_debug("write %s entry", p));
			ul_jsonwrt_value_s(&json, "node", p);
			free(p);
		}

		if (fdisk_partition_has_start(pa))
			ul_jsonwrt_value_u64(&json, "start", (uintmax_t)pa->start);

		if (fdisk_partition_has_size(pa))
			ul_jsonwrt_value_u64(&json, "size", (uintmax_t)pa->size);

		if (pa->type && fdisk_parttype_get_string(pa->type))
			ul_jsonwrt_value_s(&json, "type", fdisk_parttype_get_string(pa->type));

		else if (pa->type) {
			ul_jsonwrt_value_open(&json, "type");
			fprintf(f, "\"%x\"", fdisk_parttype_get_code(pa->type));
			ul_jsonwrt_value_close(&json);
		}

		if (pa->uuid)
			ul_jsonwrt_value_s(&json, "uuid", pa->uuid);
		if (pa->name && *pa->name)
			ul_jsonwrt_value_s(&json, "name", pa->name);

		if (pa->attrs) {
			struct fdisk_label *lb = script_get_label(dp);

			if (!lb || fdisk_label_get_type(lb) != FDISK_DISKLABEL_DOS)
				ul_jsonwrt_value_s(&json, "attrs", pa->attrs);
		}

		if (fdisk_partition_is_bootable(pa))
			ul_jsonwrt_value_boolean(&json, "bootable", 1);
		ul_jsonwrt_object_close(&json);
	}

	ul_jsonwrt_array_close(&json);
done:
	ul_jsonwrt_object_close(&json);
	ul_jsonwrt_root_close(&json);

	DBG_OBJ(SCRIPT, dp, ul_debug("write script done"));
	return 0;
}

static int write_file_sfdisk(struct fdisk_script *dp, FILE *f)
{
	struct list_head *h;
	struct fdisk_partition *pa;
	struct fdisk_iter itr;
	const char *devname = NULL;

	assert(dp);
	assert(f);

	DBG_OBJ(SCRIPT, dp, ul_debug("writing sfdisk-like script to file"));

	list_for_each(h, &dp->headers) {
		struct fdisk_scriptheader *fi = list_entry(h, struct fdisk_scriptheader, headers);
		fprintf(f, "%s: %s\n", fi->name, fi->data);
		if (fdisk_script_has_devnames(dp) && strcmp(fi->name, "device") == 0)
			devname = fi->data;
	}

	if (!dp->table || fdisk_table_is_empty(dp->table)) {
		DBG_OBJ(SCRIPT, dp, ul_debug("script table empty"));
		return 0;
	}

	DBG_OBJ(SCRIPT, dp, ul_debug("%zu entries", fdisk_table_get_nents(dp->table)));

	fputc('\n', f);

	fdisk_reset_iter(&itr, FDISK_ITER_FORWARD);
	while (fdisk_table_next_partition(dp->table, &itr, &pa) == 0) {
		char *p = NULL;
		int sep = 0;

		if (devname)
			p = fdisk_partname(devname, pa->partno + 1);
		else if (asprintf(&p, "%zu", pa->partno + 1) < 0)
			return -ENOMEM;

		if (p) {
			DBG_OBJ(SCRIPT, dp, ul_debug("write %s entry", p));
			fprintf(f, "%s : ", p);
			free(p);
		} else
			fprintf(f, "%zu : ", pa->partno + 1);

		if (fdisk_partition_has_start(pa))
			sep = fprintf(f, "start=%12ju", (uintmax_t)pa->start);
		if (fdisk_partition_has_size(pa))
			sep = fprintf(f, "%ssize=%12ju",
					sep ? ", " : " ", (uintmax_t)pa->size);

		if (pa->type && fdisk_parttype_get_string(pa->type))
			sep = fprintf(f, "%stype=%s",
					sep ? ", " : " ",
					fdisk_parttype_get_string(pa->type));
		else if (pa->type)
			sep = fprintf(f, "%stype=%x",
					sep ? ", " : " ",
					fdisk_parttype_get_code(pa->type));

		if (pa->uuid)
			sep = fprintf(f, "%suuid=%s", sep ? ", " : " ", pa->uuid);
		if (pa->name && *pa->name) {
			sep = fprintf(f, "%sname=", sep ? ", " : " ");
			fputs_quoted(pa->name, f);
		}

		if (pa->attrs) {
			struct fdisk_label *lb = script_get_label(dp);

			if (!lb || fdisk_label_get_type(lb) != FDISK_DISKLABEL_DOS)
				sep = fprintf(f, "%sattrs=\"%s\"",
						sep ? ", " : " ",
						pa->attrs);
		}
		if (fdisk_partition_is_bootable(pa))
			sep = fprintf(f, "%sbootable", sep ? ", " : " ");
		fputc('\n', f);
	}

	DBG_OBJ(SCRIPT, dp, ul_debug("write script done"));
	return 0;
}

int fdisk_script_write_file(struct fdisk_script *dp, FILE *f)
{
	assert(dp);

	if (dp->json)
		return write_file_json(dp, f);

	return write_file_sfdisk(dp, f);
}

static inline int is_header_line(const char *s)
{
	const char *p = strchr(s, ':');

	if (!p || p == s || !*(p + 1) || strchr(s, '='))
		return 0;

	return 1;
}

static int parse_line_header(struct fdisk_script *dp, char *s)
{
	size_t i;
	char *name, *value;
	static const char *const supported[] = {
		"label", "unit", "label-id", "device", "grain",
		"first-lba", "last-lba", "table-length", "sector-size"
	};

	DBG_OBJ(SCRIPT, dp, ul_debug("   parse header '%s'", s));

	if (!s || !*s)
		return -EINVAL;

	name = s;
	value = strchr(s, ':');
	if (!value)
		return -EINVAL;
	*value = '\0';
	value++;

	ltrim_whitespace((unsigned char *) name);
	rtrim_whitespace((unsigned char *) name);
	ltrim_whitespace((unsigned char *) value);
	rtrim_whitespace((unsigned char *) value);

	if (!*name || !*value)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(supported); i++) {
		if (strcmp(name, supported[i]) == 0)
			break;
	}
	if (i == ARRAY_SIZE(supported))
		return -ENOTSUP;

	if (strcmp(name, "label") == 0) {
		if (dp->cxt && !fdisk_get_label(dp->cxt, value))
			return -EINVAL;
		dp->force_label = 1;

	} else if (strcmp(name, "sector-size") == 0) {
		uint64_t x = 0;

		if (ul_strtou64(value, &x, 10) != 0)
			return -EINVAL;
		if (x > ULONG_MAX || x % 512)
			return -ERANGE;
		dp->sector_size = (unsigned long) x;

		if (dp->cxt && dp->sector_size && dp->cxt->sector_size
		    && dp->sector_size != dp->cxt->sector_size)
			fdisk_warnx(dp->cxt, _("The script and device sector size differ; the sizes will be recalculated to match the device."));

	} else if (strcmp(name, "unit") == 0) {
		if (strcmp(value, "sectors") != 0)
			return -EINVAL;

	}

	return fdisk_script_set_header(dp, name, value);
}

static int partno_from_devname(char *s)
{
	intmax_t num;
	size_t sz;
	char *end, *p;

	if (!s || !*s)
		return -1;

	sz = rtrim_whitespace((unsigned char *)s);
	end = p = s + sz;

	while (p > s && isdigit(*(p - 1)))
		p--;
	if (p == end)
		return -1;
	end = NULL;
	errno = 0;
	num = strtol(p, &end, 10);
	if (errno || !end || p == end)
		return -1;

	if (num < INT32_MIN || num > INT32_MAX) {
		errno = ERANGE;
		return -1;
	}
	return num - 1;
}

static char *next_token(char **str)
{
	char *tk_begin = NULL,
	     *tk_end = NULL,
	     *end = NULL,
	     *p;
	int open_quote = 0, terminated = 0;

	for (p = *str; p && *p; p++) {
		if (!tk_begin) {
			if (isblank(*p))
				continue;
			tk_begin = *p == '"' ? p + 1 : p;
		}
		if (*p == '"')
			open_quote ^= 1;
		if (open_quote)
			continue;
		if (isblank(*p) || *p == ',' || *p == ';' || *p == '"' )
			tk_end = p;
		else if (*(p + 1) == '\0')
			tk_end = p + 1;
		if (tk_begin && tk_end)
			break;
	}

	if (!tk_end)
		return NULL;

	end = tk_end;

	if (*end == '"')
		end++;

	if (isblank(*end)) {
		end = (char *) skip_blank(end);
		terminated++;
	}

	if (*end == ',' || *end == ';') {
		end++;
		terminated++;

	} else if (!*end)
		terminated++;

	if (!terminated) {
		DBG(SCRIPT, ul_debug("unterminated token '%s'", end));
		return NULL;
	}

	end = (char *) skip_blank(end);

	*tk_end = '\0';
	*str = end;
	return tk_begin;
}

static int is_default_value(char **str)
{
	char *p = (char *) skip_blank(*str);
	int blank = 0;

	if (*p == '-') {
		char *x = ++p;
		p = (char *) skip_blank(x);
		blank = x < p;
	}

	if (*p == ';' || *p == ',') {
		*str = ++p;
		return 1;
	}
	if (*p == '\0' || blank) {
		*str = p;
		return 1;
	}

	return 0;
}

static int next_string(char **s, char **str)
{
	char *tk, *p = NULL;
	int rc = -EINVAL;

	assert(s);
	assert(str);

	tk = next_token(s);
	if (tk) {
		p = strdup(tk);
		rc = p ? 0 : -ENOMEM;
	}

	*str = p;
	return rc;
}

static int skip_optional_sign(char **str)
{
	char *p = (char *) skip_blank(*str);

	if (*p == '-' || *p == '+') {
		*str = p+1;
		return *p;
	}
	return 0;
}

static int recount_script2device_sectors(struct fdisk_script *dp, uint64_t *num)
{
	if (!dp->cxt ||
	    !dp->sector_size ||
	    !dp->cxt->sector_size)
		return 0;

	if (dp->sector_size > dp->cxt->sector_size)
		 *num *= (dp->sector_size / dp->cxt->sector_size);

	else if (dp->sector_size < dp->cxt->sector_size) {
		uint64_t x = dp->cxt->sector_size / dp->sector_size;

		if (*num % x)
			return -EINVAL;
		*num /= x;
	}

	return 0;
}

static int parse_start_value(struct fdisk_script *dp, struct fdisk_partition *pa, char **str)
{
	char *tk;
	int rc = 0;

	assert(str);

	if (is_default_value(str)) {
		fdisk_partition_start_follow_default(pa, 1);
		return 0;
	}

	tk = next_token(str);
	if (!tk)
		return -EINVAL;

	if (strcmp(tk, "+") == 0) {
		fdisk_partition_start_follow_default(pa, 1);
		pa->movestart = FDISK_MOVE_DOWN;
	} else {
		int pow = 0, sign = skip_optional_sign(&tk);
		uint64_t num;

		rc = ul_parse_size(tk, (uintmax_t *) &num, &pow);
		if (!rc) {
			if (pow) {
				if (!dp->cxt->sector_size) {
					rc = -EINVAL;
					goto done;
				}
				num /= dp->cxt->sector_size;
			} else {
				rc = recount_script2device_sectors(dp, &num);
				if (rc) {
					fdisk_warnx(dp->cxt, _("Can't recalculate partition start to the device sectors"));
					goto done;
				}
			}

			fdisk_partition_set_start(pa, num);

			pa->movestart = sign == '-' ? FDISK_MOVE_DOWN :
					sign == '+' ? FDISK_MOVE_UP :
						      FDISK_MOVE_NONE;
		}
		fdisk_partition_start_follow_default(pa, 0);
	}

done:
	DBG_OBJ(SCRIPT, dp, ul_debug("  start parse result: rc=%d, move=%s, start=%"PRIu64", default=%s",
				rc, pa->movestart == FDISK_MOVE_DOWN ? "down" :
				    pa->movestart == FDISK_MOVE_UP ? "up" : "none",
				    pa->start,
				    pa->start_follow_default ? "on" : "off"));
	return rc;
}

static int parse_size_value(struct fdisk_script *dp, struct fdisk_partition *pa, char **str)
{
	char *tk;
	int rc = 0;

	if (is_default_value(str)) {
		fdisk_partition_end_follow_default(pa, 1);
		return 0;
	}

	tk = next_token(str);
	if (!tk)
		return -EINVAL;

	if (strcmp(tk, "+") == 0) {
		fdisk_partition_end_follow_default(pa, 1);
		pa->resize = FDISK_RESIZE_ENLARGE;
	} else {

		int pow = 0, sign = skip_optional_sign(&tk);
		uint64_t num;

		rc = ul_parse_size(tk, (uintmax_t *) &num, &pow);
		if (!rc) {
			if (pow) {
				if (!dp->cxt->sector_size) {
					rc = -EINVAL;
					goto done;
				}
				num /= dp->cxt->sector_size;
			} else {

				fdisk_partition_size_explicit(pa, 1);
				rc = recount_script2device_sectors(dp, &num);
				if (rc) {
					fdisk_warnx(dp->cxt, _("Can't recalculate partition size to the device sectors"));
					goto done;
				}
			}

			fdisk_partition_set_size(pa, num);
			pa->resize = sign == '-' ? FDISK_RESIZE_REDUCE :
				     sign == '+' ? FDISK_RESIZE_ENLARGE :
						   FDISK_RESIZE_NONE;
		}
		fdisk_partition_end_follow_default(pa, 0);
	}

done:
	DBG_OBJ(SCRIPT, dp, ul_debug("  size parse result: rc=%d, move=%s, size=%"PRIu64", default=%s",
				rc, pa->resize == FDISK_RESIZE_REDUCE ? "reduce" :
				    pa->resize == FDISK_RESIZE_ENLARGE ? "enlarge" : "none",
				    pa->size,
				    pa->end_follow_default ? "on" : "off"));
	return rc;
}

#define FDISK_SCRIPT_PARTTYPE_PARSE_FLAGS \
	(FDISK_PARTTYPE_PARSE_DATA | FDISK_PARTTYPE_PARSE_DATALAST | \
	 FDISK_PARTTYPE_PARSE_SHORTCUT | FDISK_PARTTYPE_PARSE_ALIAS | \
	 FDISK_PARTTYPE_PARSE_NAME | \
	 FDISK_PARTTYPE_PARSE_DEPRECATED)

static int parse_line_nameval(struct fdisk_script *dp, char *s)
{
	char *p, *x;
	struct fdisk_partition *pa;
	int rc = 0;
	int pno;

	assert(dp);
	assert(s);
	assert(dp->table);

	DBG_OBJ(SCRIPT, dp, ul_debug("   parse script line: '%s'", s));

	pa = fdisk_new_partition();
	if (!pa)
		return -ENOMEM;

	fdisk_partition_start_follow_default(pa, 1);
	fdisk_partition_end_follow_default(pa, 1);
	fdisk_partition_partno_follow_default(pa, 1);

	p = strstr(s, " : ");
	if (!p)
		p = strstr(s, " :");
	if (!p)
		p = strstr(s, ": ");
	if (!p)
		p = strchr(s, ':');

	x = strchr(s, '=');
	if (p && (!x || p < x)) {
		*p = '\0';
		p++;

		pno = partno_from_devname(s);
		if (pno >= 0) {
			fdisk_partition_partno_follow_default(pa, 0);
			fdisk_partition_set_partno(pa, pno);
		}
		if (*p == ':')
			p++;
	} else
		p = s;

	while (rc == 0 && p && *p) {

		DBG_OBJ(SCRIPT, dp, ul_debug(" parsing '%s'", p));
		p = (char *) skip_blank(p);

		if (!c_strncasecmp(p, "start=", 6)) {
			p += 6;
			rc = parse_start_value(dp, pa, &p);

		} else if (!c_strncasecmp(p, "size=", 5)) {
			p += 5;
			rc = parse_size_value(dp, pa, &p);

		} else if (!c_strncasecmp(p, "bootable", 8)) {

			char *tk = next_token(&p);
			if (tk && c_strcasecmp(tk, "bootable") == 0)
				pa->boot = 1;
			else
				rc = -EINVAL;

		} else if (!c_strncasecmp(p, "attrs=", 6)) {
			p += 6;
			free(pa->attrs);
			rc = next_string(&p, &pa->attrs);

		} else if (!c_strncasecmp(p, "uuid=", 5)) {
			p += 5;
			free(pa->uuid);
			rc = next_string(&p, &pa->uuid);

		} else if (!c_strncasecmp(p, "name=", 5)) {
			p += 5;
			free(pa->name);
			rc = next_string(&p, &pa->name);
			if (!rc)
				unhexmangle_string(pa->name);

		} else if (!c_strncasecmp(p, "type=", 5) ||
			   !c_strncasecmp(p, "Id=", 3)) {
			char *type = NULL;

			fdisk_unref_parttype(pa->type);
			pa->type = NULL;

			p += ((*p == 'I' || *p == 'i') ? 3 : 5);

			rc = next_string(&p, &type);
			if (rc == 0) {
				pa->type = fdisk_label_advparse_parttype(script_get_label(dp),
					type, FDISK_SCRIPT_PARTTYPE_PARSE_FLAGS);
				if (!pa->type)
					rc = -EINVAL;
			}
			free(type);
		} else {
			DBG_OBJ(SCRIPT, dp, ul_debug("script parse error: unknown field '%s'", p));
			rc = -EINVAL;
			break;
		}
	}

	if (!rc)
		rc = fdisk_table_add_partition(dp->table, pa);
	if (rc)
		DBG_OBJ(SCRIPT, dp, ul_debug("script parse error: [rc=%d]", rc));

	fdisk_unref_partition(pa);
	return rc;
}

static int parse_line_valcommas(struct fdisk_script *dp, char *s)
{
	int rc = 0;
	char *p = s;
	struct fdisk_partition *pa;
	enum { ITEM_START, ITEM_SIZE, ITEM_TYPE, ITEM_BOOTABLE };
	int item = -1;

	assert(dp);
	assert(s);
	assert(dp->table);

	pa = fdisk_new_partition();
	if (!pa)
		return -ENOMEM;

	fdisk_partition_start_follow_default(pa, 1);
	fdisk_partition_end_follow_default(pa, 1);
	fdisk_partition_partno_follow_default(pa, 1);

	while (rc == 0 && p && *p) {
		char *begin;

		p = (char *) skip_blank(p);
		item++;

		DBG_OBJ(SCRIPT, dp, ul_debug(" parsing item %d ('%s')", item, p));
		begin = p;

		switch (item) {
		case ITEM_START:
			rc = parse_start_value(dp, pa, &p);
			break;
		case ITEM_SIZE:
			rc = parse_size_value(dp, pa, &p);
			break;
		case ITEM_TYPE:
		{
			char *str = NULL;

			fdisk_unref_parttype(pa->type);
			pa->type = NULL;

			if (*p == ',' || *p == ';' || is_default_value(&p))
				break;

			rc = next_string(&p, &str);
			if (rc)
				break;

			pa->type = fdisk_label_advparse_parttype(script_get_label(dp),
						str, FDISK_SCRIPT_PARTTYPE_PARSE_FLAGS);
			free(str);
			if (!pa->type)
				rc = -EINVAL;
			break;
		}
		case ITEM_BOOTABLE:
			if (*p == ',' || *p == ';')
				break;
			else {
				char *tk = next_token(&p);
				if (tk && *tk == '*' && *(tk + 1) == '\0')
					pa->boot = 1;
				else if (tk && *tk == '-' && *(tk + 1) == '\0')
					pa->boot = 0;
				else if (tk && *tk == '+' && *(tk + 1) == '\0')
					pa->boot = 1;
				else
					rc = -EINVAL;
			}
			break;
		default:
			break;
		}

		if (begin == p)
			p++;
	}

	if (!rc)
		rc = fdisk_table_add_partition(dp->table, pa);
	if (rc)
		DBG_OBJ(SCRIPT, dp, ul_debug("script parse error: [rc=%d]", rc));

	fdisk_unref_partition(pa);
	return rc;
}

static int fdisk_script_read_buffer(struct fdisk_script *dp, char *s)
{
	int rc = 0;

	assert(dp);
	assert(s);

	DBG_OBJ(SCRIPT, dp, ul_debug("  parsing buffer"));

	s = (char *) skip_blank(s);
	if (!s || !*s)
		return 0;

	if (!dp->table && fdisk_script_get_table(dp) == NULL)
		return -ENOMEM;

	if (fdisk_table_is_empty(dp->table) && is_header_line(s))
		rc = parse_line_header(dp, s);

	else if (strchr(s, '='))
		rc = parse_line_nameval(dp, s);

	else
		rc = parse_line_valcommas(dp, s);

	if (rc)
		DBG_OBJ(SCRIPT, dp, ul_debug("%zu: parse error [rc=%d]",
				dp->nlines, rc));
	return rc;
}

int fdisk_script_set_fgets(struct fdisk_script *dp,
			  char *(*fn_fgets)(struct fdisk_script *, char *, size_t, FILE *))
{
	assert(dp);

	dp->fn_fgets = fn_fgets;
	return 0;
}

int fdisk_script_read_line(struct fdisk_script *dp, FILE *f, char *buf, size_t bufsz)
{
	char *s;

	assert(dp);
	assert(f);
	assert(bufsz);

	DBG_OBJ(SCRIPT, dp, ul_debug(" parsing line %zu", dp->nlines));

	do {
		buf[0] = '\0';
		if (dp->fn_fgets) {
			if (dp->fn_fgets(dp, buf, bufsz, f) == NULL)
				return 1;
		} else if (fgets(buf, bufsz, f) == NULL)
			return 1;

		dp->nlines++;
		s = strchr(buf, '\n');
		if (!s) {

			if (feof(f)) {
				DBG_OBJ(SCRIPT, dp, ul_debug("no final newline"));
				s = strchr(buf, '\0');
			} else {
				DBG_OBJ(SCRIPT, dp, ul_debug(
					"%zu: missing newline at line", dp->nlines));
				return -EINVAL;
			}
		}

		*s = '\0';
		if (--s >= buf && *s == '\r')
			*s = '\0';
		s = (char *) skip_blank(buf);
	} while (*s == '\0' || *s == '#');

	return fdisk_script_read_buffer(dp, s);
}

int fdisk_script_read_file(struct fdisk_script *dp, FILE *f)
{
	char buf[BUFSIZ] = { '\0' };
	int rc = 1;

	assert(dp);
	assert(f);

	DBG_OBJ(SCRIPT, dp, ul_debug("parsing file"));

	while (!feof(f)) {
		rc = fdisk_script_read_line(dp, f, buf, sizeof(buf));
		if (rc && rc != -ENOTSUP)
			break;
	}

	if (rc == 1)
		rc = 0;

	DBG_OBJ(SCRIPT, dp, ul_debug("parsing file done [rc=%d]", rc));
	return rc;
}

int fdisk_set_script(struct fdisk_context *cxt, struct fdisk_script *dp)
{
	assert(cxt);

	if (cxt->script)
		fdisk_unref_script(cxt->script);

	cxt->script = dp;
	if (cxt->script) {
		DBG_OBJ(CXT, cxt, ul_debug("setting reference to script %p", cxt->script));
		fdisk_ref_script(cxt->script);
	}

	return 0;
}

struct fdisk_script *fdisk_get_script(struct fdisk_context *cxt)
{
	assert(cxt);
	return cxt->script;
}

int fdisk_apply_script_headers(struct fdisk_context *cxt, struct fdisk_script *dp)
{
	const char *name;
	const char *str;
	int rc;

	assert(cxt);
	assert(dp);

	DBG_OBJ(SCRIPT, dp, ul_debug("applying script headers"));
	fdisk_set_script(cxt, dp);

	if (dp->sector_size && dp->cxt->sector_size != dp->sector_size) {

		if (fdisk_script_get_header(dp, "first-lba")) {
			fdisk_script_set_header(dp, "first-lba", NULL);
			fdisk_info(dp->cxt, _("Ignore \"first-lba\" header due to sector size mismatch."));
		}
		if (fdisk_script_get_header(dp, "last-lba")) {
			fdisk_script_set_header(dp, "last-lba", NULL);
			fdisk_info(dp->cxt, _("Ignore \"last-lba\" header due to sector size mismatch."));
		}
	}

	str = fdisk_script_get_header(dp, "grain");
	if (str) {
		uintmax_t sz;

		rc = ul_parse_size(str, &sz, NULL);
		if (rc == 0)
			rc = fdisk_save_user_grain(cxt, sz);
		if (rc)
			return rc;
	}

	if (fdisk_has_user_device_properties(cxt)) {
		rc = fdisk_apply_user_device_properties(cxt);
		if (rc)
			return rc;
	}

	name = fdisk_script_get_header(dp, "label");
	if (!name)
		return -EINVAL;

	rc = fdisk_create_disklabel(cxt, name);
	if (rc)
		return rc;

	str = fdisk_script_get_header(dp, "table-length");
	if (str) {
		uintmax_t sz;

		rc = ul_parse_size(str, &sz, NULL);
		if (rc == 0)
			rc = fdisk_gpt_set_npartitions(cxt, sz);
	}

	return rc;
}

int fdisk_apply_script(struct fdisk_context *cxt, struct fdisk_script *dp)
{
	int rc;
	struct fdisk_script *old;

	assert(dp);
	assert(cxt);

	DBG_OBJ(CXT, cxt, ul_debug("applying script %p", dp));

	old = fdisk_get_script(cxt);
	fdisk_ref_script(old);

	rc = fdisk_apply_script_headers(cxt, dp);

	if (!rc && dp->table)
		rc = fdisk_apply_table(cxt, dp->table);

	fdisk_set_script(cxt, old);
	fdisk_unref_script(old);

	DBG_OBJ(CXT, cxt, ul_debug("script done [rc=%d]", rc));
	return rc;
}

#ifdef FUZZ_TARGET

#ifdef __cplusplus
#define QF_EXTERN extern "C"
#else
#define QF_EXTERN extern
#endif

QF_EXTERN int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size == 0)
		return 0;

	FILE *in = fmemopen((void *) data, size, "r");
	if (!in)
		return 0;

	struct fdisk_context *cxt = fdisk_new_context();
	if (!cxt) {
		fclose(in);
		return 0;
	}

	char tmpl[] = "/tmp/fdisk_script_fuzz_XXXXXX";
	int devfd = mkstemp(tmpl);
	int assigned = 0;

	if (devfd >= 0) {

		if (ftruncate(devfd, 1024 * 1024) != 0) {

		}
		close(devfd);
		assigned = fdisk_assign_device(cxt, tmpl, 0) == 0;
	}

	struct fdisk_script *dp = fdisk_new_script(cxt);
	if (!dp) {
		fclose(in);
		if (assigned)
			fdisk_deassign_device(cxt, 1);
		if (devfd >= 0)
			unlink(tmpl);
		fdisk_unref_context(cxt);
		return 0;
	}

	int rc = fdisk_script_read_file(dp, in);
	fclose(in);

	struct fdisk_table *tb = fdisk_script_get_table(dp);
	if (tb) {
		struct fdisk_iter *itr = fdisk_new_iter(FDISK_ITER_FORWARD);

		if (itr) {
			struct fdisk_partition *pa = NULL;

			while (fdisk_table_next_partition(tb, itr, &pa) == 0) {
				fdisk_partition_get_start(pa);
				fdisk_partition_get_size(pa);
				fdisk_partition_is_bootable(pa);
			}
			fdisk_free_iter(itr);
		}
	}

	{
		char *outbuf = NULL;
		size_t outsz = 0;
		FILE *out = open_memstream(&outbuf, &outsz);

		if (out) {
			fdisk_script_write_file(dp, out);
			fclose(out);
			free(outbuf);
		}
	}

	if (rc == 0 && assigned) {
		if (fdisk_apply_script(cxt, dp) == 0)
			fdisk_write_disklabel(cxt);
	}

	fdisk_unref_script(dp);

	if (assigned)
		fdisk_deassign_device(cxt, 1);
	if (devfd >= 0)
		unlink(tmpl);

	fdisk_unref_context(cxt);

	return 0;
}

#endif

#ifdef TEST_PROGRAM
static int test_dump(struct fdisk_test *ts __attribute__((unused)),
		     int argc, char *argv[])
{
	if (argc != 2)
		return -1;

	char *devname = argv[1];
	struct fdisk_context *cxt;
	struct fdisk_script *dp;

	cxt = fdisk_new_context();
	fdisk_assign_device(cxt, devname, 1);

	dp = fdisk_new_script(cxt);
	fdisk_script_read_context(dp, NULL);

	fdisk_script_write_file(dp, stdout);
	fdisk_unref_script(dp);
	fdisk_unref_context(cxt);

	return 0;
}

static int test_read(struct fdisk_test *ts __attribute__((unused)),
		     int argc, char *argv[])
{
	if (argc != 2)
		return -1;

	char *filename = argv[1];
	struct fdisk_script *dp;
	struct fdisk_context *cxt;
	FILE *f;

	if (!(f = fopen(filename, "r")))
		err(EXIT_FAILURE, "%s: cannot open", filename);

	cxt = fdisk_new_context();
	dp = fdisk_new_script(cxt);

	fdisk_script_read_file(dp, f);
	fclose(f);

	fdisk_script_write_file(dp, stdout);
	fdisk_unref_script(dp);
	fdisk_unref_context(cxt);

	return 0;
}

static int test_stdin(struct fdisk_test *ts __attribute__((unused)),
		      int argc, char *argv[] __attribute__((unused)))
{
	if (argc != 1)
		return -1;

	char buf[BUFSIZ] = { '\0' };
	struct fdisk_script *dp;
	struct fdisk_context *cxt;
	int rc = 0;

	cxt = fdisk_new_context();
	dp = fdisk_new_script(cxt);
	fdisk_script_set_header(dp, "label", "dos");

	printf("<start>, <size>, <type>, <bootable: *|->\n");
	do {
		struct fdisk_partition *pa;
		size_t n = dp->table ? fdisk_table_get_nents(dp->table) : 0;

		printf(" #%zu :\n", n + 1);
		rc = fdisk_script_read_line(dp, stdin, buf, sizeof(buf));

		if (rc == 0) {
			pa = fdisk_table_get_partition(dp->table, n);
			printf(" #%zu  %12ju %12ju\n",	n + 1,
						(uintmax_t)fdisk_partition_get_start(pa),
						(uintmax_t)fdisk_partition_get_size(pa));
		}
	} while (rc == 0);

	if (!rc)
		fdisk_script_write_file(dp, stdout);
	fdisk_unref_script(dp);
	fdisk_unref_context(cxt);

	return rc;
}

static int test_apply(struct fdisk_test *ts __attribute__((unused)),
		      int argc, char *argv[])
{
	if (argc != 3)
		return -1;

	char *devname = argv[1], *scriptname = argv[2];
	struct fdisk_context *cxt;
	struct fdisk_script *dp;
	struct fdisk_table *tb = NULL;
	struct fdisk_iter *itr = NULL;
	struct fdisk_partition *pa = NULL;
	int rc;

	cxt = fdisk_new_context();
	fdisk_assign_device(cxt, devname, 0);

	dp = fdisk_new_script_from_file(cxt, scriptname);
	if (!dp)
		return -errno;

	rc = fdisk_apply_script(cxt, dp);
	if (rc)
		goto done;
	fdisk_unref_script(dp);

	fdisk_list_disklabel(cxt);
	fdisk_get_partitions(cxt, &tb);

	itr = fdisk_new_iter(FDISK_ITER_FORWARD);
	while (fdisk_table_next_partition(tb, itr, &pa) == 0) {
		printf(" #%zu  %12ju %12ju\n",	fdisk_partition_get_partno(pa),
						(uintmax_t)fdisk_partition_get_start(pa),
						(uintmax_t)fdisk_partition_get_size(pa));
	}

done:
	fdisk_free_iter(itr);
	fdisk_unref_table(tb);

	fdisk_unref_context(cxt);
	return 0;
}

static int test_tokens(struct fdisk_test *ts __attribute__((unused)),
		       int argc, char *argv[])
{
	if (argc != 2)
		return -1;

	char *p, *str = argc == 2 ? strdup(argv[1]) : NULL;
	int i;

	for (i = 1, p = str; p && *p; i++) {
		char *tk = next_token(&p);

		if (!tk)
			break;

		printf("#%d: '%s'\n", i, tk);
	}

	free(str);
	return 0;
}

int main(int argc, char *argv[])
{
	struct fdisk_test tss[] = {
	{ "--dump",    test_dump,    "<device>            dump PT as script" },
	{ "--read",    test_read,    "<file>              read PT script from file" },
	{ "--apply",   test_apply,   "<device> <file>     try apply script from file to device" },
	{ "--stdin",   test_stdin,   "                    read input like sfdisk" },
	{ "--tokens",  test_tokens,  "<string>            parse string" },
	{ NULL }
	};

	return fdisk_run_test(tss, argc, argv);
}

#endif
