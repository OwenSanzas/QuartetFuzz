#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "md4c-html.h"
#include "md4c.h"

struct membuffer {
    char* data;
    size_t size;
    size_t alloc;
};

static void
membuf_init(struct membuffer* buf)
{
    buf->size = 0;
    buf->alloc = 256;
    buf->data = (char*) malloc(buf->alloc);
}

static void
membuf_fini(struct membuffer* buf)
{
    if (buf->data != NULL)
        free(buf->data);
    buf->data = NULL;
    buf->size = 0;
    buf->alloc = 0;
}

static void
membuf_grow(struct membuffer* buf, size_t new_size)
{
    if (new_size > buf->alloc) {
        size_t new_alloc = buf->alloc;
        while (new_alloc < new_size)
            new_alloc *= 2;
        buf->data = (char*) realloc(buf->data, new_alloc);
        buf->alloc = new_alloc;
    }
}

static void
membuf_append(struct membuffer* buf, const char* data, size_t size)
{
    if (size == 0)
        return;
    membuf_grow(buf, buf->size + size);
    memcpy(buf->data + buf->size, data, size);
    buf->size += size;
}

static void
process_output(const MD_CHAR* text, MD_SIZE size, void* userdata)
{
    struct membuffer* buf = (struct membuffer*) userdata;
    membuf_append(buf, text, (size_t) size);
}

static const unsigned s_parser_flag_combos[] = {
    0,
    MD_DIALECT_COMMONMARK,
    MD_DIALECT_GITHUB,
    MD_FLAG_COLLAPSEWHITESPACE,
    MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS,
    MD_FLAG_FOOTNOTES | MD_FLAG_WIKILINKS,
    MD_FLAG_LATEXMATHSPANS | MD_FLAG_UNDERLINE | MD_FLAG_SPOILERS,
    MD_FLAG_SUPERSCRIPTS | MD_FLAG_SUBSCRIPTS | MD_FLAG_HIGHLIGHT | MD_FLAG_INSERT,
    MD_FLAG_ADMONITIONS | MD_FLAG_PRESERVEBLANKLINES | MD_FLAG_HARD_SOFT_BREAKS,
    MD_FLAG_NOINDENTEDCODEBLOCKS | MD_FLAG_NOHTMLBLOCKS | MD_FLAG_NOHTMLSPANS,
    MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_PERMISSIVEATXHEADERS,
    MD_FLAG_TABLES | MD_FLAG_TASKLISTS | MD_FLAG_FOOTNOTES | MD_FLAG_ADMONITIONS | MD_FLAG_COLLAPSEWHITESPACE,
    ~0u,
};

static const unsigned s_renderer_flag_combos[] = {
    0,
    MD_HTML_FLAG_DEBUG,
    MD_HTML_FLAG_VERBATIM_ENTITIES,
    MD_HTML_FLAG_SKIP_UTF8_BOM,
    MD_HTML_FLAG_XHTML,
    MD_HTML_FLAG_DEBUG | MD_HTML_FLAG_XHTML | MD_HTML_FLAG_VERBATIM_ENTITIES | MD_HTML_FLAG_SKIP_UTF8_BOM,
};

enum {
    TOK_SCRIPT = 0,
    TOK_PRE,
    TOK_STYLE,
    TOK_COMMENT,
    TOK_PHP,
    TOK_DOCTYPE,
    TOK_CDATA,
    TOK_DIV,
    TOK_TABLE_SIMPLE,
    TOK_UL,
    TOK_ENTITY_SURROGATE,
    TOK_ENTITY_OOR,
    TOK_ENTITY_MISC,
    TOK_FOOTNOTE,
    TOK_REFLINK,
    TOK_NESTED_PARENS,
    TOK_PIPE_TABLE,
    TOK_EXT_SPANS,
    TOK_IMAGE,
    TOK_ADMON_NOTE,
    TOK_ADMON_WARNING,
    TOK_TASKLIST,
    TOK_HTML_CUSTOM,
    TOK_SETEXT_EQ,
    TOK_SETEXT_DASH,
    TOK_NESTED_CONTAINERS,
    TOK_UNICODE_BOUNDARY,
    TOK_REF_HASH_COLLISION,
    TOK_BLANKLINES_FENCE,
    TOK_BLANKLINES_HTML,
    TOK_BLANKLINES_LIST,
    TOK_BLANKLINES_NESTED,
    TOK_BLANKLINES_SPLIT,
    TOK_BLANKLINES_WS,
    TOK_BLANKLINES_ATX_HR,
    TOK_ADMON_TIP,
    TOK_ADMON_IMPORTANT,
    TOK_ADMON_CAUTION,
    TOK_UNICODE_FOLD,
    TOK_ESCAPED_DEST,
    TOK_MULTILINE_TITLE,
    TOK_LABEL_WS_NORMALIZE,

