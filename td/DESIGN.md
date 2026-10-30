# td: a debugger for ttr

Status: design draft, 2026-10-24. Nothing is implemented yet. Section 11
lists the questions to decide before milestone 1.

gdb is hard to use, debugging through VS Code is worse, and dbx and
ProDev WorkShop are not much better. ttr gets its own debugger, in three
parts:

- **td**, a library that controls a process and reads its symbols. It
  knows nothing about user interfaces.
- **ttrdb**, a small command-line program on td. It is for trying td
  out and for scripted tests, not the main way to use it.
- **The debugger in ttrcode**, modelled on Visual C++ 6's debugger, with
  the same keys and windows. This is the real goal.

Everything here was checked against the systems we have unless marked
*to verify*: Linux x86-64 (this machine) and IRIX 6.5 (the emulated
guest's headers and binaries).

## 1. Goals and non-goals

Goals:

- Start a program under the debugger, or attach to a running one.
- Breakpoints on a line or a function, with a condition and a hit count.
- Step into, step over and step out by source line, and step by
  instruction.
- Run to the cursor and continue.
- The call stack, with the source line of each frame.
- Local variables, arguments and expressions (C and C++), shown with
  their types: structures, arrays, pointers, enums.
- Watching memory where the system can (section 3.3).
- Threads: list them, switch between them, and stop all of them.
- Shared libraries: their symbols, including libraries loaded with
  `dlopen`.
- Core files, for crashes after the fact. IRIX leaves a `core` in the
  working directory, which is a reason to have this early.
- The same code on Linux and IRIX, in C++98 that MIPSpro 7.4 builds.

Not in the first version:

- Calling functions in the debugged program from expressions.
- Edit and continue.
- Debugging another machine over the network (section 9 keeps the door
  open).
- Languages other than C and C++.
- A gdb-compatible command syntax. ttrdb's commands are short and
  obvious, but they are not gdb's.

## 2. The parts

```
ttrcode debugger UI        ttrdb (command line)
           \                 /
            td::Session  (events, breakpoints, stepping, threads)
           /           \
 td::Target              td::Symbols
 (one process:           (ELF, DWARF: lines, frames,
  stop, run, regs,        types, variables)
  memory, maps)
   |          |               |
 ptrace     IRIX /proc     td::Arch (x86-64, MIPS:
 (Linux)    ioctls          registers, breakpoint
                            instruction, stepping help)
```

- **Target** is the only part that differs between systems. Following
  `tp/` (ttrc/DESIGN.md section 6.1), it has one implementation file per
  system, `td/target_linux.cpp` and `td/target_irix.cpp`, chosen by the
  Makefile, with no `#ifdef`s spread through the rest. *To decide*
  (section 11): AGENTS.md says only `src/wayland/` may use Linux-only
  APIs, yet tp already does this for ptys. ptrace needs the same
  exception.
- **Arch** describes a CPU: its registers and their DWARF numbers, the
  breakpoint instruction, how to find the next instruction for stepping,
  and how a call and return look.
- **Symbols** reads ELF and DWARF from files. It never touches a
  process, so it works the same for a live process and a core file.
- **Session** puts them together: the stop-run loop, breakpoints,
  stepping by line, and the list of threads and modules. The two user
  interfaces only talk to the Session.

## 3. Controlling a process

Every operation the Session needs, and how each system provides it.

### 3.1 Linux: ptrace and /proc

| Need | How |
|---|---|
| Start a program | `fork`; in the child `PTRACE_TRACEME`, then `exec`. It stops at its first instruction with SIGTRAP. |
| Attach | `PTRACE_SEIZE`, then `PTRACE_INTERRUPT` to stop it. `PTRACE_SEIZE` does not send a SIGSTOP, unlike `PTRACE_ATTACH`. |
| Events | `waitpid(-1, ..., __WALL)`. Options `PTRACE_O_TRACECLONE`, `TRACEFORK`, `TRACEVFORK`, `TRACEEXEC` and `TRACEEXIT`. `PTRACE_GETSIGINFO` says why a SIGTRAP came. |
| Resume | `PTRACE_CONT`, with or without the pending signal. |
| Single step | `PTRACE_SINGLESTEP` (the x86 trap flag). |
| Registers | `PTRACE_GETREGS`/`SETREGS`; `PTRACE_GETREGSET` with `NT_PRFPREG` and `NT_X86_XSTATE` for floating point and vector registers. |
| Memory | `pread`/`pwrite` on `/proc/<pid>/mem`, any size; `PTRACE_POKEDATA` also writes to read-only text. |
| Breakpoints | `int3` (0xCC) written over the first byte of the instruction. |
| Watchpoints | Debug registers DR0 to DR3 and DR7, written with `PTRACE_POKEUSER` at `offsetof(struct user, u_debugreg)`. Four, each up to 8 bytes. |
| Threads | Every thread is traced on its own. New ones arrive through `PTRACE_O_TRACECLONE`. When attaching, `/proc/<pid>/task` lists the existing ones. `tgkill` stops one. |
| Memory map | `/proc/<pid>/maps`. |
| Load address | `/proc/<pid>/auxv`: `AT_PHDR` and `AT_ENTRY` give the offset of a position-independent executable. |
| Kill, detach | `PTRACE_KILL` or `kill`; `PTRACE_DETACH`. |

Permissions: this machine has no Yama module, so a process can trace any
process of the same user. Distributions with
`/proc/sys/kernel/yama/ptrace_scope` set to 1 allow attaching only to
your own children (starting a program always works). ttrdb says so when
attaching fails with EPERM.

### 3.2 IRIX: the /proc ioctls

From the guest's `/usr/include/sys/procfs.h`. You open `/proc/<pid>`
(*to verify:* `/proc/pinfo/<pid>` holds the read-only process
information) and issue ioctls on it.

| Need | How |
|---|---|
| Start a program | `fork`, the child stops itself; the parent opens its `/proc` file, sets the events (`PIOCSFAULT`, `PIOCSENTRY` on `exec`) and lets it `exec`. *To verify:* the exact handshake dbx uses. |
| Attach | open `/proc/<pid>` read-write, then `PIOCSTOP`. |
| Wait for a stop | `PIOCWSTOP`, or `poll` on the file (*to verify* on IRIX). |
| Why it stopped | `PIOCSTATUS`: `pr_why` and `pr_what` (a signal, a fault, a system call). |
| Resume | `PIOCRUN` with a `prrun_t`. Its flags: `PRCSIG` (clear the signal), `PRCFAULT` (clear the fault), `PRSTRACE`/`PRSFAULT` (change the traced sets), `PRSTEP` (single step), `PRSABORT`, `PRSTOP`. |
| Events | `PIOCSFAULT` with a fault set: `FLTBPT` (breakpoint), `FLTTRACE` (step), `FLTWATCH` (watchpoint), `FLTILL`, `FLTACCESS` and the rest from `<sys/fault.h>`. `PIOCSTRACE` for signals. `PIOCSENTRY`/`PIOCSEXIT` for system calls. `PIOCSFORK` makes children inherit the tracing. |
| Registers | `PIOCGREG`/`PIOCSREG` (`gregset_t`), `PIOCGFPREG`/`PIOCSFPREG`. |
| Memory | `lseek` and `read`/`write` on the same file. `PIOCREAD` takes a 64-bit offset. |
| Breakpoints | The MIPS `break` instruction, reported as `FLTBPT`. |
| Watchpoints | `PIOCNWATCH`, `PIOCGWATCH`, `PIOCSWATCH` with a `prwatch_t` (address, size, `MA_READ`/`MA_WRITE`/`MA_EXEC`), reported as `FLTWATCH`. |
| Threads | `PIOCGUTID` lists the thread ids. `PIOCTHREAD` with a `prthreadctl_t` runs any of these ioctls on one thread, or on all, the stopped ones, or the ones with new events (`PTFD_*`, `PTFS_*` flags). A `sproc` member is a process of its own. |
| Memory map | `PIOCNMAP` and `PIOCMAP` (`prmap_t`), `PIOCMAP_SGI` with more detail. |
| The file behind a mapping | `PIOCOPENM` returns an open descriptor for a mapped object, no path needed. |

Notes:

- **Single step.** MIPS has no hardware single step, so `PRSTEP` must be
  done by the kernel. *To verify* on the Octane: that it follows taken
  branches and returns after exactly one instruction.
- **Branch delay slots.** The instruction after a branch runs before the
  branch takes effect. A breakpoint in a delay slot reports the branch's
  address, and stepping off a breakpoint on a branch has to account for
  both possible next addresses. Arch handles this (section 6).
- **Watchpoints are probably page protection.** The fault list has
  `FLTSCWATCH`, "hit a store conditional on a watched page", so the
  kernel seems to protect whole pages and check each access, not use the
  R10000's watch registers. If so, there is no limit on their number,
  and they work on any machine that handles ordinary page faults.
  *To verify* on the Octane, and under lightning.
- **The ABIs.** Our builds are n32. A debugger built n32 has to debug o32
  and 64-bit programs as well. `procfs.h` has `PIOC_IRIX5_N32` and
  `PIOC_IRIX5_64` variants. *To verify:* how they change the register
  and map structures.

### 3.3 Under lightning

Breakpoints and stepping by breakpoints need nothing but the `break`
instruction, which lightning runs like any other instruction. Lightning
leaves out architectural watchpoints for speed, so a watchpoint that
needs the watch registers will not fire there. Page-protection
watchpoints (see above) may still work. Watching memory gets tested on
the Octane in any case.

### 3.4 Core files

Both systems write ELF core files: program headers for the memory, and
notes for the registers (`NT_PRSTATUS` on Linux). A core-file Target
answers the same questions as a live one, read-only. *To verify:* the
layout of IRIX's notes.

## 4. Shared libraries

The Session needs the list of loaded objects and their load addresses,
and an event each time it changes.

**Linux.** The executable's `DT_DEBUG` dynamic entry points to the
dynamic linker's `struct r_debug`. Its `r_map` is the `link_map` list
(`l_addr`, `l_name`, `l_ld`). Its `r_brk` is a function the linker calls
before and after every change. A breakpoint there gives the event.

**IRIX.** Checked in the guest's `/lib32/rld`, which exports:

- `__dbx_link`, an 8-byte function: a hook for debuggers to put a
  breakpoint on;
- `__rld_calls_on_rld_list_change`;
- `pObj_Head`, the head of the loaded-object list. Its entries
  (`struct obj_list`, `<obj_list.h>`) point to `struct obj` (`<obj.h>`),
  with `o_path`, `o_base_address`, `o_text_start` and `o_text_size`.

*To verify:* which of these rld calls on each change, and whether
programs also export `__rld_obj_head`. Failing all that, `PIOCMAP` and
`PIOCOPENM` list every mapped object without rld's help.

## 5. Symbols

Both systems use ELF with DWARF, so there is one reader.

### 5.1 What the compilers write

**MIPSpro 7.4 on IRIX** (checked on our `build-irix/ttrt`):

- DWARF version 2, 4-byte addresses in n32.
- Sections typed `SHT_MIPS_DWARF`: `.debug_info`, `.debug_abbrev`,
  `.debug_line`, `.debug_frame`, `.debug_aranges`, `.debug_pubnames`,
  and SGI's own name indexes `.debug_funcnames`, `.debug_varnames`,
  `.debug_typenames` and `.debug_weaknames`.
- `.debug_frame` CIEs of version 1, with the return address in column 64.
- Even our `-O2` builds without `-g` carry line tables and frame
  information, so backtraces with file and line work. The tardist strips
  them, so test builds must be the unstripped ones in `build-irix/`.
- *To verify:* older objects and system libraries may carry the
  ECOFF-style `.mdebug` instead (`<sym.h>`, `<syms.h>`). If they do, a
  reader for `.mdebug` line tables and procedures comes later.

**gcc and clang on Linux:**

- DWARF 4 or 5. Version 5 adds `.debug_str_offsets`, `.debug_addr`,
  `.debug_rnglists`, `.debug_loclists` and `.debug_line_str`.
- Unwinding uses `.eh_frame`, which stays in stripped binaries, so system
  libraries can always be unwound.
- Sections may be compressed (`SHF_COMPRESSED`, zlib or zstd). zlib is
  already in `third_party/`; zstd is not (section 11).

### 5.2 Finding the debug information

In order:

1. The file itself.
2. Linux: the build id (the `.note.gnu.build-id` note) in
   `/usr/lib/debug/.build-id/xx/rest.debug`.
3. Linux: `.gnu_debuglink` next to the file, in a `.debug` subdirectory,
   or under `/usr/lib/debug`.
4. Linux, later: debuginfod (`DEBUGINFOD_URLS`), which fetches debug
   files over HTTP. Arch runs a server. This needs an HTTP client, so it
   is not in the first version.

Without any debug information, there are still the ELF symbol tables
(`.symtab`, `.dynsym`) and `.eh_frame`: function names, backtraces, and
stepping by instruction.

### 5.3 Reading it fast

- Map the file and index lazily. On start, read only the symbol table
  and the address ranges of compilation units (`.debug_aranges`, or the
  CU headers when it is missing).
- Read a CU's line table when a breakpoint or a stop needs it.
- Read a CU's DIEs only when its variables or types are shown.
- Find names through the name indexes: `.debug_pubnames`, SGI's
  `.debug_funcnames`, DWARF 5's `.debug_names`. Otherwise scan the
  top-level DIEs once and keep a hash table.

### 5.4 Our own reader, or libdwarf

libdwarf started at SGI, and IRIX ships it (`/usr/lib32/libdwarf.a`,
`libdwarf_sgi.a`, `libelf.a`). The current one is maintained, is written
in C, and is under the LGPL 2.1.

The recommendation is our own reader, as with terminfo in tu:

- We need a subset: line tables, CFI, DIEs with the attributes C and C++
  use, location expressions. That is a few thousand lines.
- C++98 without the STL, built the same way as the rest of ttr.
- No compatibility problems between libdwarf versions on the two
  systems.

libdwarf's `dwarfdump` is still useful for checking our results.

### 5.5 Names

- C++ names have to be demangled for display.
- Linux uses the Itanium C++ ABI. `abi::__cxa_demangle` exists, but
  pulling in libstdc++'s interface goes against the no-STL rule, so td
  gets its own demangler, to be written later.
- MIPSpro uses its own mangling scheme. *To verify:* the scheme, and
  whether IRIX's `c++filt` or a library function can be called.

### 5.6 Unwinding

- From `.debug_frame` or `.eh_frame` CFI.
- Without CFI on MIPS: scan back for the function's prologue
  (`addiu sp, sp, -n`, `sw ra, n(sp)`), as dbx does.
- Signal frames: IRIX's `sigcontext` and Linux's `rt_sigframe`, which
  `.eh_frame` describes for the signal trampoline. *To verify* on IRIX.

## 6. Arch

| | x86-64 | MIPS |
|---|---|---|
| Breakpoint | `int3`, 1 byte | `break`, 4 bytes |
| PC after the trap | one byte after the breakpoint: step back | at the `break` instruction (*to verify*) |
| Single step | hardware | kernel `PRSTEP`; or a temporary breakpoint at every possible next address |
| Step over a call | breakpoint at the return address | the same, after the delay slot |
| Return address | on the stack | `$ra` at the call; CFI after that |
| DWARF registers | the System V numbering | 0-31 general, 32-63 floating point, 64 the return address column (SGI) |

Stepping off a breakpoint: put the original instruction back, step one
instruction, put the breakpoint back. Other threads must not run during
that step; the Session stops them, or steps with a copy of the
instruction placed elsewhere. *To decide:* which, for each system.

## 7. Session

- **The stop-run loop:** resume, wait for an event, classify it. The
  event is a breakpoint, a finished step, a signal, a new or finished
  thread, a library change, exec, or exit. The Session then either
  answers it internally (a conditional breakpoint whose condition is
  false, a library event) or reports a stop to the user interface.
- **All threads stop together** when one stops, Visual C++ style.
- **Breakpoints** are kept by file and line, or by function name. They
  are planted when the module containing them loads, and a breakpoint
  with no code yet waits for one.
- **Stepping by line** uses the line table:
  - Step into: step instructions until the line changes; when a call
    lands in a function without line information, step out of it.
  - Step over: the same, but run over calls.
  - Step out: a breakpoint at the return address, then check the stack
    pointer (recursion).
  - Run to cursor: a temporary breakpoint.
- **Expressions:** a small C and C++ expression parser and evaluator.
  Variables, members (`.` and `->`), indexing, `*` and `&`, casts to
  known types, arithmetic and comparison, registers as `$pc` and
  `$sp`. No function calls.
- **Events reach the user interface through a callback interface**, not
  by blocking. ttrcode runs one Xt event loop, so the Session must never
  block (section 8.2).

## 8. The user interfaces

### 8.1 ttrdb

A line-oriented program for trying td and for tests, scriptable like the
ttrc tests (`tl` reads its screen, or plain text on a pipe):

```
ttrdb ./prog args          ttrdb -p PID          ttrdb ./prog core
```

| Command | Does |
|---|---|
| `r` | run (start) |
| `c` | continue |
| `s`, `n`, `f` | step into, step over, step out (finish) |
| `si`, `ni` | step one instruction, over calls |
| `b file:line`, `b func`, `b *addr` | set a breakpoint; `b` lists them; `d N` deletes one |
| `bt`, `up`, `down`, `frame N` | the call stack |
| `p expr` | print a value |
| `l` | list the source around the current line |
| `x addr [n]` | dump memory |
| `regs` | the registers |
| `t`, `t N` | list threads, switch to one |
| `mods` | the loaded objects and whether they have debug information |
| `w expr` | watch a variable |
| `k`, `detach`, `q` | kill, detach, quit |

### 8.2 ttrcode: Visual C++ 6

ttrcode already follows Visual Studio 6's keys (ttrcode/DESIGN.md
section 2), and the debugger keeps that up:

