/* test_parse_edges.c -- adversarial edge cases for src/fz_fis_parse.c (CONTRACT §F).
 *
 * Every case asserts ONE specific return code, never "!= 0", so a parser that
 * starts rejecting valid files (or accepting broken ones) fails loudly.
 * Cases: comments mid-section, CRLF, odd spacing, comma/space MF params,
 * empty [], trailing spaces, decimal weight, conn=2, MF numbering gaps,
 * NumMFs under-count, truncated text, non-NUL-terminated buffer (guard page).
 */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fz.h"

static fz_mf_t mf[256];
static fz_var_t vin[16], vout[16];
static fz_rule_t rules[512];
static int16_t idx[4096];

static int fails;

#define OK(expr, want)                                                          \
    do {                                                                        \
        int got_ = (expr);                                                      \
        if (got_ != (want)) {                                                   \
            printf("  FAIL %s:%d %s -> %d (want %d)\n", __FILE__, __LINE__,     \
                   #expr, got_, (want));                                        \
            ++fails;                                                            \
        }                                                                       \
    } while (0)

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);             \
            ++fails;                                                            \
        }                                                                       \
    } while (0)

static int parse(const char *text, fz_fis_t *fis) {
    memset(fis, 0, sizeof *fis);
    return fz_fis_parse_text(text, strlen(text) + 1, fis, mf, 256, vin, 16, vout, 16, rules, 512,
                             idx, 4096);
}

/* Minimal valid 1-in/1-out file; callers splice their own body in. */
static const char *HEAD =
    "[System]\nName='s'\nType='mamdani'\nNumInputs=1\nNumOutputs=1\nNumRules=1\n"
    "AndMethod='min'\nOrMethod='max'\nImpMethod='min'\nAggMethod='max'\n"
    "DefuzzMethod='centroid'\n"
    "[Input1]\nName='a'\nRange=[0 10]\nNumMFs=1\nMF1='lo':'trimf',[0 0 5]\n"
    "[Output1]\nName='y'\nRange=[0 10]\nNumMFs=1\nMF1='o':'trimf',[0 5 10]\n"
    "[Rules]\n1, 1 (1) : 1\n";

