#!/bin/sh
# Every C code block of the documentation must compile against the current
# headers, so the docs cannot drift from the API.
#
#   scripts/check_docs.sh [FILE.md...]
#
# Default files: README.md, CONTRIBUTING.md, docs/*.md (except the design
# record docs/3.0-plan.md) and the two skills in .claude/skills. $CC (default
# cc) compiles each ```c block with -std=c11 -Wall -Wextra -pedantic -Werror
# (compile only, nothing is linked or run). How a block is compiled:
#
# - A block need not be a complete program. Function definitions,
#   declarations starting with static, extern, typedef, struct, union or enum,
#   and preprocessor lines stay at file scope, and so does a plain
#   declaration (`T* name;`, `T* name = NULL;`) that one of those functions
#   uses; every other statement goes, in order, into a generated function.
#   Preprocessor lines before the first other line come before the headers,
#   so a block that #defines configuration sees its effect.
# - Every feature with declarations (ISR, memory and performance statistics,
#   trace) is on unless the block sets it. ss_lib.h and the usual standard
#   headers are included.
# - Placeholder names that blocks use without defining them (bus, data,
#   my_handler, ADC, ...) are declared by the list below; a block that
#   defines one of them as static, as a type or as a macro keeps its own
#   definition. Add a name there when a new block needs one.
# - A comment line `// WRONG`, `// CORRECT`, `/* 2.x */` or `/* 3.0 */`
#   between statements starts a new part that is compiled on its own, so a
#   block can show the wrong and the right version of one function. Parts
#   marked 2.x are not compiled: they show code for the old API.
# - A typedef of a library type (ss_*_t with a body) copied from ss_lib.h is
#   not compiled (it would be a redefinition) but must match the header,
#   apart from comments and white space.
# - An HTML comment on the line before the fence changes how the block is
#   checked: `<!-- check-docs: internal -->` compiles it against
#   src/ss_lib.c instead of the header (library code), and
#   `<!-- check-docs: skip REASON -->` skips it (for example a block that
#   needs a vendor header). Keep skips rare.
#
# Compiler messages point at the Markdown file and line.
set -eu

cd "$(dirname "$0")/.."

CC=${CC:-cc}
out=${BUILD_DIR:-build}/check-docs
CFLAGS="-std=c11 -Wall -Wextra -pedantic -Werror -I. -Iinclude"

