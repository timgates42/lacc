#if _XOPEN_SOURCE < 600
#  undef _XOPEN_SOURCE
#  define _XOPEN_SOURCE 600 /* isblank, strtoul */
#endif
#include "strtab.h"
#include "tokenize.h"
#include <lacc/context.h>
#include <lacc/typetree.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Static initializer for token. Only works with string representation
 * that can fit inline.
 */
#define TOK(t, s) {(t), 0, {SHORT_STRING_INIT(s)}}

const struct token basic_token[] = {
/* 0x00 */  TOK(END, "$"),              TOK(AUTO, "auto"),
            TOK(BREAK, "break"),        TOK(CASE, "case"),
            TOK(CHAR, "char"),          TOK(CONST, "const"),
            TOK(CONTINUE, "continue"),  TOK(DEFAULT, "default"),
/* 0x08 */  TOK(DO, "do"),              TOK(DOUBLE, "double"),
            TOK(NEWLINE, "\n"),         TOK(ELSE, "else"),
            TOK(ENUM, "enum"),          TOK(EXTERN, "extern"),
            TOK(FLOAT, "float"),        TOK(FOR, "for"), 
/* 0x10 */  TOK(GOTO, "goto"),          TOK(IF, "if"),
            TOK(INT, "int"),            TOK(LONG, "long"),
            TOK(REGISTER, "register"),  TOK(RETURN, "return"),
            TOK(SHORT, "short"),        TOK(SIGNED, "signed"),
/* 0x18 */  TOK(SIZEOF, "sizeof"),      TOK(STATIC, "static"),
            TOK(STRUCT, "struct"),      TOK(SWITCH, "switch"),
            TOK(TYPEDEF, "typedef"),    TOK(UNION, "union"),
            TOK(UNSIGNED, "unsigned"),  TOK(VOID, "void"),
/* 0x20 */  {0},                        TOK(NOT, "!"),
            TOK(VOLATILE, "volatile"),  TOK(HASH, "#"),
            TOK(WHILE, "while"),        TOK(MODULO, "%"),
            TOK(AND, "&"),              {0},
/* 0x28 */  TOK(OPEN_PAREN, "("),       TOK(CLOSE_PAREN, ")"),
            TOK(STAR, "*"),             TOK(PLUS, "+"),
            TOK(COMMA, ","),            TOK(MINUS, "-"),
            TOK(DOT, "."),              TOK(SLASH, "/"),
/* 0x30 */  {0},                        {0},
            {0},                        {0},
            {0},                        {0},
            {0},                        {0},
/* 0x38 */  {0},                        {0},
            TOK(COLON, ":"),            TOK(SEMICOLON, ";"),
            TOK(LT, "<"),               TOK(ASSIGN, "="),
            TOK(GT, ">"),               TOK(QUESTION, "?"),
/* 0x40 */  TOK(DOTS, "..."),           TOK(LOGICAL_OR, "||"),
            TOK(LOGICAL_AND, "&&"),     TOK(LEQ, "<="),
            TOK(GEQ, ">="),             TOK(EQ, "=="),
            TOK(NEQ, "!="),             TOK(ARROW, "->"),
/* 0x48 */  TOK(INCREMENT, "++"),       TOK(DECREMENT, "--"),
            TOK(LSHIFT, "<<"),          TOK(RSHIFT, ">>"),
            TOK(MUL_ASSIGN, "*="),      TOK(DIV_ASSIGN, "/="),
            TOK(MOD_ASSIGN, "%="),      TOK(PLUS_ASSIGN, "+="),
/* 0x50 */  TOK(MINUS_ASSIGN, "-="),    TOK(LSHIFT_ASSIGN, "<<="),
            TOK(RSHIFT_ASSIGN, ">>="),  TOK(AND_ASSIGN, "&="),
            TOK(XOR_ASSIGN, "^="),      TOK(OR_ASSIGN, "|="),
            TOK(TOKEN_PASTE, "##"),     {0},
/* 0x58 */  {0},                        {0},
            {0},                        TOK(OPEN_BRACKET, "["),
            {0},                        TOK(CLOSE_BRACKET, "]"),
            TOK(XOR, "^"),              {0},
/* 0x60 */  {0},                        {0},
            {0},                        {0},
            {0},                        {0},
            {0},                        {0},
/* 0x68 */  {0},                        {0},
            {0},                        {0},
            {0},                        {0},
            {0},                        {0},
/* 0x70 */  {0},                        {0},
            {0},                        {0},
            {NUMBER},                   {IDENTIFIER},
            {STRING},                   {PARAM},
/* 0x78 */  {EMPTY_ARG},                {PREP_NUMBER},
            {0},                        TOK(OPEN_CURLY, "{"),
            TOK(OR, "|"),               TOK(CLOSE_CURLY, "}"),
            TOK(NEG, "~"),              {0},
};

