/*
 * td/disasm_x86.cpp - x86-64 instructions through Zydis (td/disasm.h;
 * third_party/zydis, built by td/third/zydis.c). Intel syntax, lower case
 * hex, addresses named through the Symbolizer by a hook on Zydis's
 * absolute address printer (jump and call targets, rip-relative data).
 * Linux only: IRIX builds disasm_nox86.cpp instead.
 */
#include "disasm.h"

#define ZYCORE_STATIC_BUILD
#define ZYDIS_STATIC_BUILD
#include <Zydis/Zydis.h>

#include <string.h>

namespace td {

namespace {

bool g_ready;
ZydisDecoder g_decoder;
ZydisFormatter g_formatter;
ZydisFormatterFunc g_print_address;     /* Zydis's own */

/* The hook: "name (0x401000)" when the Symbolizer has a name. */
ZyanStatus print_address(const ZydisFormatter* f, ZydisFormatterBuffer* buffer,
                         ZydisFormatterContext* context)
{
    const Symbolizer* sym = (const Symbolizer*)context->user_data;
    ZyanU64 a;
    Text t;
    if (!sym || !ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(context->instruction, context->operand,
                                                       context->runtime_address, &a)) ||
        !sym->symbol((Addr)a, &t))
        return g_print_address(f, buffer, context);
    t.clear();
    format_address((Addr)a, sym, &t);
    ZyanStatus s = ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL);
    ZyanString* str;
    ZyanStringView v;
    if (ZYAN_SUCCESS(s))
        s = ZydisFormatterBufferGetString(buffer, &str);
    if (ZYAN_SUCCESS(s))
        s = ZyanStringViewInsideBuffer(&v, t.c_str());
    if (ZYAN_SUCCESS(s))
        s = ZyanStringAppend(str, &v);
    return s;
}

bool init()
{
    if (g_ready)
        return true;
    if (!ZYAN_SUCCESS(ZydisDecoderInit(&g_decoder, ZYDIS_MACHINE_MODE_LONG_64,
                                       ZYDIS_STACK_WIDTH_64)) ||
        !ZYAN_SUCCESS(ZydisFormatterInit(&g_formatter, ZYDIS_FORMATTER_STYLE_INTEL)))
        return false;
    /* [rbp-4], not [rbp-0x04]; 0x1f, not 0x1F. */
    ZydisFormatterSetProperty(&g_formatter, ZYDIS_FORMATTER_PROP_DISP_PADDING,
                              ZYDIS_PADDING_DISABLED);
    ZydisFormatterSetProperty(&g_formatter, ZYDIS_FORMATTER_PROP_IMM_PADDING,
                              ZYDIS_PADDING_DISABLED);
    ZydisFormatterSetProperty(&g_formatter, ZYDIS_FORMATTER_PROP_ADDR_PADDING_ABSOLUTE,
                              ZYDIS_PADDING_DISABLED);
    ZydisFormatterSetProperty(&g_formatter, ZYDIS_FORMATTER_PROP_HEX_UPPERCASE, ZYAN_FALSE);
    /* "mov dword ptr [rbp-4], edi", as Visual C++ wrote it, even where
     * the register says the size. */
    ZydisFormatterSetProperty(&g_formatter, ZYDIS_FORMATTER_PROP_FORCE_SIZE, ZYAN_TRUE);
    g_print_address = print_address;
    ZydisFormatterSetHook(&g_formatter, ZYDIS_FORMATTER_FUNC_PRINT_ADDRESS_ABS,
                          (const void**)&g_print_address);
    g_ready = true;
    return true;
}

/* Zydis names conditions as Intel's tables do (jz, jnbe, setnz,
 * cmovnb); Visual C++ and objdump as the manuals' first names (je, ja,
 * setne, cmovae). */
const char* const k_conditions[][2] = {
    { "z", "e" }, { "nz", "ne" }, { "nb", "ae" }, { "nbe", "a" }, { "nl", "ge" }, { "nle", "g" },
    { 0, 0 }
};

void rename_condition(char* m, size_t size)
{
    static const char* const prefixes[3] = { "j", "set", "cmov" };
    int i, k;
    for (i = 0; i < 3; i++) {
        size_t n = strlen(prefixes[i]);
        if (strncmp(m, prefixes[i], n))
            continue;
        for (k = 0; k_conditions[k][0]; k++)
            if (!strcmp(m + n, k_conditions[k][0])) {
                if (n + strlen(k_conditions[k][1]) < size)
                    strcpy(m + n, k_conditions[k][1]);
                return;
            }
    }
}

void copy(char* to, size_t size, const char* from, size_t n)
{
    if (n >= size)
        n = size - 1;
    memcpy(to, from, n);
    to[n] = 0;
}

} /* namespace */

bool disassemble_x86_64(Addr addr, const unsigned char* bytes, size_t n, const Symbolizer* sym,
                        Insn* out)
{
    if (!init())
        return false;
    ZydisDecodedInstruction insn;
    ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&g_decoder, bytes, n, &insn, ops)))
        return false;
    char text[256];
    if (!ZYAN_SUCCESS(ZydisFormatterFormatInstruction(&g_formatter, &insn, ops,
                                                      insn.operand_count_visible, text,
                                                      sizeof(text), addr, (void*)sym)))
        return false;
    out->len = insn.length;
    memcpy(out->bytes, bytes, insn.length);
    switch (insn.meta.category) {
    case ZYDIS_CATEGORY_CALL: out->kind = INSN_CALL; break;
    case ZYDIS_CATEGORY_RET: out->kind = INSN_RETURN; break;
    case ZYDIS_CATEGORY_UNCOND_BR: out->kind = INSN_JUMP; break;
    case ZYDIS_CATEGORY_COND_BR: out->kind = INSN_BRANCH; break;
    default: out->kind = INSN_OTHER; break;
    }
    if (out->kind != INSN_OTHER && insn.operand_count_visible > 0 &&
        ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && ops[0].imm.is_relative) {
        ZyanU64 a;
        if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&insn, &ops[0], addr, &a))) {
            out->has_target = true;
            out->target = (Addr)a;
        }
    }
    /* "rep movsb": the mnemonic is everything up to its own name. */
    const char* name = ZydisMnemonicGetString(insn.mnemonic);
    const char* at = name ? strstr(text, name) : 0;
    size_t split = at ? (size_t)(at - text) + strlen(name) : strcspn(text, " ");
    copy(out->mnemonic, sizeof(out->mnemonic), text, split);
    rename_condition(out->mnemonic, sizeof(out->mnemonic));
    const char* rest = text + split;
    while (*rest == ' ')
        rest++;
    copy(out->operands, sizeof(out->operands), rest, strlen(rest));
    return true;
}

} /* namespace td */