    TOK_LATEXMATH,
    TOK_UNDERLINE,
    TOK_SPOILER,
    TOK_WIKILINK,
    TOK_PERMISSIVE_AUTOLINK,
    TOK_PERMISSIVE_ATX,
    TOK_STD_AUTOLINK,
    TOK_INLINE_RAW_HTML,
    TOK_FENCE_LANG,
    TOK_ORDERED_LIST_START,
    TOK_TABLE_ALIGN,
    TOK_NESTED_IMAGE,
    TOK_REF_REPEAT_BUDGET,
    N_TOKENS
};

static const char* const s_md_tokens[N_TOKENS] = {
                 "<script>\nalert(1);\n</script>\n",
                     "<pre>\nsome pre text\n</pre>\n",
                   "<style>\nbody{color:red;}\n</style>\n",
                 "<!-- a comment -->\n",
                     "<?php echo 1; ?>\n",
                 "<!DOCTYPE html>\n",
                   "<![CDATA[ some data ]]>\n",
                     "<div>\nhello\n</div>\n",
            "<table><tr><td>x</td></tr></table>\n",
                      "<ul>\n<li>item</li>\n</ul>\n",
        "&#xD800;\n",
              "&#1114112;\n",
             "&copy;&nbsp;&amp;&#1234;&#xabcd;\n",
                "[^1]: footnote definition text\nSee the note[^1].\n",
                 "[ref]: /url \"title\"\n[ref][] [ref](/url 'title') [ref](/url (title))\n",
           "[text](((((((((((((((((((((((((((((((((((url)))))))))))))))))))))))))))))))))))\n",
              "| a | b |\n|---|---|\n| 1 | 2 |\n",
               "~~strike~~ ==mark== ^sup^ ~sub~ ++ins++\n",

                   "![alt *em* text](/img.png \"title\")\n",

              "> [!NOTE]\nSome note body.\n",
           "> [!WARNING]\nSome warning body.\n",

                "- [ ] todo item\n- [x] done item\n",

             "<customtag attr=\"v\">\nbody\n</customtag>\n",

               "Heading text\n===========\n",
             "Heading text\n-----------\n",

       "> > > > > * * * * * nested\n",

    "X__foo__X\n\xC5\x98__foo__\xC5\x98\n\xE0\xAE\xA3__foo__\xE0\xAE\xA3\n"
    "\xF0\x93\x85\x82__foo__\xF0\x93\x85\x82\n"
    "x\xE2\x80\x80__foo__\xE2\x80\x80\n.\xC2\xB7__foo__\xC2\xB7\n",

    "[foo]: /foo\n[qnptgbh]: /qnptgbh\n[abgbrwcv]: /abgbrwcv\n"
    "[abgbrwcv]: /abgbrwcv2\n[abgbrwcv]: /abgbrwcv3\n[abgbrwcv]: /abgbrwcv4\n"
    "[alqadfgn]: /alqadfgn\n\n[foo]\n[qnptgbh]\n[abgbrwcv]\n[alqadfgn]\n[axgydtdu]\n",

        "```\nfoo\n```\nbar\n",

         "<div>\nx\n\n\nafter\n",

         "* a\n\n\n* b\n",

       "> * a\n>\n>\n> * b\n",

    "> quote with trailing blank lines.\n>\n>\n\n\nafter the quote.\n",

           "foo\n   \n   \nbar\n",

       "para\n\n\n# H\n\n\n---\n\n\nend\n",

               "> [!TIP]\nSome tip body.\n",
         "> [!IMPORTANT]\nSome important body.\n",
           "> [!CAUTION]\nSome caution body.\n",

    "[P\xC5\x99\xC3\xADli\xC5\xA1 \xC5\xBElu\xC5\xA5ou\xC4\x8dk\xC3\xBD "
    "k\xC5\xAF\xC5\x88 \xC3\xBAp\xC4\x9bl \xC4\x8f\xC3\xA1" "belsk\xC3\xA9 "
    "\xC3\xB3" "dy.]\n\n"
    "[P\xC5\x98\xC3\x8dLI\xC5\xA0 \xC5\xBDLU\xC5\xA4OU\xC4\x8cK\xC3\x9d "
    "K\xC5\xae\xC5\x87 \xC3\x9aP\xC4\x9aL \xC4\x8e\xC3\x81" "BELSK\xC3\x89 "
    "\xC3\x93" "DY.]: /url\n",

            "[link](</url\\.with\\.escape>)\n",

         "> [link](/url 'foo\n> bar')\n",

      "[foo   bar]\n\n[foo bar]: /url\n",

               "Inline $x^2+y^2=z^2$ and block $$\\int_0^1 f(x)dx$$\n",

               "This is _underlined_ text.\n",

                 "This is ||a spoiler|| text.\n",

                "See [[Some Wiki Page]] for details.\n",

      "Visit www.example.com or mail me@example.com today.\n",

          "#Heading-no-space\nbody\n",

            "See <http://example.com> or <user@example.com>.\n",

         "Some <span class=\"x\" data-y='z'>inline html</span> text.\n",

              "```c\nint main(void) { return 0; }\n```\n",

      "7. seven\n8. eight\n9. nine\n",

             "| a | b | c |\n|:--|:--:|--:|\n| 1 | 2 | 3 |\n",

            "![alt ![inner](/inner.png) text](/outer.png \"t\")\n",

    "[r]: /url-with-a-fairly-long-destination-string \"a reasonably long title too\"\n"
    "[r][r][r][r][r][r][r][r][r][r][r][r][r][r][r][r][r][r][r][r]\n"
    "[r][r][r][r][r][r][r][r][r][r][r][r][r][r][r][r][r][r][r][r]\n",
};