| Key | Does |
|---|---|
| F5 | start, or continue |
| Shift+F5 | stop debugging |
| Ctrl+Shift+F5 | restart |
| F9 | set or clear a breakpoint on the caret's line |
| Ctrl+F9 | enable or disable it |
| F10 | step over |
| F11 | step into (see section 11: F11 is full screen today) |
| Shift+F11 | step out |
| Ctrl+F10 | run to the cursor |
| Shift+F9 | QuickWatch on the selection or the word under the caret |
| Alt+3 to Alt+8 | Watch, Variables, Registers, Memory, Call Stack, Disassembly |

The windows:

- **The source view** shows a yellow arrow at the current line and red
  dots for breakpoints. Hovering over a variable shows its value.
- **Variables** shows locals and "auto" values (the variables of the
  current line and the previous one), as VC6 did.
- **Watch** lists expressions, which can be edited in place. Structures
  and arrays open as a tree.
- **Call Stack**: double-clicking a frame shows its source and variables.
- **Registers, Memory, Disassembly**: changed values are shown in red.
- **Output**: the program's own output (it runs on a pty, like the
  terminal pane), and debugger messages.
- **Threads** and **Modules** are dialogs, as in VC6.

How this fits ttrcode:

- The debugger windows live in the bottom pane, with tabs next to the
  terminal and Find Results.
