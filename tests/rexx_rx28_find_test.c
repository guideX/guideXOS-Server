#include "../rexx_builtin.h"
#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX28_PROPERTY_CASES 12000u
#define RX28_BUFFER 1024u
#define RX28_MAX_WORDS 32u
#define RX28_MAX_STRING ((size_t)16777216u)
#define RX28_PROPERTY_SEED UINT32_C(0x52583238)

static size_t assertions;
static size_t failures;
static size_t property_cases;
static size_t interpreter_allocation_failpoints;

typedef struct Rx28Word {
    size_t start;
    size_t length;
} Rx28Word;

typedef struct Rx28Capture {
    unsigned char bytes[1024];
    size_t length;
} Rx28Capture;

static void check(int condition, const char *description)
{
    ++assertions;
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL: %s\n", description);
    }
}

static int result_is_number(const RexxValue *result, size_t expected)
{
    char expected_text[3u * sizeof(size_t) + 1u];
    int written = snprintf(expected_text, sizeof(expected_text), "%zu",
                           expected);
    return written > 0 && (size_t)written < sizeof(expected_text) &&
           result != NULL && result->type == REXX_VALUE_STRING &&
           result->as.string.length == (size_t)written &&
           memcmp(result->as.string.bytes, expected_text,
                  (size_t)written) == 0;
}

static void clear_values(RexxValue *values, size_t count)
{
    size_t index;
    for (index = 0u; index < count; ++index) rexx_value_clear(&values[index]);
}

static int is_word_separator(unsigned char byte)
{
    return byte == (unsigned char)' ' ||
           (byte >= (unsigned char)'\t' && byte <= (unsigned char)'\r');
}

/* Independent bytewise reference: it scans spans and compares whole words. */
static int oracle_next_word(const unsigned char *bytes,
                            size_t length,
                            size_t *cursor,
                            Rx28Word *word)
{
    size_t position = *cursor;
    while (position < length && is_word_separator(bytes[position])) ++position;
    if (position == length) {
        *cursor = position;
        return 0;
    }
    word->start = position;
    while (position < length && !is_word_separator(bytes[position])) ++position;
    word->length = position - word->start;
    *cursor = position;
    return 1;
}

static size_t oracle_find(const unsigned char *string,
                          size_t string_length,
                          const unsigned char *phrase,
                          size_t phrase_length)
{
    Rx28Word phrase_first;
    size_t phrase_cursor = 0u;
    size_t string_cursor = 0u;
    size_t word_number = 0u;

    if (!oracle_next_word(phrase, phrase_length, &phrase_cursor,
                          &phrase_first))
        return 0u;
    for (;;) {
        Rx28Word candidate;
        size_t search_cursor;
        size_t wanted_cursor;
        Rx28Word wanted = phrase_first;
        Rx28Word actual;

        if (!oracle_next_word(string, string_length, &string_cursor,
                              &candidate))
            return 0u;
        ++word_number;
        search_cursor = string_cursor;
        wanted_cursor = phrase_cursor;
        actual = candidate;
        for (;;) {
            if (wanted.length != actual.length ||
                (wanted.length != 0u &&
                 memcmp(phrase + wanted.start, string + actual.start,
                        wanted.length) != 0)) {
                break;
            }
            if (!oracle_next_word(phrase, phrase_length, &wanted_cursor,
                                  &wanted))
                return word_number;
            if (!oracle_next_word(string, string_length, &search_cursor,
                                  &actual)) {
                break;
            }
        }
    }
}

