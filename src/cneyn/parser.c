#include "parser.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define next(ch) \
    if ((parser->ptr++)[0] != ch) return neyn_result_failed;

#define skip(ret)                                                          \
    while (parser->ptr[0] == ' ' || parser->ptr[0] == '\t') ++parser->ptr; \
    if (parser->ptr >= parser->end) return ret;

#define rskip \
    while (parser->ptr[-1] == ' ' || parser->ptr[-1] == '\t') --parser->ptr;

#define find                                                                                            \
    while (parser->ptr[0] != ' ' && parser->ptr[0] != '\t' && parser->ptr < parser->end) ++parser->ptr; \
    if (parser->ptr >= parser->end) return neyn_result_failed;

#define cfind                                                                                                     \
    while (parser->ptr[0] != ' ' && parser->ptr[0] != '\t' && parser->ptr[0] != ':' && parser->ptr < parser->end) \
        ++parser->ptr;                                                                                            \
    if (parser->ptr >= parser->end) return neyn_result_failed;

int neyn_parser_icmp(struct neyn_string *str, char *ptr)
{
    for (neyn_size i = 0; i < str->len; ++i)
        if (ptr[i] == 0 || tolower(str->ptr[i]) != tolower(ptr[i])) return 0;
    return ptr[str->len] == 0;
}

int neyn_parser_body(struct neyn_string *string)
{
    for (neyn_size i = 0; i < sizeof(neyn_method_body) / sizeof(const char *); ++i)
        if (string->len == strlen(neyn_method_body[i]) && strncmp(string->ptr, neyn_method_body[i], string->len) == 0)
            return 1;
    return 0;
}

int neyn_parser_method(struct neyn_string *string)
{
    for (neyn_size i = 0; i < sizeof(neyn_method_list) / sizeof(const char *); ++i)
        if (string->len == strlen(neyn_method_list[i]) && strncmp(string->ptr, neyn_method_list[i], string->len) == 0)
            return 1;
    return 0;
}

char *neyn_parser_find(struct neyn_parser *parser)
{
    for (char *i = parser->ptr; i < parser->finish - 1; ++i)
        if (i[0] == '\r' && i[1] == '\n') return i;
    return parser->finish;
}

uint16_t neyn_parser_stou16(char *ptr, char **finish, int *ok)
{
    char *i = ptr;
    neyn_size number = 0;
    for (; i < ptr + 4; ++i)
    {
        if (i[0] < '0' || '9' < i[0]) break;
        number = number * 10 + (i[0] - '0');
    }
    *finish = i, *ok = (i != ptr) && (i[0] < '0' || '9' < i[0]);
    return number;
}

neyn_size neyn_parser_stons(char *ptr, char **finish, int *ok)
{
    char *i = ptr;
    neyn_size number = 0;
    for (; i < ptr + (sizeof(neyn_size) == 4 ? 9 : 19); ++i)
    {
        if (i[0] < '0' || '9' < i[0]) break;
        number = number * 10 + (i[0] - '0');
    }
    *finish = i, *ok = (i != ptr) && (i[0] < '0' || '9' < i[0]);
    return number;
}

neyn_size neyn_parser_htons(char *ptr, char **finish, int *ok)
{
    char *i = ptr;
    neyn_size number = 0;
    for (; i < ptr + 2 * sizeof(neyn_size); ++i)
    {
        if ('0' <= i[0] && i[0] <= '9')
            number = number * 16 + (i[0] - '0');
        else if ('a' <= i[0] && i[0] <= 'f')
            number = number * 16 + (i[0] - 'a') + 10;
        else if ('A' <= i[0] && i[0] <= 'F')
            number = number * 16 + (i[0] - 'A') + 10;
        else
            break;
    }
    *finish = i;
    *ok = (i != ptr) && (i[0] < '0' || '9' < i[0]) && (i[0] < 'a' || 'f' < i[0]) && (i[0] < 'A' || 'F' < i[0]);
    return number;
}

struct neyn_header *neyn_parser_expand(struct neyn_parser *parser)
{
    neyn_size len = ++parser->request->header.len;
    if (len > parser->max)
    {
        parser->max = pow(2, ceil(log2(len)));
        parser->request->header.ptr = realloc(parser->request->header.ptr, parser->max * sizeof(struct neyn_header));
    }
    return parser->request->header.ptr + len - 1;
}

enum neyn_result neyn_parser_request(struct neyn_parser *parser)
{
    int ok;
    skip(neyn_result_failed) parser->request->method.ptr = parser->ptr;
    find parser->request->method.len = parser->ptr - parser->request->method.ptr;
    if (neyn_parser_method(&parser->request->method) != 1) return neyn_result_not_implemented;