- The Session runs inside ttrcode's single Xt event loop:
  - Linux: a SIGCHLD handler writes to a pipe watched by
    `XtAppAddInput`, then `waitpid(WNOHANG)`.
  - IRIX: the same SIGCHLD pipe if stops send SIGCHLD to a /proc tracer
    (*to verify*). Otherwise a short `XtAppAddTimeOut` poll of
    `PIOCSTATUS` while the program runs.
- Run and debug settings (the program, its arguments, its working
  directory) belong to the workspace and are stored with ttrcode's
  workspace settings. Building first is ttrcode's later Build menu, not
  part of this.

## 9. Later: debugging another machine

A Target that forwards each call to a small server on the other machine:
for example ttrcode on Linux debugging a program on the Octane. Symbols
stay on the debugger's side, read from the same files over NFS, as the
`/files` share already allows. The Target interface is kept narrow (stop,
run, registers, memory, maps, events) so this is a matter of
serialising it. Nothing is built for it now.

## 10. Testing

- **td unit tests:**
  - Symbol reading on checked-in small binaries: a gcc `-g` x86-64
    program and a MIPSpro n32 one, with their expected line tables and
    unwind results.
  - `llvm-dwarfdump` or `dwarfdump` produce the expected values once.
- **ttrdb scripts** on small test programs (`td/test/progs/*.c`):
  breakpoints, stepping through recursion and across a shared library,
  a signal, threads, a crash and its core file.
  - Run on Linux.
  - Run on the IRIX guest (breakpoints and stepping) and on the Octane
    (watchpoints too).