if [ $# -eq 0 ]; then
    set -- README.md CONTRIBUTING.md
    for f in docs/*.md; do
        [ "$f" = docs/3.0-plan.md ] || set -- "$@" "$f"
    done
    set -- "$@" .claude/skills/ss-lib/SKILL.md .claude/skills/ss-lib-review/SKILL.md
fi

rm -rf "$out"
mkdir -p "$out"

# name|declaration. Slots are prototypes, so a block may also define them.
cat > "$out/prelude.txt" <<'EOF'
bus|extern ss_bus_t* bus;
bus_a|extern ss_bus_t* bus_a;
bus_b|extern ss_bus_t* bus_b;
data|extern const ss_data_t* data;
data1|extern const ss_data_t* data1;
data2|extern const ss_data_t* data2;
d|extern ss_data_t* d;
user_data|extern void* user_data;
ud|extern void* ud;
handle|extern ss_connection_t handle;
buffer|extern char buffer[64];
x|extern int x;
v|extern int v;
tick_count|extern volatile int tick_count;
fn|void fn(const ss_data_t* data, void* user_data);
slot|void slot(const ss_data_t* data, void* user_data);
my_slot|void my_slot(const ss_data_t* data, void* user_data);
my_handler|void my_handler(const ss_data_t* data, void* user_data);
on_data|void on_data(const ss_data_t* data, void* user_data);
on_click|void on_click(const ss_data_t* data, void* user_data);
on_critical|void on_critical(const ss_data_t* data, void* user_data);
on_log|void on_log(const ss_data_t* data, void* user_data);
on_volume|void on_volume(const ss_data_t* data, void* user_data);
on_adc_ready|void on_adc_ready(const ss_data_t* data, void* user_data);
read_sensor|void read_sensor(const ss_data_t* data, void* user_data);
critical_handler|void critical_handler(const ss_data_t* data, void* user_data);
normal_handler|void normal_handler(const ss_data_t* data, void* user_data);
log_handler|void log_handler(const ss_data_t* data, void* user_data);
logger|void logger(const ss_data_t* data, void* user_data);
adc_read|int adc_read(void);
get_status|int get_status(void);
log_append|void log_append(char* line);
log_list_append|void log_list_append(const char* line);
Blob|typedef struct { unsigned char* bytes; size_t n; } Blob;
PointList|typedef struct { void* points; size_t count; } PointList;
pl|extern PointList pl;
point|extern struct check_docs_point { int x, y; } point;
app_queue_t|typedef struct app_queue app_queue_t;
to_a|extern app_queue_t to_a;
app_queue_push|void app_queue_push(app_queue_t* q, int value);
app_queue_pop|int app_queue_pop(app_queue_t* q, int* value);
producer|void* producer(void* arg);
consumer|void* consumer(void* arg);
t1|extern pthread_t t1;
t2|extern pthread_t t2;
ADC|extern struct check_docs_adc { volatile unsigned int DR, DATA; }* ADC;
ADC_IRQn|enum { ADC_IRQn = 18 };
NVIC_EnableIRQ|void NVIC_EnableIRQ(int irq);
enable_adc_interrupt|void enable_adc_interrupt(void);
EOF

# Shared by both awk programs: strip comments and string contents from a
# line; incomment carries a block comment over to the next line
lib='
function strip(s,    o, i, n, c, c2, q) {
    o = ""; n = length(s); i = 1
    while (i <= n) {
        c = substr(s, i, 1); c2 = substr(s, i, 2)
        if (incomment) {
            if (c2 == "*/") { incomment = 0; i += 2; o = o " " } else i++
            continue
        }
        if (c2 == "/*") { incomment = 1; i += 2; continue }
        if (c2 == "//") break
        if (c == "\"" || c == "'\''") {
            q = c; o = o q; i++
            while (i <= n) {
                c = substr(s, i, 1)
                if (c == "\\") { i += 2; continue }
                i++
                if (c == q) break
            }
            o = o q
            continue
        }
        o = o c; i++
    }
    return o
}
function trim(s) { sub(/^[ \t\n]+/, "", s); sub(/[ \t\n]+$/, "", s); return s }
# For comparing declarations: white space collapsed and dropped around
# punctuation (comments are already stripped)
function norm(s,    n, p, i) {
    gsub(/[ \t\n]+/, " ", s)
    n = split("[ ] { } ( ) ; , * =", p, " ")
    for (i = 1; i <= n; i++) {
        gsub(" [" p[i] "]", p[i], s)
        gsub("[" p[i] "] ", p[i], s)
    }
    return trim(s)
}
'

# The header, normalized like the typedef copies are
awk "$lib"'
{ t = t " " strip($0) }
END { print norm(t) }' include/ss_lib.h > "$out/header.norm"

for md in "$@"; do
    awk -v md="$md" -v out="$out" "$lib"'