static unsigned
required_flags_for_token(unsigned idx)
{
    switch (idx) {
        case TOK_FOOTNOTE:
        case TOK_REF_HASH_COLLISION:
            return MD_FLAG_FOOTNOTES;
        case TOK_PIPE_TABLE:
            return MD_FLAG_TABLES;
        case TOK_TASKLIST:
            return MD_FLAG_TASKLISTS;
        case TOK_EXT_SPANS:
            return MD_FLAG_STRIKETHROUGH | MD_FLAG_HIGHLIGHT |
                   MD_FLAG_SUPERSCRIPTS | MD_FLAG_SUBSCRIPTS | MD_FLAG_INSERT;
        case TOK_ADMON_NOTE:
        case TOK_ADMON_WARNING:
        case TOK_ADMON_TIP:
        case TOK_ADMON_IMPORTANT:
        case TOK_ADMON_CAUTION:
            return MD_FLAG_ADMONITIONS;
        case TOK_BLANKLINES_FENCE:
        case TOK_BLANKLINES_HTML:
        case TOK_BLANKLINES_LIST:
        case TOK_BLANKLINES_NESTED:
        case TOK_BLANKLINES_SPLIT:
        case TOK_BLANKLINES_WS:
        case TOK_BLANKLINES_ATX_HR:
            return MD_FLAG_PRESERVEBLANKLINES;
        case TOK_LATEXMATH:
            return MD_FLAG_LATEXMATHSPANS;
        case TOK_UNDERLINE:
            return MD_FLAG_UNDERLINE;
        case TOK_SPOILER:
            return MD_FLAG_SPOILERS;
        case TOK_WIKILINK:

            return MD_FLAG_WIKILINKS;
        case TOK_PERMISSIVE_AUTOLINK:
            return MD_FLAG_PERMISSIVEAUTOLINKS;
        case TOK_PERMISSIVE_ATX:
            return MD_FLAG_PERMISSIVEATXHEADERS;
        case TOK_TABLE_ALIGN:
            return MD_FLAG_TABLES;
        case TOK_REF_REPEAT_BUDGET:
            return 0;
        default:
            return 0;
    }
}

