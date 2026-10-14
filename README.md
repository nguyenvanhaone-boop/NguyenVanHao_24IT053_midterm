# myls - Midterm Project

A modular C implementation of the required NetBSD `ls(1)` subset for the VKU System Programming midterm.

## Build and test

```sh
make
make test
```

## Usage

```sh
./myls [options] [file ...]
```

Implemented options: `-A -a -c -d -F -f -h -i -k -l -n -q -R -r -S -s -t -u -w`.
The implementation supports multiple operands, long output, numeric IDs, inode and block data, human-readable sizes, name/time/size sorting, reverse and unsorted output, recursive traversal, file classification, symbolic links, non-printable names, `BLOCKSIZE`, and error reporting.

## Source layout

- `src/main.c`: operand dispatch and recursive traversal
- `src/options.c`: option parsing and environment handling
- `src/listing.c`: directory reading and metadata collection
- `src/sort.c`: comparison and sort order
- `src/display.c`: long-format and name output
- `src/util.c`: allocation, path, link, and error helpers
- `include/myls.h`: shared types and declarations

## Student

Nguyen Van Hao - 24IT053

GitHub repository: https://github.com/nguyenvanhaone-boop/NguyenVanHao_24IT053_midterm
