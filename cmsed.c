/* CMSED 0.1: a CMS-record stream editor for GCCCMS/PDPCLIB. */
/* Supported commands: [address][!]d, p, q, and s<d>BRE<d>text<d>[g]. */
typedef struct cms_file FILE;
extern FILE *fopen(const char *, const char *);
extern char *fgets(char *, int, FILE *);
extern int fclose(FILE *);
extern int printf(const char *, ...);
extern int puts(const char *);

enum {
    MAX_COMMANDS = 64,
    MAX_SCRIPT_LINE = 256,
    MAX_RECORD = 1024,
    MAX_STRING = 256,
    MAX_INPUT_FILES = 16
};

enum {
    CMD_SUBSTITUTE = 1,
    CMD_DELETE,
    CMD_PRINT,
    CMD_QUIT
};

enum {
    ADDR_ALL = 0,
    ADDR_LINE,
    ADDR_REGEX
};

typedef struct {
    int kind;
    int address_kind;
    int address_line;
    int address_not;
    int global;
    char address[MAX_STRING];
    char pattern[MAX_STRING];
    char replacement[MAX_STRING];
} SedCommand;

static SedCommand commands[MAX_COMMANDS];
static int command_count;

static int text_length(const char *text) {
    int length = 0;
    while (text[length]) length++;
    return length;
}

