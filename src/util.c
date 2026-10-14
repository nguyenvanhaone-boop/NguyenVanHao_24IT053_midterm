#include "myls.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int exit_status;

void report_error(const char *path, int error)
{
    fprintf(stderr, "myls: %s: %s\n", path, strerror(error));
    exit_status = EXIT_FAILURE;
}

void *xrealloc(void *ptr, size_t size)
{
    void *result = realloc(ptr, size);
    if (!result) {
        report_error("memory allocation", ENOMEM);
        exit(EXIT_FAILURE);
    }
    return result;
}

char *xstrdup(const char *s)
{
    size_t size = strlen(s) + 1;
    char *copy = xrealloc(NULL, size);
    memcpy(copy, s, size);
    return copy;
}

char *join_path(const char *parent, const char *name)
{
    size_t a = strlen(parent), b = strlen(name);
    if (a > SIZE_MAX - b - 2) {
        report_error(parent, ENAMETOOLONG);
        exit(EXIT_FAILURE);
    }
    char *path = xrealloc(NULL, a + b + 2);
    memcpy(path, parent, a);
    if (a && parent[a - 1] != '/') path[a++] = '/';
    memcpy(path + a, name, b + 1);
    return path;
}

char *link_target(const char *path)
{
    size_t size = 128;
    char *buffer = NULL;
    for (;;) {
        buffer = xrealloc(buffer, size + 1);
        ssize_t n = readlink(path, buffer, size);
        if (n < 0) {
            report_error(path, errno);
            free(buffer);
            return NULL;
        }
        if ((size_t)n < size) {
            buffer[n] = '\0';
            return buffer;
        }
        if (size > (SIZE_MAX - 1) / 2) {
            report_error(path, ENAMETOOLONG);
            free(buffer);
            return NULL;
        }
        size *= 2;
    }
}