BEGIN {
    while ((getline line < (out "/prelude.txt")) > 0) {
        np++
        pname[np] = substr(line, 1, index(line, "|") - 1)
        pdecl[np] = substr(line, index(line, "|") + 1)
    }
    close(out "/prelude.txt")
    if ((getline nparts < (out "/count")) <= 0) nparts = 0
    close(out "/count")
}
# One part: items of kind lead, file, body or type
function flush_part(    f, i, k, code, defined) {
    if (nitems == 0) return
    if (partmark == "2.x") { nitems = 0; return }
    nparts++
    f = sprintf("%s/part%04d.c", out, nparts)
    for (i = 1; i <= nitems; i++) {
        if (declname[i] == "") continue
        for (k = 1; k <= nitems; k++)
            if (kind[k] == "file" \
                && icode[k] ~ ("(^|[^A-Za-z0-9_])" declname[i] "([^A-Za-z0-9_]|$)")) kind[i] = "file"
    }
    print "#ifndef _POSIX_C_SOURCE" > f
    print "#define _POSIX_C_SOURCE 200809L" > f
    print "#endif" > f
    # Configuration that the block defines, then the feature defaults, then
    # the includes of the block
    for (i = 1; i <= nitems; i++)
        if (kind[i] == "lead" && icode[i] ~ /^[ \t\n]*#[ \t]*(define|undef)/) print text[i] > f
    print "#ifndef SS_ENABLE_ISR_SAFE\n#define SS_ENABLE_ISR_SAFE 1\n#endif" > f
    print "#ifndef SS_ENABLE_MEMORY_STATS\n#define SS_ENABLE_MEMORY_STATS 1\n#endif" > f
    print "#ifndef SS_ENABLE_PERFORMANCE_STATS\n#define SS_ENABLE_PERFORMANCE_STATS 1\n#endif" > f
    print "#ifndef SS_ENABLE_DEBUG_TRACE\n#define SS_ENABLE_DEBUG_TRACE 1\n#endif" > f
    print "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>" > f
    print "#include <stdint.h>\n#include <pthread.h>" > f
    for (i = 1; i <= nitems; i++)
        if (kind[i] == "lead" && icode[i] !~ /^[ \t\n]*#[ \t]*(define|undef)/) print text[i] > f
    print (internal ? "#include \"src/ss_lib.c\"" : "#include \"ss_lib.h\"") > f
    code = ""
    for (i = 1; i <= nitems; i++) code = code "\n" icode[i]
    for (k = 1; k <= np; k++) {
        defined = 0
        if (code ~ ("static[^;{]*[^A-Za-z0-9_]" pname[k] "[ \t]*[[;=(]")) defined = 1
        if (code ~ ("}[ \t]*" pname[k] "[ \t]*;")) defined = 1
        if (code ~ ("typedef[^;{]*[^A-Za-z0-9_]" pname[k] "[ \t]*;")) defined = 1
        if (code ~ ("#[ \t]*define[ \t]+" pname[k] "([^A-Za-z0-9_]|$)")) defined = 1
        if (!defined) print pdecl[k] > f
    }
    for (i = 1; i <= nitems; i++)
        if (kind[i] == "file") print text[i] > f
    for (i = 1; i <= nitems; i++)
        if (kind[i] == "type") print norm(icode[i]) > (f ".types")
    nbody = 0
    for (i = 1; i <= nitems; i++) if (kind[i] == "body") nbody++
    if (nbody) {
        printf "int check_docs_part%04d(void);\nint check_docs_part%04d(void) {\n", \
            nparts, nparts > f
        for (i = 1; i <= nitems; i++)
            if (kind[i] == "body") print text[i] > f
        print "return 0;\n}" > f
    }
    close(f)
    printf "%s\t%s:%d\n", f, md, startline >> (out "/list")
    close(out "/list")
    nitems = 0
}

function start_item(t, lineno) {
    nitems++
    if (t ~ /^#/) kind[nitems] = (seen_code ? "file" : "lead")
    else if (t ~ /^(typedef|struct|union|enum|static|extern)([ \t*{]|$)/) kind[nitems] = "file"
    else if (t ~ /^(if|else|for|while|do|switch|return|case|default|goto|break|continue)([ \t(;{]|$)/ \
             || t ~ /^[{]/) kind[nitems] = "body"
    else kind[nitems] = "maybe"
    if (t !~ /^#/) seen_code = 1
    ispp = (t ~ /^#/)
    text[nitems] = sprintf("#line %d \"%s\"", lineno, md)
    icode[nitems] = ""
}

# A function definition: "...) {" with balanced parentheses and no "="
function end_item(    c, b, head, po, pc) {
    c = icode[nitems]
    if (kind[nitems] == "maybe") {
        b = index(c, "{")
        head = substr(c, 1, b)
        po = head; pc = head
        kind[nitems] = (b && head ~ /\)[ \t\n]*[{]$/ && head !~ /=/ \
                        && gsub(/[(]/, "", po) == gsub(/[)]/, "", pc)) ? "file" : "body"
    }
    if (kind[nitems] == "file" && c ~ /^[ \t\n]*typedef[^;]*[{]/ \
        && c ~ /}[ \t\n]*ss_[a-z_]*_t[ \t\n]*;[ \t\n]*$/) kind[nitems] = "type"
    # "T* name;" or "T* name = NULL;": a global if a function of the part uses it
    declname[nitems] = ""
    if (kind[nitems] == "body" \
        && c ~ /^[ \t\n]*[A-Za-z_][A-Za-z0-9_]*[ \t\n*]+[A-Za-z_][A-Za-z0-9_]*[ \t\n]*(=[ \t\n]*(NULL|0)[ \t\n]*)?;[ \t\n]*$/ \
        && c !~ /^[ \t\n]*(return|goto|case)[^A-Za-z0-9_]/) {
        sub(/[ \t\n]*(=[^;]*)?;[ \t\n]*$/, "", c)
        match(c, /[A-Za-z_][A-Za-z0-9_]*$/)
        declname[nitems] = substr(c, RSTART, RLENGTH)
    }
    itemopen = 0
}

function process_block(    i, line, code, t, opens, closes, tmp) {
    nitems = 0; seen_code = 0; partmark = ""; startline = bstart
    incomment = 0; itemopen = 0; depth = 0
    internal = (bmode ~ /internal/)
    if (bmode ~ /skip/) {
        printf "%s:%d\n", md, bstart >> (out "/skipped")
        close(out "/skipped")
        return
    }
    printf "%s:%d\n", md, bstart >> (out "/blocks")
    close(out "/blocks")
    for (i = 1; i <= nb; i++) {
        line = bl[i]
        wascomment = incomment
        code = strip(line)
        t = trim(code)
        if (!itemopen && depth == 0 && !wascomment \
            && line ~ /^[ \t]*(\/\/|\/\*)[ \t]*(WRONG|CORRECT|2\.x|3\.0)([^A-Za-z0-9.]|$)/) {
            flush_part()
            seen_code = 0
            partmark = (line ~ /2\.x/ && line !~ /WRONG|CORRECT/) ? "2.x" : "other"
            startline = bln[i]
            continue
        }
        if (!itemopen) {
            if (t == "") continue
            start_item(t, bln[i])
            itemopen = 1
        }
        text[nitems] = text[nitems] "\n" line
        icode[nitems] = icode[nitems] "\n" code
        tmp = code; opens = gsub(/[{(]/, "", tmp)
        tmp = code; closes = gsub(/[})]/, "", tmp)
        depth += opens - closes
        if (ispp) {
            if (t !~ /\\$/) { end_item(); depth = 0 }
        } else if (depth == 0 && !incomment && t ~ /[;}]$/) end_item()
    }
    if (itemopen) end_item()
    flush_part()
}

!inblock {
    if ($0 ~ /^[ \t]*<!--[ \t]*check-docs:/) { mode = $0; next }
    if ($0 ~ /^[ \t]*```c[ \t]*$/) {
        inblock = 1; nb = 0; bstart = NR + 1; bmode = mode; mode = ""
        indent = index($0, "`") - 1
        next
    }
    if ($0 !~ /^[ \t]*$/) mode = ""
    next
}
/^[ \t]*```[ \t]*$/ {
    process_block()
    inblock = 0
    next
}
{
    line = $0
    for (k = 0; k < indent && substr(line, 1, 1) == " "; k++) line = substr(line, 2)
    bl[++nb] = line; bln[nb] = NR
}
END { print nparts > (out "/count") }
' "$md"
done

fail=0
total=0
if [ -f "$out/list" ]; then
    while IFS="	" read -r file where; do
        total=$((total + 1))
        if ! "$CC" $CFLAGS -c "$file" -o "$file.o" 2> "$file.err"; then
            echo "check-docs: $where: does not compile ($file):"
            cat "$file.err"
            fail=$((fail + 1))
        fi
        if [ -f "$file.types" ]; then
            while IFS= read -r item; do
                if ! grep -F -q -- "$item" "$out/header.norm"; then
                    echo "check-docs: $where: type differs from include/ss_lib.h: $item"
                    fail=$((fail + 1))
                fi
            done < "$file.types"
        fi
    done < "$out/list"
fi
count() { if [ -f "$1" ]; then wc -l < "$1" | tr -d ' '; else echo 0; fi; }
blocks=$(count "$out/blocks")
skipped=$(count "$out/skipped")

if [ $fail -ne 0 ]; then
    echo "check-docs: $fail of $total parts fail"
    exit 1
fi
echo "Documentation: $blocks C code blocks ($total parts) compile, $skipped skipped"
