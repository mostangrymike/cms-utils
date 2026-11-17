# CMSED 0.2

CMSED is a small C89 stream editor that processes records. The CMS build reads
and writes CMS files; the host build accepts ordinary paths and standard input.
It does not implement every GNU or POSIX sed extension.

## Build and run on CMS

Build on CMS with GCCCMS:

```
CMSCLNK S2SED PLAIN
```

CMS invocation uses `fn ft fm` triples:

```
S2SED [-n] [-e command] [-f scriptFN scriptFT scriptFM]
      scriptFN scriptFT scriptFM dataFN dataFT dataFM [...]
```

If no `-e` or `-f` is given, the first triple is the script file.
CMS `-i` stages each result and preserves the original as `S2B#### SEDBAK`
on the same filemode.

## Host build and run

```
gcc -std=c89 -DCMSED_HOST -o s2sed cmsed.c
s2sed -n -e 's/foo/bar/g' input.txt
s2sed -f script.sed input.txt
```

Host mode reads standard input when no input path is provided. `-i` stages
output in a sibling temporary file and renames it only after success.

## Supported commands

Each script record holds one command. `-e` adds one command and `-f` loads a
script file. The implemented commands are:

- `s/regex/replacement/`, with `g` and `p` flags
- `d`, `p`, `q`
- `y/source/destination/`
- `h`, `H`, `g`, `G`, `x`
- `b label`, `t label`, and `:label`
- `a`, `i`, `c`, `r`, and `w`

Addresses support line numbers, `$`, `/regex/`, `!`, and two-address ranges.
Line numbers are cumulative across input files. Substitutions support `&`,
escaped capture groups `\(...\)`, and backreferences `\1` through `\9`.

The basic regular-expression subset supports literals, `.`, `^`, `$`, `*`,
bracket classes/ranges, groups, and backreferences. It does not support
alternation, `+`, `?`, intervals, POSIX character classes, or semicolon-
separated commands. Other GNU sed options are not implemented.

Records are limited to 1023 characters, script records to 255 characters,
commands to 128, and input files to 16. CMS scripts and inputs are CMS files;
host paths and stdin are host-only.

## Smoke test

`tests/test.sed`, `tests/test.in`, and `tests/test.expected` cover substitution,
addresses, delete, print, backreferences, and quit. Run on CMS with:

```
S2SED S2TSCR SED A1 S2TDATA DATA A1
```
