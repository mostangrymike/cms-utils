# CMSED

CMSED is a small C89, record-oriented stream editor for CMS. It reads a CMS
script file and one or more CMS input files, then writes edited records to the
console. It never edits input files in place.

## Build and run on CMS

Build on the CMS C toolchain:

```
CMSCLNK S2SED PLAIN
```

The command format is:

```
S2SED scriptFN scriptFT scriptFM dataFN dataFT dataFM [...]
```

For example:

```
S2SED S2T1 SED A1 S2D1 DATA A1
```

## Supported script subset

Each nonblank script record contains one command:

- `s/regex/replacement/` replaces the first match.
- `s/regex/replacement/g` replaces every non-overlapping match.
- `d` deletes the current record.
- `p` prints the current record in addition to default output.
- `q` prints the current record and stops.

One address may precede a command: a 1-based input line number or `/regex/`.
Append `!` to invert an address. Line numbers continue across input files.
Patterns support literals, `.`, `^`, `$`, `*`, bracket classes and ranges.
Backslash quotes the next pattern character. In replacements, `&` expands to
the complete match, and backslash quotes the next character.

This is a small sed subset, not a GNU/POSIX sed replacement. It does not
support address ranges, capture groups/backreferences, `-n`, branching, hold
space, or in-place editing. CMS records are lines, bounded to 1023 characters.

## Tests

`tests/` contains a smoke script, input, and expected output. It covers global
substitution, regex and numeric addresses, delete, print, and quit.
