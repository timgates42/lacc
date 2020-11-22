#ifndef STRING_H
#define STRING_H
#if !defined(INTERNAL) || !defined(EXTERNAL)
# error Missing amalgamation macros
#endif

#include <stddef.h>
#include <stdio.h>
#include <limits.h>

/*
 * Compact representation of strings, such as identifiers and literals.
 * Optimize for short lengths, storing it inline in the object itself.
 * This type fits in 2 eightbytes.
 *
 * Short strings have first 15 bytes being the string value, and last
 * byte is zero. String length must be computed by counting characters.
 *
 * Long strings (more than 15 chars) are stored as a pointer followed by
 * a length of 3 bytes, and ending with a non-zero byte. We rely on
 * little endian for length access to work by just masking away the last
 * byte.
 */
typedef union {
    char data[16];
    struct {
        const char *str;
        size_t len;
    } p;
} String;

/*
 * String values not exceeding a specific length is stored inline. This
 * value also includes terminal null byte.
 */
#define SHORT_STRING_LEN 16

/* Length is represented by 7 bytes. */
#define MAX_STRING_LEN 0x00ffffffffffffff

/* Inline construct a String object which fits in the small variant. */
#define SHORT_STRING_INIT(s) {{s}}

#define IS_SHORT_STRING(s) ((s).data[15] == 0)

/*
 * Get pointer to plain C string representation. This depends on the
 * type of string, whether it is short or long.
 *
 * Note that this cannot be implemented as a function call, since the
 * resulting pointer is only valid for the same duration as the string
 * argument.
 */
#define str_raw(s) (IS_SHORT_STRING(s) ? (s).data : (s).p.str)

/* Compute length of string. */
INTERNAL size_t str_len(String s);

/* Compare string length to 0. */
INTERNAL int str_isempty(String s);

/* Write length and buffer to pre-allocated string object. */
INTERNAL void str_set(String *s, const char *str, size_t len);

/* Initialize string, where the length can be determined by strlen. */
INTERNAL String str_init(const char *str);

/* Compare two strings, returning 0 if equal. */
INTERNAL int str_cmp(String s1, String s2);

/* Return 1 iff string contains given character. */
INTERNAL int str_haschr(String s, char c);

/*
 * Output string to stream, in safe encoding for textual assembly or as
 * plain C code.
 */
INTERNAL int fprintstr(FILE *stream, String str);

#endif