/* Valid identifier character, except in the first position which does
 * not allow numbers.
 */
#define isident(c) (isalnum(c) || (c) == '_')

/* Macros to make state state machine implementation of identifier and
 * operator tokenization simpler.
 */
#define at(c) (**endptr == (c) && (*endptr)++)
#define get(c) (*(*endptr)++ == (c))
#define end() !isident(**endptr)

#define S1(a) (at(a) && end())
#define S2(a, b) (at(a) && get(b) && end())
#define S3(a, b, c) (at(a) && get(b) && get(c) && end())
#define S4(a, b, c, d) (at(a) && get(b) && get(c) && get(d) && end())
#define S5(a, b, c, d, e) \
    (at(a) && get(b) && get(c) && get(d) && get(e) && end())
#define S6(a, b, c, d, e, f) \
    (at(a) && get(b) && get(c) && get(d) && get(e) && get(f) && end())
#define S7(a, b, c, d, e, f, g) \
    (at(a) && get(b) && get(c) && get(d) && get(e) && get(f) && get(g) && end())

/* Parse preprocessing number, which starts with an optional period
 * before a digit, then a sequence of period, letter underscore, digit,
 * or any of 'e+', 'e-', 'E+', 'E-'.
 *
 * This represents a superset of valid numbers in C, but is required
 * as intermediate representation for preprocessing.
 *
 * There is no such thing as a negative literal; expressions like '-2'
 * is the unary operator applied to the number 2.
 *
 * Regular expression:
 *
 *      (\.)?(0-9){\.a-zA-Z_0-9(e+|e-|E+|E-)}*
 *
 */
static struct token strtonum(char *in, char **endptr)
{
    char *ptr = in;
    struct token tok = {PREP_NUMBER};

    if (*in == '.') {
        in++;
    }

    assert(isdigit(*in));
    while (1) {
        if (isdigit(*in) || *in == '.' || *in == '_') {
            in++;
        } else if (isalpha(*in)) {
            if ((tolower(*in) == 'e' ||
                    (context.standard >= STD_C99 && tolower(*in) == 'p'))
                && (in[1] == '+' || in[1] == '-'))
            {
                in++;
            }
            in++;
        } else {
            break;
        }
    }

    tok.d.string = str_register(ptr, in - ptr);
    *endptr = in;
    return tok;
}

struct token convert_preprocessing_number(struct token t)
{
    const char *in;
    char *endptr;
    unsigned len;
    struct token tok = {NUMBER};

    assert(t.token == PREP_NUMBER);
    in = str_raw(t.d.string);
    len = t.d.string.len;
    tok.leading_whitespace = t.leading_whitespace;

    /* Try to read as integer. Handle suffixes u, l, ll, ul, ull, in all
     * permuations of upper- and lower case. */
    errno = 0;
    tok.d.number.type = &basic_type__int;
    tok.d.number.val.u = strtoul(in, &endptr, 0);
    if (endptr - in < len) {
        if (*endptr == 'u' || *endptr == 'U') {
            tok.d.number.type = &basic_type__unsigned_int;
            endptr++;
        }
        if (endptr - in < len && (*endptr == 'l' || *endptr == 'L')) {
            tok.d.number.type = 
                (is_unsigned(tok.d.number.type)) ?
                    &basic_type__unsigned_long :
                    &basic_type__long;
            endptr++;

            /* Also consider additional suffix for long long, not part
             * of C89. */
            if (endptr - in < len && *endptr == *(endptr - 1)) {
                endptr++;
            }
        }
        if (is_signed(tok.d.number.type)) {
            if (*endptr == 'u' || *endptr == 'U') {
                tok.d.number.type =
                    (tok.d.number.type->size == 4) ?
                        &basic_type__unsigned_int :
                        &basic_type__unsigned_long;
                endptr++;
            }
        }
    }