static int check_find_case(const unsigned char *string,
                           size_t string_length,
                           const unsigned char *phrase,
                           size_t phrase_length,
                           size_t expected,
                           const char *description)
{
    RexxValue arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
    RexxValue wordpos_arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    int passed = 1;

    status = rexx_value_set_string(&arguments[0], (const char *)string,
                                   string_length);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_value_set_string(&arguments[1], (const char *)phrase,
                                       phrase_length);
    if (status != REXX_VM_STATUS_OK) {
        check(0, "FIND test arguments allocate");
        clear_values(arguments, 2u);
        return 0;
    }

    status = rexx_builtin_dispatch("FIND", 4u, arguments, 2u, &result);
    if (status != REXX_VM_STATUS_OK || !result_is_number(&result, expected)) {
        fprintf(stderr,
                "  %s expected=%zu status=%s result-type=%d\n",
                description, expected, rexx_vm_status_name(status),
                (int)result.type);
        passed = 0;
    }
    check(passed, description);
    check(arguments[0].type == REXX_VALUE_STRING &&
              arguments[0].as.string.length == string_length &&
              (string_length == 0u ||
               memcmp(arguments[0].as.string.bytes, string,
                      string_length) == 0) &&
              arguments[1].type == REXX_VALUE_STRING &&
              arguments[1].as.string.length == phrase_length &&
              (phrase_length == 0u ||
               memcmp(arguments[1].as.string.bytes, phrase,
                      phrase_length) == 0),
          "FIND preserves both input strings byte for byte");

    wordpos_arguments[0] = arguments[1];
    wordpos_arguments[1] = arguments[0];
    status = rexx_builtin_dispatch("WORDPOS", 7u, wordpos_arguments, 2u,
                                   &result);
    passed = status == REXX_VM_STATUS_OK &&
             result_is_number(&result, expected);
    check(passed, "FIND(string,phrase) equals WORDPOS(phrase,string)");

    rexx_value_clear(&result);
    clear_values(arguments, 2u);
    return passed;
}

static void test_ibm_vectors_and_edges(void)
{
    struct Case {
        const char *string;
        const char *phrase;
        size_t expected;
        const char *description;
    } cases[] = {
        { "now is the time", "is the time", 2u, "IBM multiword FIND vector" },
        { "now is  the time", "is    the", 2u, "IBM repeated-blank normalization" },
        { "now is the time", "is time", 0u, "FIND requires consecutive words" },
        { "now is the time", "the", 3u, "FIND returns the first word number" },
        { "a b c", "a", 1u, "single word at the beginning" },
        { "a b c", "b", 2u, "single word in the middle" },
        { "a b c", "c", 3u, "single word at the end" },
        { "a b c", "d", 0u, "absent single word" },
        { "a b a b", "a b", 1u, "first repeated multiword phrase" },
        { "a b a b", "b a", 2u, "overlapping phrase begins at word two" },
        { "ABC DEF", "abc", 0u, "case-sensitive mismatch" },
        { "ABC DEF", "ABC", 1u, "case-sensitive exact match" },
        { "abc def", "bc", 0u, "partial word does not match" },
        { "", "", 0u, "two empty strings contain no words" },
        { "abc", "", 0u, "empty phrase contains no words" },
        { "", "abc", 0u, "empty haystack" },
        { "   ", " ", 0u, "blank-only phrase contains no words" },
        { "  a  b  ", "a b", 1u, "leading and trailing blanks" },
        { "a\tb\nc\rd", "b\nc", 2u, "existing ASCII blank-class separators" },
        { "left,right !?", "left,right", 1u, "punctuation is part of a word" },
        { "left,right !?", "left", 0u, "punctuation-sensitive partial word" },
        { "a b", "a b c", 0u, "phrase longer than haystack" },
        { "a  a a", "a a", 1u, "first of repeated candidate phrases" }
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const unsigned char *string =
            (const unsigned char *)cases[index].string;
        const unsigned char *phrase =
            (const unsigned char *)cases[index].phrase;
        size_t string_length = strlen(cases[index].string);
        size_t phrase_length = strlen(cases[index].phrase);
        check(oracle_find(string, string_length, phrase, phrase_length) ==
                  cases[index].expected,
              "independent oracle agrees with IBM edge-vector expectation");
        (void)check_find_case(string, string_length, phrase, phrase_length,
                              cases[index].expected,
                              cases[index].description);
    }
}

