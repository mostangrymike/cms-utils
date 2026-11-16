/* CMSAWK 0.3: CMS-hosted AWK core implemented in C for GCCCMS. */
/* Invocation: CMSAWK script-fn script-ft script-fm data-fn data-ft data-fm */
/* Script: one rule per line; BEGIN/END; /regex/; print item[,item...]. */
/* Items: $0-$9, NR, NF, quoted strings, and numeric literals. */
/* Regex: ^ $ . * + ? and simple [abc]/[^abc] character classes. */
/* Data: CMS text records up to 1023 bytes; blank/tab default field split. */
/* Scalar integer assignments and updates; next and exit. */
/* No array variables, if/for/while, user functions or printf yet. */
typedef struct cms_file FILE;
extern FILE *fopen(const char *, const char *);
extern char *fgets(char *, int, FILE *);
extern int fclose(FILE *);
extern int puts(const char *);
extern int printf(const char *, ...);
enum {
    MAX_RULES = 16,
    MAX_ITEMS = 8,
    MAX_PATTERN = 128,
    MAX_STRING = 128,
    MAX_RECORD = 1024,
    MAX_FIELDS = 64,
    MAX_VARS = 64,
    RULE_NORMAL = 0,
    RULE_BEGIN = 1,
    RULE_END = 2,
    ITEM_FIELD = 0,
    ITEM_NR = 1,
    ITEM_NF = 2,
    ITEM_STRING = 3,
    ITEM_VAR = 4,
    ITEM_NUMBER = 5,
    ACT_PRINT = 0,
    ACT_SET = 1,
    ACT_ADD = 2,
    ACT_SUB = 3,
    ACT_MUL = 4,
    ACT_DIV = 5,
    ACT_INC = 6,
    ACT_DEC = 7,
    ACT_NEXT = 8,
    ACT_EXIT = 9
};
typedef struct {
    int kind;
    int field;
    int length;
    char text[MAX_STRING];
} AwkItem;
typedef struct {
    int kind;
    int pattern_length;
    char pattern[MAX_PATTERN];
    int action;
    char variable[MAX_STRING];
    AwkItem rhs;
    int item_count;
    AwkItem item[MAX_ITEMS];
} AwkRule;
typedef struct {
    int used;
    int initialized;
    char name[MAX_STRING];
    long value;
} AwkVariable;
typedef struct {
    int start;
    int length;
} AwkField;
static AwkRule rules[MAX_RULES];
static int rule_count;
static unsigned long nr;
static int nf;
static AwkField fields[MAX_FIELDS];
static AwkVariable variables[MAX_VARS];
static int variable_count;
static int next_requested;
static int exit_requested;
static int text_length(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}
static int is_space(char c) {
    return c == ' ' || c == '\t';
}
static int is_alpha(char c) {
    return (c >= 'A' && c <= 'I') || (c >= 'J' && c <= 'R') ||
           (c >= 'S' && c <= 'Z') || (c >= 'a' && c <= 'i') ||
           (c >= 'j' && c <= 'r') || (c >= 's' && c <= 'z');
}
static char *skip_space(char *p) {
    while (is_space(*p)) p++;
    return p;
}
static int starts_word(const char *p, const char *word) {
    int n = text_length(word);
    int i;
    for (i = 0; i < n; i++)
        if (!p[i] || p[i] != word[i]) return 0;
    return is_space(p[n]) || p[n] == '{' || p[n] == '}' ||
           p[n] == ',' || p[n] == 0;
}
static int number_text(unsigned long value, char *out) {
    char reversed[24];
    int count = 0;
    int i;
    do {
        reversed[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && count < 24);
    for (i = 0; i < count; i++) out[i] = reversed[count - i - 1];
    out[count] = 0;
    return count;
}
static int signed_number_text(long value, char *out) {
    unsigned long magnitude;
    int n;
    if (value < 0) {
        out[0] = '-';
        magnitude = (unsigned long)(-(value + 1)) + 1;
        n = number_text(magnitude, out + 1);
        return n + 1;
    }
    return number_text((unsigned long)value, out);
}
static int same_text(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == 0 && b[i] == 0;
}
static void copy_name(char *to, const char *from, int limit) {
    int i = 0;
    while (from[i] && i < limit - 1) {
        to[i] = from[i];
        i++;
    }
    to[i] = 0;
}
static int variable_index(const char *name, int create) {
    int i;
    for (i = 0; i < variable_count; i++)
        if (same_text(variables[i].name, name)) return i;
    if (!create || variable_count >= MAX_VARS) return -1;
    i = variable_count++;
    variables[i].used = 1;
    variables[i].initialized = 0;
    copy_name(variables[i].name, name, MAX_STRING);
    variables[i].value = 0;
    return i;
}
static long integer_value(const char *text) {
    long value = 0;
    int sign = 1;
    int i = 0;
    while (is_space(text[i])) i++;
    if (text[i] == '-') {
        sign = -1;
        i++;
    } else if (text[i] == '+') {
        i++;
    }
    while (text[i] >= '0' && text[i] <= '9') {
        value = value * 10 + (text[i] - '0');
        i++;
    }
    return sign * value;
}
static long item_number(const AwkItem *item, const char *record,
                        unsigned long recno, int fields_count) {
    char number[128];
    int index;
    int start = 0;
    int length = 0;
    int i;
    if (item->kind == ITEM_NR) return (long)recno;
    if (item->kind == ITEM_NF) return (long)fields_count;
    if (item->kind == ITEM_VAR) {
        index = variable_index(item->text, 1);
        return index < 0 ? 0 : variables[index].value;
    }
    if (item->kind == ITEM_FIELD) {
        if (item->field == 0) {
            length = text_length(record);
        } else if (item->field <= fields_count &&
                   item->field <= MAX_FIELDS) {
            start = fields[item->field - 1].start;
            length = fields[item->field - 1].length;
        }
        if (length >= (int)sizeof number) length = (int)sizeof number - 1;
        for (i = 0; i < length; i++) number[i] = record[start + i];
        number[length] = 0;
        return integer_value(number);
    }
    return integer_value(item->text);
}
static int parse_item(char **cursor, AwkItem *item) {
    char *p = skip_space(*cursor);
    int n = 0;
    item->kind = ITEM_FIELD;
    item->field = 0;
    item->length = 0;
    item->text[0] = 0;
    if (*p == '$') {
        p++;
        if (*p < '0' || *p > '9') return 0;
        item->field = *p - '0';
        p++;
        *cursor = p;
        return 1;
    }
    if (starts_word(p, "NR")) {
        item->kind = ITEM_NR;
        *cursor = p + 2;
        return 1;
    }
    if (starts_word(p, "NF")) {
        item->kind = ITEM_NF;
        *cursor = p + 2;
        return 1;
    }
    if (is_alpha(*p) || *p == '_') {
        item->kind = ITEM_VAR;
        while ((is_alpha(*p) || (*p >= '0' && *p <= '9') || *p == '_') &&
               n < MAX_STRING - 1)
            item->text[n++] = *p++;
        item->text[n] = 0;
        item->length = n;
        *cursor = p;
        return 1;
    }
    item->kind = ITEM_STRING;
    if (*p == '"') {
        p++;
        while (*p && *p != '"') {
            char c = *p++;
            if (c == '\\') {
                if (!*p) return 0;
                c = *p++;
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
                else if (c != '\\' && c != '"') return 0;
            }
            if (n >= MAX_STRING - 1) return 0;
            item->text[n++] = c;
        }
        if (*p != '"') return 0;
        p++;
    } else if ((*p >= '0' && *p <= '9') ||
               (*p == '-' && p[1] >= '0' && p[1] <= '9')) {
        while ((*p >= '0' && *p <= '9') || *p == '.' ||
                (*p == '-' && n == 0)) {
            if (n >= MAX_STRING - 1) return 0;
            item->text[n++] = *p++;
        }
        item->kind = ITEM_NUMBER;
    } else {
        return 0;
    }
    item->text[n] = 0;
    item->length = n;
    *cursor = p;
    return 1;
}
static int parse_name(char **cursor, char *out, int limit) {
    char *p = skip_space(*cursor);
    int n = 0;
    if (!is_alpha(*p) && *p != '_') return 0;
    while ((is_alpha(*p) || (*p >= '0' && *p <= '9') || *p == '_') &&
           n < limit - 1)
        out[n++] = *p++;
    out[n] = 0;
    *cursor = p;
    return n > 0;
}
static int parse_rule(char *line, AwkRule *rule) {
    char *p = skip_space(line);
    int n = 0;
    int closed = 0;
    int i;
    if (!*p || *p == '#') return 0;
    rule->kind = RULE_NORMAL;
    rule->pattern_length = 0;
    rule->pattern[0] = 0;
    rule->action = ACT_PRINT;
    rule->variable[0] = 0;
    rule->rhs.kind = ITEM_NUMBER;
    rule->rhs.text[0] = '0';
    rule->rhs.text[1] = 0;
    rule->rhs.length = 1;
    rule->item_count = 0;
    for (i = 0; i < MAX_ITEMS; i++) {
        rule->item[i].kind = ITEM_FIELD;
        rule->item[i].field = 0;
        rule->item[i].length = 0;
        rule->item[i].text[0] = 0;
    }
    if (starts_word(p, "BEGIN")) {
        rule->kind = RULE_BEGIN;
        p = skip_space(p + 5);
    } else if (starts_word(p, "END")) {
        rule->kind = RULE_END;
        p = skip_space(p + 3);
    } else if (*p == '/') {
        int escaped = 0;
        p++;
        while (*p) {
            char c = *p++;
            if (!escaped && c == '/') {
                closed = 1;
                break;
            }
            if (n >= MAX_PATTERN - 1) return -1;
            rule->pattern[n++] = c;
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
        }
        if (!closed) return -1;
        rule->pattern[n] = 0;
        rule->pattern_length = n;
        p = skip_space(p);
    } else {
        p = skip_space(p);
    }
    if (*p++ != '{') return -1;
    p = skip_space(p);
    if (starts_word(p, "print")) {
        p = skip_space(p + 5);
        if (*p == '}') {
            rule->item_count = 1;
            rule->item[0].kind = ITEM_FIELD;
            rule->item[0].field = 0;
            p++;
        } else {
            for (;;) {
                if (rule->item_count >= MAX_ITEMS) return -1;
                if (!parse_item(&p, &rule->item[rule->item_count])) return -1;
                rule->item_count++;
                p = skip_space(p);
                if (*p == ',') {
                    p = skip_space(p + 1);
                    continue;
                }
                if (*p != '}') return -1;
                p++;
                break;
            }
        }
    } else if (starts_word(p, "next")) {
        rule->action = ACT_NEXT;
        p = skip_space(p + 4);
        if (*p != '}') return -1;
        p++;
    } else if (starts_word(p, "exit")) {
        rule->action = ACT_EXIT;
        p = skip_space(p + 4);
        if (*p != '}') return -1;
        p++;
    } else {
        if (!parse_name(&p, rule->variable, MAX_STRING)) return -1;
        p = skip_space(p);
        if (p[0] == '+' && p[1] == '+') {
            rule->action = ACT_INC;
            p += 2;
        } else if (p[0] == '-' && p[1] == '-') {
            rule->action = ACT_DEC;
            p += 2;
        } else if (p[0] == '+' && p[1] == '=') {
            rule->action = ACT_ADD;
            p += 2;
            if (!parse_item(&p, &rule->rhs)) return -1;
        } else if (p[0] == '-' && p[1] == '=') {
            rule->action = ACT_SUB;
            p += 2;
            if (!parse_item(&p, &rule->rhs)) return -1;
        } else if (p[0] == '*' && p[1] == '=') {
            rule->action = ACT_MUL;
            p += 2;
            if (!parse_item(&p, &rule->rhs)) return -1;
        } else if (p[0] == '/' && p[1] == '=') {
            rule->action = ACT_DIV;
            p += 2;
            if (!parse_item(&p, &rule->rhs)) return -1;
        } else if (*p == '=') {
            rule->action = ACT_SET;
            p++;
            if (!parse_item(&p, &rule->rhs)) return -1;
        } else {
            return -1;
        }
        p = skip_space(p);
        if (rule->action >= ACT_SET && rule->action <= ACT_DIV &&
            rule->rhs.kind == ITEM_STRING) return -1;
        if (*p != '}') return -1;
        p++;
    }
    p = skip_space(p);
    if (*p && *p != '#') return -1;
    return 1;
}
static int atom_match(const char **pattern, char text) {
    const char *p = *pattern;
    int negate = 0;
    int matched = 0;
    if (*p == '.') {
        *pattern = p + 1;
        return text != 0;
    }
    if (*p == '\\' && p[1]) {
        *pattern = p + 2;
        return p[1] == text;
    }
    if (*p != '[') {
        if (*p) *pattern = p + 1;
        return *p == text;
    }
    p++;
    if (*p == '^') {
        negate = 1;
        p++;
    }
    while (*p && *p != ']') {
        if (p[1] == '-' && p[2] && p[2] != ']') {
            if (text >= p[0] && text <= p[2]) matched = 1;
            p += 3;
        } else {
            if (*p == text) matched = 1;
            p++;
        }
    }
    if (*p != ']') {
        *pattern = *pattern + 1;
        return text == '[';
    }
    *pattern = p + 1;
    return negate ? !matched : matched;
}
static int match_here(const char *pattern, const char *text);
static int match_star(const char *atom, const char *rest, const char *text) {
    const char *next;
    if (match_here(rest, text)) return 1;
    while (*text) {
        next = atom;
        if (!atom_match(&next, *text)) break;
        text++;
        if (match_here(rest, text)) return 1;
    }
    return 0;
}
static int match_here(const char *pattern, const char *text) {
    const char *next = pattern;
    int matched;
    if (!*pattern) return 1;
    if (*pattern == '$' && pattern[1] == 0) return *text == 0;
    matched = atom_match(&next, *text);
    if (*next == '*') return match_star(pattern, next + 1, text);
    if (*next == '+')
        return matched && match_star(pattern, next + 1, text + 1);
    if (*next == '?') {
        next++;
        if (matched && match_here(next, text + 1)) return 1;
        return match_here(next, text);
    }
    if (!matched) return 0;
    return match_here(next, text + 1);
}
static int regex_match(const char *pattern, const char *text) {
    const char *p = pattern;
    if (*p == '^') return match_here(p + 1, text);
    for (;;) {
        if (match_here(p, text)) return 1;
        if (!*text) return 0;
        text++;
    }
}
static void split_record(const char *record, int length) {
    int i = 0;
    nf = 0;
    while (i < length) {
        int start;
        while (i < length && (record[i] == ' ' || record[i] == '\t')) i++;
        if (i >= length) break;
        start = i;
        while (i < length && record[i] != ' ' && record[i] != '\t') i++;
        if (nf < MAX_FIELDS) {
            fields[nf].start = start;
            fields[nf].length = i - start;
        }
        nf++;
    }
}
static int append(char *out, int *used, const char *text, int length) {
    int i;
    if (length < 0 || *used + length >= MAX_RECORD * 4) return 0;
    for (i = 0; i < length; i++) out[(*used)++] = text[i];
    out[*used] = 0;
    return 1;
}
static int print_rule(const AwkRule *rule, const char *record,
                      int reclen, unsigned long recno, int fields_count) {
    char out[MAX_RECORD * 4];
    char numbuf[24];
    int used = 0;
    int i;
    out[0] = 0;
    for (i = 0; i < rule->item_count; i++) {
        const AwkItem *item = &rule->item[i];
        const char *text = "";
        int length = 0;
        if (i && !append(out, &used, " ", 1)) return 0;
        if (item->kind == ITEM_FIELD) {
            if (item->field == 0) {
                text = record;
                length = reclen;
            } else if (item->field <= fields_count &&
                       item->field <= MAX_FIELDS) {
                text = record + fields[item->field - 1].start;
                length = fields[item->field - 1].length;
            }
        } else if (item->kind == ITEM_NR) {
            length = number_text(recno, numbuf);
            text = numbuf;
        } else if (item->kind == ITEM_NF) {
            length = number_text((unsigned long)fields_count, numbuf);
            text = numbuf;
        } else if (item->kind == ITEM_VAR) {
            int vi = variable_index(item->text, 1);
            if (vi >= 0 && variables[vi].initialized) {
                length = signed_number_text(variables[vi].value, numbuf);
                text = numbuf;
            }
        } else {
            text = item->text;
            length = item->length;
        }
        if (!append(out, &used, text, length)) return 0;
    }
    puts(out);
    return 1;
}
static int run_rule(const AwkRule *rule, const char *record, int reclen,
                    unsigned long recno, int fields_count) {
    int vi;
    long rhs;
    if (rule->action == ACT_PRINT)
        return print_rule(rule, record, reclen, recno, fields_count);
    if (rule->action == ACT_NEXT) {
        next_requested = 1;
        return 1;
    }
    if (rule->action == ACT_EXIT) {
        exit_requested = 1;
        return 1;
    }
    vi = variable_index(rule->variable, 1);
    if (vi < 0) return 0;
    if (rule->action == ACT_INC) {
        variables[vi].value++;
    } else if (rule->action == ACT_DEC) {
        variables[vi].value--;
    } else {
        rhs = item_number(&rule->rhs, record, recno, fields_count);
        if (rule->action == ACT_SET) variables[vi].value = rhs;
        else if (rule->action == ACT_ADD) variables[vi].value += rhs;
        else if (rule->action == ACT_SUB) variables[vi].value -= rhs;
        else if (rule->action == ACT_MUL) variables[vi].value *= rhs;
        else if (rule->action == ACT_DIV) {
            if (rhs == 0) return 0;
            variables[vi].value /= rhs;
        }
    }
    variables[vi].initialized = 1;
    return 1;
}
static void trim_record(char *line) {
    int n = text_length(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' ||
                     line[n - 1] == ' ')) line[--n] = 0;
}
static int make_file_id(char *out, const char *fn,
                        const char *ft, const char *fm) {
    int i = 0;
    while (*fn) out[i++] = *fn++;
    out[i++] = ' ';
    while (*ft) out[i++] = *ft++;
    out[i++] = ' ';
    while (*fm) out[i++] = *fm++;
    out[i] = 0;
    return i;
}
int main(int argc, char **argv) {
    char script_id[32], data_id[32], line[MAX_RECORD];
    char last_record[MAX_RECORD];
    FILE *script, *data;
    int i, last_nf = 0, run_data = 1, data_open = 0;
    if (argc != 7) {
        puts("Usage: CMSAWK scriptFN scriptFT scriptFM dataFN dataFT dataFM");
        return 4;
    }
    make_file_id(script_id, argv[1], argv[2], argv[3]);
    make_file_id(data_id, argv[4], argv[5], argv[6]);
    script = fopen(script_id, "r");
    if (!script) {
        puts("CMSAWK: cannot open script file");
        return 8;
    }
    rule_count = 0;
    while (fgets(line, sizeof line, script)) {
        int parsed;
        trim_record(line);
        if (!*skip_space(line) || *skip_space(line) == '#') continue;
        if (rule_count >= MAX_RULES) {
            fclose(script);
            puts("CMSAWK: rule limit exceeded");
            return 4;
        }
        parsed = parse_rule(line, &rules[rule_count]);
        if (parsed == 0) continue;
        if (parsed < 0) {
            printf("CMSAWK invalid rule %d: %s\n", rule_count + 1, line);
            fclose(script);
            puts("CMSAWK: invalid rule or rule limit exceeded");
            return 4;
        }
        rule_count++;
    }
    fclose(script);
    nr = 0;
    nf = 0;
    variable_count = 0;
    next_requested = 0;
    exit_requested = 0;
    last_record[0] = 0;
    for (i = 0; i < rule_count; i++) {
        if (rules[i].kind == RULE_BEGIN &&
            !run_rule(&rules[i], "", 0, nr, nf)) return 8;
        if (exit_requested) {
            run_data = 0;
            break;
        }
    }
    data = 0;
    if (run_data) {
        data = fopen(data_id, "r");
        if (!data) {
            puts("CMSAWK: cannot open data file");
            return 8;
        }
        data_open = 1;
    }
    while (run_data && !exit_requested && fgets(line, sizeof line, data)) {
        int length;
        trim_record(line);
        length = text_length(line);
        nr++;
        next_requested = 0;
        split_record(line, length);
        last_nf = nf;
        if (length >= MAX_RECORD) {
            fclose(data);
            puts("CMSAWK: input record too long");
            return 8;
        }
        {
            int j;
            for (j = 0; j <= length; j++) last_record[j] = line[j];
        }
        for (i = 0; i < rule_count; i++) {
            if (rules[i].kind == RULE_NORMAL &&
                (!rules[i].pattern_length ||
                 regex_match(rules[i].pattern, line))) {
                if (!run_rule(&rules[i], line, length, nr, nf)) {
                    if (data_open) fclose(data);
                    puts("CMSAWK: action failed");
                    return 8;
                }
                if (next_requested) break;
                if (exit_requested) break;
            }
        }
    }
    if (data_open) fclose(data);
    for (i = 0; i < rule_count; i++) {
        if (rules[i].kind == RULE_END &&
            !run_rule(&rules[i], last_record, text_length(last_record),
                      nr, last_nf)) return 8;
    }
    return 0;
}