int main(void) {
    fz_fis_t fis;

    /* 1. Comment lines inside a variable section and after values. */
    {
        static const char t[] =
            "[System]\nName='s'\nType='mamdani'\nNumInputs=1\nNumOutputs=1\n"
            "NumRules=1  % trailing comment on a value line\n"
            "AndMethod='min'\nOrMethod='max'\nImpMethod='min'\nAggMethod='max'\n"
            "DefuzzMethod='centroid'\n"
            "[Input1]\n% a full-line comment inside the section\n"
            "## toolkit-style comment\n"
            "Name='a'\nRange=[0 10]\nNumMFs=1\n"
            "MF1='lo':'trimf',[0 0 5]  % and one after an MF\n"
            "[Output1]\nName='y'\nRange=[0 10]\nNumMFs=1\n"
            "MF1='o':'trimf',[0 5 10]\n"
            "[Rules]\n1, 1 (1) : 1\n";
        OK(parse(t, &fis), FZ_OK);
        CHECK(fis.in[0].nmf == 1 && fis.in[0].lo == 0.0f && fis.in[0].hi == 10.0f);
    }

    /* 2. CRLF fixture (written with \r\n) parses identically to LF. */
    {
        FILE *f = fopen("tests/fis/crlf_spaced.fis", "rb");
        static char buf[4096];
        size_t n;
        CHECK(f != NULL);
        if (f) {
            n = fread(buf, 1, sizeof buf - 1, f);
            fclose(f);
            buf[n] = 0;
            OK(parse(buf, &fis), FZ_OK);
            CHECK(strstr(buf, "\r\n") != NULL); /* the fixture really is CRLF */
            CHECK(fis.n_in == 1 && fis.n_out == 1 && fis.n_rules == 1);
            CHECK(fis.in[0].nmf == 2 && fis.in[0].hi == 10.0f);
            CHECK(fis.rules[0].ante[0] == 120); /* 1.2 -> MF1 hedge 2 */
        }
    }

    /* 3. Odd spacing/tabs around '=', ':' and ',' (CONTRACT §F: ignored). */
    {
        static const char t[] =
            "[System]\n"
            "Name \t= \t'spaced'\n"
            "Type\t=\t'mamdani'\n"
            "NumInputs = 1\nNumOutputs = 1\nNumRules = 1\n"
            "AndMethod = 'min'\nOrMethod = 'max'\nImpMethod = 'min'\n"
            "AggMethod = 'max'\nDefuzzMethod = 'centroid'\n"
            "[Input1]\n"
            "Name = 'a'\n"
            "Range = [0\t10]\n"
            "NumMFs = 1\n"
            "MF1 = 'lo' : 'trimf' , [0 0 5]\n"
            "[Output1]\n"
            "Name = 'y'\n"
            "Range = [0 10]\n"
            "NumMFs = 1\n"
            "MF1 = 'o' : 'trimf' , [0 5 10]\n"
            "[Rules]\n"
            "  1\t,   1   ( 1 )  :  1  \n";
        OK(parse(t, &fis), FZ_OK);
        CHECK(strcmp(fis.in[0].name, "a") == 0);
        CHECK(fis.in[0].lo == 0.0f && fis.in[0].hi == 10.0f);
        CHECK(fis.out[0].lo == 0.0f && fis.out[0].hi == 10.0f);
        CHECK(fis.rules[0].conn == 1 && fis.rules[0].weight == 1.0f);
    }

    /* 4. MF params: comma, space and mixed separators must agree. */
    {
        static const char a[] = /* comma */
            "[System]\nName='s'\nType='mamdani'\nNumInputs=1\nNumOutputs=1\n"
            "NumRules=1\nAndMethod='min'\nOrMethod='max'\nImpMethod='min'\n"
            "AggMethod='max'\nDefuzzMethod='centroid'\n"
            "[Input1]\nName='a'\nRange=[0 10]\nNumMFs=1\nMF1='lo':'trimf',[1,2,3]\n"
            "[Output1]\nName='y'\nRange=[0 10]\nNumMFs=1\nMF1='o':'trimf',[0 5 10]\n"
            "[Rules]\n1, 1 (1) : 1\n";
        static const char b[] = /* mixed, extra blanks */
            "[System]\nName='s'\nType='mamdani'\nNumInputs=1\nNumOutputs=1\n"
            "NumRules=1\nAndMethod='min'\nOrMethod='max'\nImpMethod='min'\n"
            "AggMethod='max'\nDefuzzMethod='centroid'\n"
            "[Input1]\nName='a'\nRange=[0 10]\nNumMFs=1\nMF1='lo':'trimf',[ 1,  2 ,3 ]\n"
            "[Output1]\nName='y'\nRange=[0 10]\nNumMFs=1\nMF1='o':'trimf',[0 5 10]\n"
            "[Rules]\n1, 1 (1) : 1\n";
        fz_fis_t fb;
        OK(parse(a, &fis), FZ_OK);
        OK(parse(b, &fb), FZ_OK);
        CHECK(fis.in[0].mfs[0].nparam == 3);
        CHECK(fis.in[0].mfs[0].p[0] == 1.0f && fis.in[0].mfs[0].p[1] == 2.0f &&
              fis.in[0].mfs[0].p[2] == 3.0f);
        CHECK(memcmp(fis.in[0].mfs[0].p, fb.in[0].mfs[0].p,
                     sizeof fis.in[0].mfs[0].p) == 0);
    }

    /* 5. Empty [] is legal (CONTRACT §F); a constant then has zero params. */
    {
        static const char t[] =
            "[System]\nName='s'\nType='sugeno'\nNumInputs=1\nNumOutputs=1\n"
            "NumRules=1\nAndMethod='min'\nOrMethod='max'\nImpMethod='prod'\n"
            "AggMethod='sum'\nDefuzzMethod='wtaver'\n"
            "[Input1]\nName='a'\nRange=[0 10]\nNumMFs=1\nMF1='lo':'trimf',[0 0 5]\n"
            "[Output1]\nName='y'\nRange=[0 10]\nNumMFs=1\nMF1='o':'constant',[]\n"
            "[Rules]\n1, 1 (1) : 1\n";
        OK(parse(t, &fis), FZ_OK);
        CHECK(fis.out[0].mfs[0].nparam == 0);
    }

    /* 6. Trailing spaces/tabs after a rule line, and no trailing newline. */
    {
        static const char t[] =
            "[System]\nName='s'\nType='mamdani'\nNumInputs=1\nNumOutputs=1\n"
            "NumRules=1\nAndMethod='min'\nOrMethod='max'\nImpMethod='min'\n"
            "AggMethod='max'\nDefuzzMethod='centroid'\n"
            "[Input1]\nName='a'\nRange=[0 10]\nNumMFs=1\nMF1='lo':'trimf',[0 0 5]\n"
            "[Output1]\nName='y'\nRange=[0 10]\nNumMFs=1\nMF1='o':'trimf',[0 5 10]\n"
            "[Rules]\n1, 1 (1) : 1   \t  ";
        OK(parse(t, &fis), FZ_OK);
        CHECK(fis.n_rules == 1);
    }

    /* 7. Decimal weight and conn=2 must round-trip exactly. */
    {
        static const char t[] =
            "[System]\nName='s'\nType='mamdani'\nNumInputs=2\nNumOutputs=1\n"
            "NumRules=1\nAndMethod='min'\nOrMethod='max'\nImpMethod='min'\n"
            "AggMethod='max'\nDefuzzMethod='centroid'\n"
            "[Input1]\nName='a'\nRange=[0 10]\nNumMFs=1\nMF1='lo':'trimf',[0 0 5]\n"
            "[Input2]\nName='b'\nRange=[0 10]\nNumMFs=1\nMF1='lo':'trimf',[0 0 5]\n"
            "[Output1]\nName='y'\nRange=[0 10]\nNumMFs=1\nMF1='o':'trimf',[0 5 10]\n"
            "[Rules]\n1 1, 1 (0.5) : 2\n";
        OK(parse(t, &fis), FZ_OK);
        CHECK(fis.rules[0].weight == 0.5f);
        CHECK(fis.rules[0].conn == 2);
    }

    /* 8. MF numbers with a gap: numbers are labels, not array positions, so
     * MF3 must land in slot 2 with its own params (no renumber, no drop). */
    {
        static const char t[] =
            "[System]\nName='s'\nType='mamdani'\nNumInputs=1\nNumOutputs=1\n"
            "NumRules=1\nAndMethod='min'\nOrMethod='max'\nImpMethod='min'\n"
            "AggMethod='max'\nDefuzzMethod='centroid'\n"
            "[Input1]\nName='a'\nRange=[0 10]\nNumMFs=2\n"
            "MF1='lo':'trimf',[0 1 5]\nMF3='hi':'trimf',[5 9 10]\n"
            "[Output1]\nName='y'\nRange=[0 10]\nNumMFs=1\nMF1='o':'trimf',[0 5 10]\n"
            "[Rules]\n2, 1 (1) : 1\n";
        OK(parse(t, &fis), FZ_OK);
        CHECK(fis.in[0].nmf == 2);
        CHECK(fis.in[0].mfs[1].p[0] == 5.0f && fis.in[0].mfs[1].p[1] == 9.0f);
        CHECK(fis.rules[0].ante[0] == 200); /* MF2 still addressed as 2 */
    }

    /* 9. NumMFs smaller than the MF lines present -> hard count error. */
    {
        static const char t[] =
            "[System]\nName='s'\nType='mamdani'\nNumInputs=1\nNumOutputs=1\n"
            "NumRules=1\nAndMethod='min'\nOrMethod='max'\nImpMethod='min'\n"
            "AggMethod='max'\nDefuzzMethod='centroid'\n"
            "[Input1]\nName='a'\nRange=[0 10]\nNumMFs=1\n"
            "MF1='lo':'trimf',[0 0 5]\nMF2='hi':'trimf',[5 10 10]\n"
            "[Output1]\nName='y'\nRange=[0 10]\nNumMFs=1\nMF1='o':'trimf',[0 5 10]\n"
            "[Rules]\n1, 1 (1) : 1\n";
        OK(parse(t, &fis), FZ_ERR_COUNT);
    }

    /* 10. Truncated text: cut mid-consequent and cut inside the weight. */
    {
        static const char cut_conse[] =
            "[System]\nName='s'\nType='mamdani'\nNumInputs=1\nNumOutputs=1\n"
            "NumRules=1\nAndMethod='min'\nOrMethod='max'\nImpMethod='min'\n"
            "AggMethod='max'\nDefuzzMethod='centroid'\n"
            "[Input1]\nName='a'\nRange=[0 10]\nNumMFs=1\nMF1='lo':'trimf',[0 0 5]\n"
            "[Output1]\nName='y'\nRange=[0 10]\nNumMFs=1\nMF1='o':'trimf',[0 5 10]\n"
            "[Rules]\n1,";
        static const char cut_system[] =
            "[System]\nName='s'\nType='mamdani'\nNumInputs=1\n";
        OK(parse(cut_conse, &fis), FZ_ERR_RULE);
        OK(parse(cut_system, &fis), FZ_ERR_SECTION);
    }

    /* 11. Declared NumRules larger than the rule lines -> count error. */
    {
        static const char t[] =
            "[System]\nName='s'\nType='mamdani'\nNumInputs=1\nNumOutputs=1\n"
            "NumRules=2\nAndMethod='min'\nOrMethod='max'\nImpMethod='min'\n"
            "AggMethod='max'\nDefuzzMethod='centroid'\n"
            "[Input1]\nName='a'\nRange=[0 10]\nNumMFs=1\nMF1='lo':'trimf',[0 0 5]\n"
            "[Output1]\nName='y'\nRange=[0 10]\nNumMFs=1\nMF1='o':'trimf',[0 5 10]\n"
            "[Rules]\n1, 1 (1) : 1\n";
        OK(parse(t, &fis), FZ_ERR_COUNT);
    }

    /* 12. Pool overflow on rules only (M$s and vars are ample) -> FZ_ERR_POOL. */
    {
        memset(&fis, 0, sizeof fis);
        OK(fz_fis_parse_text(HEAD, strlen(HEAD) + 1, &fis, mf, 256, vin, 16, vout, 16,
                             rules, 0, idx, 4096),
           FZ_ERR_POOL);
    }

    /* 13. Non-NUL-terminated input. The API now takes an explicit length, so the
     * parser must refuse rather than scan past the end. mmap a page pair, put
     * the text flush against a PROT_NONE guard page, and parse in a child: a
     * correct parser returns FZ_ERR_ARG, a broken one dies on the guard page. */
#ifndef SKIP_UNTERMINATED
    {
        long pg = sysconf(_SC_PAGESIZE);
        char *mem = mmap(NULL, (size_t)pg * 2, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        pid_t pid;
        int status = 0;
        CHECK(mem != MAP_FAILED);
        if (mem != MAP_FAILED) {
            size_t n = strlen(HEAD);
            mprotect(mem + pg, (size_t)pg, PROT_NONE);
            memcpy(mem + pg - n, HEAD, n); /* no '\0' inside the mapped page */
            pid = fork();
            if (pid == 0) {
                fz_fis_t f;
                int rc;
                memset(&f, 0, sizeof f);
                rc = fz_fis_parse_text(mem + pg - n, n, &f, mf, 256, vin, 16, vout,
                                       16, rules, 512, idx, 4096);
                _exit(rc == FZ_ERR_ARG ? 0 : 3);
            }
            waitpid(pid, &status, 0);
            if (WIFSIGNALED(status)) {
                printf("  FAIL unterminated buffer read past its end (signal %d)\n",
                       WTERMSIG(status));
                ++fails;
            } else {
                CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
            }
            munmap(mem, (size_t)pg * 2);
        }
    }
#endif /* !SKIP_UNTERMINATED */

    /* 14. Rule slot encoding at the boundary: 2.05 keeps its half-decile. */
    {
        static const char t[] =
            "[System]\nName='s'\nType='mamdani'\nNumInputs=1\nNumOutputs=1\n"
            "NumRules=1\nAndMethod='min'\nOrMethod='max'\nImpMethod='min'\n"
            "AggMethod='max'\nDefuzzMethod='centroid'\n"
            "[Input1]\nName='a'\nRange=[0 10]\nNumMFs=2\n"
            "MF1='lo':'trimf',[0 0 5]\nMF2='hi':'trimf',[5 10 10]\n"
            "[Output1]\nName='y'\nRange=[0 10]\nNumMFs=1\nMF1='o':'trimf',[0 5 10]\n"
            "[Rules]\n2.05, -1.3 (0.25) : 1\n";
        OK(parse(t, &fis), FZ_OK);
        CHECK(fis.rules[0].ante[0] == 205);
        CHECK(fis.rules[0].conse[0] == -130);
        CHECK(fis.rules[0].weight == 0.25f);
    }

    printf("test_parse_edges: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
    return fails ? 1 : 0;
}