static void test_binary_words(void)
{
    static const unsigned char binary_string[] = {
        0x80u, (unsigned char)' ', 0x00u, 0xffu, (unsigned char)' ',
        0x41u
    };
    static const unsigned char nul_word[] = { 0x00u };
    static const unsigned char nul_ff_word[] = { 0x00u, 0xffu };
    static const unsigned char ff_only[] = { 0xffu };
    static const unsigned char high_letter_word[] = { 0xffu, 0x80u, 'A' };
    static const unsigned char adjacent_high_string[] = {
        (unsigned char)'A', 0x80u, 0xffu, (unsigned char)'B'
    };
    static const unsigned char nonprintable_word[] = { 0x01u, 0x00u, 0x1fu };
    static const unsigned char separated_nonprintable[] = {
        (unsigned char)' ', 0x01u, 0x00u, 0x1fu, (unsigned char)' '
    };

    (void)check_find_case(binary_string, sizeof(binary_string), nul_ff_word,
                          sizeof(nul_ff_word), 2u,
                          "NUL and FF bytes form a complete second word");
    (void)check_find_case(binary_string, sizeof(binary_string), ff_only,
                          sizeof(ff_only), 0u,
                          "FF byte inside a longer word is not a whole-word match");
    (void)check_find_case(nul_word, sizeof(nul_word), nul_word,
                          sizeof(nul_word), 1u,
                          "a NUL byte is a word byte, not a C-string terminator");
    (void)check_find_case(adjacent_high_string, sizeof(adjacent_high_string),
                          high_letter_word, sizeof(high_letter_word), 0u,
                          "high-bit bytes adjacent to letters stay in one word");
    (void)check_find_case(adjacent_high_string, sizeof(adjacent_high_string),
                          adjacent_high_string + 1u, 2u, 0u,
                          "high-bit substring does not match part of a word");
    (void)check_find_case(separated_nonprintable,
                          sizeof(separated_nonprintable), nonprintable_word,
                          sizeof(nonprintable_word), 1u,
                          "nonprintable nonblank bytes match as a whole word");
    (void)check_find_case(NULL, 0u, NULL, 0u, 0u,
                          "zero-length binary strings remain valid");
    (void)check_find_case(binary_string, sizeof(binary_string), NULL, 0u,
                          0u, "zero-length binary phrase returns zero");
}

static void test_argument_validation(void)
{
    RexxValue arguments[3] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;

    check(rexx_builtin_lookup("FIND", 4u) != NULL,
          "FIND is registered in the built-in table");
    check(rexx_builtin_lookup("FIND", 4u)->minimum_arguments == 2u &&
              rexx_builtin_lookup("FIND", 4u)->maximum_arguments == 2u,
          "FIND descriptor requires exactly two arguments");
    status = rexx_builtin_dispatch("FIND", 4u, NULL, 0u, &result);
    check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
              result.type == REXX_VALUE_UNSET,
          "FIND rejects no arguments without constructing a result");
    (void)rexx_value_set_string(&arguments[0], "a b", 3u);
    status = rexx_builtin_dispatch("FIND", 4u, arguments, 1u, &result);
    check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
              result.type == REXX_VALUE_UNSET,
          "FIND rejects a missing phrase argument");
    (void)rexx_value_set_string(&arguments[1], "a", 1u);
    rexx_value_clear(&arguments[0]);
    status = rexx_builtin_dispatch("FIND", 4u, arguments, 2u, &result);
    check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
              result.type == REXX_VALUE_UNSET,
          "FIND rejects a missing first string argument");
    (void)rexx_value_set_string(&arguments[0], "a b", 3u);
    rexx_value_clear(&arguments[1]);
    status = rexx_builtin_dispatch("FIND", 4u, arguments, 2u, &result);
    check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
              result.type == REXX_VALUE_UNSET,
          "FIND rejects a missing second phrase argument");
    (void)rexx_value_set_string(&arguments[1], "a", 1u);
    (void)rexx_value_set_string(&arguments[2], "2", 1u);
    status = rexx_builtin_dispatch("FIND", 4u, arguments, 3u, &result);
    check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
              result.type == REXX_VALUE_UNSET,
          "FIND rejects a third start-position argument");
    status = rexx_builtin_dispatch("FIND", 4u, arguments, 4u, &result);
    check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
              result.type == REXX_VALUE_UNSET,
          "FIND rejects additional excess arguments");
    status = rexx_builtin_dispatch("fInD", 4u, arguments, 2u, &result);
    check(status == REXX_VM_STATUS_OK && result_is_number(&result, 1u),
          "FIND function name follows existing case-insensitive dispatch");

    rexx_value_clear(&result);
    clear_values(arguments, 3u);
    check(rexx_vm_test_allocator_live_blocks() == 0u,
          "argument validation releases all prepared values");
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static size_t append_random_separator(unsigned char *buffer,
                                     size_t length,
                                     uint32_t *state,
                                     int leading_or_trailing)
{
    static const unsigned char separators[] = {
        (unsigned char)' ', (unsigned char)'\t', (unsigned char)'\n',
        (unsigned char)'\v', (unsigned char)'\f', (unsigned char)'\r'
    };
    size_t count = 1u + (size_t)(next_random(state) % 4u);
    size_t index;
    if (leading_or_trailing && (next_random(state) & 1u) == 0u) return length;
    for (index = 0u; index < count && length < RX28_BUFFER; ++index)
        buffer[length++] = separators[next_random(state) %
                                      (sizeof(separators) / sizeof(separators[0]))];
    return length;
}