    skip(neyn_result_failed) parser->request->path.ptr = parser->ptr;
    find parser->request->path.len = parser->ptr - parser->request->path.ptr;

    skip(neyn_result_failed) if (strncmp(parser->ptr, "HTTP/", 5) != 0) return neyn_result_failed;
    parser->ptr += 5, parser->request->major = neyn_parser_stou16(parser->ptr, &parser->ptr, &ok);
    if (!ok) return neyn_result_failed;
    if (parser->request->major > 1) return neyn_result_not_supported;

    next('.') parser->request->minor = neyn_parser_stou16(parser->ptr, &parser->ptr, &ok);
    if (!ok) return neyn_result_failed;
    skip(neyn_result_ok) return neyn_result_failed;
}

enum neyn_result neyn_parser_header_(struct neyn_parser *parser)
{
    struct neyn_header *header = neyn_parser_expand(parser);
    skip(neyn_result_failed) header->name.ptr = parser->ptr;
    cfind header->name.len = parser->ptr - header->name.ptr;
    next(':') skip(neyn_result_failed);

    header->value.ptr = parser->ptr, parser->ptr = parser->end;
    rskip header->value.len = parser->ptr - header->value.ptr;
    return neyn_result_ok;
}

enum neyn_result neyn_parser_header(struct neyn_parser *parser)
{
    enum neyn_result result = neyn_parser_header_(parser);
    if (result != neyn_result_ok) return result;

    int ok;
    struct neyn_header *header = parser->request->header.ptr + parser->request->header.len - 1;
    if (neyn_parser_icmp(&header->name, "Content-Length") == 1)
    {
        neyn_size prev = parser->length;
        parser->length = neyn_parser_stons(header->value.ptr, &parser->ptr, &ok);
        if (!ok || header->value.ptr + header->value.len != parser->ptr) return neyn_result_failed;
        if (prev != (neyn_size)-1 && prev != parser->length) return neyn_result_failed;
    }
    if (neyn_parser_icmp(&header->name, "Transfer-Encoding"))
    {
        if (parser->transfer == 1) return neyn_result_failed;
        parser->ptr = header->value.ptr + header->value.len - 7;
        if (header->value.len == 7 && strncmp(parser->ptr, "chunked", 7) == 0) parser->transfer = 1;
        if (header->value.len > 7 && strncmp(parser->ptr, "chunked", 7) == 0)
            rskip if (parser->ptr[-1] == ',') parser->transfer = 1;
    }
    return neyn_result_ok;
}

enum neyn_result neyn_parser_main(struct neyn_parser *parser)
{
    parser->transfer = 0;
    parser->length = (neyn_size)-1;
    parser->max = 8;
    parser->request->header.ptr = malloc(parser->max * sizeof(struct neyn_header));

    parser->end = neyn_parser_find(parser);
    enum neyn_result result = neyn_parser_request(parser);
    if (result != neyn_result_ok) return result;

    while (parser->end < parser->finish)
    {
        parser->ptr = parser->end + 2;
        parser->end = neyn_parser_find(parser);
        result = neyn_parser_header(parser);
        if (result != neyn_result_ok) return result;
    }

    if (parser->length != (neyn_size)-1 && parser->transfer == 1) return neyn_result_failed;
    if (parser->length == 0) return neyn_result_nobody;
    if (parser->length != (neyn_size)-1) return neyn_result_body;
    if (parser->transfer == 1) return neyn_result_transfer;
    if (neyn_parser_body(&parser->request->method) != 1) return neyn_result_nobody;
    return neyn_result_failed;
}

enum neyn_result neyn_parser_chunk(struct neyn_parser *parser)
{
    int ok;
    parser->end = parser->finish;
    skip(neyn_result_failed) parser->length = neyn_parser_htons(parser->ptr, &parser->end, &ok);
    if (!ok) return neyn_result_failed;
    return neyn_result_ok;
}

enum neyn_result neyn_parser_trailer(struct neyn_parser *parser)
{
    parser->end = parser->finish;
    skip(neyn_result_ok);
    parser->max = pow(2, ceil(log2(parser->request->header.len)));

    while (1)
    {
        parser->end = neyn_parser_find(parser);
        enum neyn_result result = neyn_parser_header_(parser);
        if (result != neyn_result_ok) return result;
        if (parser->end >= parser->finish) break;
        parser->ptr = parser->end + 2;
    }
    return neyn_result_ok;
}