    /* If the integer conversion did not consume the whole token, try to
     * read as floating point number. */
    if (endptr - in != len) {
        errno = 0;
        tok.d.number.type = &basic_type__double;
        tok.d.number.val.d = strtod(in, &endptr);
        if (endptr - in < len && (*endptr == 'f' || *endptr == 'F')) {
            tok.d.number.type = &basic_type__float;
            tok.d.number.val.f = (float) tok.d.number.val.d;
            endptr++;
        }
    }

    if (errno || (endptr - in != len)) {
        if (errno == ERANGE) {
            error("Numeric literal '%s' is out of range.", str_raw(t.d.string));
        } else {
            error("Invalid numeric literal '%s'.", str_raw(t.d.string));
        }
        exit(1);
    }

    return tok;
}

/* Parse character escape code, including octal and hexadecimal number
 * literals. Unescaped characters are returned as-is. Invalid escape
 * sequences continues with an error, consuming only the backslash.
 */
static char escpchar(char *in, char **endptr)
{
    if (*in == '\\') {
        *endptr = in + 2;
        switch (in[1]) {
        case 'a': return 0x7;
        case 'b': return 0x8;
        case 't': return 0x9;
        case 'n': return 0xa;
        case 'v': return 0xb;
        case 'f': return 0xc;
        case 'r': return 0xd;
        case '\\': return '\\';
        case '?': return '\?';
        case '\'': return '\'';
        case '\"': return '\"';
        case '0':
            if (isdigit(in[2]) && in[2] < '8')
                return (char) strtol(&in[1], endptr, 8);
            return '\0';
        case 'x':
            return (char) strtol(&in[2], endptr, 16);
        default:
            error("Invalid escape sequence '\\%c'.", in[1]);
            return in[1];
        }
    }

    *endptr = in + 1;
    return *in;
}

/* Parse character literals in the format 'a', '\xaf', '\0', '\077' etc,
 * starting from *in. The position of the character after the last '
 * character is stored in endptr. If no valid conversion can be made,
 * *endptr == in.
 */
static struct token strtochar(char *in, char **endptr)
{
    struct token tok = {NUMBER};
    assert(*in == '\'');

    in++;
    tok.d.number.type = &basic_type__int;
    tok.d.number.val.i = escpchar(in, endptr);
    if (**endptr != '\'')
        error("Invalid character constant %c.", *in);

    *endptr += 1;
    return tok;
}

/* Parse string literal inputs delimited by quotation marks, handling
 * escaped quotes. The input buffer is destructively overwritten while
 * resolving escape sequences. Concatenate string literals separated by
 * whitespace.
 */
static struct token strtostr(char *in, char **endptr)
{
    struct token string = {STRING};
    char *start, *str;
    int len = 0;

    start = str = in;
    *endptr = in;

    do {
        if (*in++ == '"') {
            while (*in != '"' && *in) {
                *str++ = escpchar(in, &in);
                len++;
            }

            if (*in++ == '"') {
                *str = '\0';
                *endptr = in;
            }
        }

        /* See if there is another string after this one. */
        while (isblank(*in)) in++;
        if (*in != '"')
            break;
    } while (1);

    if (*endptr == start) {
        error("Invalid string literal.");
        exit(1);
    }

    string.d.string = str_register(start, len);
    return string;
}

/* Parse string as keyword or identifier. First character should be
 * alphabetic or underscore.
 */
static struct token strtoident(char *in, char **endptr)
{
    struct token ident = {IDENTIFIER};

