#include <stdint.h>
#include <stddef.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>
#include <libxslt/security.h>
#include <libexslt/exslt.h>

#ifndef ATTRIBUTE_UNUSED
#define ATTRIBUTE_UNUSED __attribute__((unused))
#endif

/* Dummy error handler to suppress noise during fuzzing */
static void dummyGenericErrorFunc(void *ctx ATTRIBUTE_UNUSED, const char *msg ATTRIBUTE_UNUSED, ...) {}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 10) return 0;

    static int initialized = 0;
    if (!initialized) {
        xmlInitParser();
        xsltInit();
        exsltRegisterAll();
        /* Redirect errors to dummy function */
        xmlSetGenericErrorFunc(NULL, dummyGenericErrorFunc);
        xsltSetGenericErrorFunc(NULL, dummyGenericErrorFunc);
        initialized = 1;
    }

    /* Split input into two equal parts for stylesheet and source document */
    size_t xslt_len = size / 2;
    size_t xml_len = size - xslt_len;

    const char *xslt_ptr = (const char *)data;
    const char *xml_ptr = (const char *)(data + xslt_len);

    /* Parse XSLT stylesheet XML */
    xmlDocPtr xsltDoc = xmlReadMemory(xslt_ptr, (int)xslt_len, "fuzz_xslt.xml", NULL, XSLT_PARSE_OPTIONS);
    if (xsltDoc == NULL) return 0;

    /* Parse source XML document */
    xmlDocPtr doc = xmlReadMemory(xml_ptr, (int)xml_len, "fuzz_doc.xml", NULL, XSLT_PARSE_OPTIONS);
    if (doc == NULL) {
        xmlFreeDoc(xsltDoc);
        return 0;
    }

    /* Create stylesheet and compile it */
    xsltStylesheetPtr sheet = xsltNewStylesheet();
    if (sheet == NULL) {
        xmlFreeDoc(xsltDoc);
        xmlFreeDoc(doc);
        return 0;
    }

    /* xsltParseStylesheetUser assigns xsltDoc to sheet->doc and will be freed by xsltFreeStylesheet */
    if (xsltParseStylesheetUser(sheet, xsltDoc) != 0) {
        xsltFreeStylesheet(sheet);
        xmlFreeDoc(doc);
        return 0;
    }

    /* Set up security preferences to disallow all I/O for safety */
    xsltSecurityPrefsPtr sec = xsltNewSecurityPrefs();
    if (sec != NULL) {
        xsltSetSecurityPrefs(sec, XSLT_SECPREF_READ_FILE, xsltSecurityForbid);
        xsltSetSecurityPrefs(sec, XSLT_SECPREF_WRITE_FILE, xsltSecurityForbid);
        xsltSetSecurityPrefs(sec, XSLT_SECPREF_CREATE_DIRECTORY, xsltSecurityForbid);
        xsltSetSecurityPrefs(sec, XSLT_SECPREF_READ_NETWORK, xsltSecurityForbid);
        xsltSetSecurityPrefs(sec, XSLT_SECPREF_WRITE_NETWORK, xsltSecurityForbid);
    }

    /* Create transformation context */
    xsltTransformContextPtr ctxt = xsltNewTransformContext(sheet, doc);
    if (ctxt != NULL) {
        if (sec != NULL) xsltSetCtxtSecurityPrefs(sec, ctxt);
        
        /* Apply limits to prevent hangs/OOM */
        ctxt->maxTemplateDepth = 100;
        ctxt->opLimit = 20000;

        /* Core transformation call */
        xmlDocPtr result = xsltApplyStylesheetUser(sheet, doc, NULL, NULL, NULL, ctxt);
        
        if (result != NULL) {
            xmlChar *output = NULL;
            int output_len = 0;
            /* exercise result saving logic */
            xsltSaveResultToString(&output, &output_len, result, sheet);
            if (output) xmlFree(output);
            xmlFreeDoc(result);
        }
        xsltFreeTransformContext(ctxt);
    }

    /* Cleanup all allocated resources */
    if (sec != NULL) xsltFreeSecurityPrefs(sec);
    xsltFreeStylesheet(sheet); /* Frees sheet->doc (xsltDoc) as well */
    xmlFreeDoc(doc);

    return 0;
}
