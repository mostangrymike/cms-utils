/* CMSAWK 0.4: C89 AWK interpreter for GCCCMS/PDPCLIB. */
/* Supports arrays, expressions, if/loops, functions, printf and sprintf. */
/* One CMS source file; fixed tables bound source, variables and array sizes. */
typedef struct cms_file FILE;
extern FILE *fopen(const char *, const char *);
extern char *fgets(char *, int, FILE *);
extern int fclose(FILE *);
extern int puts(const char *);
extern int printf(const char *, ...);
extern int sprintf(char *, const char *, ...);
enum {
    SOURCE_SIZE = 16384, MAX_RULES = 64, MAX_FUNCS = 24,
    MAX_VARS = 64, MAX_ARRAYS = 32, MAX_ITEMS = 64,
    MAX_STRING = 128, MAX_KEY = 64, MAX_RECORD = 1024,
    MAX_FIELDS = 64, MAX_PARAMS = 16, MAX_FRAMES = 12,
    TOK_ID = 256, TOK_NUM, TOK_STR, TOK_REGEX, TOK_SEMI,
    TOK_EQ, TOK_NE, TOK_LE, TOK_GE, TOK_AND, TOK_OR,
    TOK_INC, TOK_DEC, TOK_ADD_EQ, TOK_SUB_EQ, TOK_MUL_EQ,
    TOK_DIV_EQ, TOK_MOD_EQ, TOK_MATCH, TOK_NMATCH, TOK_APPEND, TOK_IN,
    V_EMPTY = 0, V_NUM, V_STR, V_ARRAY,
    RULE_ALL = 0, RULE_REGEX = 1, RULE_EXPR = 2,
    RULE_BEGIN = 3, RULE_END = 4,
    FLOW_NONE = 0, FLOW_BREAK, FLOW_CONTINUE, FLOW_NEXT,
    FLOW_EXIT, FLOW_RETURN
};
typedef struct {
    int kind;
    double number;
    char text[MAX_STRING];
    int array_id;
    int ref_kind;
    int ref_index;
    int ref_entry;
} Value;
typedef struct {
    int used;
    char key[MAX_KEY];
    Value value;
} ArrayItem;
typedef struct {
    int used;
    int count;
    ArrayItem item[MAX_ITEMS];
} AwkArray;
typedef struct {
    int used;
    char name[MAX_STRING];
    int array_id;
    Value value;
    int alias_kind;
    int alias_index;
} AwkVar;
typedef struct {
    int kind;
    int pattern_start;
    int body_start;
    int body_end;
    char pattern[MAX_STRING];
} AwkRule;
typedef struct {
    int used;
    char name[MAX_STRING];
    int param_count;
    char param[MAX_PARAMS][MAX_STRING];
    int body_start;
    int body_end;
} AwkFunction;
typedef struct {
    int kind;
    int start;
    int end;
    double number;
    char text[MAX_STRING];
} Token;
typedef struct {
    int pos;
    Token token;
    int regex_next;
} ParserState;
typedef struct {
    AwkVar var[MAX_PARAMS];
    int count;
    int flow;
    Value result;
} CallFrame;
static char source[SOURCE_SIZE];
static int source_length;
static int source_pos;
static Token token;
static int regex_next;
static AwkRule rules[MAX_RULES];
static int rule_count;
static AwkFunction functions[MAX_FUNCS];
static int function_count;
static AwkVar globals[MAX_VARS];
static int global_count;
static AwkArray arrays[MAX_ARRAYS];
static int array_count;
static CallFrame frames[MAX_FRAMES];
static int frame_depth;
static int flow_state;
static unsigned long nr;
static int nf;
static int field_start[MAX_FIELDS];
static int field_length[MAX_FIELDS];
static char record[MAX_RECORD];
static int record_length;
static int error_flag;
static int execute_enabled = 1;
static void format_output(const char *, Value *, int, char *, int);
static int slen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}
static void scopy(char *to, const char *from, int limit) {
    int i = 0;
    while (from[i] && i < limit - 1) {
        to[i] = from[i];
        i++;
    }
    to[i] = 0;
}
static int seq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == 0 && b[i] == 0;
}
static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}
static int is_alpha(char c) {
    return (c >= 'A' && c <= 'I') || (c >= 'J' && c <= 'R') ||
           (c >= 'S' && c <= 'Z') || (c >= 'a' && c <= 'i') ||
           (c >= 'j' && c <= 'r') || (c >= 's' && c <= 'z');
}
static int is_digit(char c) {
    return c >= '0' && c <= '9';
}
static void token_text(Token *t, const char *s, int n) {
    int i;
    if (n >= MAX_STRING) n = MAX_STRING - 1;
    for (i = 0; i < n; i++) t->text[i] = s[i];
    t->text[n] = 0;
}
static void lex_next(void) {
    int start, n;
    char c;
    while (source_pos < source_length) {
        c = source[source_pos];
        if (is_space(c)) {
            source_pos++;
            continue;
        }
        if (c == '#') {
            while (source_pos < source_length &&
                   source[source_pos] != '\n') source_pos++;
            continue;
        }
        break;
    }
    token.start = source_pos;
    token.text[0] = 0;
    token.number = 0;
    if (source_pos >= source_length) {
        token.kind = 0;
        token.end = source_pos;
        return;
    }
    c = source[source_pos++];
    if (c == '\n' || c == ';') {
        while (source_pos < source_length &&
               (source[source_pos] == '\n' || source[source_pos] == ';'))
            source_pos++;
        token.kind = TOK_SEMI;
        token.end = source_pos;
        return;
    }
    if (regex_next && c == '/') {
        int escaped = 0;
        start = source_pos;
        while (source_pos < source_length) {
            c = source[source_pos++];
            if (!escaped && c == '/') break;
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
        }
        token.kind = TOK_REGEX;
        token_text(&token, source + start, source_pos - start - 1);
        regex_next = 0;
        token.end = source_pos;
        return;
    }
    regex_next = 0;
    if (is_alpha(c) || c == '_') {
        start = source_pos - 1;
        while (source_pos < source_length &&
               (is_alpha(source[source_pos]) || is_digit(source[source_pos]) ||
                source[source_pos] == '_')) source_pos++;
        n = source_pos - start;
        token.kind = TOK_ID;
        token_text(&token, source + start, n);
        if (seq(token.text, "in")) token.kind = TOK_IN;
        token.end = source_pos;
        return;
    }
    if (is_digit(c) || (c == '.' && is_digit(source[source_pos]))) {
        double value = 0;
        double place = 0.1;
        int exponent = 0, exponent_sign = 1;
        int fraction = 0;
        start = source_pos - 1;
        if (c != '.') value = c - '0';
        else fraction = 1;
        while (source_pos < source_length) {
            c = source[source_pos];
            if (is_digit(c)) {
                source_pos++;
                if (fraction) {
                    value += (c - '0') * place;
                    place *= 0.1;
                } else value = value * 10 + (c - '0');
            } else if (c == '.' && !fraction) {
                fraction = 1;
                source_pos++;
            } else break;
        }
        if (source_pos < source_length &&
            (source[source_pos] == 'e' || source[source_pos] == 'E')) {
            source_pos++;
            if (source_pos < source_length &&
                (source[source_pos] == '+' || source[source_pos] == '-')) {
                if (source[source_pos] == '-') exponent_sign = -1;
                source_pos++;
            }
            while (source_pos < source_length &&
                   is_digit(source[source_pos]) && exponent < 100) {
                exponent = exponent * 10 + source[source_pos++] - '0';
            }
            while (exponent-- > 0) {
                if (exponent_sign > 0) value *= 10;
                else value /= 10;
            }
        }
        token.kind = TOK_NUM;
        token.number = value;
        token_text(&token, source + start, source_pos - start);
        token.end = source_pos;
        return;
    }
    if (c == '"') {
        n = 0;
        while (source_pos < source_length) {
            c = source[source_pos++];
            if (c == '"') break;
            if (c == '\\' && source_pos < source_length) {
                c = source[source_pos++];
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
                else if (c != '\\' && c != '"') {
                    error_flag = 1;
                    break;
                }
            }
            if (n < MAX_STRING - 1) token.text[n++] = c;
        }
        token.text[n] = 0;
        token.kind = TOK_STR;
        token.end = source_pos;
        return;
    }
    token.kind = (unsigned char)c;
    if (source_pos < source_length) {
        char d = source[source_pos];
        if (c == '=' && d == '=') token.kind = TOK_EQ;
        else if (c == '!' && d == '=') token.kind = TOK_NE;
        else if (c == '<' && d == '=') token.kind = TOK_LE;
        else if (c == '>' && d == '=') token.kind = TOK_GE;
        else if (c == '&' && d == '&') token.kind = TOK_AND;
        else if (c == '|' && d == '|') token.kind = TOK_OR;
        else if (c == '+' && d == '+') token.kind = TOK_INC;
        else if (c == '-' && d == '-') token.kind = TOK_DEC;
        else if (c == '+' && d == '=') token.kind = TOK_ADD_EQ;
        else if (c == '-' && d == '=') token.kind = TOK_SUB_EQ;
        else if (c == '*' && d == '=') token.kind = TOK_MUL_EQ;
        else if (c == '/' && d == '=') token.kind = TOK_DIV_EQ;
        else if (c == '%' && d == '=') token.kind = TOK_MOD_EQ;
        else if (c == '!' && d == '~') token.kind = TOK_NMATCH;
        else if (c == '~') token.kind = TOK_MATCH;
        else if (c == '>' && d == '>') token.kind = TOK_APPEND;
        if (token.kind != (unsigned char)c) source_pos++;
    }
    if (token.kind == TOK_MATCH || token.kind == TOK_NMATCH) regex_next = 1;
    token.end = source_pos;
}
static ParserState save_parser(void) {
    ParserState s;
    s.pos = source_pos;
    s.token = token;
    s.regex_next = regex_next;
    return s;
}
static void restore_parser(ParserState s) {
    source_pos = s.pos;
    token = s.token;
    regex_next = s.regex_next;
}
static void set_parser(int pos) {
    source_pos = pos;
    regex_next = 0;
    lex_next();
}
static int accept(int kind) {
    if (token.kind != kind) return 0;
    lex_next();
    return 1;
}
static int require(int kind) {
    if (accept(kind)) return 1;
    error_flag = 1;
    return 0;
}
static void skip_semis(void) {
    while (accept(TOK_SEMI)) {}
}
static Value empty_value(void) {
    Value v;
    int i;
    v.kind = V_EMPTY;
    v.number = 0;
    v.text[0] = 0;
    v.array_id = -1;
    v.ref_kind = 0;
    v.ref_index = -1;
    v.ref_entry = -1;
    for (i = 1; i < MAX_STRING; i++) v.text[i] = 0;
    return v;
}
static Value number_value(double n) {
    Value v = empty_value();
    v.kind = V_NUM;
    v.number = n;
    return v;
}
static Value string_value(const char *s) {
    Value v = empty_value();
    v.kind = V_STR;
    scopy(v.text, s, MAX_STRING);
    return v;
}
static int number_string(const char *s, double *out) {
    int i = 0, sign = 1, digits = 0;
    int exponent = 0, exponent_sign = 1;
    double n = 0, place = 0.1;
    while (is_space(s[i])) i++;
    if (s[i] == '-') { sign = -1; i++; }
    else if (s[i] == '+') i++;
    while (is_digit(s[i])) {
        digits = 1;
        n = n * 10 + s[i++] - '0';
    }
    if (s[i] == '.') {
        i++;
        while (is_digit(s[i])) {
            digits = 1;
            n += (s[i++] - '0') * place;
            place *= 0.1;
        }
    }
    if (s[i] == 'e' || s[i] == 'E') {
        i++;
        if (s[i] == '-') { exponent_sign = -1; i++; }
        else if (s[i] == '+') i++;
        if (!is_digit(s[i])) return 0;
        while (is_digit(s[i]) && exponent < 100)
            exponent = exponent * 10 + s[i++] - '0';
        while (exponent-- > 0) {
            if (exponent_sign > 0) n *= 10;
            else n /= 10;
        }
    }
    while (is_space(s[i])) i++;
    if (!digits || s[i]) return 0;
    *out = sign * n;
    return 1;
}
static double as_number(Value v) {
    double n;
    if (v.kind == V_NUM) return v.number;
    if (v.kind == V_STR && number_string(v.text, &n)) return n;
    return 0;
}
static void as_string(Value v, char *out) {
    if (v.kind == V_STR || v.kind == V_EMPTY) scopy(out, v.text, MAX_STRING);
    else if (v.kind == V_NUM) sprintf(out, "%.10g", v.number);
    else scopy(out, "", MAX_STRING);
}
static int truth(Value v) {
    if (v.kind == V_EMPTY) return 0;
    if (v.kind == V_STR) return v.text[0] != 0;
    if (v.kind == V_ARRAY) return 1;
    return v.number != 0;
}
static int find_global(const char *name, int create) {
    int i;
    for (i = 0; i < global_count; i++)
        if (seq(globals[i].name, name)) return i;
    if (!create || global_count >= MAX_VARS) return -1;
    i = global_count++;
    globals[i].used = 1;
    scopy(globals[i].name, name, MAX_STRING);
    globals[i].array_id = -1;
    globals[i].value = empty_value();
    globals[i].alias_kind = 0;
    globals[i].alias_index = -1;
    return i;
}
static int find_local(const char *name, int create) {
    int i;
    CallFrame *f;
    if (frame_depth <= 0) return -1;
    f = &frames[frame_depth - 1];
    for (i = 0; i < f->count; i++)
        if (seq(f->var[i].name, name)) return i;
    if (!create || f->count >= MAX_PARAMS) return -1;
    i = f->count++;
    f->var[i].used = 1;
    scopy(f->var[i].name, name, MAX_STRING);
    f->var[i].array_id = -1;
    f->var[i].value = empty_value();
    f->var[i].alias_kind = 0;
    f->var[i].alias_index = -1;
    return i;
}
static AwkVar *resolve_var(const char *name, int create,
                           int *scope, int *index) {
    int i;
    if (frame_depth > 0) {
        i = find_local(name, 0);
        if (i >= 0) {
            *scope = frame_depth;
            *index = i;
            return &frames[frame_depth - 1].var[i];
        }
    }
    i = find_global(name, create);
    if (i < 0) return 0;
    *scope = 0;
    *index = i;
    return &globals[i];
}
static AwkVar *ref_var(int scope, int index) {
    if (scope > 0 && scope <= frame_depth)
        return &frames[scope - 1].var[index];
    if (scope == 0 && index >= 0 && index < global_count)
        return &globals[index];
    return 0;
}
static int new_array(void) {
    int i;
    if (array_count >= MAX_ARRAYS) return -1;
    i = array_count++;
    arrays[i].used = 1;
    arrays[i].count = 0;
    return i;
}
static int ensure_array(AwkVar *v) {
    if (v->array_id >= 0) return v->array_id;
    if (v->alias_kind) {
        AwkVar *alias = ref_var(v->alias_kind == 2 ?
                               frame_depth - 1 : 0, v->alias_index);
        if (alias && alias->array_id >= 0) {
            v->array_id = alias->array_id;
            return v->array_id;
        }
        if (alias) {
            alias->array_id = new_array();
            v->array_id = alias->array_id;
            return v->array_id;
        }
    }
    v->array_id = new_array();
    return v->array_id;
}
static int array_item(int aid, const char *key, int create) {
    int i;
    int free_slot = -1;
    if (aid < 0 || aid >= array_count) return -1;
    for (i = 0; i < arrays[aid].count; i++) {
        if (arrays[aid].item[i].used &&
            seq(arrays[aid].item[i].key, key)) return i;
        if (!arrays[aid].item[i].used && free_slot < 0) free_slot = i;
    }
    if (!create) return -1;
    if (free_slot >= 0) i = free_slot;
    else {
        if (arrays[aid].count >= MAX_ITEMS) return -1;
        i = arrays[aid].count++;
    }
    arrays[aid].item[i].used = 1;
    scopy(arrays[aid].item[i].key, key, MAX_KEY);
    arrays[aid].item[i].value = empty_value();
    return i;
}
static int ref_kind_array(void) {
    return 2;
}
static Value get_variable(const char *name) {
    Value v = empty_value();
    AwkVar *var;
    int scope, index;
    var = resolve_var(name, 1, &scope, &index);
    if (!var) {
        error_flag = 1;
        return v;
    }
    v = var->array_id >= 0 ? empty_value() : var->value;
    v.ref_kind = 1;
    v.ref_index = index;
    v.ref_entry = scope;
    v.array_id = var->array_id;
    if (var->array_id >= 0) v.kind = V_ARRAY;
    return v;
}
static Value get_element(const char *name, const char *key, int create) {
    Value v = empty_value();
    AwkVar *var;
    int scope, index, aid, entry;
    var = resolve_var(name, 1, &scope, &index);
    if (!var) return v;
    aid = ensure_array(var);
    entry = array_item(aid, key, create);
    if (entry < 0) return v;
    v = arrays[aid].item[entry].value;
    v.ref_kind = ref_kind_array();
    v.ref_index = aid;
    v.ref_entry = entry;
    return v;
}
static void assign_ref(Value ref, Value value) {
    AwkVar *var;
    if (ref.ref_kind == 1) {
        var = ref_var(ref.ref_entry, ref.ref_index);
        if (var) var->value = value;
    } else if (ref.ref_kind == 2 && ref.ref_index >= 0 &&
               ref.ref_index < array_count && ref.ref_entry >= 0 &&
               ref.ref_entry < arrays[ref.ref_index].count) {
        arrays[ref.ref_index].item[ref.ref_entry].value = value;
    } else error_flag = 1;
}
static void split_fields(void) {
    int i = 0;
    nf = 0;
    while (i < record_length) {
        int start;
        while (i < record_length &&
               (record[i] == ' ' || record[i] == '\t')) i++;
        if (i >= record_length) break;
        start = i;
        while (i < record_length &&
               record[i] != ' ' && record[i] != '\t') i++;
        if (nf < MAX_FIELDS) {
            field_start[nf] = start;
            field_length[nf] = i - start;
        }
        nf++;
    }
    if (nf > MAX_FIELDS) nf = MAX_FIELDS;
}
static Value field_value(int n) {
    char text[MAX_STRING];
    int i, len;
    if (n == 0) return string_value(record);
    if (n < 1 || n > nf || n > MAX_FIELDS) return empty_value();
    len = field_length[n - 1];
    if (len >= MAX_STRING) len = MAX_STRING - 1;
    for (i = 0; i < len; i++) text[i] = record[field_start[n - 1] + i];
    text[len] = 0;
    return string_value(text);
}
static int regex_here(const char *p, const char *s);
static int regex_atom(const char **p, char c) {
    const char *q = *p;
    int neg = 0, hit = 0;
    if (*q == '.') {
        *p = q + 1;
        return c != 0;
    }
    if (*q == '\\' && q[1]) {
        *p = q + 2;
        return q[1] == c;
    }
    if (*q != '[') {
        if (*q) *p = q + 1;
        return *q == c;
    }
    q++;
    if (*q == '^') { neg = 1; q++; }
    while (*q && *q != ']') {
        if (q[1] == '-' && q[2] && q[2] != ']') {
            if (c >= q[0] && c <= q[2]) hit = 1;
            q += 3;
        } else {
            if (c == *q) hit = 1;
            q++;
        }
    }
    if (*q == ']') q++;
    *p = q;
    return neg ? !hit : hit;
}
static int regex_star(const char *atom, const char *rest, const char *s) {
    const char *p;
    if (regex_here(rest, s)) return 1;
    while (*s) {
        p = atom;
        if (!regex_atom(&p, *s)) break;
        s++;
        if (regex_here(rest, s)) return 1;
    }
    return 0;
}
static int regex_here(const char *p, const char *s) {
    const char *q = p;
    int hit;
    if (!*p) return 1;
    if (*p == '$' && !p[1]) return !*s;
    hit = regex_atom(&q, *s);
    if (*q == '*') return regex_star(p, q + 1, s);
    if (*q == '+') return hit && regex_star(p, q + 1, s + 1);
    if (*q == '?') {
        q++;
        return (hit && regex_here(q, s + 1)) || regex_here(q, s);
    }
    return hit && regex_here(q, s + 1);
}
static int regex_match(const char *p, const char *s) {
    if (*p == '^') return regex_here(p + 1, s);
    do {
        if (regex_here(p, s)) return 1;
    } while (*s++);
    return 0;
}
static int prec_start(int k) {
    return k == TOK_ID || k == TOK_NUM || k == TOK_STR || k == '$' ||
           k == '(' || k == '!';
}
static Value parse_expression(void);
static Value parse_assignment(void);
static Value parse_statement(int run);
static int find_function(const char *name) {
    int i;
    for (i = 0; i < function_count; i++)
        if (seq(functions[i].name, name)) return i;
    return -1;
}
static Value call_function(int fi, Value *args, int argc) {
    Value result = empty_value();
    ParserState resume;
    int i, caller_flow;
    if (!execute_enabled) return result;
    if (fi < 0 || fi >= function_count || frame_depth >= MAX_FRAMES) {
        error_flag = 1;
        return result;
    }
    resume = save_parser();
    caller_flow = flow_state;
    flow_state = FLOW_NONE;
    frame_depth++;
    frames[frame_depth - 1].count = 0;
    frames[frame_depth - 1].flow = FLOW_NONE;
    frames[frame_depth - 1].result = empty_value();
    for (i = 0; i < functions[fi].param_count; i++) {
        int vi = find_local(functions[fi].param[i], 1);
        if (vi < 0) { error_flag = 1; break; }
        if (i < argc) {
            frames[frame_depth - 1].var[vi].value = args[i];
            if (args[i].ref_kind == 1 && args[i].array_id >= 0) {
                frames[frame_depth - 1].var[vi].array_id = args[i].array_id;
            } else if (args[i].ref_kind == 1) {
                frames[frame_depth - 1].var[vi].alias_kind =
                    args[i].ref_entry == 0 ? 1 : 2;
                frames[frame_depth - 1].var[vi].alias_index =
                    args[i].ref_index;
            } else if (args[i].kind == V_ARRAY) {
                frames[frame_depth - 1].var[vi].array_id = args[i].array_id;
            }
        }
    }
    set_parser(functions[fi].body_start);
    parse_statement(1);
    if (flow_state == FLOW_RETURN) {
        result = frames[frame_depth - 1].result;
        flow_state = caller_flow;
    } else if (flow_state != FLOW_EXIT && flow_state != FLOW_NEXT) {
        if (flow_state != FLOW_NONE) error_flag = 1;
        flow_state = caller_flow;
    }
    frame_depth--;
    restore_parser(resume);
    return result;
}
static Value call_builtin(const char *name, Value *args, int argc) {
    Value v = empty_value();
    char text[MAX_STRING];
    if (seq(name, "length")) {
        if (argc && args[0].kind == V_ARRAY &&
            args[0].array_id >= 0 && args[0].array_id < array_count) {
            int i, count = 0;
            for (i = 0; i < arrays[args[0].array_id].count; i++)
                if (arrays[args[0].array_id].item[i].used) count++;
            return number_value(count);
        }
        if (argc) as_string(args[0], text);
        else scopy(text, record, MAX_STRING);
        return number_value((double)slen(text));
    }
    if (seq(name, "int") && argc)
        return number_value((long)as_number(args[0]));
    if (seq(name, "tolower") && argc) {
        int i;
        as_string(args[0], text);
        for (i = 0; text[i]; i++)
            if (text[i] >= 'A' && text[i] <= 'Z') text[i] += 32;
        return string_value(text);
    }
    if (seq(name, "toupper") && argc) {
        int i;
        as_string(args[0], text);
        for (i = 0; text[i]; i++)
            if (text[i] >= 'a' && text[i] <= 'z') text[i] -= 32;
        return string_value(text);
    }
    if (seq(name, "substr") && argc >= 2) {
        int start = (int)as_number(args[1]) - 1;
        int len, i;
        as_string(args[0], text);
        if (start < 0) start = 0;
        len = argc >= 3 ? (int)as_number(args[2]) : slen(text) - start;
        if (len < 0) len = 0;
        for (i = 0; i < len && i < MAX_STRING - 1 &&
                    text[start + i]; i++) v.text[i] = text[start + i];
        v.text[i] = 0;
        v.kind = V_STR;
        return v;
    }
    return v;
}
static Value parse_primary(void) {
    Value v = empty_value();
    if (token.kind == TOK_NUM) {
        v = number_value(token.number);
        lex_next();
        return v;
    }
    if (token.kind == TOK_STR) {
        v = string_value(token.text);
        lex_next();
        return v;
    }
    if (token.kind == TOK_REGEX) {
        v = string_value(token.text);
        lex_next();
        return v;
    }
    if (accept('(')) {
        v = parse_expression();
        require(')');
        return v;
    }
    if (accept('$')) {
        Value n = parse_primary();
        return field_value((int)as_number(n));
    }
    if (token.kind == TOK_ID) {
        char name[MAX_STRING];
        Value args[MAX_PARAMS];
        int argc = 0, fi;
        scopy(name, token.text, MAX_STRING);
        lex_next();
        if (seq(name, "NR")) return number_value((double)nr);
        if (seq(name, "NF")) return number_value((double)nf);
        if (seq(name, "SUBSEP")) {
            char sep[2];
            sep[0] = '\034';
            sep[1] = 0;
            return string_value(sep);
        }
        if (accept('(')) {
            if (token.kind != ')') {
                do {
                    if (argc >= MAX_PARAMS) { error_flag = 1; break; }
                    args[argc++] = parse_assignment();
                } while (accept(','));
            }
            require(')');
            if (!execute_enabled) return empty_value();
            if (seq(name, "sprintf")) {
                char fmt[MAX_STRING], out[1024];
                if (!argc) return string_value("");
                as_string(args[0], fmt);
                format_output(fmt, args + 1, argc - 1, out, sizeof out);
                return string_value(out);
            }
            if (seq(name, "length") || seq(name, "int") ||
                seq(name, "tolower") || seq(name, "toupper") ||
                seq(name, "substr"))
                return call_builtin(name, args, argc);
            fi = find_function(name);
            return call_function(fi, args, argc);
        }
        if (accept('[')) {
            char key[MAX_KEY];
            Value index = parse_assignment();
            as_string(index, key);
            while (accept(',')) {
                char part[MAX_KEY], joined[MAX_KEY];
                int n, j;
                index = parse_assignment();
                as_string(index, part);
                scopy(joined, key, MAX_KEY);
                n = slen(joined);
                if (n < MAX_KEY - 1) joined[n++] = '\034';
                joined[n] = 0;
                for (j = 0; part[j] && n < MAX_KEY - 1; j++)
                    joined[n++] = part[j];
                joined[n] = 0;
                scopy(key, joined, MAX_KEY);
            }
            require(']');
            if (!execute_enabled) return empty_value();
            return get_element(name, key, 1);
        }
        return get_variable(name);
    }
    error_flag = 1;
    if (token.kind) lex_next();
    return v;
}
static Value parse_unary(void) {
    Value v;
    int op = token.kind;
    if (op == TOK_INC || op == TOK_DEC) {
        int inc = op == TOK_INC;
        double n;
        lex_next();
        v = parse_unary();
        if (!v.ref_kind) error_flag = 1;
        n = as_number(v) + (inc ? 1 : -1);
        if (execute_enabled) assign_ref(v, number_value(n));
        return number_value(n);
    }
    if (op == '!' || op == '-' || op == '+') {
        lex_next();
        v = parse_unary();
        if (op == '!') return number_value(!truth(v));
        if (op == '-') return number_value(-as_number(v));
        return number_value(as_number(v));
    }
    v = parse_primary();
    if (token.kind == TOK_INC || token.kind == TOK_DEC) {
        int inc = token.kind == TOK_INC;
        double n = as_number(v);
        lex_next();
        if (!v.ref_kind) error_flag = 1;
        else if (execute_enabled)
            assign_ref(v, number_value(n + (inc ? 1 : -1)));
        return number_value(n);
    }
    return v;
}
static Value parse_multiply(void) {
    Value a = parse_unary(), b;
    int op;
    while (token.kind == '*' || token.kind == '/' || token.kind == '%') {
        op = token.kind;
        lex_next();
        b = parse_unary();
        if (op == '*') a = number_value(as_number(a) * as_number(b));
        else if (op == '/') {
            if (as_number(b) == 0) error_flag = 1;
            else a = number_value(as_number(a) / as_number(b));
        } else {
            if ((long)as_number(b) == 0) error_flag = 1;
            else a = number_value((long)as_number(a) % (long)as_number(b));
        }
    }
    return a;
}
static Value parse_add(void) {
    Value a = parse_multiply(), b;
    int op;
    while (token.kind == '+' || token.kind == '-') {
        op = token.kind;
        lex_next();
        b = parse_multiply();
        a = number_value(op == '+' ?
                         as_number(a) + as_number(b) :
                         as_number(a) - as_number(b));
    }
    return a;
}
static Value parse_concat(void) {
    Value a = parse_add(), b;
    char left[MAX_STRING], right[MAX_STRING], joined[MAX_STRING];
    int n, i;
    while (prec_start(token.kind)) {
        b = parse_add();
        as_string(a, left);
        as_string(b, right);
        n = slen(left);
        for (i = 0; right[i] && n < MAX_STRING - 1; i++)
            left[n++] = right[i];
        left[n] = 0;
        scopy(joined, left, MAX_STRING);
        a = string_value(joined);
    }
    return a;
}
static int compare_values(Value a, Value b) {
    char x[MAX_STRING], y[MAX_STRING];
    double nx, ny;
    if (a.kind == V_NUM && b.kind == V_NUM)
        return a.number < b.number ? -1 : a.number > b.number ? 1 : 0;
    as_string(a, x);
    as_string(b, y);
    if (number_string(x, &nx) && number_string(y, &ny))
        return nx < ny ? -1 : nx > ny ? 1 : 0;
    {
        int i = 0;
        while (x[i] && y[i] && x[i] == y[i]) i++;
        return x[i] == y[i] ? 0 : (x[i] < y[i] ? -1 : 1);
    }
}
static Value parse_compare(void) {
    Value a = parse_concat(), b;
    int op, cmp;
    while (token.kind == TOK_EQ || token.kind == TOK_NE ||
           token.kind == '<' || token.kind == '>' ||
           token.kind == TOK_LE || token.kind == TOK_GE ||
           token.kind == TOK_MATCH || token.kind == TOK_NMATCH ||
           token.kind == TOK_IN) {
        op = token.kind;
        lex_next();
        b = parse_concat();
        if (op == TOK_IN) {
            char key[MAX_KEY];
            as_string(a, key);
            a = number_value(b.kind == V_ARRAY && b.array_id >= 0 &&
                             array_item(b.array_id, key, 0) >= 0);
            continue;
        }
        cmp = compare_values(a, b);
        if (op == TOK_EQ) a = number_value(cmp == 0);
        else if (op == TOK_NE) a = number_value(cmp != 0);
        else if (op == '<') a = number_value(cmp < 0);
        else if (op == '>') a = number_value(cmp > 0);
        else if (op == TOK_LE) a = number_value(cmp <= 0);
        else if (op == TOK_GE) a = number_value(cmp >= 0);
        else {
            char pat[MAX_STRING], text[MAX_STRING];
            as_string(a, text);
            as_string(b, pat);
            cmp = regex_match(pat, text);
            a = number_value(op == TOK_MATCH ? cmp : !cmp);
        }
    }
    return a;
}
static Value parse_and(void) {
    Value a = parse_compare(), b;
    while (accept(TOK_AND)) {
        int old_enabled = execute_enabled;
        if (!truth(a)) execute_enabled = 0;
        b = parse_compare();
        execute_enabled = old_enabled;
        a = number_value(truth(a) && truth(b));
    }
    return a;
}
static Value parse_or(void) {
    Value a = parse_and(), b;
    while (accept(TOK_OR)) {
        int old_enabled = execute_enabled;
        if (truth(a)) execute_enabled = 0;
        b = parse_and();
        execute_enabled = old_enabled;
        a = number_value(truth(a) || truth(b));
    }
    return a;
}
static Value parse_conditional(void) {
    Value a = parse_or(), b, c;
    if (accept('?')) {
        int old_enabled = execute_enabled;
        if (!truth(a)) execute_enabled = 0;
        b = parse_assignment();
        execute_enabled = old_enabled;
        require(':');
        if (truth(a)) execute_enabled = 0;
        c = parse_assignment();
        execute_enabled = old_enabled;
        return truth(a) ? b : c;
    }
    return a;
}
static Value parse_assignment(void) {
    Value left = parse_conditional(), right;
    int op = token.kind;
    double a, b;
    if (op != '=' && op != TOK_ADD_EQ && op != TOK_SUB_EQ &&
        op != TOK_MUL_EQ && op != TOK_DIV_EQ && op != TOK_MOD_EQ)
        return left;
    lex_next();
    right = parse_assignment();
    if (!left.ref_kind) {
        error_flag = 1;
        return right;
    }
    if (op == '=') {
        if (execute_enabled) assign_ref(left, right);
    }
    else {
        a = as_number(left);
        b = as_number(right);
        if (op == TOK_ADD_EQ) a += b;
        else if (op == TOK_SUB_EQ) a -= b;
        else if (op == TOK_MUL_EQ) a *= b;
        else if (op == TOK_DIV_EQ) {
            if (b == 0) error_flag = 1;
            else a /= b;
        } else {
            if ((long)b == 0) error_flag = 1;
            else a = (long)a % (long)b;
        }
        right = number_value(a);
        if (execute_enabled) assign_ref(left, right);
    }
    return right;
}
static Value parse_expression(void) {
    return parse_assignment();
}
static int find_statement_end(int start);
static int append_output(char *out, int used, int cap, const char *text) {
    int i = 0;
    while (text[i] && used + i < cap - 1) {
        out[used + i] = text[i];
        i++;
    }
    out[used + i] = 0;
    return used + i;
}
static void format_output(const char *fmt, Value *args, int argc,
                          char *out, int cap) {
    int i = 0, used = 0, ai = 0, n, j;
    char spec[32], temp[256], text[MAX_STRING];
    char c;
    out[0] = 0;
    while (fmt[i] && used < cap - 1) {
        if (fmt[i] != '%') {
            temp[0] = fmt[i++];
            temp[1] = 0;
            used = append_output(out, used, cap, temp);
            continue;
        }
        i++;
        if (fmt[i] == '%') {
            used = append_output(out, used, cap, "%");
            i++;
            continue;
        }
        n = 0;
        spec[n++] = '%';
        while (fmt[i] && n < 28 &&
               (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' ||
                fmt[i] == '#' || fmt[i] == '0' || is_digit(fmt[i]) ||
                fmt[i] == '.')) spec[n++] = fmt[i++];
        if (fmt[i] == 'l' && n < 29) spec[n++] = fmt[i++];
        c = fmt[i];
        if (!c) break;
        if (c == 's' || c == 'c' || c == 'd' || c == 'i' ||
            c == 'u' || c == 'x' || c == 'X' || c == 'o' ||
            c == 'f' || c == 'F' || c == 'e' || c == 'E' ||
            c == 'g' || c == 'G') {
            spec[n++] = c;
            spec[n] = 0;
            i++;
            if (ai >= argc) {
                used = append_output(out, used, cap, "");
                continue;
            }
            if (c == 's') {
                as_string(args[ai], text);
                sprintf(temp, spec, text);
            } else if (c == 'c') {
                sprintf(temp, spec, (int)as_number(args[ai]));
            } else if (c == 'd' || c == 'i' || c == 'u' ||
                       c == 'x' || c == 'X' || c == 'o') {
                if (n > 2 && spec[n - 2] == 'l')
                    sprintf(temp, spec, (long)as_number(args[ai]));
                else sprintf(temp, spec, (int)as_number(args[ai]));
            } else {
                sprintf(temp, spec, as_number(args[ai]));
            }
            ai++;
            used = append_output(out, used, cap, temp);
        } else {
            for (j = 0; j < n && used < cap - 1; j++) {
                temp[0] = spec[j];
                temp[1] = 0;
                used = append_output(out, used, cap, temp);
            }
            if (fmt[i]) i++;
        }
    }
}
static void print_value(Value v) {
    char text[MAX_STRING];
    as_string(v, text);
    printf("%s", text);
}
static void print_statement(int run, int formatted) {
    Value args[MAX_PARAMS];
    int argc = 0, i;
    if (formatted) {
        if (accept('(')) {
            if (token.kind != ')') {
                do {
                    if (argc < MAX_PARAMS) args[argc++] = parse_assignment();
                    else error_flag = 1;
                } while (accept(','));
            }
            require(')');
        } else {
            do {
                if (argc < MAX_PARAMS) args[argc++] = parse_assignment();
                else error_flag = 1;
            } while (accept(','));
        }
        if (run && argc > 0) {
            char fmt[MAX_STRING], out[1024];
            as_string(args[0], fmt);
            format_output(fmt, args + 1, argc - 1, out, sizeof out);
            printf("%s", out);
        }
        return;
    }
    if (accept('(')) {
        if (token.kind != ')') {
            do {
                if (argc < MAX_PARAMS) args[argc++] = parse_assignment();
                else error_flag = 1;
            } while (accept(','));
        }
        require(')');
    } else if (token.kind != TOK_SEMI && token.kind != '}' &&
        token.kind != 0) {
        do {
            if (argc < MAX_PARAMS) args[argc++] = parse_assignment();
            else error_flag = 1;
        } while (accept(','));
    } else {
        args[argc++] = field_value(0);
    }
    if (run) {
        int oi = find_global("OFS", 0);
        int ri = find_global("ORS", 0);
        char sep[MAX_STRING], end[MAX_STRING];
        if (oi >= 0 && globals[oi].value.kind != V_EMPTY)
            as_string(globals[oi].value, sep);
        else scopy(sep, " ", MAX_STRING);
        if (ri >= 0 && globals[ri].value.kind != V_EMPTY)
            as_string(globals[ri].value, end);
        else scopy(end, "\n", MAX_STRING);
        for (i = 0; i < argc; i++) {
            if (i) printf("%s", sep);
            print_value(args[i]);
        }
        printf("%s", end);
    }
}
static void skip_balanced(int left, int right) {
    int depth = 1;
    lex_next();
    while (token.kind && depth > 0) {
        if (token.kind == left) depth++;
        else if (token.kind == right) depth--;
        lex_next();
    }
}
static void skip_to_delimiter(int delimiter) {
    int parens = 0, brackets = 0;
    while (token.kind) {
        if (token.kind == delimiter && parens == 0 && brackets == 0) return;
        if (token.kind == '(') parens++;
        else if (token.kind == ')' && parens > 0) parens--;
        else if (token.kind == '[') brackets++;
        else if (token.kind == ']' && brackets > 0) brackets--;
        lex_next();
    }
}
static int find_statement_end(int start) {
    ParserState saved = save_parser();
    int end, body_end;
    set_parser(start);
    if (token.kind == '{') {
        skip_balanced('{', '}');
        end = token.start;
    } else if (token.kind == TOK_ID && seq(token.text, "if")) {
        lex_next();
        if (token.kind == '(') skip_balanced('(', ')');
        body_end = find_statement_end(token.start);
        set_parser(body_end);
        if (token.kind == TOK_ID && seq(token.text, "else")) {
            lex_next();
            end = find_statement_end(token.start);
        } else end = body_end;
    } else if (token.kind == TOK_ID &&
               (seq(token.text, "while") || seq(token.text, "for"))) {
        lex_next();
        if (token.kind == '(') skip_balanced('(', ')');
        end = find_statement_end(token.start);
    } else if (token.kind == TOK_ID && seq(token.text, "do")) {
        lex_next();
        body_end = find_statement_end(token.start);
        set_parser(body_end);
        if (token.kind == TOK_ID && seq(token.text, "while")) {
            lex_next();
            if (token.kind == '(') skip_balanced('(', ')');
            if (token.kind == TOK_SEMI) {
                lex_next();
                end = token.start;
            } else end = token.start;
        } else end = body_end;
    } else {
        while (token.kind && token.kind != TOK_SEMI && token.kind != '}')
            lex_next();
        if (token.kind == TOK_SEMI) lex_next();
        end = token.start;
    }
    restore_parser(saved);
    return end;
}
static Value parse_statement_impl(int run) {
    Value result = empty_value();
    int loop_end, cond_pos, inc_pos, body_start;
    int loop, aid, entry;
    char name[MAX_STRING], key[MAX_KEY];
    double cond;
    if (token.kind == TOK_SEMI) {
        lex_next();
        return result;
    }
    if (token.kind == '{') {
        lex_next();
        skip_semis();
        while (token.kind && token.kind != '}') {
            int enabled = run && flow_state == FLOW_NONE;
            parse_statement(enabled);
            skip_semis();
        }
        require('}');
        return result;
    }
    if (token.kind == TOK_ID && seq(token.text, "if")) {
        lex_next();
        require('(');
        result = parse_expression();
        require(')');
        cond = truth(result);
        parse_statement(run && cond);
        skip_semis();
        if (token.kind == TOK_ID && seq(token.text, "else")) {
            lex_next();
            parse_statement(run && !cond && flow_state == FLOW_NONE);
        }
        return result;
    }
    if (token.kind == TOK_ID && seq(token.text, "while")) {
        lex_next();
        require('(');
        cond_pos = token.start;
        skip_to_delimiter(')');
        require(')');
        body_start = token.start;
        loop_end = find_statement_end(body_start);
        loop = 0;
        while (run && !error_flag && loop++ < 100000) {
            set_parser(cond_pos);
            result = parse_expression();
            if (!truth(result)) break;
            set_parser(body_start);
            parse_statement(1);
            if (flow_state == FLOW_BREAK) {
                flow_state = FLOW_NONE;
                break;
            }
            if (flow_state == FLOW_CONTINUE) flow_state = FLOW_NONE;
            else if (flow_state != FLOW_NONE) break;
        }
        if (loop >= 100000) error_flag = 1;
        set_parser(loop_end);
        return result;
    }
    if (token.kind == TOK_ID && seq(token.text, "do")) {
        lex_next();
        body_start = token.start;
        loop_end = find_statement_end(body_start);
        loop = 0;
        do {
            set_parser(body_start);
            parse_statement(run);
            if (flow_state == FLOW_BREAK) {
                flow_state = FLOW_NONE;
                break;
            }
            if (flow_state == FLOW_CONTINUE) flow_state = FLOW_NONE;
            else if (flow_state != FLOW_NONE) break;
            set_parser(loop_end);
            if (!(token.kind == TOK_ID && seq(token.text, "while"))) {
                error_flag = 1;
                return result;
            }
            lex_next();
            require('(');
            result = parse_expression();
            require(')');
        } while (run && truth(result) && ++loop < 100000);
        if (loop >= 100000) error_flag = 1;
        accept(TOK_SEMI);
        return result;
    }
    if (token.kind == TOK_ID && seq(token.text, "for")) {
        lex_next();
        require('(');
        if (token.kind == TOK_ID) {
            ParserState mark = save_parser();
            scopy(name, token.text, MAX_STRING);
            lex_next();
            if (token.kind == TOK_IN) {
                lex_next();
                if (token.kind != TOK_ID) error_flag = 1;
                else {
                    char array_name[MAX_STRING];
                    AwkVar *av;
                    int scope, vi;
                    scopy(array_name, token.text, MAX_STRING);
                    lex_next();
                    require(')');
                    body_start = token.start;
                    loop_end = find_statement_end(body_start);
                    av = resolve_var(array_name, 1, &scope, &vi);
                    aid = av ? ensure_array(av) : -1;
                    loop = 0;
                    for (entry = 0; run && aid >= 0 &&
                         entry < arrays[aid].count && loop++ < MAX_ITEMS;
                         entry++) {
                        if (!arrays[aid].item[entry].used) continue;
                        {
                            Value v = string_value(arrays[aid].item[entry].key);
                            Value l = get_variable(name);
                            assign_ref(l, v);
                        }
                        set_parser(body_start);
                        parse_statement(1);
                        if (flow_state == FLOW_BREAK) {
                            flow_state = FLOW_NONE;
                            break;
                        }
                        if (flow_state == FLOW_CONTINUE) flow_state = FLOW_NONE;
                        else if (flow_state != FLOW_NONE) break;
                    }
                    set_parser(loop_end);
                    return result;
                }
            }
            restore_parser(mark);
        }
        if (token.kind != TOK_SEMI) parse_expression();
        require(TOK_SEMI);
        cond_pos = token.start;
        skip_to_delimiter(TOK_SEMI);
        require(TOK_SEMI);
        inc_pos = token.start;
        skip_to_delimiter(')');
        require(')');
        body_start = token.start;
        loop_end = find_statement_end(body_start);
        loop = 0;
        while (run && !error_flag && loop++ < 100000) {
            set_parser(cond_pos);
            if (token.kind != TOK_SEMI) result = parse_expression();
            else result = number_value(1);
            if (!truth(result)) break;
            set_parser(body_start);
            parse_statement(1);
            if (flow_state == FLOW_BREAK) {
                flow_state = FLOW_NONE;
                break;
            }
            if (flow_state == FLOW_CONTINUE) flow_state = FLOW_NONE;
            else if (flow_state != FLOW_NONE) break;
            set_parser(inc_pos);
            if (token.kind != ')') parse_expression();
        }
        if (loop >= 100000) error_flag = 1;
        set_parser(loop_end);
        return result;
    }
    if (token.kind == TOK_ID &&
        (seq(token.text, "print") || seq(token.text, "printf"))) {
        int formatted = seq(token.text, "printf");
        lex_next();
        print_statement(run, formatted);
        return result;
    }
    if (token.kind == TOK_ID && seq(token.text, "next")) {
        lex_next();
        if (run) flow_state = FLOW_NEXT;
        return result;
    }
    if (token.kind == TOK_ID && seq(token.text, "exit")) {
        lex_next();
        if (token.kind != TOK_SEMI && token.kind != '}' && token.kind)
            result = parse_expression();
        if (run) flow_state = FLOW_EXIT;
        return result;
    }
    if (token.kind == TOK_ID && seq(token.text, "break")) {
        lex_next();
        if (run) flow_state = FLOW_BREAK;
        return result;
    }
    if (token.kind == TOK_ID && seq(token.text, "continue")) {
        lex_next();
        if (run) flow_state = FLOW_CONTINUE;
        return result;
    }
    if (token.kind == TOK_ID && seq(token.text, "return")) {
        lex_next();
        if (token.kind != TOK_SEMI && token.kind != '}' && token.kind)
            result = parse_expression();
        if (run && frame_depth > 0) {
            frames[frame_depth - 1].result = result;
            flow_state = FLOW_RETURN;
        }
        return result;
    }
    if (token.kind == TOK_ID && seq(token.text, "delete")) {
        AwkVar *var;
        int scope, vi;
        lex_next();
        if (token.kind != TOK_ID) { error_flag = 1; return result; }
        scopy(name, token.text, MAX_STRING);
        lex_next();
        var = resolve_var(name, 1, &scope, &vi);
        if (accept('[')) {
            Value idx = parse_assignment();
            as_string(idx, key);
            while (accept(',')) {
                char part[MAX_KEY], joined[MAX_KEY];
                int n, k;
                idx = parse_assignment();
                as_string(idx, part);
                scopy(joined, key, MAX_KEY);
                n = slen(joined);
                if (n < MAX_KEY - 1) joined[n++] = '\034';
                joined[n] = 0;
                for (k = 0; part[k] && n < MAX_KEY - 1; k++)
                    joined[n++] = part[k];
                joined[n] = 0;
                scopy(key, joined, MAX_KEY);
            }
            require(']');
            if (run && var) {
                int array = ensure_array(var);
                int e = array_item(array, key, 0);
                if (e >= 0) arrays[array].item[e].used = 0;
            }
        } else if (run && var && var->array_id >= 0) {
            int e;
            for (e = 0; e < arrays[var->array_id].count; e++)
                arrays[var->array_id].item[e].used = 0;
            arrays[var->array_id].count = 0;
        }
        return result;
    }
    result = parse_expression();
    return result;
}
static Value parse_statement(int run) {
    int old_enabled = execute_enabled;
    Value result;
    execute_enabled = old_enabled && run;
    result = parse_statement_impl(execute_enabled);
    execute_enabled = old_enabled;
    return result;
}
static int load_program(const char *fileid) {
    FILE *f;
    char line[MAX_RECORD];
    int i, n;
    f = fopen(fileid, "r");
    if (!f) return 0;
    source_length = 0;
    while (fgets(line, sizeof line, f)) {
        n = slen(line);
        if (source_length + n + 1 >= SOURCE_SIZE) {
            fclose(f);
            return 0;
        }
        for (i = 0; i < n; i++) source[source_length++] = line[i];
        if (!n || line[n - 1] != '\n') source[source_length++] = '\n';
    }
    source[source_length] = 0;
    return fclose(f) == 0;
}
static int parse_function(void) {
    AwkFunction *fn;
    char name[MAX_STRING];
    int start, end;
    lex_next();
    if (token.kind != TOK_ID || function_count >= MAX_FUNCS) return 0;
    scopy(name, token.text, MAX_STRING);
    if (find_function(name) >= 0) return 0;
    fn = &functions[function_count++];
    fn->used = 1;
    scopy(fn->name, name, MAX_STRING);
    fn->param_count = 0;
    lex_next();
    if (!require('(')) return 0;
    if (token.kind != ')') {
        do {
            if (token.kind != TOK_ID || fn->param_count >= MAX_PARAMS)
                return 0;
            scopy(fn->param[fn->param_count++], token.text, MAX_STRING);
            lex_next();
        } while (accept(','));
    }
    if (!require(')') || token.kind != '{') return 0;
    start = token.start;
    end = find_statement_end(start);
    fn->body_start = start;
    fn->body_end = end;
    set_parser(end);
    skip_semis();
    return 1;
}
static int parse_rule_source(void) {
    AwkRule *r;
    int body_start, body_end, start, p, escaped, old_enabled;
    char c;
    if (rule_count >= MAX_RULES) return 0;
    r = &rules[rule_count];
    r->kind = RULE_ALL;
    r->pattern_start = -1;
    r->pattern[0] = 0;
    if (token.kind == TOK_ID && seq(token.text, "BEGIN")) {
        r->kind = RULE_BEGIN;
        lex_next();
        skip_semis();
    } else if (token.kind == TOK_ID && seq(token.text, "END")) {
        r->kind = RULE_END;
        lex_next();
        skip_semis();
    } else if (token.kind == '{') {
        r->kind = RULE_ALL;
    } else if (token.kind == '/') {
        r->kind = RULE_REGEX;
        start = token.start + 1;
        p = start;
        escaped = 0;
        while (p < source_length) {
            c = source[p++];
            if (!escaped && c == '/') break;
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
        }
        if (p > source_length || source[p - 1] != '/') return 0;
        token_text(&token, source + start, p - start - 1);
        scopy(r->pattern, token.text, MAX_STRING);
        set_parser(p);
        skip_semis();
    } else {
        r->kind = RULE_EXPR;
        r->pattern_start = token.start;
        old_enabled = execute_enabled;
        execute_enabled = 0;
        parse_expression();
        execute_enabled = old_enabled;
        skip_semis();
    }
    if (token.kind != '{') return 0;
    body_start = token.start;
    body_end = find_statement_end(body_start);
    r->body_start = body_start;
    r->body_end = body_end;
    rule_count++;
    set_parser(body_end);
    skip_semis();
    return 1;
}
static int parse_program(void) {
    set_parser(0);
    rule_count = 0;
    function_count = 0;
    while (token.kind) {
        skip_semis();
        if (!token.kind) break;
        if (token.kind == TOK_ID && seq(token.text, "function")) {
            if (!parse_function()) return 0;
        } else if (!parse_rule_source()) {
            return 0;
        }
    }
    return !error_flag;
}
static int rule_matches(const AwkRule *r) {
    Value v;
    ParserState saved;
    int yes;
    if (r->kind == RULE_ALL) return 1;
    if (r->kind == RULE_BEGIN || r->kind == RULE_END) return 0;
    if (r->kind == RULE_REGEX) return regex_match(r->pattern, record);
    saved = save_parser();
    set_parser(r->pattern_start);
    v = parse_expression();
    yes = truth(v);
    restore_parser(saved);
    return yes;
}
static int execute_rule(const AwkRule *r) {
    set_parser(r->body_start);
    parse_statement(1);
    return !error_flag;
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
int main(int argc, char **argv) {
    char script_id[32], data_id[32];
    FILE *data;
    int i, run_data;
    if (argc != 7) {
        puts("Usage: CMSAWK scriptFN scriptFT scriptFM dataFN dataFT dataFM");
        return 4;
    }
    make_file_id(script_id, argv[1], argv[2], argv[3]);
    make_file_id(data_id, argv[4], argv[5], argv[6]);
    if (!load_program(script_id)) {
        puts("CMSAWK: cannot read script or script too large");
        return 8;
    }
    if (!parse_program()) {
        puts("CMSAWK: invalid script or interpreter limit exceeded");
        return 4;
    }
    global_count = 0;
    array_count = 0;
    frame_depth = 0;
    flow_state = FLOW_NONE;
    nr = 0;
    nf = 0;
    error_flag = 0;
    run_data = 1;
    for (i = 0; i < rule_count; i++) {
        if (rules[i].kind == RULE_BEGIN) {
            flow_state = FLOW_NONE;
            if (!execute_rule(&rules[i])) break;
            if (flow_state == FLOW_EXIT) {
                run_data = 0;
                break;
            }
            if (flow_state != FLOW_NONE) {
                error_flag = 1;
                break;
            }
        }
    }
    data = 0;
    if (run_data && !error_flag) {
        data = fopen(data_id, "r");
        if (!data) {
            puts("CMSAWK: cannot open data file");
            return 8;
        }
        while (fgets(record, sizeof record, data)) {
            record_length = slen(record);
            while (record_length > 0 &&
                   (record[record_length - 1] == '\n' ||
                    record[record_length - 1] == '\r'))
                record[--record_length] = 0;
            if (record_length >= MAX_RECORD - 1) {
                error_flag = 1;
                break;
            }
            nr++;
            split_fields();
            flow_state = FLOW_NONE;
            for (i = 0; i < rule_count; i++) {
                if (rules[i].kind == RULE_ALL ||
                    rules[i].kind == RULE_REGEX ||
                    rules[i].kind == RULE_EXPR) {
                    if (rule_matches(&rules[i])) {
                        if (!execute_rule(&rules[i])) {
                            error_flag = 1;
                            break;
                        }
                    }
                    if (flow_state == FLOW_NEXT ||
                        flow_state == FLOW_EXIT) break;
                }
            }
            if (flow_state == FLOW_NEXT) flow_state = FLOW_NONE;
            else if (flow_state == FLOW_EXIT) break;
            if (error_flag) break;
        }
        fclose(data);
    }
    for (i = 0; i < rule_count; i++) {
        if (rules[i].kind == RULE_END) {
            flow_state = FLOW_NONE;
            if (!execute_rule(&rules[i])) error_flag = 1;
        }
    }
    if (error_flag) {
        puts("CMSAWK: runtime error or resource limit exceeded");
        return 8;
    }
    return 0;
}
