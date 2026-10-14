#include "myls.h"
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <sys/sysmacros.h>
#endif
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

static void mode_string(mode_t mode, char out[11])
{
    out[0] = S_ISREG(mode) ? '-' : S_ISDIR(mode) ? 'd' : S_ISLNK(mode) ? 'l' :
             S_ISCHR(mode) ? 'c' : S_ISBLK(mode) ? 'b' : S_ISFIFO(mode) ? 'p' :
             S_ISSOCK(mode) ? 's' : '?';
#ifdef S_ISWHT
    if (S_ISWHT(mode)) out[0] = 'w';
#endif
    const char *permissions = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i)
        out[i + 1] = mode & (1 << (8 - i)) ? permissions[i] : '-';
    if (mode & S_ISUID) out[3] = mode & S_IXUSR ? 's' : 'S';
    if (mode & S_ISGID) out[6] = mode & S_IXGRP ? 's' : 'S';
    if (mode & S_ISVTX) out[9] = mode & S_IXOTH ? 't' : 'T';
    out[10] = '\0';
}

static char file_suffix(const Entry *entry)
{
    if (S_ISDIR(entry->st.st_mode)) return '/';
    if (S_ISLNK(entry->st.st_mode)) return '@';
    if (S_ISFIFO(entry->st.st_mode)) return '|';
    if (S_ISSOCK(entry->st.st_mode)) return '=';
#ifdef S_ISWHT
    if (S_ISWHT(entry->st.st_mode)) return '%';
#endif
    if (S_ISREG(entry->st.st_mode) &&
        (entry->st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) return '*';
    return '\0';
}

static void human_size(uintmax_t value, char *out, size_t size)
{
    const char *units = "BKMGTPE";
    double number = (double)value;
    unsigned unit = 0;
    while (number >= 1024.0 && unit < 6) { number /= 1024.0; ++unit; }
    if (!unit) snprintf(out, size, "%ju", value);
    else snprintf(out, size, "%.1f%c", number, units[unit]);
}

static uintmax_t allocated_bytes(const Entry *entry)
{
    if (entry->st.st_blocks < 0) return 0;
    uintmax_t blocks = (uintmax_t)entry->st.st_blocks;
    if (blocks > UINTMAX_MAX / 512) {
        report_error(entry->path, EOVERFLOW);
        return 0;
    }
    return blocks * 512;
}

static void format_blocks(uintmax_t bytes, const Options *opts, char out[32])
{
    if (opts->human) human_size(bytes, out, 32);
    else snprintf(out, 32, "%ju", bytes / opts->block_size +
                  (bytes % opts->block_size != 0));
}

void print_text(const char *text, const Options *opts)
{
    bool quote = opts->print_mode == 1 ||
                 (opts->print_mode == 0 && isatty(STDOUT_FILENO));
    if (!quote) {
        fputs(text, stdout);
        return;
    }
    /* Decode characters so printable UTF-8 names survive -q in a UTF-8 locale.
       Invalid byte sequences consume one byte, guaranteeing forward progress. */
    mbstate_t state = {0};
    size_t remaining = strlen(text);
    while (remaining) {
        wchar_t character;
        size_t count = mbrtowc(&character, text, remaining, &state);
        if (count == (size_t)-1 || count == (size_t)-2) {
            count = 1;
            memset(&state, 0, sizeof state);
            putchar('?');
        } else {
            if (iswprint(character)) fwrite(text, 1, count, stdout);
            else putchar('?');
        }
        text += count;
        remaining -= count;
    }
}

static void print_name(const Entry *entry, const Options *opts)
{
    print_text(entry->name, opts);
    if (opts->classify) {
        char suffix = file_suffix(entry);
        if (suffix) putchar(suffix);
    }
}

void print_entries(const EntryList *list, const Options *opts, bool directory)
{
    if (directory && (opts->long_format ||
                      (opts->blocks && isatty(STDOUT_FILENO)))) {
        uintmax_t total = 0;
        for (size_t i = 0; i < list->count; ++i) {
            uintmax_t bytes = allocated_bytes(&list->items[i]);
            if (bytes > UINTMAX_MAX - total) report_error("block total", EOVERFLOW);
            else total += bytes;
        }
        char value[32];
        format_blocks(total, opts, value);
        printf("total %s\n", value);
    }
    for (size_t i = 0; i < list->count; ++i) {
        const Entry *entry = &list->items[i];
        if (opts->inode) printf("%ju ", (uintmax_t)entry->st.st_ino);
        if (opts->blocks) {
            char value[32];
            format_blocks(allocated_bytes(entry), opts, value);
            printf("%s ", value);
        }
        if (opts->long_format) {
            char mode[11], size[32], date[64];
            mode_string(entry->st.st_mode, mode);
            time_t selected = entry_time(entry, opts).tv_sec;
            struct tm *time_info = localtime(&selected);
            if (!time_info || !strftime(date, sizeof date, "%b %e %H:%M", time_info)) {
                strcpy(date, "??? ?? ??:??");
                report_error(entry->path, EOVERFLOW);
            }
            if (S_ISCHR(entry->st.st_mode) || S_ISBLK(entry->st.st_mode))
                snprintf(size, sizeof size, "%ju,%ju", (uintmax_t)major(entry->st.st_rdev),
                         (uintmax_t)minor(entry->st.st_rdev));
            else if (opts->human) human_size(entry->st.st_size, size, sizeof size);
            else snprintf(size, sizeof size, "%lld", (long long)entry->st.st_size);
            printf("%s %3ju ", mode, (uintmax_t)entry->st.st_nlink);
            struct passwd *owner = opts->long_format == 2 ? NULL : getpwuid(entry->st.st_uid);
            if (owner) printf("%s ", owner->pw_name);
            else printf("%ju ", (uintmax_t)entry->st.st_uid);
            struct group *group = opts->long_format == 2 ? NULL : getgrgid(entry->st.st_gid);
            if (group) printf("%s ", group->gr_name);
            else printf("%ju ", (uintmax_t)entry->st.st_gid);
            printf("%8s %s ", size, date);
        }
        print_name(entry, opts);
        if (opts->long_format && S_ISLNK(entry->st.st_mode)) {
            char *target = link_target(entry->path);
            if (target) {
                fputs(" -> ", stdout);
                print_text(target, opts);
                free(target);
            }
        }
        putchar('\n');
    }
}
