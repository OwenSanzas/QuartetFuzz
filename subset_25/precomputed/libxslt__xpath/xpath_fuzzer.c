#include <stdint.h>
#include <stddef.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxslt/xslt.h>
#include <libxslt/xsltutils.h>
#include <libxslt/transform.h>
#include <libexslt/exslt.h>

/*
 * Ignore error messages from libxml2 and libxslt to keep stderr clean.
 */
static void ignore(void* ctx, const char* msg, ...) {
    (void)ctx;
    (void)msg;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4) return 0;

    static int initialized = 0;
    if (!initialized) {
        xmlInitParser();
        xsltInit();
        exsltRegisterAll();
        xmlSetGenericErrorFunc(NULL, (xmlGenericErrorFunc)ignore);
        xsltSetGenericErrorFunc(NULL, (xmlGenericErrorFunc)ignore);
        initialized = 1;
    }

    /*
     * The input is split into two parts: an XSLT stylesheet and an XML document.
     * The first byte is used to determine the split point.
     */
    size_t split = (data[0] * (size - 1)) / 255;
    const uint8_t *xslt_data = data + 1;
    size_t xslt_size = split;
    const uint8_t *xml_data = data + 1 + split;
    size_t xml_size = size - 1 - split;

    /*
     * Use options to prevent network access and external entities.
     * XML_PARSE_RECOVER allows the parser to proceed even with small errors.
     */
    int options = XML_PARSE_NONET | XML_PARSE_NOENT | XML_PARSE_RECOVER;

    /* Parse the stylesheet */
    xmlDocPtr xslt_doc = xmlReadMemory((const char *)xslt_data, xslt_size, "fuzz.xsl", NULL, options);
    if (xslt_doc == NULL) return 0;

    xsltStylesheetPtr sheet = xsltParseStylesheetDoc(xslt_doc);
    if (sheet == NULL) {
        xmlFreeDoc(xslt_doc);
        return 0;
    }

    /* Parse the input XML document */
    xmlDocPtr doc = xmlReadMemory((const char *)xml_data, xml_size, "fuzz.xml", NULL, options);
    if (doc != NULL) {
        /* 
         * Apply the stylesheet to the document.
         * xsltApplyStylesheetUser is the main entry point for transformation.
         * It also handles the integration with XPath evaluation and extension functions.
         */
        xmlDocPtr result = xsltApplyStylesheetUser(sheet, doc, NULL, NULL, NULL, NULL);
        if (result != NULL) {
            xmlFreeDoc(result);
        }
        xmlFreeDoc(doc);
    }

    /* 
     * Cleanup. xsltFreeStylesheet also frees the associated xslt_doc (if it was linked).
     */
    xsltFreeStylesheet(sheet);
    return 0;
}
