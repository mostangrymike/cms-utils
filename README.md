# CMSED 0.4

CMSED is a C89 stream editor for CMS records with a host build for ordinary
paths and standard input. It follows POSIX sed and the common GNU extensions,
with the limits listed below.

## Build and run on CMS

Build with GCCCMS:

```
CMSCLNK S2SED PLAIN
```

CMS invocation uses `fn ft fm` triples:

```
S2SED [-n] [-s] [-E] [-e command]
      [-f scriptFN scriptFT scriptFM]
      scriptFN scriptFT scriptFM dataFN dataFT dataFM [...]
```

If no `-e` or `-f` is given, the first triple is the script file. CMS `-i`
stages each result and keeps the original as `S2B#### SEDBAK` on the same
filemode. The `e` command and `s///e` are not available on CMS.

## Host build and run

```
gcc -std=c89 -DCMSED_HOST -o s2sed cmsed.c
s2sed -n -e 's/foo/bar/g' input.txt
s2sed -f script.sed input.txt
s2sed -i.bak -e 's/foo/bar/' file1 file2
```

Host mode reads standard input when no path is given. `-i` writes a sibling
temporary file and renames it only after success; an optional suffix keeps a
backup.

## Options

`-n`, `-s`, `-z`, `-E`/`-r`, `-l N`, `-e`, `-f -`, `-i[SUFFIX]`, `-u`,
`--posix`, `--sandbox`, `--follow-symlinks`, `--help`, `--version`.
`--sandbox` disables the `e`, `r`, `R`, `w`, and `W` commands and `s///w`.

## Script syntax

- Commands may be separated by newlines or semicolons.
- `{` and `}` group commands, with addresses and `!` on the group.
- `#n` as the first script record enables quiet mode.
- `a`, `i`, and `c` accept one-line text or a backslash followed by one or
  more following script records.
- An empty regular expression reuses the last regular expression.

## Commands

- `s/regex/replacement/flags` with `g`, `p`, `w file`, `i`/`I`, `m`/`M`,
  `e`, and a numeric occurrence.
- Replacement text supports `&`, `\1`-`\9`, and the GNU case operators
  `\U`, `\L`, `\u`, `\l`, and `\E`.
- `d`, `p`, `q [code]`, `Q [code]`, `n`, `N`, `P`, `D`
- `=`, `l [width]`, `F`, `z`, `v [version]`
- `y/source/destination/`
- `h`, `H`, `g`, `G`, `x`
- `b label`, `t label`, `T label`, and `:label`
- `a`, `i`, `c`, `r`, `R`, `w`, `W`
- `e command` (host only)

## Addresses

Line numbers, `$`, `/regex/`, `\c/regex/c`, `first~step`, `addr,+N`,
`addr,~N`, `0,/regex/`, regex `I`/`M` modifiers, `!`, and two-address ranges.
Line numbers are cumulative across input files unless `-s` is used.

## Regular expressions

Basic regular expressions support literals, `.`, `^`, `$`, `*`, `\+`, `\?`,
`\|`, `\{m,n\}`, bracket classes and ranges, POSIX character classes, groups,
backreferences `\1` through `\9`, `\b`, `\B`, `\<`, `\>`, `\``, and `\'`
anchors, and the `\w`, `\W`, `\s`, and `\S` character classes.
Matching is leftmost-longest. `-E`/`-r` accepts extended syntax and translates
it to the same engine.

## Input and output

Multiple input files are read as one stream, and `n`/`N` cross file
boundaries. `-s` resets line numbers and ranges per file. `-z` uses NUL as the
record separator. A missing trailing separator is preserved. Existing `w`/`W`
targets are truncated at startup. Host `/dev/stdout` and `/dev/stderr` are
mapped to the standard streams.

## Limits

Records are limited to 4095 characters, scripts to 1024 characters, commands
to 128, input files to 16, groups to 9, and block nesting to 32. CMS scripts
and inputs are CMS files; host paths and standard input are host-only.

## Tests

`tests/test.sed`, `tests/test.in`, and `tests/test.expected` cover
substitution, addresses, delete, print, backreferences, and quit. Run on CMS:

```
S2SED S2TSCR SED A1 S2TDATA DATA A1
```

Host regression scripts T1-T3 are in `tests/`.