static char*
build_sparse_table(size_t* out_len)
{
    const unsigned n_cols = 40;
    const unsigned n_body_rows = 150;
    size_t cap = 4 + (size_t)n_cols * 2 + 4 + (size_t)n_body_rows * 2 + 16;
    char* buf = (char*) malloc(cap);
    size_t pos = 0;
    unsigned i;

    if (buf == NULL) {
        *out_len = 0;
        return NULL;
    }

    buf[pos++] = 'h';
    buf[pos++] = '\n';

    buf[pos++] = '|';
    for (i = 0; i < n_cols; i++) {
        buf[pos++] = '-';
        buf[pos++] = '|';
    }
    buf[pos++] = '\n';

    for (i = 0; i < n_body_rows; i++) {
        buf[pos++] = 'x';
        buf[pos++] = '\n';
    }

    *out_len = pos;
    return buf;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    unsigned parser_flags = 0;
    unsigned renderer_flags = 0;
    const uint8_t* body = data;
    size_t body_size = size;
    unsigned char sel = 0;
    unsigned char feature_sel = 0;
    unsigned i;

    if (size >= 4) {
        parser_flags = s_parser_flag_combos[
            data[0] % (sizeof(s_parser_flag_combos) / sizeof(s_parser_flag_combos[0]))];
        renderer_flags = s_renderer_flag_combos[
            data[1] % (sizeof(s_renderer_flag_combos) / sizeof(s_renderer_flag_combos[0]))];
        sel = data[2];
        feature_sel = data[3];
        body = data + 4;
        body_size = size - 4;
    }

    const size_t n_tokens_table = sizeof(s_md_tokens) / sizeof(s_md_tokens[0]);
    unsigned n_tokens = sel % 8;
    size_t tokens_len = 0;
    size_t token_idx[8];
    for (i = 0; i < n_tokens; i++) {
        token_idx[i] = (size_t)((sel + i * 7u) % n_tokens_table);
        tokens_len += strlen(s_md_tokens[token_idx[i]]);
        parser_flags |= required_flags_for_token((unsigned) token_idx[i]);
    }

    int add_bom = (sel & 0x80u) ? 1 : 0;

    int add_sparse_table = ((feature_sel & 0x07u) == 0) ? 1 : 0;
    char* sparse_table = NULL;
    size_t sparse_table_len = 0;
    if (add_sparse_table) {
        sparse_table = build_sparse_table(&sparse_table_len);
        if (sparse_table == NULL)
            add_sparse_table = 0;
        else
            parser_flags |= MD_FLAG_TABLES;
    }

    int add_ref_budget_stress = ((feature_sel & 0x38u) == 0x38) ? 1 : 0;
    const char* ref_budget_tok = add_ref_budget_stress ? s_md_tokens[TOK_REF_REPEAT_BUDGET] : NULL;
    size_t ref_budget_len = ref_budget_tok ? strlen(ref_budget_tok) : 0;

    size_t prefix_len = (add_bom ? 3 : 0) + tokens_len +
                         (add_sparse_table ? sparse_table_len : 0) +
                         ref_budget_len;
    size_t total_size = prefix_len + body_size;

    char* md_input = (char*) malloc(total_size > 0 ? total_size : 1);
    if (md_input == NULL) {
        if (sparse_table != NULL)
            free(sparse_table);
        return 0;
    }

    size_t pos = 0;
    if (add_bom) {
        memcpy(md_input + pos, "\xEF\xBB\xBF", 3);
        pos += 3;
    }
    for (i = 0; i < n_tokens; i++) {
        const char* tok = s_md_tokens[token_idx[i]];
        size_t tlen = strlen(tok);
        memcpy(md_input + pos, tok, tlen);
        pos += tlen;
    }
    if (add_sparse_table) {
        memcpy(md_input + pos, sparse_table, sparse_table_len);
        pos += sparse_table_len;
        free(sparse_table);
        sparse_table = NULL;
    }
    if (ref_budget_len > 0) {
        memcpy(md_input + pos, ref_budget_tok, ref_budget_len);
        pos += ref_budget_len;
    }
    if (body_size > 0)
        memcpy(md_input + pos, body, body_size);
    pos += body_size;

    struct membuffer buf_out;
    membuf_init(&buf_out);

    md_html((const MD_CHAR*) md_input, (MD_SIZE) pos,
            process_output, &buf_out, parser_flags, renderer_flags);

    membuf_fini(&buf_out);
    free(md_input);

    return 0;
}