static void append_random_word(unsigned char *buffer,
                               size_t *length,
                               Rx28Word *word,
                               uint32_t *state)
{
    static const unsigned char alphabet[] = {
        'a', 'b', 'c', 'A', 'B', '0', '9', '!', '.',
        0x00u, 0x01u, 0x7fu, 0x80u, 0xffu
    };
    size_t count = 1u + (size_t)(next_random(state) % 9u);
    size_t index;
    word->start = *length;
    for (index = 0u; index < count && *length < RX28_BUFFER; ++index)
        buffer[(*length)++] = alphabet[next_random(state) %
                                        (sizeof(alphabet) / sizeof(alphabet[0]))];
    word->length = *length - word->start;
}

static void test_properties(void)
{
    unsigned char string[RX28_BUFFER];
    unsigned char phrase[RX28_BUFFER];
    uint32_t state = RX28_PROPERTY_SEED;
    size_t case_index;

    for (case_index = 0u; case_index < RX28_PROPERTY_CASES; ++case_index) {
        Rx28Word words[RX28_MAX_WORDS];
        size_t word_count = (size_t)(next_random(&state) %
                                     (RX28_MAX_WORDS + 1u));
        size_t string_length = 0u;
        size_t phrase_length = 0u;
        size_t index;
        size_t expected;
        unsigned int mode = (unsigned int)(case_index % 6u);

        string_length = append_random_separator(string, string_length,
                                                &state, 1);
        for (index = 0u; index < word_count; ++index) {
            if (index != 0u)
                string_length = append_random_separator(string, string_length,
                                                        &state, 0);
            append_random_word(string, &string_length, &words[index], &state);
        }
        string_length = append_random_separator(string, string_length,
                                                &state, 1);

        if (mode == 0u || (word_count == 0u && mode != 4u)) {
            phrase_length = 0u;
        } else if ((mode == 1u || mode == 2u || mode == 3u) &&
                   word_count != 0u) {
            size_t first = (size_t)(next_random(&state) % word_count);
            size_t count = mode == 1u ? 1u : 1u +
                (size_t)(next_random(&state) % (word_count - first));
            size_t word_index;
            if (count > 5u) count = 5u;
            phrase_length = append_random_separator(phrase, phrase_length,
                                                    &state, 1);
            for (word_index = 0u; word_index < count; ++word_index) {
                if (word_index != 0u)
                    phrase_length = append_random_separator(
                        phrase, phrase_length, &state, 0);
                memcpy(phrase + phrase_length, string + words[first + word_index].start,
                       words[first + word_index].length);
                phrase_length += words[first + word_index].length;
            }
            phrase_length = append_random_separator(phrase, phrase_length,
                                                    &state, 1);
            if (mode == 3u && count > 1u) {
                /* A generated repeated-word candidate tests first-match choice. */
                size_t copy_length = phrase_length;
                if (copy_length + words[first].length + 1u < sizeof(phrase)) {
                    phrase[copy_length++] = (unsigned char)' ';
                    memcpy(phrase + copy_length, string + words[first].start,
                           words[first].length);
                    phrase_length = copy_length + words[first].length;
                }
            }
        } else if (mode == 4u && word_count != 0u && words[0].length > 1u) {
            phrase_length = words[0].length - 1u;
            memcpy(phrase, string + words[0].start, phrase_length);
        } else {
            Rx28Word generated[4];
            size_t generated_count = 1u + (size_t)(next_random(&state) % 4u);
            size_t generated_index;
            for (generated_index = 0u; generated_index < generated_count;
                 ++generated_index) {
                if (generated_index != 0u)
                    phrase_length = append_random_separator(
                        phrase, phrase_length, &state, 0);
                append_random_word(phrase, &phrase_length,
                                   &generated[generated_index], &state);
            }
        }

        expected = oracle_find(string, string_length, phrase, phrase_length);
        (void)check_find_case(string, string_length, phrase, phrase_length,
                              expected,
                              "deterministic FIND/WORDPOS property matches independent word oracle");
        ++property_cases;
    }
}

