/*
 * The wrapper through which td compiles Zydis, its x86-64 disassembler
 * (third_party/zydis, pinned at v4.1.1, and its Zycore library,
 * third_party/zycore, at the commit that release names; MIT; AGENTS.md:
 * third-party code is never edited, only compiled through wrapper files).
 * Only decoding and the Intel formatter are used: no encoder, no Xeon Phi
 * (KNC) instructions. C99, and only on x86-64 hosts (td/disasm_x86.cpp).
 *
 * The formatters cannot share one translation unit (each defines the
 * same static strings), so FormatterATT.c and FormatterIntel.c have
 * wrappers of their own: zydis_att.c, zydis_intel.c (zydis_config.h has
 * the settings all three share).
 */
#include "zydis_config.h"

#include "../../third_party/zycore/src/Allocator.c"
#include "../../third_party/zycore/src/Vector.c"
#include "../../third_party/zycore/src/String.c"
#include "../../third_party/zycore/src/Zycore.c"

#include "../../third_party/zydis/src/Decoder.c"
#include "../../third_party/zydis/src/DecoderData.c"
#include "../../third_party/zydis/src/Formatter.c"
#include "../../third_party/zydis/src/FormatterBase.c"
#include "../../third_party/zydis/src/FormatterBuffer.c"
#include "../../third_party/zydis/src/MetaInfo.c"
#include "../../third_party/zydis/src/Mnemonic.c"
#include "../../third_party/zydis/src/Register.c"
#include "../../third_party/zydis/src/SharedData.c"
#include "../../third_party/zydis/src/String.c"
#include "../../third_party/zydis/src/Utils.c"
#include "../../third_party/zydis/src/Zydis.c"