- **ttrcode's debugger** is tried by hand on ttr's own programs.

## 11. To decide

1. **Names.** `td` for the library and `ttrdb` for the program?
2. **The platform rule.** ptrace is a Linux-only API outside
   `src/wayland/`. The proposal is per-system Target files, as `tp/`
   already has for ptys; AGENTS.md would say so.
3. **F11.** It is ttrcode's full screen today, and Step Into in Visual
   C++. Proposal: F11 steps, and full screen moves to Shift+Alt+Enter,
   Visual Studio's later key for full screen.
4. **zstd** for compressed debug sections on Linux: a `third_party`
   submodule, or leave such files unread for now. (Our own builds don't
   compress them.)
5. **Alt+F9.** VC6's Breakpoints dialog key is the window manager's
   minimize key in ttr. The dialog would be reached from the Debug menu
   only.

## 12. Milestones

1. **ttrdb on Linux x86-64, with lines and frames only.**
   - Start a program, breakpoints by line and function, continue.
   - Step by line and by instruction, backtrace, registers, memory.
   - Shared libraries.
   - Built from: the ELF reader, line tables, CFI, the ptrace Target,
     the x86-64 Arch.
2. **The same on IRIX.**
   - The /proc Target and the MIPS Arch, with delay slots.
   - Tested on the guest and on the Octane, with MIPSpro-built programs.
   - The *to verify* items of sections 3 to 6 get answered here.
3. **Values.**
   - DWARF types and locations, locals and arguments.
   - The expression evaluator.
   - Watchpoints (debug registers on Linux, /proc watchpoints on IRIX).
   - Core files.
4. **ttrcode.**
   - The keys and windows of section 8.2.
   - The Session in the Xt loop.
   - The program's output on a pty.
5. **Threads and polish.**
   - Threads on both systems.
   - Attaching to a running program.
   - Demangling for both ABIs.
   - Hover tips.
