/* CMSED 0.2: a CMS-record stream editor for GCCCMS/PDPCLIB. */
/* Supports basic editing, hold space, branching, ranges, and substitutions. */
#if defined(CMSED_HOST)
#include <stdio.h>
#include <string.h>
#else
typedef struct cms_file FILE;
extern FILE *fopen(const char *, const char *);
extern char *fgets(char *, int, FILE *);
extern int fclose(FILE *);
extern int printf(const char *, ...);
extern int sprintf(char *, const char *, ...);
extern int puts(const char *);
extern int fputs(const char *, FILE *);
extern int fputc(int, FILE *);
extern int remove(const char *);
#endif

enum {
    MAX_COMMANDS = 128,
    MAX_SCRIPT_LINE = 256,
    MAX_RECORD = 1024,
    MAX_STRING = 256,
    MAX_CAPTURES = 9,
    MAX_INPUT_FILES = 16
};

enum {
    CMD_SUBSTITUTE = 1,
    CMD_DELETE,
    CMD_PRINT,
    CMD_QUIT,
    CMD_TRANSLATE,
    CMD_HOLD,
    CMD_APPEND_HOLD,
    CMD_GET_HOLD,
    CMD_APPEND_GET,
    CMD_EXCHANGE,
    CMD_BRANCH,
    CMD_TEST,
    CMD_LABEL,
    CMD_APPEND,
    CMD_INSERT,
    CMD_CHANGE,
    CMD_READ_FILE,
    CMD_WRITE_FILE
};

enum {
    ADDR_ALL = 0,
    ADDR_LINE,
    ADDR_REGEX,
    ADDR_LAST
};

typedef struct {
    int kind;
    int address_kind;
    int address_line;
    int end_kind;
    int end_line;
    int has_end;
    int address_not;
    int range_active;
    int global;
    int substitute_print;
    int branch_target;
    FILE *write_file;
    char address[MAX_STRING];
    char end_address[MAX_STRING];
    char pattern[MAX_STRING];
    char replacement[MAX_STRING];
    char text[MAX_STRING];
    char second_text[MAX_STRING];
    char label[MAX_STRING];
} SedCommand;

static SedCommand commands[MAX_COMMANDS];
static int command_count;
static int quiet_mode;
static int last_substitution;
static int total_lines;
static char hold_space[MAX_RECORD];
static int in_place;
static FILE *output_stream;

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

static int parse_address(const char **cursor, int *kind, int *number,
                         char *pattern) {
    const char *text = *cursor;
    int value = 0;
    if (*text == '$') {
        *kind = ADDR_LAST;
        *number = 0;
        *cursor = text + 1;
        return 1;
    }
    if (is_digit(*text)) {
        *kind = ADDR_LINE;
        while (is_digit(*text)) {
            if (value > 100000000) return 0;
            value = value * 10 + *text++ - '0';
        }
        if (value < 1) return 0;
        *number = value;
        *cursor = text;
        return 1;
    }
    if (*text == '/') {
        *kind = ADDR_REGEX;
        text++;
        if (!read_delimited(&text, '/', pattern)) return 0;
        *number = 0;
        *cursor = text;
        return 1;
    }
    return 0;
}

