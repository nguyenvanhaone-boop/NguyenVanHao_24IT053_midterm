#include "myls.h"
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

void entry_list_add(EntryList *list, const char *name, const char *path,
                    const struct stat *st)
{
    if (list->count == list->capacity) {
        if (list->capacity > SIZE_MAX / 2 / sizeof(Entry)) {
            report_error(path, ENOMEM);
            exit(EXIT_FAILURE);
        }
        size_t capacity = list->capacity ? list->capacity * 2 : 32;
        list->items = xrealloc(list->items, capacity * sizeof(Entry));
        list->capacity = capacity;
    }
    list->items[list->count++] = (Entry){xstrdup(name), xstrdup(path), *st};
}

void entry_list_free(EntryList *list)
{
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i].name);
        free(list->items[i].path);
    }
    free(list->items);
    *list = (EntryList){0};
}

bool operand_stat(const char *path, const Options *opts, struct stat *st)
{
    if (lstat(path, st) < 0) {
        report_error(path, errno);
        return false;
    }
    /* Plain directory-link operands list their contents. -d, -l and -F
       preserve the link itself; links found inside directories are never followed. */
    if (S_ISLNK(st->st_mode) && !opts->directory_arg &&
        !opts->long_format && !opts->classify) {
        struct stat target;
        if (stat(path, &target) == 0 && S_ISDIR(target.st_mode)) *st = target;
    }
    return true;
}

bool collect_directory(const char *path, const Options *opts, EntryList *list)
{
    DIR *directory = opendir(path);
    if (!directory) {
        report_error(path, errno);
        return false;
    }
    for (;;) {
        /* Reset errno because metadata calls can leave an unrelated error. */
        errno = 0;
        struct dirent *item = readdir(directory);
        if (!item) {
            if (errno) report_error(path, errno);
            break;
        }
        bool dot = !strcmp(item->d_name, ".") || !strcmp(item->d_name, "..");
        if (!opts->all && item->d_name[0] == '.' && (!opts->almost_all || dot))
            continue;
        char *full = join_path(path, item->d_name);
        struct stat st;
        if (lstat(full, &st) < 0) report_error(full, errno);
        else entry_list_add(list, item->d_name, full, &st);
        free(full);
    }
    if (closedir(directory) < 0) report_error(path, errno);
    return true;
}
