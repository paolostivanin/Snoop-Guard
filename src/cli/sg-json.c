#include "sg-json.h"

gchar *
sg_json_escape (const gchar *input)
{
    const gchar *valid_input = input ? input : "";
    gchar *valid = g_utf8_make_valid (valid_input, -1);
    GString *out = g_string_new (NULL);
    for (const guchar *p = (const guchar *) valid; *p; p++) {
        switch (*p) {
        case '"': g_string_append (out, "\\\""); break;
        case '\\': g_string_append (out, "\\\\"); break;
        case '\b': g_string_append (out, "\\b"); break;
        case '\f': g_string_append (out, "\\f"); break;
        case '\n': g_string_append (out, "\\n"); break;
        case '\r': g_string_append (out, "\\r"); break;
        case '\t': g_string_append (out, "\\t"); break;
        default:
            if (*p < 0x20) g_string_append_printf (out, "\\u%04x", *p);
            else g_string_append_c (out, (gchar) *p);
        }
    }
    g_free (valid);
    return g_string_free (out, FALSE);
}