static int parse_script_line(const char *line, int script_line) {
    SedCommand command;
    const char *text = line;
    int has_address = 0;
    int has_end = 0;
    while (is_space(*text)) text++;
    if (!*text || *text == '#') return 1;
    if (command_count >= MAX_COMMANDS) return 0;
    command.kind = 0;
    command.address_kind = ADDR_ALL;
    command.address_line = 0;
    command.end_kind = ADDR_ALL;
    command.end_line = 0;
    command.has_end = 0;
    command.address_not = 0;
    command.range_active = 0;
    command.global = 0;
    command.substitute_print = 0;
    command.branch_target = -1;
    command.write_file = 0;
    command.address[0] = 0;
    command.end_address[0] = 0;
    command.pattern[0] = 0;
    command.replacement[0] = 0;
    command.text[0] = 0;
    command.second_text[0] = 0;
    command.label[0] = 0;
    if (*text == ':') {
        int i = 0;
        text++;
        skip_space(&text);
        while (*text && !is_space(*text) && i < MAX_STRING - 1)
            command.label[i++] = *text++;
        command.label[i] = 0;
        skip_space(&text);
        if (!command.label[0] || *text) return 0;
        command.kind = CMD_LABEL;
        commands[command_count++] = command;
        return 1;
    }
    if (is_digit(*text) || *text == '$' || *text == '/') {
        has_address = 1;
        if (!parse_address(&text, &command.address_kind,
                           &command.address_line, command.address)) return 0;
        if (*text == ',') {
            text++;
            if (!parse_address(&text, &command.end_kind,
                               &command.end_line, command.end_address))
                return 0;
            has_end = command.has_end = 1;
        }
    }
    if (has_address && *text == '!') {
        command.address_not = 1;
        text++;
    }
    skip_space(&text);
    if (*text == 'd' || *text == 'p' || *text == 'q' ||
        *text == 'h' || *text == 'H' || *text == 'g' ||
        *text == 'G' || *text == 'x') {
        char c = *text++;
        skip_space(&text);
        if (*text) return 0;
        if (c == 'd') command.kind = CMD_DELETE;
        else if (c == 'p') command.kind = CMD_PRINT;
        else if (c == 'q') command.kind = CMD_QUIT;
        else if (c == 'h') command.kind = CMD_HOLD;
        else if (c == 'H') command.kind = CMD_APPEND_HOLD;
        else if (c == 'g') command.kind = CMD_GET_HOLD;
        else if (c == 'G') command.kind = CMD_APPEND_GET;
        else command.kind = CMD_EXCHANGE;
    } else if (*text == 's') {
        char delimiter;
        text++;
        delimiter = *text++;
        if (!delimiter || is_space(delimiter)) return 0;
        if (!read_delimited(&text, delimiter, command.pattern)) return 0;
        if (!read_delimited(&text, delimiter, command.replacement)) return 0;
        skip_space(&text);
        while (*text == 'g' || *text == 'p') {
            if (*text == 'g') command.global = 1;
            else command.substitute_print = 1;
            text++;
        }
        skip_space(&text);
        if (*text) return 0;
        command.kind = CMD_SUBSTITUTE;
    } else if (*text == 'y') {
        char delimiter;
        text++;
        delimiter = *text++;
        if (!delimiter || is_space(delimiter)) return 0;
        if (!read_delimited(&text, delimiter, command.pattern)) return 0;
        if (!read_delimited(&text, delimiter, command.replacement)) return 0;
        skip_space(&text);
        if (*text || text_length(command.pattern) !=
                     text_length(command.replacement)) return 0;
        command.kind = CMD_TRANSLATE;
    } else if (*text == 'b' || *text == 't') {
        int label_len;
        command.kind = *text++ == 'b' ? CMD_BRANCH : CMD_TEST;
        skip_space(&text);
        text_copy(command.label, text, MAX_STRING);
        label_len = text_length(command.label);
        while (label_len > 0 && is_space(command.label[label_len - 1]))
            command.label[--label_len] = 0;
    } else if (*text == 'a' || *text == 'i' || *text == 'c') {
        char c = *text++;
        skip_space(&text);
        text_copy(command.text, text, MAX_STRING);
        command.kind = c == 'a' ? CMD_APPEND :
                       c == 'i' ? CMD_INSERT : CMD_CHANGE;
    } else if (*text == 'r' || *text == 'w') {
        char c = *text++;
        skip_space(&text);
        text_copy(command.text, text, MAX_STRING);
        if (!command.text[0]) return 0;
        command.kind = c == 'r' ? CMD_READ_FILE : CMD_WRITE_FILE;
    } else {
        return 0;
    }
    if (!command.kind || (command.address_kind != ADDR_ALL && !has_address))
        return 0;
    if (has_end && !has_address) return 0;
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

static int match_here(const char *pattern, const char *text,
                      const char *base, int *end,
                      int *capture_start, int *capture_end,
                      int *capture_count) {
    int used, count, i;
    if (!*pattern) {
        *end = 0;
        return 1;
    }
    if (pattern[0] == '$' && !pattern[1]) {
        *end = 0;
        return !*text;
    }
    if (pattern[0] == '\\' && is_digit(pattern[1]) &&
        pattern[1] != '0') {
        int group = pattern[1] - '0';
        int start = capture_start[group];
        int length = capture_end[group] - start, j;
        if (start < 0 || length < 0) return 0;
        for (j = 0; j < length; j++)
            if (text[j] != base[start + j]) return 0;
        if (match_here(pattern + 2, text + length, base, end,
                       capture_start, capture_end, capture_count)) {
            *end += length;
            return 1;
        }
        return 0;
    }
    if (pattern[0] == '\\' && pattern[1] == '(') {
        int i, depth = 1, close = -1, group_length, tail_length;
        int group_index;
        int local_count = *capture_count;
        int start_save[MAX_CAPTURES + 1], end_save[MAX_CAPTURES + 1];
        char group_pattern[MAX_STRING];
        for (i = 0; i <= MAX_CAPTURES; i++) {
            start_save[i] = capture_start[i];
            end_save[i] = capture_end[i];
        }
        for (i = 2; pattern[i] && pattern[i + 1]; i++) {
            if (pattern[i] == '\\' && pattern[i + 1] == '(') {
                depth++;
                i++;
            } else if (pattern[i] == '\\' && pattern[i + 1] == ')') {
                depth--;
                if (!depth) {
                    close = i;
                    break;
                }
                i++;
            }
        }
        if (close < 0 || *capture_count >= MAX_CAPTURES) return 0;
        if (close - 2 >= MAX_STRING) return 0;
        for (i = 0; i < close - 2; i++)
            group_pattern[i] = pattern[i + 2];
        group_pattern[close - 2] = 0;
        group_index = ++(*capture_count);
        capture_start[group_index] = (int)(text - base);
        if (!match_here(group_pattern, text, base, &group_length,
                        capture_start, capture_end, capture_count)) {
            for (i = 0; i <= MAX_CAPTURES; i++) {
                capture_start[i] = start_save[i];
                capture_end[i] = end_save[i];
            }
            *capture_count = local_count;
            return 0;
        }
        capture_end[group_index] = capture_start[group_index] + group_length;
        if (match_here(pattern + close + 2, text + group_length, base,
                       &tail_length, capture_start, capture_end,
                       capture_count)) {
            *end = group_length + tail_length;
            return 1;
        }
        for (i = 0; i <= MAX_CAPTURES; i++) {
            capture_start[i] = start_save[i];
            capture_end[i] = end_save[i];
        }
        *capture_count = local_count;
        return 0;
    }
    if (pattern[0] == '\\' && pattern[1] == ')') {
        return match_here(pattern + 2, text, base, end, capture_start,
                          capture_end, capture_count);
    }
    atom_match(pattern, *text, &used);
    if (pattern[used] == '*') {
        int saved_count = *capture_count;
        int saved_start[MAX_CAPTURES + 1], saved_end[MAX_CAPTURES + 1];
        count = 0;
        for (i = 0; i <= MAX_CAPTURES; i++) {
            saved_start[i] = capture_start[i];
            saved_end[i] = capture_end[i];
        }
        while (text[count] &&
               atom_match(pattern, text[count], &i)) count++;
        for (; count >= 0; count--) {
            if (match_here(pattern + used + 1, text + count, base, end,
                           capture_start, capture_end, capture_count)) {
                *end += count;
                return 1;
            }
            *capture_count = saved_count;
            for (i = 0; i <= MAX_CAPTURES; i++) {
                capture_start[i] = saved_start[i];
                capture_end[i] = saved_end[i];
            }
        }
        return 0;
    }
    if (!*text || !atom_match(pattern, *text, &used)) return 0;
    if (match_here(pattern + used, text + 1, base, end, capture_start,
                   capture_end, capture_count)) {
        (*end)++;
        return 1;
    }
    return 0;
}

static int find_match(const char *pattern, const char *text,
                      int *start, int *length,
                      int *capture_start, int *capture_end) {
    int offset, end, anchored = pattern[0] == '^';
    const char *body = anchored ? pattern + 1 : pattern;
    int textlen = text_length(text);
    for (offset = 0; offset <= textlen; offset++) {
        int capture_count = 0, i;
        for (i = 0; i <= MAX_CAPTURES; i++) {
            capture_start[i] = -1;
            capture_end[i] = -1;
        }
        if (match_here(body, text + offset, text + offset, &end,
                       capture_start, capture_end, &capture_count)) {
            for (i = 1; i <= MAX_CAPTURES; i++) {
                if (capture_start[i] >= 0) {
                    capture_start[i] += offset;
                    capture_end[i] += offset;
                }
            }
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
    int capture_start[MAX_CAPTURES + 1], capture_end[MAX_CAPTURES + 1];
    result[0] = 0;
    while (offset <= line_len &&
           find_match(pattern, line + offset, &start, &length,
                      capture_start, capture_end)) {
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
                char next = replacement[++r];
                if (next >= '1' && next <= '9' &&
                    capture_start[next - '0'] >= 0) {
                    int group = next - '0';
                    int j, from = offset + capture_start[group];
                    int to = offset + capture_end[group];
                    for (j = from; j < to; j++) {
                        used = append_char(result, used, line[j]);
                        if (used < 0) return -1;
                    }
                } else {
                    used = append_char(result, used, next);
                    if (used < 0) return -1;
                }
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

static int one_address_matches(int kind, int number, const char *pattern,
                               const char *line, int line_number) {
    int start, length;
    int capture_start[MAX_CAPTURES + 1], capture_end[MAX_CAPTURES + 1];
    if (kind == ADDR_ALL) return 1;
    if (kind == ADDR_LINE) return line_number == number;
    if (kind == ADDR_LAST) return line_number == total_lines;
    return find_match(pattern, line, &start, &length,
                      capture_start, capture_end);
}

static int address_matches(SedCommand *command, const char *line,
                           int line_number) {
    int match;
    if (command->address_kind == ADDR_ALL) return 1;
    if (!command->has_end) {
        match = one_address_matches(command->address_kind,
                                    command->address_line,
                                    command->address, line, line_number);
    } else if (command->range_active) {
        match = 1;
        if (one_address_matches(command->end_kind, command->end_line,
                                command->end_address, line, line_number))
            command->range_active = 0;
    } else {
        match = one_address_matches(command->address_kind,
                                    command->address_line,
                                    command->address, line, line_number);
        if (match) {
            if (command->end_kind == ADDR_LINE &&
                command->end_line <= line_number)
                command->range_active = 0;
            else if (command->end_kind == ADDR_LAST &&
                     line_number == total_lines)
                command->range_active = 0;
            else
                command->range_active = 1;
        }
    }
    return command->address_not ? !match : match;
}

static void output_record(const char *line) {
    if (output_stream) {
        fputs(line, output_stream);
        fputc('\n', output_stream);
    } else {
        puts(line);
    }
}

#if !defined(CMSED_HOST)
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
#endif

static int load_script(const char *fileid) {
    FILE *script;
    char line[MAX_SCRIPT_LINE];
    int line_number = 0;
    script = fopen(fileid, "r");
    if (!script) return 0;
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

static int count_file_lines(const char *fileid, int *count) {
    FILE *input = fopen(fileid, "r");
    char line[MAX_RECORD];
    if (!input) return 0;
    while (fgets(line, sizeof line, input)) (*count)++;
    return fclose(input) == 0;
}

static int resolve_branches(void) {
    int i, j;
    for (i = 0; i < command_count; i++) {
        SedCommand *command = &commands[i];
        if (command->kind != CMD_BRANCH && command->kind != CMD_TEST)
            continue;
        command->branch_target = command_count;
        if (!command->label[0]) continue;
        for (j = 0; j < command_count; j++) {
            if (commands[j].kind == CMD_LABEL &&
                text_length(command->label) == text_length(commands[j].label)) {
                int k = 0;
                while (command->label[k] &&
                       command->label[k] == commands[j].label[k]) k++;
                if (!command->label[k]) break;
            }
        }
        if (j == command_count) return 0;
        command->branch_target = j;
    }
    return 1;
}

static void close_write_files(void) {
    int i;
    for (i = 0; i < command_count; i++) {
        if (commands[i].write_file) {
            fclose(commands[i].write_file);
            commands[i].write_file = 0;
        }
    }
}

static int process_stream(FILE *input, int *line_number, int *quit) {
    char line[MAX_RECORD], appended[4096], text[MAX_RECORD];
    int pc, deleted, result;
    while (!*quit && fgets(line, sizeof line, input)) {
        int length = text_length(line);
        while (length > 0 &&
               (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = 0;
        (*line_number)++;
        deleted = 0;
        last_substitution = 0;
        appended[0] = 0;
        pc = 0;
        while (pc < command_count) {
            SedCommand *command = &commands[pc];
            if (command->kind == CMD_LABEL) {
                pc++;
                continue;
            }
            if (!address_matches(command, line, *line_number)) {
                pc++;
                continue;
            }
            if (command->kind == CMD_DELETE) {
                deleted = 1;
                break;
            }
            if (command->kind == CMD_PRINT) {
                if (!deleted) output_record(line);
            } else if (command->kind == CMD_QUIT) {
                *quit = 1;
                break;
            } else if (command->kind == CMD_SUBSTITUTE) {
                result = substitute(line, command->pattern,
                                    command->replacement, command->global);
                if (result < 0) {
                    puts("sed: output record too long");
                    return 0;
                }
                if (result > 0) {
                    last_substitution = 1;
                    if (command->substitute_print) puts(line);
                }
            } else if (command->kind == CMD_TRANSLATE) {
                int j, k, n = text_length(command->pattern);
                for (j = 0; line[j]; j++)
                    for (k = 0; k < n; k++)
                        if (line[j] == command->pattern[k]) {
                            line[j] = command->replacement[k];
                            break;
                        }
            } else if (command->kind == CMD_HOLD) {
                text_copy(hold_space, line, MAX_RECORD);
            } else if (command->kind == CMD_APPEND_HOLD) {
                int n = text_length(hold_space);
                if (n && n < MAX_RECORD - 1) hold_space[n++] = '\n';
                text_copy(hold_space + n, line, MAX_RECORD - n);
            } else if (command->kind == CMD_GET_HOLD) {
                text_copy(line, hold_space, MAX_RECORD);
            } else if (command->kind == CMD_APPEND_GET) {
                int n = text_length(line);
                if (n < MAX_RECORD - 1) line[n++] = '\n';
                text_copy(line + n, hold_space, MAX_RECORD - n);
            } else if (command->kind == CMD_EXCHANGE) {
                text_copy(text, line, MAX_RECORD);
                text_copy(line, hold_space, MAX_RECORD);
                text_copy(hold_space, text, MAX_RECORD);
            } else if (command->kind == CMD_INSERT) {
                output_record(command->text);
            } else if (command->kind == CMD_APPEND ||
                       command->kind == CMD_READ_FILE) {
                if (command->kind == CMD_APPEND) {
                    int n = text_length(appended);
                    int m = text_length(command->text);
                    if (n + m + 2 >= 4096) {
                        fclose(input);
                        puts("sed: appended text too long");
                        return 0;
                    }
                    if (n) appended[n++] = '\n';
                    text_copy(appended + n, command->text, 4096 - n);
                    n += m;
                    appended[n++] = '\n';
                    appended[n] = 0;
                } else {
                    FILE *readfile = fopen(command->text, "r");
                    char readline[MAX_RECORD];
                    if (readfile) {
                        while (fgets(readline, sizeof readline, readfile)) {
                            int n = text_length(appended);
                            int m = text_length(readline);
                            if (n + m + 1 >= 4096) {
                                fclose(readfile);
                                puts("sed: appended text too long");
                                return 0;
                            }
                            text_copy(appended + n, readline, 4096 - n);
                            n += m;
                            if (!m || readline[m - 1] != '\n')
                                appended[n++] = '\n';
                            appended[n] = 0;
                        }
                        fclose(readfile);
                    }
                }
            } else if (command->kind == CMD_CHANGE) {
                output_record(command->text);
                deleted = 1;
                break;
            } else if (command->kind == CMD_WRITE_FILE) {
                if (!command->write_file) {
                    command->write_file = fopen(command->text, "w");
                    if (!command->write_file) {
                        fclose(input);
                        printf("sed: cannot open output %s\n", command->text);
                        return 0;
                    }
                }
                fputs(line, command->write_file);
                fputc('\n', command->write_file);
            } else if (command->kind == CMD_BRANCH) {
                pc = command->branch_target;
                continue;
            } else if (command->kind == CMD_TEST) {
                if (last_substitution) {
                    last_substitution = 0;
                    pc = command->branch_target;
                    continue;
                }
                last_substitution = 0;
            }
            pc++;
        }
        if (!deleted && !quiet_mode) output_record(line);
        if (appended[0]) {
            if (output_stream) fputs(appended, output_stream);
            else printf("%s", appended);
        }
    }
    return 1;
}

static int run_input(const char *fileid, int *line_number, int *quit) {
    FILE *input = fopen(fileid, "r");
    int ok;
    if (!input) {
        printf("sed: cannot open input %s\n", fileid);
        return 0;
    }
    ok = process_stream(input, line_number, quit);
    if (fclose(input) != 0) ok = 0;
    return ok;
}

#if !defined(CMSED_HOST)
static int copy_records(FILE *input, FILE *output) {
    char line[MAX_RECORD];
    while (fgets(line, sizeof line, input))
        if (fputs(line, output) < 0) return 0;
    return 1;
}

static int cms_in_place(const char *input_id, const char *filemode,
                        int *line_number, int *quit) {
    char temp_id[32], backup_id[32], temp_name[8], backup_name[8];
    FILE *probe, *input, *output, *backup, *staged;
    int serial, ok;
    for (serial = 1; serial < 10000; serial++) {
        sprintf(temp_name, "S2T%04d", serial);
        sprintf(backup_name, "S2B%04d", serial);
        make_file_id(temp_id, temp_name, "SEDTMP", filemode);
        make_file_id(backup_id, backup_name, "SEDBAK", filemode);
        probe = fopen(temp_id, "r");
        if (probe) {
            fclose(probe);
            continue;
        }
        probe = fopen(backup_id, "r");
        if (probe) {
            fclose(probe);
            continue;
        }
        break;
    }
    if (serial >= 10000) {
        puts("sed: no unused CMS temporary ids");
        return 0;
    }
    input = fopen(input_id, "r");
    if (!input) {
        printf("sed: cannot open input %s\n", input_id);
        return 0;
    }
    output = fopen(temp_id, "w");
    if (!output) {
        fclose(input);
        printf("sed: cannot create temporary %s\n", temp_id);
        return 0;
    }
    output_stream = output;
    ok = process_stream(input, line_number, quit);
    if (fclose(input) != 0) ok = 0;
    if (fclose(output) != 0) ok = 0;
    output_stream = 0;
    if (!ok) {
        remove(temp_id);
        return 0;
    }
    input = fopen(input_id, "r");
    backup = fopen(backup_id, "w");
    if (!input || !backup) {
        if (input) fclose(input);
        if (backup) fclose(backup);
        remove(temp_id);
        printf("sed: cannot preserve backup for %s\n", input_id);
        return 0;
    }
    ok = copy_records(input, backup);
    if (fclose(input) != 0) ok = 0;
    if (fclose(backup) != 0) ok = 0;
    if (!ok) {
        remove(temp_id);
        printf("sed: cannot write backup %s\n", backup_id);
        return 0;
    }
    staged = fopen(temp_id, "r");
    output = fopen(input_id, "w");
    if (!staged || !output) {
        if (staged) fclose(staged);
        if (output) fclose(output);
        printf("sed: cannot replace %s; backup is %s\n",
               input_id, backup_id);
        return 0;
    }
    ok = copy_records(staged, output);
    if (fclose(staged) != 0) ok = 0;
    if (fclose(output) != 0) ok = 0;
    if (!ok) {
        backup = fopen(backup_id, "r");
        output = fopen(input_id, "w");
        if (backup && output) {
            int restored = copy_records(backup, output);
            if (fclose(backup) != 0) restored = 0;
            if (fclose(output) != 0) restored = 0;
            if (!restored)
                printf("sed: restore failed; backup is %s\n", backup_id);
        } else {
            if (backup) fclose(backup);
            if (output) fclose(output);
            printf("sed: replacement failed; backup is %s\n", backup_id);
        }
        remove(temp_id);
        printf("sed: replacement failed; backup is %s\n", backup_id);
        return 0;
    }
    if (remove(temp_id) != 0)
        printf("sed: temporary retained as %s\n", temp_id);
    printf("sed: original backup kept as %s\n", backup_id);
    return ok;
}
#endif

#if defined(CMSED_HOST)
static int host_main(int argc, char **argv) {
    const char *files[MAX_INPUT_FILES];
    FILE *spool = 0;
    char line[MAX_RECORD];
    int file_count = 0, arg = 1, line_number = 0, quit = 0, i;
    int stdin_needed = 0, stdin_used = 0;
    command_count = 0;
    quiet_mode = 0;
    in_place = 0;
    hold_space[0] = 0;
    while (arg < argc && argv[arg][0] == '-') {
        if (!strcmp(argv[arg], "--")) {
            arg++;
            break;
        }
        if (!strcmp(argv[arg], "-n")) {
            quiet_mode = 1;
            arg++;
        } else if (!strcmp(argv[arg], "-i")) {
            in_place = 1;
            arg++;
        } else if (!strcmp(argv[arg], "-e")) {
            if (++arg >= argc || !parse_script_line(argv[arg], 1)) {
                puts("sed: invalid -e script");
                return 4;
            }
            arg++;
        } else if (!strcmp(argv[arg], "-f")) {
            if (++arg >= argc || !load_script(argv[arg++])) {
                puts("sed: cannot read -f script");
                return 4;
            }
        } else {
            printf("sed: unsupported option %s\n", argv[arg]);
            return 4;
        }
    }
    if (!command_count) {
        if (arg >= argc || !load_script(argv[arg++])) {
            puts("sed: no script supplied");
            return 4;
        }
    }
    while (arg < argc) {
        if (file_count >= MAX_INPUT_FILES) {
            puts("sed: too many input files");
            return 4;
        }
        files[file_count++] = argv[arg++];
    }
    if (!file_count) {
        files[file_count++] = "-";
    }
    for (i = 0; i < file_count; i++)
        if (!strcmp(files[i], "-")) stdin_needed = 1;
    if (in_place && stdin_needed) {
        puts("sed: -i cannot edit standard input");
        return 4;
    }
    if (!resolve_branches()) {
        puts("sed: undefined branch label");
        return 4;
    }
    total_lines = 0;
    for (i = 0; i < file_count; i++) {
        if (!strcmp(files[i], "-")) continue;
        if (!count_file_lines(files[i], &total_lines)) {
            printf("sed: cannot count input %s\n", files[i]);
            return 8;
        }
    }
    if (stdin_needed) {
        spool = tmpfile();
        if (!spool) {
            puts("sed: cannot spool standard input");
            return 8;
        }
        while (fgets(line, sizeof line, stdin)) {
            fputs(line, spool);
            total_lines++;
        }
        fseek(spool, 0L, 0);
    }
    for (i = 0; i < file_count && !quit; i++) {
        if (!strcmp(files[i], "-")) {
            if (stdin_used) continue;
            stdin_used = 1;
            fseek(spool, 0L, 0);
            output_stream = 0;
            if (!process_stream(spool, &line_number, &quit)) {
                fclose(spool);
                close_write_files();
                return 8;
            }
        } else if (!in_place) {
            output_stream = 0;
            if (!run_input(files[i], &line_number, &quit)) {
                close_write_files();
                return 8;
            }
        } else {
            char temp[MAX_STRING];
            FILE *input, *output, *probe;
            int ok;
            if (text_length(files[i]) >= MAX_STRING - 8) {
                puts("sed: input path too long for -i");
                close_write_files();
                return 4;
            }
            sprintf(temp, "%s.sedtmp", files[i]);
            probe = fopen(temp, "r");
            if (probe) {
                fclose(probe);
                printf("sed: temporary file already exists: %s\n", temp);
                close_write_files();
                return 8;
            }
            input = fopen(files[i], "r");
            output = fopen(temp, "w");
            if (!input || !output) {
                if (input) fclose(input);
                if (output) fclose(output);
                remove(temp);
                printf("sed: cannot stage %s\n", files[i]);
                close_write_files();
                return 8;
            }
            output_stream = output;
            ok = process_stream(input, &line_number, &quit);
            if (fclose(input) != 0) ok = 0;
            if (fclose(output) != 0) ok = 0;
            output_stream = 0;
            if (!ok || rename(temp, files[i]) != 0) {
                remove(temp);
                printf("sed: cannot replace %s\n", files[i]);
                close_write_files();
                return 8;
            }
        }
    }
    if (spool) fclose(spool);
    close_write_files();
    return 0;
}
#endif

int main(int argc, char **argv) {
#if defined(CMSED_HOST)
    return host_main(argc, argv);
#else
    char script_id[32], input_id[32];
    int count, line_number = 0, quit = 0, i, arg, have_script = 0;
    command_count = 0;
    quiet_mode = 0;
    last_substitution = 0;
    in_place = 0;
    hold_space[0] = 0;
    arg = 1;
    while (arg < argc && argv[arg][0] == '-') {
        if (!argv[arg][1] || (argv[arg][1] == '-' && !argv[arg][2])) {
            arg++;
            break;
        }
        if (argv[arg][1] == 'n' && !argv[arg][2]) {
            quiet_mode = 1;
            arg++;
        } else if (argv[arg][1] == 'i' && !argv[arg][2]) {
            in_place = 1;
            arg++;
        } else if (argv[arg][1] == 'e' && !argv[arg][2]) {
            if (++arg >= argc || !parse_script_line(argv[arg], 1)) {
                puts("sed: invalid -e script");
                return 4;
            }
            have_script = 1;
            arg++;
        } else if (argv[arg][1] == 'f' && !argv[arg][2]) {
            if (arg + 3 >= argc) {
                puts("sed: -f requires a CMS file id");
                return 4;
            }
            make_file_id(script_id, argv[arg + 1], argv[arg + 2],
                         argv[arg + 3]);
            if (!load_script(script_id)) {
                printf("sed: cannot read script %s\n", script_id);
                return 8;
            }
            have_script = 1;
            arg += 4;
        } else {
            printf("sed: unsupported option %s\n", argv[arg]);
            return 4;
        }
    }
    if (!have_script) {
        if (arg + 2 >= argc) {
            puts("Usage: S2SED [-n] [-e command] [-f fn ft fm]"
                 " scriptFN scriptFT scriptFM dataFN dataFT dataFM ...");
            return 4;
        }
        make_file_id(script_id, argv[arg], argv[arg + 1], argv[arg + 2]);
        if (!load_script(script_id)) {
            puts("sed: cannot read script");
            return 8;
        }
        arg += 3;
    }
    if ((argc - arg) < 3 || (argc - arg) % 3 != 0) {
        puts("sed: input file ids must be fn ft fm triples");
        return 4;
    }
    count = (argc - arg) / 3;
    if (count > MAX_INPUT_FILES) {
        puts("sed: too many input files");
        return 4;
    }
    if (!resolve_branches()) {
        puts("sed: undefined branch label");
        return 4;
    }
    total_lines = 0;
    for (i = 0; i < count; i++) {
        int input_arg = arg + i * 3;
        make_file_id(input_id, argv[input_arg], argv[input_arg + 1],
                     argv[input_arg + 2]);
        if (!count_file_lines(input_id, &total_lines)) {
            printf("sed: cannot count input %s\n", input_id);
            return 8;
        }
    }
    for (i = 0; i < count && !quit; i++) {
        int input_arg = arg + i * 3;
        make_file_id(input_id, argv[input_arg], argv[input_arg + 1],
                     argv[input_arg + 2]);
        if (!(in_place ?
              cms_in_place(input_id, argv[input_arg + 2],
                           &line_number, &quit) :
              run_input(input_id, &line_number, &quit))) {
            close_write_files();
            return 8;
        }
    }
    close_write_files();
    return 0;
#endif
}