    *endptr = in;
    switch (*(*endptr)++) {
    case 'a':
        if (S3('u', 't', 'o')) return basic_token[AUTO];
        break;
    case 'b':
        if (S4('r', 'e', 'a', 'k')) return basic_token[BREAK];
        break;
    case 'c':
        if (S3('a', 's', 'e')) return basic_token[CASE];
        if (S3('h', 'a', 'r')) return basic_token[CHAR];
        if (at('o') && get('n')) {
            if (S2('s', 't')) return basic_token[CONST];
            if (S5('t', 'i', 'n', 'u', 'e')) return basic_token[CONTINUE];
        }
        break;
    case 'd':
        if (S6('e', 'f', 'a', 'u', 'l', 't')) return basic_token[DEFAULT];
        if (at('o')) {
            if (S4('u', 'b', 'l', 'e')) return basic_token[DOUBLE];
            if (end()) return basic_token[DO];
        }
        break;
    case 'e':
        if (S3('l', 's', 'e')) return basic_token[ELSE];
        if (S3('n', 'u', 'm')) return basic_token[ENUM];
        if (S5('x', 't', 'e', 'r', 'n')) return basic_token[EXTERN];
        break;
    case 'f':
        if (S4('l', 'o', 'a', 't')) return basic_token[FLOAT];
        if (S2('o', 'r')) return basic_token[FOR];
        break;
    case 'g':
        if (S3('o', 't', 'o')) return basic_token[GOTO];
        break;
    case 'i':
        if (S1('f')) return basic_token[IF];
        if (S2('n', 't')) return basic_token[INT];
        break;
    case 'l':
        if (S3('o', 'n', 'g')) return basic_token[LONG];
        break;
    case 'r':
        if (at('e')) {
            if (S6('g', 'i', 's', 't', 'e', 'r')) return basic_token[REGISTER];
            if (S4('t', 'u', 'r', 'n')) return basic_token[RETURN];
        }
        break;
    case 's':
        if (S4('h', 'o', 'r', 't')) return basic_token[SHORT];
        if (S5('w', 'i', 't', 'c', 'h')) return basic_token[SWITCH];
        if (at('i')) {
            if (S4('g', 'n', 'e', 'd')) return basic_token[SIGNED];
            if (S4('z', 'e', 'o', 'f')) return basic_token[SIZEOF];
        }
        if (at('t')) {
            if (S4('a', 't', 'i', 'c')) return basic_token[STATIC];
            if (S4('r', 'u', 'c', 't')) return basic_token[STRUCT];
        }
        break;
    case 't':
        if (S6('y', 'p', 'e', 'd', 'e', 'f')) return basic_token[TYPEDEF];
        break;
    case 'u':
        if (at('n')) {
            if (S3('i', 'o', 'n')) return basic_token[UNION];
            if (S6('s', 'i', 'g', 'n', 'e', 'd')) return basic_token[UNSIGNED];
        }
        break;
    case 'v':
        if (at('o')) {
            if (S2('i', 'd')) return basic_token[VOID];
            if (S6('l', 'a', 't', 'i', 'l', 'e')) return basic_token[VOLATILE];
        }
        break;
    case 'w':
        if (S4('h', 'i', 'l', 'e')) return basic_token[WHILE];
    default:
        break;
    }

    /* Fallthrough means we have consumed at least one character, and
     * the token should be identifier. Backtrack one position to correct
     * a get() that moved us past the end. */
    (*endptr)--;

    while (isident(**endptr))
        (*endptr)++;

    ident.d.string = str_register(in, *endptr - in);
    return ident;
}

