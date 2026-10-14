#include "myls.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uintmax_t environment_block_size(void)
{
    const char *value = getenv("BLOCKSIZE");
    if (!value || !*value) return 512;
    char *end;
    errno = 0;
    uintmax_t number = strtoull(value, &end, 10), factor = 1;
    if (end == value && strchr("kKmMgG", *value)) number = 1;
    if (*end) {
        switch (*end++) {
        case 'k': case 'K': factor = 1024; break;
        case 'm': case 'M': factor = 1024 * 1024; break;
        case 'g': case 'G': factor = 1024 * 1024 * 1024; break;
        default: factor = 0; break;
        }
    }
    if (errno || *value == '-' || *end || !number || !factor ||
        number > UINTMAX_MAX / factor) {
        fprintf(stderr, "myls: invalid BLOCKSIZE '%s'; using 512\n", value);
        return 512;
    }
    return number * factor;
}

void options_init(Options *opts)
{
    *opts = (Options){0};
    opts->time_field = 'm';
    opts->almost_all = geteuid() == 0;
    opts->block_size = 512;
}

int options_parse(int argc, char **argv, Options *o, int *first)
{
    int c;
    bool units = false;
    opterr = 0;
    while ((c = getopt(argc, argv, "AacdFfhiklnqRrSstuw")) != -1) {
        switch (c) {
        case 'A': o->almost_all = true; break;
        case 'a': o->all = true; break;
        case 'c': o->time_field = 'c'; break;
        case 'd': o->directory_arg = true; o->recursive = false; break;
        case 'F': o->classify = true; break;
        case 'f': o->no_sort = true; break;
        case 'h': o->human = true; o->kilobytes = false; units = true; break;
        case 'i': o->inode = true; break;
        case 'k':
            o->human = false; o->kilobytes = true;
            o->block_size = 1024; units = true;
            break;
        case 'l': o->long_format = 1; break;
        case 'n': o->long_format = 2; break;
        case 'q': o->print_mode = 1; break;
        case 'R': o->recursive = true; o->directory_arg = false; break;
        case 'r': o->reverse = true; break;
        case 'S': o->size_sort = true; o->time_sort = false; break;
        case 's': o->blocks = true; break;
        case 't': o->time_sort = true; o->size_sort = false; break;
        case 'u': o->time_field = 'a'; break;
        case 'w': o->print_mode = 2; break;
        default:
            fprintf(stderr, "myls: unknown option -- %c\n"
                    "usage: myls [-AacdFfhiklnqRrSstuw] [file ...]\n", optopt);
            return -1;
        }
    }
    if (!units) o->block_size = environment_block_size();
    *first = optind;
    return 0;
}
