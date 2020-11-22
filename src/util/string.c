#if !AMALGAMATION
# define INTERNAL
# define EXTERNAL extern
#endif
#include <lacc/context.h>
#include <lacc/string.h>

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int printchar(FILE *stream, char ch)
{
    int c = (unsigned char) ch;
    if (isprint(c) && c != '"' && c != '\\') {
        putc(c, stream);
        return 1;
    }

    switch (c) {
    case '\b':
        return fprintf(stream, "\\b");
    case '\t':
        return fprintf(stream, "\\t");
    case '\n':
        return fprintf(stream, "\\n");
    case '\f':
        return fprintf(stream, "\\f");
    case '\r':
        return fprintf(stream, "\\r");
    case '\\':
        return fprintf(stream, "\\\\");
    case '\"':
        return fprintf(stream, "\\\"");
    default:
        return fprintf(stream, "\\%03o", c);
    }
}

INTERNAL int fprintstr(FILE *stream, String str)
{
    int n, i;
    size_t len;
    const char *raw;

    raw = str_raw(str);
    len = str_len(str);
    putc('"', stream);
    for (n = 0, i = 0; i < len; ++i) {
        n += printchar(stream, raw[i]);
    }

    putc('"', stream);
    return n + 2;
}

INTERNAL String str_init(const char *str)
{
    String s = {0};
    size_t len;

    len = strlen(str);
    str_set(&s, str, len);
    return s;
}

INTERNAL void str_set(String *s, const char *str, size_t len)
{
    if (len < SHORT_STRING_LEN) {
        memcpy(s->data, str, len);
        memset(s->data + len, '\0', 16 - len);
    } else if (len <= MAX_STRING_LEN) {
        s->p.str = str;
        s->p.len = len;
        s->data[15] = 1;
    } else {
        error("String length %lu exceeds maximum supported size.", len);
        exit(1);
    }
}

INTERNAL size_t str_len(String s)
{
    return s.data[15] ? s.p.len & MAX_STRING_LEN : strlen(s.data);
}

INTERNAL int str_isempty(String s)
{
    return s.data[15] ? (s.p.len & MAX_STRING_LEN) == 0 : s.data[0] == '\0';
}

INTERNAL int str_cmp(String s1, String s2)
{
    if (s1.p.len != s2.p.len)
        return 1;

    return s1.data[15]
        ? memcmp(s1.p.str, s2.p.str, s1.p.len & MAX_STRING_LEN)
        : strcmp(s1.data, s2.data);
}

INTERNAL int str_haschr(String s, char c)
{
    int i;
    size_t len;
    const char *str;

    len = str_len(s);
    str = str_raw(s);
    for (i = 0; i < len; ++i) {
        if (str[i] == c) {
            return 1;
        }
    }

    return 0;
}