static int capture_output(void *user_data, const char *bytes, size_t length)
{
    Rx28Capture *capture = (Rx28Capture *)user_data;
    if (capture == NULL || bytes == NULL ||
        length > sizeof(capture->bytes) - capture->length)
        return -1;
    memcpy(capture->bytes + capture->length, bytes, length);
    capture->length += length;
    return (int)length;
}

static int compile_source(const char *source, RexxBytecodeChunk **chunk_out)
{
    RexxLexer lexer = { 0 };
    RexxParser parser = { 0 };
    RexxAstProgram *program = NULL;
    RexxCompilerError error = { 0 };
    RexxLexerStatus lexer_status;
    RexxParserStatus parser_status;
    RexxCompilerStatus compiler_status;

    *chunk_out = NULL;
    lexer_status = rexx_lexer_init(&lexer, source, strlen(source));
    if (lexer_status != REXX_LEXER_STATUS_OK) return 0;
    parser_status = rexx_parser_init(&parser, &lexer);
    if (parser_status == REXX_PARSER_STATUS_OK)
        parser_status = rexx_parser_parse(&parser, &program);
    if (parser_status != REXX_PARSER_STATUS_OK) {
        rexx_parser_free_ast(program);
        return 0;
    }
    compiler_status = rexx_compiler_compile(program, chunk_out, &error);
    rexx_parser_free_ast(program);
    return compiler_status == REXX_COMPILER_STATUS_OK;
}

static void test_interpreter_calls(void)
{
    static const char source[] =
        "say find('now is  the time','is    the')\n"
        "say wordpos('is the','now is  the time')\n"
        "say find(space('a  b'), 'b')\n"
        "say helper('a b','b')\n"
        "say c2x('80 00FF 41'X)\n"
        "say find('80 00FF 41'X,'00FF'X)\n"
        "say find('80'X || ' ' || '00FF'X,'00FF'X)\n"
        "say find('80'X || '00FF'X,'FF'X)\n"
        "exit 0\n"
        "helper: procedure\n"
        "  parse arg text, phrase\n"
        "  return find(text, phrase)\n";
    static const unsigned char expected[] =
        "2\n2\n2\n2\n8000FF41\n0\n2\n0\n";
    static const char recovery_source[] =
        "signal on syntax\n"
        "bad = find('a b')\n"
        "say 'not reached'\n"
        "exit 1\n"
        "syntax:\n"
        "say 'trapped'\n"
        "say find('a b','b')\n"
        "exit 0\n";
    static const unsigned char recovery_expected[] = "trapped\n2\n";
    static const char malformed_source[] = "say find('a b','b'\n";
    RexxBytecodeChunk *chunk = NULL;
    RexxBytecodeChunk *recovery_chunk = NULL;
    RexxVM vm_a = { 0 };
    RexxVM vm_b = { 0 };
    Rx28Capture capture_a = { { 0u }, 0u };
    Rx28Capture capture_b = { { 0u }, 0u };
    RexxVMStatus status_a;
    RexxVMStatus status_b;

    check(compile_source(source, &chunk),
          "nested FIND, WORDPOS, SPACE, X-literal, and internal-routine source compiles");
    if (chunk != NULL) {
        status_a = rexx_vm_init(&vm_a, 0u);
        status_b = rexx_vm_init(&vm_b, 0u);
        check(status_a == REXX_VM_STATUS_OK && status_b == REXX_VM_STATUS_OK,
              "two independent VMs initialize for FIND execution");
        if (status_a == REXX_VM_STATUS_OK && status_b == REXX_VM_STATUS_OK) {
            rexx_vm_set_output_writer(&vm_a, capture_output, &capture_a);
            rexx_vm_set_output_writer(&vm_b, capture_output, &capture_b);
            status_a = rexx_vm_execute(&vm_a, chunk);
            status_b = rexx_vm_execute(&vm_b, chunk);
            check(status_a == REXX_VM_STATUS_OK &&
                      capture_a.length == sizeof(expected) - 1u &&
                      memcmp(capture_a.bytes, expected,
                             sizeof(expected) - 1u) == 0,
                  "interpreter exercises nested calls, internal routines, and binary literals");
            check(status_b == REXX_VM_STATUS_OK &&
                      capture_b.length == sizeof(expected) - 1u &&
                      memcmp(capture_b.bytes, expected,
                             sizeof(expected) - 1u) == 0,
                  "second VM independently returns identical FIND results");
        }
        rexx_vm_free(&vm_a);
        rexx_vm_free(&vm_b);
        rexx_bytecode_chunk_free(chunk);
    }

    check(!compile_source(malformed_source, &chunk),
          "malformed FIND source expression fails during compilation");
    check(compile_source(recovery_source, &recovery_chunk),
          "FIND syntax-trap recovery source compiles");
    if (recovery_chunk != NULL) {
        RexxVM recovery_vm = { 0 };
        Rx28Capture recovery_capture = { { 0u }, 0u };
        RexxVMStatus status = rexx_vm_init(&recovery_vm, 0u);
        check(status == REXX_VM_STATUS_OK,
              "syntax-trap recovery VM initializes");
        if (status == REXX_VM_STATUS_OK) {
            rexx_vm_set_output_writer(&recovery_vm, capture_output,
                                      &recovery_capture);
            status = rexx_vm_execute(&recovery_vm, recovery_chunk);
            check(status == REXX_VM_STATUS_OK &&
                      recovery_capture.length == sizeof(recovery_expected) - 1u &&
                      memcmp(recovery_capture.bytes, recovery_expected,
                             sizeof(recovery_expected) - 1u) == 0,
                  "FIND error trap recovers and a subsequent FIND call succeeds");
        }
        rexx_vm_free(&recovery_vm);
        rexx_bytecode_chunk_free(recovery_chunk);
    }
}

