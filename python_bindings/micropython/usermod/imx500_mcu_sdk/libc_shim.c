#include <stddef.h>

#include "py/misc.h"

void *malloc(size_t size) {
    return m_malloc_maybe(size);
}

void free(void *ptr) {
    if (ptr) {
        m_free(ptr);
    }
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    const char *p = nptr;
    unsigned long value = 0;
    int any = 0;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v') {
        ++p;
    }
    if (*p == '+') {
        ++p;
    }

    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    } else if (base == 0 && *p == '0') {
        base = 8;
        ++p;
        any = 1;
    } else if (base == 0) {
        base = 10;
    }

    for (;; ++p) {
        unsigned int digit;
        if (*p >= '0' && *p <= '9') {
            digit = (unsigned int)(*p - '0');
        } else if (*p >= 'a' && *p <= 'z') {
            digit = (unsigned int)(*p - 'a' + 10);
        } else if (*p >= 'A' && *p <= 'Z') {
            digit = (unsigned int)(*p - 'A' + 10);
        } else {
            break;
        }
        if (digit >= (unsigned int)base) {
            break;
        }
        value = value * (unsigned int)base + digit;
        any = 1;
    }

    if (endptr) {
        *endptr = (char *)(any ? p : nptr);
    }
    return any ? value : 0;
}