static struct token strtoop(char *in, char **endptr)
{
    *endptr = in;
    switch (*(*endptr)++) {
    case '*':
        if (at('=')) return basic_token[MUL_ASSIGN];
        break;
    case '/':
        if (at('=')) return basic_token[DIV_ASSIGN];
        break;
    case '%':
        if (at('=')) return basic_token[MOD_ASSIGN];
        break;
    case '+':
        if (at('+')) return basic_token[INCREMENT];
        if (at('=')) return basic_token[PLUS_ASSIGN];
        break;
    case '-':
        if (at('>')) return basic_token[ARROW];
        if (at('-')) return basic_token[DECREMENT];
        if (at('=')) return basic_token[MINUS_ASSIGN];
        break;
    case '<':
        if (at('=')) return basic_token[LEQ];
        if (at('<')) {
            if (at('=')) return basic_token[LSHIFT_ASSIGN];
            return basic_token[LSHIFT];
        }
        break;
    case '>':
        if (at('=')) return basic_token[GEQ];
        if (at('>')) {
            if (at('=')) return basic_token[RSHIFT_ASSIGN];
            return basic_token[RSHIFT];
        }
        break;
    case '&':
        if (at('=')) return basic_token[AND_ASSIGN];
        if (at('&')) return basic_token[LOGICAL_AND];
        break;
    case '^':
        if (at('=')) return basic_token[XOR_ASSIGN];
        break;
    case '|':
        if (at('=')) return basic_token[OR_ASSIGN];
        if (at('|')) return basic_token[LOGICAL_OR];
        break;
    case '.':
        if (at('.') && get('.')) return basic_token[DOTS];
        break;
    case '=':
        if (at('=')) return basic_token[EQ];
        break;
    case '!':
        if (at('=')) return basic_token[NEQ];
        break;
    case '#':
        if (at('#')) return basic_token[TOKEN_PASTE];
        break;
    default:
        break;
    }

    *endptr = in + 1;
    return basic_token[(int) *in];
}

/* Parse string as whitespace tokens, consuming space and tabs. Return
 * number of characters.
 */
static int skip_spaces(char *in, char **endptr)
{
    char *start = in;

    while (isblank(*in))
        in++;

    *endptr = in;
    return in - start;
}

String tokstr(struct token tok)
{
    static char buf[64];
    struct number num;
    int w = 0;
    assert(tok.token != PARAM);
    assert(tok.token != EMPTY_ARG);

    if (tok.token == NUMBER) {
        /* The string representation is lost during tokenization, so we
         * cannot necessarily reconstruct the same suffixes. */
        num = tok.d.number;
        switch (num.type->type) {
        case T_UNSIGNED:
            w += snprintf(buf + w, sizeof(buf) - w, "%luu", num.val.u);
            if (num.type->size == 8) {
                w += snprintf(buf + w, sizeof(buf) - w, "l");
            }
            break;
        case T_SIGNED:
            w += snprintf(buf + w, sizeof(buf) - w, "%ld", num.val.i);
            if (num.type->size == 8) {
                w += snprintf(buf + w, sizeof(buf) - w, "l");
            }
            break;
        case T_REAL:
            if (is_float(num.type)) {
                w += snprintf(buf + w, sizeof(buf) - w, "%f", num.val.f);
                w += snprintf(buf + w, sizeof(buf) - w, "f");
            } else {
                w += snprintf(buf + w, sizeof(buf) - w, "%f", num.val.d);                
            }
            break;
        default:
            assert(0);
            break;
        }
        return str_init(buf);
    }

    return tok.d.string;
}

struct token pastetok(struct token a, struct token b)
{
    char *str;
    size_t len;
    String as = tokstr(a), bs = tokstr(b);
    struct token tok = {STRING};

    len = as.len + bs.len;
    str = calloc(len + 1, sizeof(*str));
    memcpy(str, str_raw(as), as.len);
    memcpy(str + as.len, str_raw(bs), bs.len);

    tok.d.string = str_register(str, len);
    free(str);
    return tok;
}

struct token tokenize(char *in, char **endptr)
{
    int ws;
    struct token tok;

    assert(in);
    assert(endptr);

    ws = skip_spaces(in, endptr);
    in = *endptr;

    if (isalpha(*in) || *in == '_') {
        tok = strtoident(in, endptr);
    } else if (*in == '\0') {
        tok = basic_token[END];
    } else if (isdigit(*in) || (*in == '.' && isdigit(in[1]))) {
        tok = strtonum(in, endptr);
    } else if (*in == '"') {
        tok = strtostr(in, endptr);
    } else if (*in == '\'') {
        tok = strtochar(in, endptr);
    } else {
        tok = strtoop(in, endptr);
    }

    tok.leading_whitespace = ws;
    return tok;
}