static void test_allocation_recovery(void)
{
    RexxValue arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    size_t live_before;
    RexxVMStatus status;

    status = rexx_value_set_string(&arguments[0], "a b c", 5u);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_value_set_string(&arguments[1], "b", 1u);
    check(status == REXX_VM_STATUS_OK,
          "FIND allocation-failure arguments are prepared");
    if (status != REXX_VM_STATUS_OK) {
        clear_values(arguments, 2u);
        return;
    }
    live_before = rexx_vm_test_allocator_live_blocks();
    rexx_vm_test_allocator_fail_on(1u);
    status = rexx_builtin_dispatch("FIND", 4u, arguments, 2u, &result);
    check(status == REXX_VM_STATUS_OUT_OF_MEMORY &&
              result.type == REXX_VALUE_UNSET,
          "FIND result allocation failure reports OOM with no partial result");
    check(rexx_vm_test_allocator_attempts() == 1u,
          "FIND wrapper adds no temporary allocation before WORDPOS result creation");
    rexx_vm_test_allocator_disable();
    check(rexx_vm_test_allocator_live_blocks() == live_before,
          "failed FIND result allocation leaves existing ownership unchanged");
    status = rexx_builtin_dispatch("FIND", 4u, arguments, 2u, &result);
    check(status == REXX_VM_STATUS_OK && result_is_number(&result, 2u),
          "FIND succeeds on the same arguments after allocation failure");
    clear_values(arguments, 2u);
    rexx_value_clear(&result);
    check(rexx_vm_test_allocator_live_blocks() == 0u,
          "FIND allocation recovery releases all direct-call values");
}