static void text_copy(char *to, const char *from, int size) {
    int i = 0;
    while (from[i] && i < size - 1) {
        to[i] = from[i];
        i++;
    }
    to[i] = 0;
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static void skip_space(const char **text) {
    while (is_space(**text)) (*text)++;
}

static int read_delimited(const char **input, char delimiter, char *out) {
    int used = 0;
    const char *text = *input;
    while (*text && *text != delimiter) {
        if (*text == '\\' && text[1] == delimiter) {
            if (used < MAX_STRING - 1) out[used++] = *text;
            text++;
        }
        if (used < MAX_STRING - 1) out[used++] = *text;
        text++;
    }
    if (*text != delimiter) return 0;
    out[used] = 0;
    *input = text + 1;
    return 1;
}

static int parse_script_line(const char *line, int script_line) {
    SedCommand command;
    const char *text = line;
    int has_address = 0;
    while (is_space(*text)) text++;
    if (!*text || *text == '#') return 1;
    if (command_count >= MAX_COMMANDS) return 0;
    command.kind = 0;
    command.address_kind = ADDR_ALL;
    command.address_line = 0;
    command.address_not = 0;
    command.global = 0;
    command.address[0] = 0;
    command.pattern[0] = 0;
    command.replacement[0] = 0;
    if (is_digit(*text)) {
        has_address = 1;
        command.address_kind = ADDR_LINE;
        while (is_digit(*text)) {
            command.address_line = command.address_line * 10 +
                                   (*text - '0');
            text++;
        }
        if (command.address_line < 1) return 0;
    } else if (*text == '/') {
        has_address = 1;
        command.address_kind = ADDR_REGEX;
        text++;
        if (!read_delimited(&text, '/', command.address)) return 0;
    }
    if (has_address && *text == '!') {
        command.address_not = 1;
        text++;
    }
    skip_space(&text);
    if (*text == 'd') {
        text++;
        skip_space(&text);
        if (*text) return 0;
        command.kind = CMD_DELETE;
    } else if (*text == 'p') {
        text++;
        skip_space(&text);
        if (*text) return 0;
        command.kind = CMD_PRINT;
    } else if (*text == 'q') {
        text++;
        skip_space(&text);
        if (*text) return 0;
        command.kind = CMD_QUIT;
    } else if (*text == 's') {
        char delimiter;
        text++;
        delimiter = *text++;
        if (!delimiter || is_space(delimiter)) return 0;
        if (!read_delimited(&text, delimiter, command.pattern)) return 0;
        if (!read_delimited(&text, delimiter, command.replacement))
            return 0;
        skip_space(&text);
        if (*text == 'g') {
            command.global = 1;
            text++;
        }
        skip_space(&text);
        if (*text) return 0;
        command.kind = CMD_SUBSTITUTE;
    } else {
        return 0;
    }
    if (!command.kind || (command.address_kind != ADDR_ALL && !has_address))
        return 0;
    commands[command_count++] = command;
    (void)script_line;
    return 1;
}

static int atom_match(const char *pattern, char c, int *used) {
    int i, matched = 0, invert = 0;
    if (pattern[0] == '\\' && pattern[1]) {
        *used = 2;
        return pattern[1] == c;
    }
    if (pattern[0] == '.') {
        *used = 1;
        return 1;
    }
    if (pattern[0] != '[') {
        *used = 1;
        return pattern[0] == c;
    }
    i = 1;
    if (pattern[i] == '^') {
        invert = 1;
        i++;
    }
    if (pattern[i] == ']') {
        if (c == ']') matched = 1;
        i++;
    }
    while (pattern[i] && pattern[i] != ']') {
        char first = pattern[i++];
        char last = first;
        if (pattern[i] == '-' && pattern[i + 1] &&
            pattern[i + 1] != ']') {
            i++;
            last = pattern[i++];
        }
        if (c >= first && c <= last) matched = 1;
    }
    if (pattern[i] != ']') {
        *used = 1;
        return c == '[';
    }
    *used = i + 1;
    return invert ? !matched : matched;
}

static int match_here(const char *pattern, const char *text, int *end) {
    int used, count, i;
    if (!*pattern) {
        *end = 0;
        return 1;
    }
    if (pattern[0] == '$' && !pattern[1]) {
        *end = 0;
        return !*text;
    }
    if (pattern[0] == '\\' && pattern[1] == '(') {
        pattern += 2;
        if (match_here(pattern, text, end)) return 1;
        return 0;
    }
    if (pattern[0] == '\\' && pattern[1] == ')') {
        return match_here(pattern + 2, text, end);
    }
    atom_match(pattern, *text, &used);
    if (pattern[used] == '*') {
        count = 0;
        while (text[count] &&
               atom_match(pattern, text[count], &i)) count++;
        for (; count >= 0; count--) {
            if (match_here(pattern + used + 1, text + count, end)) {
                *end += count;
                return 1;
            }
        }
        return 0;
    }
    if (!*text || !atom_match(pattern, *text, &used)) return 0;
    if (match_here(pattern + used, text + 1, end)) {
        (*end)++;
        return 1;
    }
    return 0;
}

static int find_match(const char *pattern, const char *text,
                      int *start, int *length) {
    int offset, end, anchored = pattern[0] == '^';
    const char *body = anchored ? pattern + 1 : pattern;
    int textlen = text_length(text);
    for (offset = 0; offset <= textlen; offset++) {
        if (match_here(body, text + offset, &end)) {
            *start = offset;
            *length = end;
            return 1;
        }
        if (anchored) break;
    }
    return 0;
}

static int append_char(char *out, int used, char c) {
    if (used >= MAX_RECORD - 1) return -1;
    out[used] = c;
    out[used + 1] = 0;
    return used + 1;
}

static int substitute(char *line, const char *pattern,
                      const char *replacement, int global) {
    char result[MAX_RECORD];
    int used = 0, offset = 0, start, length, changed = 0;
    int line_len = text_length(line), i, r;
    result[0] = 0;
    while (offset <= line_len &&
           find_match(pattern, line + offset, &start, &length)) {
        if (offset && pattern[0] == '^') break;
        start += offset;
        for (i = offset; i < start; i++) {
            used = append_char(result, used, line[i]);
            if (used < 0) return -1;
        }
        for (r = 0; replacement[r]; r++) {
            char c = replacement[r];
            if (c == '&') {
                int j;
                for (j = 0; j < length; j++) {
                    used = append_char(result, used, line[start + j]);
                    if (used < 0) return -1;
                }
            } else if (c == '\\' && replacement[r + 1]) {
                used = append_char(result, used, replacement[++r]);
                if (used < 0) return -1;
            } else {
                used = append_char(result, used, c);
                if (used < 0) return -1;
            }
        }
        changed = 1;
        offset = start + length;
        if (!global) break;
        if (!length && offset < line_len) {
            used = append_char(result, used, line[offset++]);
            if (used < 0) return -1;
        } else if (!length && offset == line_len) {
            break;
        }
    }
    if (!changed) return 0;
    for (i = offset; i < line_len; i++) {
        used = append_char(result, used, line[i]);
        if (used < 0) return -1;
    }
    text_copy(line, result, MAX_RECORD);
    return 1;
}

static int address_matches(const SedCommand *command, const char *line,
                           int line_number) {
    int match = 1, start, length;
    if (command->address_kind == ADDR_LINE)
        match = line_number == command->address_line;
    else if (command->address_kind == ADDR_REGEX)
        match = find_match(command->address, line, &start, &length);
    return command->address_not ? !match : match;
}

static int make_file_id(char *out, const char *fn,
                        const char *ft, const char *fm) {
    int i = 0;
    while (*fn && i < 8) out[i++] = *fn++;
    out[i++] = ' ';
    while (*ft && i < 17) out[i++] = *ft++;
    out[i++] = ' ';
    while (*fm && i < 25) out[i++] = *fm++;
    out[i] = 0;
    return i;
}

static int load_script(const char *fileid) {
    FILE *script;
    char line[MAX_SCRIPT_LINE];
    int line_number = 0;
    script = fopen(fileid, "r");
    if (!script) return 0;
    command_count = 0;
    while (fgets(line, sizeof line, script)) {
        int length = text_length(line);
        line_number++;
        while (length > 0 &&
               (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = 0;
        if (!parse_script_line(line, line_number)) {
            printf("sed: bad script at line %d\n", line_number);
            fclose(script);
            return 0;
        }
    }
    return fclose(script) == 0;
}

static int run_input(const char *fileid, int *line_number, int *quit) {
    FILE *input;
    char line[MAX_RECORD];
    int i, deleted, result;
    input = fopen(fileid, "r");
    if (!input) {
        printf("sed: cannot open input %s\n", fileid);
        return 0;
    }
    while (!*quit && fgets(line, sizeof line, input)) {
        int length = text_length(line);
        while (length > 0 &&
               (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = 0;
        (*line_number)++;
        deleted = 0;
        for (i = 0; i < command_count; i++) {
            SedCommand *command = &commands[i];
            if (!address_matches(command, line, *line_number)) continue;
            if (command->kind == CMD_DELETE) {
                deleted = 1;
                break;
            }
            if (command->kind == CMD_PRINT) {
                if (!deleted) puts(line);
            } else if (command->kind == CMD_QUIT) {
                *quit = 1;
                break;
            } else if (command->kind == CMD_SUBSTITUTE) {
                result = substitute(line, command->pattern,
                                    command->replacement, command->global);
                if (result < 0) {
                    puts("sed: output record too long");
                    fclose(input);
                    return 0;
                }
            }
        }
        if (!deleted) puts(line);
    }
    return fclose(input) == 0;
}

int main(int argc, char **argv) {
    char script_id[32], input_id[32];
    int count, line_number = 0, quit = 0, i, arg;
    if (argc < 7 || (argc - 4) % 3 != 0) {
        puts("Usage: S2SED scriptFN scriptFT scriptFM"
             " dataFN dataFT dataFM ...");
        return 4;
    }
    count = (argc - 4) / 3;
    if (count > MAX_INPUT_FILES) {
        puts("sed: too many input files");
        return 4;
    }
    make_file_id(script_id, argv[1], argv[2], argv[3]);
    if (!load_script(script_id)) {
        puts("sed: cannot read script");
        return 8;
    }
    for (i = 0; i < count && !quit; i++) {
        arg = 4 + i * 3;
        make_file_id(input_id, argv[arg], argv[arg + 1], argv[arg + 2]);
        if (!run_input(input_id, &line_number, &quit)) return 8;
    }
    return 0;
}