static void test_interpreter_failure_recovery(void)
{
    static const char source[] = "say find('alpha beta','beta')\n";
    static const unsigned char expected[] = "2\n";
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm = { 0 };
    Rx28Capture capture = { { 0u }, 0u };
    RexxVMStatus status;
    size_t allocation_points;
    size_t failpoint;

    check(compile_source(source, &chunk),
          "FIND interpreter failpoint source compiles before injection");
    if (chunk == NULL) return;
    status = rexx_vm_init(&vm, 0u);
    check(status == REXX_VM_STATUS_OK, "FIND failpoint VM initializes");
    if (status != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        return;
    }
    rexx_vm_set_output_writer(&vm, capture_output, &capture);
    rexx_vm_test_allocator_fail_on(SIZE_MAX);
    status = rexx_vm_execute(&vm, chunk);
    allocation_points = rexx_vm_test_allocator_attempts();
    interpreter_allocation_failpoints = allocation_points;
    rexx_vm_test_allocator_disable();
    check(status == REXX_VM_STATUS_OK && capture.length == sizeof(expected) - 1u &&
              memcmp(capture.bytes, expected, sizeof(expected) - 1u) == 0,
          "baseline FIND interpreter call returns its expected output");

    for (failpoint = 1u; failpoint <= allocation_points; ++failpoint) {
        RexxVMStatus injected_status;
        size_t attempts;

        rexx_vm_free(&vm);
        memset(&vm, 0, sizeof(vm));
        memset(&capture, 0, sizeof(capture));
        status = rexx_vm_init(&vm, 0u);
        check(status == REXX_VM_STATUS_OK,
              "FIND interpreter failpoint VM reinitializes cleanly");
        if (status != REXX_VM_STATUS_OK) break;
        rexx_vm_set_output_writer(&vm, capture_output, &capture);
        rexx_vm_test_allocator_fail_on(failpoint);
        injected_status = rexx_vm_execute(&vm, chunk);
        attempts = rexx_vm_test_allocator_attempts();
        rexx_vm_test_allocator_disable();
        check(injected_status == REXX_VM_STATUS_OUT_OF_MEMORY &&
                  attempts >= failpoint,
              "measured FIND interpreter allocation failpoint reports OOM");
        capture.length = 0u;
        status = rexx_vm_execute(&vm, chunk);
        check(status == REXX_VM_STATUS_OK &&
                  capture.length == sizeof(expected) - 1u &&
                  memcmp(capture.bytes, expected, sizeof(expected) - 1u) == 0,
              "same VM completes FIND after an injected allocation failure");
        rexx_vm_free(&vm);
        check(rexx_vm_test_allocator_live_blocks() == 0u,
              "FIND interpreter failure/recovery releases all VM allocations");
    }
    rexx_vm_free(&vm);
    rexx_vm_test_allocator_disable();
    rexx_bytecode_chunk_free(chunk);
}

static void test_many_words_and_limit_word(void)
{
    unsigned char *many = (unsigned char *)malloc(20001u);
    unsigned char *long_word = (unsigned char *)malloc(RX28_MAX_STRING);
    size_t length = 0u;
    size_t index;

    check(many != NULL, "many-word FIND test buffer allocates");
    if (many != NULL) {
        for (index = 0u; index < 10000u; ++index) {
            many[length++] = (unsigned char)'a';
            if (index + 1u != 10000u) many[length++] = (unsigned char)' ';
        }
        (void)check_find_case(many, length,
                              (const unsigned char *)"a a", 3u, 1u,
                              "FIND handles a long string containing many words");
        free(many);
    }

    check(long_word != NULL, "maximum-cap FIND word buffer allocates");
    if (long_word != NULL) {
        memset(long_word, 'x', RX28_MAX_STRING);
        (void)check_find_case(long_word, RX28_MAX_STRING,
                              long_word, RX28_MAX_STRING, 1u,
                              "FIND matches a word at the 16 MiB runtime string cap");
        free(long_word);
    }
}

int main(void)
{
    test_ibm_vectors_and_edges();
    test_binary_words();
    test_argument_validation();
    test_properties();
    test_interpreter_calls();
    test_allocation_recovery();
    test_interpreter_failure_recovery();
    test_many_words_and_limit_word();
    printf("RX28 FIND seed=0x%08x property_cases=%zu assertions=%zu failures=%zu interpreter_failpoints=%zu limit=%zu\n",
           (unsigned)RX28_PROPERTY_SEED, property_cases, assertions, failures,
           interpreter_allocation_failpoints, RX28_MAX_STRING);
    rexx_vm_test_allocator_disable();
    return failures == 0u ? 0 : 1;
}
