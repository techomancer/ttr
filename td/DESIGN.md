# td: a debugger for ttr

Status: design draft, 2026-10-24. Section 11 lists the open questions.

Prototype, 2026-10-24: the core of milestones 1 and 2, on Linux x86-64
and on IRIX (the emulated guest, MIPSpro n32 programs):

- td: the ptrace Target (`td/target_linux.cpp`) and the /proc Target
  (`td/target_irix.cpp`), an ELF symbol reader, libdwarf's line and frame
  tables (`td/module.cpp`), breakpoints by function, line or address,
  continue, step and next by line, step by instruction, finish, and
  backtraces from the frame tables (through recursion, to `__start` on
  IRIX).
- libdwarf vendored and built with plain make on both systems.
- ttrdb with the commands of section 8.1 except `p`, `w`, `t`, `mods`
  and `detach`; `-x SCRIPT` runs a test script. `make td-test` runs the
  scripts in `td/test/` (breakpoints, stepping, a crash): all pass on
  both systems.
- Shared objects: the dynamic linker's list (Linux `r_debug`, IRIX rld's
  `Elf32_Obj_Info` list through `__rld_obj_head`), read at the entry
  point and after every stop; symbols, lines and unwinding in each;
  breakpoints that wait for their library ("pending"); stepping into a
  library function through its PLT entry or stub. IRIX's own libc.so.1
  has line tables (`puts.c:22`) and unwinds back into the program.
- On MIPS a function breakpoint goes after the prologue as dbx finds it
  (scanning the stack and gp setup), and on the second line row only
  when straight-line code reaches it: SGI's libraries are optimized.
- Sources: read where the line table says, else under directories the
  user adds (`dir`), trying the path's trailing parts (`td/source.h`).
- dlopen: an internal breakpoint where the dynamic linker reports
  changes: glibc's `r_debug.r_brk`, IRIX rld's `__dbx_link` (an empty
  function for debuggers; rld calls it once per dlopen with the new
  object already listed). rld is not on its own list and is loaded as a
  module from `/lib32/rld` (at its link address).
- On the Octane (IP30, real R10000-class hardware, 64-bit kernel): the
  same ttrdb, built n32 in the guest, passes every test with the n32
  test programs and with the same programs compiled `-64` there. A
  64-bit IRIX program needs no 64-bit debugger: /proc gives an n32
  caller 64-bit registers and 64-bit file offsets into the address space
  (its stack is at 0xfffffbfe70), and rld's list has an Elf64_Obj_Info
  layout. The ioctls with pointer-sized fields, such as `PIOCMAP` and
  `PIOCSWATCH`, need `PIOC_IRIX5_64` and the 64-bit structures
  (`PIOCSWATCH` does so, checked on the Octane). The guest, an IP22 with
  a 32-bit kernel, cannot run 64-bit programs.
- ttrdb -d DIR adds a source directory; the test runner passes the test
  sources, so programs compiled on one machine are tested on another.
- ttrcode (section 8.2), 2026-10-26: the Debug menu with VC6's keys
  (F11 is Step Into; full screen moved to Shift+Alt+Enter), breakpoints
  as red dots in the margin, the yellow arrow and line where it stopped,
  Call Stack (a green arrow for a chosen caller), Registers, Threads, a
  Program pane (a terminal of its own for the program's input and
  output), and the debugging layout. td waits through an idle function,
  so the window goes on and Debug > Break interrupts the program.
  Motif's F10 for the menu bar is moved out of the way.
- Values, 2026-10-27 (milestone 3's first part): DWARF types (base
  types, pointers, structs and unions with bit fields, enums, arrays of
  any rank, typedefs and qualifiers), each function's arguments and local
  variables by lexical block, globals, and their locations (DWARF
  expressions and location lists: `DW_OP_fbreg` against
  `DW_AT_frame_base`, `DW_OP_call_frame_cfa`, registers, stack values),
  read when first asked for (`td/info.h`). Values are formatted as
  Visual C++ shows them and open up into members, elements and what a
  pointer points to (`td/value.h`); a C expression evaluator handles
  watch expressions (`td/expr.cpp`, section 7). ttrdb has `p`, `locals`,
  `e`, `pt` and `fr`; `td/test/values.ttrdb` passes on Linux and on the
  Octane, with n32 and `-64` programs. MIPSpro writes DWARF 2:
  `DW_AT_frame_base` is `DW_OP_bregx 29+N` (sp plus the frame size),
  members are at `DW_OP_consts N`, bit fields count from the high bit,
  and `char` is unsigned.
- ttrcode's Watch, Variables (Auto and Locals, and the frame), Memory,
  Registers, Call Stack and QuickWatch (Shift+F9) show them, as trees
  with what changed since the last stop in red (section 8.2).
- Watchpoints and core files, 2026-10-28 (the rest of milestone 3):
  - Watchpoints on Linux use the debug registers (section 3.1), on IRIX
    /proc's watched areas (section 3.2). The Session stops only when the
    value changed, and one on a local ends when its frame returns
    (section 7). ttrdb's `w`; `td/test/watch.ttrdb`.
  - Core files are a third Target (`td/core.h`, section 3.4), for
    Linux's ELF cores and IRIX's own format, either read on any system.
    ttrdb's `-c`; `td/test/core.ttrdb`.
  - ttrcode: Debug > Breakpoints with data breakpoints, and Debug > Open
    Core File (section 8.2).
  - Both scripts pass on Linux and on the Octane, with n32 and `-64`
    programs. Under lightning (the guest): not run yet.
- Threads, attaching, C++ names and DataTips, 2026-10-29 (milestone 5):
  - Threads on Linux (section 3.1): every thread traced with
    `PTRACE_SEIZE`, all stopped when one stops (`PTRACE_INTERRUPT`) and
    run again together; a single step runs the current one. On IRIX
    (section 3.2): `PIOCTHREAD`, joint stop, IRIX's M:N pthreads. The
    Session's current thread (section 7); ttrdb's `t`; Linux core files'
    threads. `td/test/threads.ttrdb`, with a watchpoint every thread
    hits.
  - Attaching: every thread stopped; `td/test/attach.ttrdb` attaches to
    a running program and detaches, and the runner checks it runs on.
    ttrcode's Debug > Attach to Process (section 8.2).
  - C++ names (section 5.5): td's own demanglers for the Itanium ABI and
    MIPSpro's cfront scheme; breakpoints by those names; names from the
    DWARF where the symbol has none. `td/test/cpp.ttrdb`.
  - ttrcode: the Threads pane switches threads; DataTips (section 8.2).
  - All 11 scripts pass on Linux and on the Octane (n32).
- Disassembly, 2026-10-29 (section 6.1):
  - x86-64 through Zydis, MIPS by td's own decoder; addresses named by
    the symbols, PLT entries (`printf@plt`) and, on MIPS, the global
    offset table words that `jalr` calls go through.
  - ttrdb's `di` and `ni`; ttrcode's Disassembly window (section 8.2).
    `td/test/disasm.ttrdb`.
  - Linux: a breakpoint SIGTRAP left queued behind `PTRACE_INTERRUPT`
    (section 3.1) killed a program after detaching; fixed.
  - All 12 scripts pass on Linux and on the Octane, with n32 and `-64`
    programs.
- Not yet: changing values, C++
  `this` and members by inheritance, thread-local variables, IRIX's
  pthreads that have no kernel thread (section 3.2), IRIX core files'
  other threads.

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
| Start a program | `fork`; the child waits on a pipe while the parent `PTRACE_SEIZE`s it (with `PTRACE_O_TRACEEXEC`), then `exec`s: the exec's stop is at its first instruction. Seized, not `PTRACE_TRACEME`: `PTRACE_INTERRUPT` needs it, and new threads are seized too. |
| Attach | `PTRACE_SEIZE`, then `PTRACE_INTERRUPT` to stop it. `PTRACE_SEIZE` does not send a SIGSTOP, unlike `PTRACE_ATTACH`. Every thread in `/proc/<pid>/task`, read again until no new one appears (one may start another before it is traced). |
| Events | `waitpid(tid, ..., __WALL \| WNOHANG)` for each known thread, and `waitid(P_ALL, ..., WNOWAIT)` to block or to see a new thread before its creator says so: never `waitpid(-1)`, which would reap the debugger's other children (ttrcode's terminals). Options `PTRACE_O_TRACECLONE`, `TRACEEXIT` and `EXITKILL`. `PTRACE_GETSIGINFO` says why a SIGTRAP came. |
| Resume | `PTRACE_CONT`, with or without the pending signal. |
| Single step | `PTRACE_SINGLESTEP` (the x86 trap flag). |
| Registers | `PTRACE_GETREGS`/`SETREGS`; `PTRACE_GETREGSET` with `NT_PRFPREG` and `NT_X86_XSTATE` for floating point and vector registers. |
| Memory | `pread`/`pwrite` on `/proc/<pid>/mem`, any size; `PTRACE_POKEDATA` also writes to read-only text. |
| Breakpoints | `int3` (0xCC) written over the first byte of the instruction. |
| Watchpoints | Debug registers DR0 to DR3 and DR7, written with `PTRACE_POKEUSER` at `offsetof(struct user, u_debugreg)`. Four, each 1, 2, 4 or 8 bytes aligned to its size: an area takes as many as its aligned pieces need (32 bytes at most). The trap comes after the write, as a SIGTRAP; DR6 bits 0 to 3 say which register fired (also when it was a single step that wrote), and td clears DR6. |
| Threads | Every thread is traced on its own. New ones arrive through `PTRACE_O_TRACECLONE` and stop first with `PTRACE_EVENT_STOP`. When one stops, `PTRACE_INTERRUPT` stops the others; one that stopped for something of its own meanwhile keeps it for later, except a breakpoint: its pc goes back to the int3, which it reaches again if the breakpoint is still there. An interrupt left over from that is a `PTRACE_EVENT_STOP` nobody asked for: run on. A thread can reach an int3 just as the interrupt stops it: it reports the interrupt, its SIGTRAP still queued (`SigPnd` in `/proc/<tid>/status`), its pc past the int3; td lets it take the signal at once and treats it as any breakpoint met while stopping (left queued, it would come after the breakpoint is gone, or kill the program once detached). Debug registers are each thread's: all get the same, new ones when they start. A thread that ends while being stepped lets the others run. |
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
| Wait for a stop | `PIOCSTATUS` until `PR_ISTOP` (and every thread stopped), with short naps; and `waitpid(WNOHANG)` for our own child. Not the process's `PIOCWSTOP`: with threads it can go on waiting after the process has ended (checked on the Octane). A single step waits on its thread's `PIOCWSTOP`. |
| Why it stopped | `PIOCSTATUS`: `pr_why` and `pr_what` (a signal, a fault, a system call). |
| Resume | `PIOCRUN` with a `prrun_t`. Its flags: `PRCSIG` (clear the signal), `PRCFAULT` (clear the fault), `PRSTRACE`/`PRSFAULT` (change the traced sets), `PRSTEP` (single step), `PRSABORT`, `PRSTOP`. |
| Events | `PIOCSFAULT` with a fault set: `FLTBPT` (breakpoint), `FLTTRACE` (step), `FLTWATCH` (watchpoint), `FLTILL`, `FLTACCESS` and the rest from `<sys/fault.h>`. `PIOCSTRACE` for signals. `PIOCSENTRY`/`PIOCSEXIT` for system calls. `PIOCSFORK` makes children inherit the tracing. |
| Registers | `PIOCGREG`/`PIOCSREG` (`gregset_t`), `PIOCGFPREG`/`PIOCSFPREG`. |
| Memory | `lseek` and `read`/`write` on the same file. `PIOCREAD` takes a 64-bit offset. |
| Breakpoints | The MIPS `break` instruction, reported as `FLTBPT`. |
| Watchpoints | `PIOCNWATCH`, `PIOCGWATCH`, `PIOCSWATCH` with a `prwatch_t` (address, size, `MA_READ`/`MA_WRITE`/`MA_EXEC`), reported as `FLTWATCH`. |
| Threads | `PIOCTHREAD` with a `prthreadctl_t` runs an ioctl on one thread (`PIOCSTATUS`, `PIOCGREG`, `PIOCSREG`, `PIOCRUN`, `PIOCWSTOP`, `PIOCGHOLD`...). Walking them: `PIOCSTATUS` with `PTFD_GEQ` from id 0, then `PTFD_GTR` from each `pr_who`, until ENOENT (`PIOCGUTID` answers EINVAL however asked). A `sproc` member is a process of its own. Section 3.2's notes. |
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
- **Breakpoints are always patched `break` instructions.** IRIX's /proc
  has no hardware breakpoint call, and the R4000 to R10000 have no
  instruction breakpoint registers (their one WatchLo/WatchHi pair
  watches data loads and stores). The text page is copied when /proc
  writes to it, so other processes running the same program are not
  affected.
- **Watchpoints are emulated by the kernel**, by page protection. From
  proc(4), and checked on the Octane:
  - Any number of areas (up to `PIOCNWATCH`), each any size in bytes, but
    they must not overlap (EINVAL). A `prwatch_t` with size 0 removes
    the area at its address.
  - `MA_READ`, `MA_WRITE` and `MA_EXEC`: execution too, which the watch
    registers cannot do. td watches writes only.
  - A write is caught before memory changes, as `FLTWATCH`; the pc is the
    instruction, and `pr_info.si_addr` the address written (32 bits wide
    for an n32 debugger, also with a 64-bit program).
  - **Resuming does not get past it** while the area is watched: a step
    or a run with the fault cleared faults again on the same instruction,
    and memory does not change (checked on the Octane). td does what it
    does for breakpoints: it removes the areas, steps the instruction,
    sets them again, and reports the stop after the write, as on Linux.
  - Deleting a watchpoint on a running process can fail with EBUSY "when
    the kernel is stepping over a watchpoint" (when the process is not
    traced for `FLTWATCH`); td retries.
  - Accesses through /proc do not trigger them; accesses by system calls
    are reported after the call (`FLTKWATCH`).

  So they work on any machine that handles ordinary page faults,
  lightning included (*to verify*). The cost: every access to a watched
  page traps, so a watched local variable slows the program a lot.
- **Threads** (checked on the Octane):
  - `PIOCSET` with `PR_JOINTSTOP | PR_JOINTPOLL`: when one thread stops
    for an event, the others stop too (`pr_why` `PR_REQUESTED`), and the
    process's `PIOCSTATUS` says stopped only once all have. The process's
    `PIOCWSTOP` names a stand-in thread (`pr_who`), not the one that
    stopped: td walks the threads for the one with a fault or a signal.
  - The process's `PIOCRUN` runs them all; `PIOCTHREAD` with `PIOCRUN`
    and `PRSTEP` steps one while the others stay stopped, and its
    `PIOCWSTOP` waits for it. `PIOCRUN` on each thread instead does not
    run them (the joint stop takes them back).
  - libpthread is M:N: it runs pthreads on kernel threads (uthreads,
    ids from 0), as many as it sees fit. Pthreads that block in the
    kernel (`usleep`, I/O) keep one each; ones that compute share them,
    switched by its signals `SIGPTINTR` (47) and `SIGPTRESCHED` (48).
    So td's threads on IRIX are the kernel's: a pthread waiting for one
    is not listed, and a pthread may come back on another. td does not
    trace those two signals, holds them during a single step (else the
    step lands in their handler), and `run_to` also accepts its frame
    in another thread (section 7). Listing pthreads would need
    libpthread's own table (`__pt_lib_info`, what dbx reads; not
    documented).
  - A store-conditional on a watched page (a `pthread_mutex_lock` next
    to a watched variable) cannot be stepped: the trap between its `ll`
    and `sc` clears the link and the `sc` fails, so the lock retries
    forever; untraced, the kernel sends SIGTRAP instead. The Octane
    reports it as a SIGTRAP at the `sc`. td puts the thread back at its
    `ll` (a few instructions before, the same base and offset) and runs
    it alone, the watched areas out, to a break after the `sc`: untrapped,
    so atomic as the program meant; again if the `sc` failed. Alone it may
    spin on a lock a stopped thread holds: after 50 ms it is stopped
    where it is (a consistent place: starting at the `ll` is what a failed
    `sc` does) and all run on. Reported as a watched write. (Making the
    store for it instead broke locks: another thread's store could have
    come between its `ll` and `sc`.)
  - `PR_KLC` (kill on last close) for a program td starts, `PR_RLC` (run
    on last close) for one it attached to: if the debugger dies, the
    first ends and the second runs on.
- **The ABIs.** Our builds are n32, and an n32 debugger debugs 64-bit
  programs (checked on the Octane, as dbx, itself n32, does): registers
  come 64 bits wide to n32 callers, memory is read at 64-bit offsets, and
  ioctls whose structures hold pointers are or'ed with `PIOC_IRIX5_64`
  and given the 64-bit layout (proc(4)). o32 programs: not tried.

### 3.3 Under lightning

Breakpoints and stepping by breakpoints need nothing but the `break`
instruction, which lightning runs like any other instruction. Lightning
leaves out architectural watchpoints for speed, so a watchpoint that
needs the watch registers will not fire there. Page-protection
watchpoints (see above) may still work. Watching memory gets tested on
the Octane in any case.

### 3.4 Core files

A core file is a Target too (`td/core.h`): registers and memory,
read-only, and `resume` fails. Neither system's core files hold the
program's code (at most its first page), so memory the core does not
have is read from the executable's and the shared objects' files, at
their load addresses. The shared objects are found as in a live process
(section 4): the dynamic linker's list is in the core's data. Both
formats are plain file reading, so either is read on either system
(IRIX core files on Linux, for one).

**Linux** writes an ELF file of type `ET_CORE`. Its `PT_LOAD` segments
are memory (`p_filesz` 0 for what was left out, such as code), and a
`PT_NOTE` segment has:

- `NT_PRSTATUS`, one per thread, the crashing one first: the signal
  (`pr_cursig` at 12), the pid (32) and the registers
  (`user_regs_struct` at 112, x86-64);
- `NT_PRPSINFO`: the program's name (`pr_fname`, 16 bytes at 40);
- `NT_AUXV`: `AT_ENTRY` gives a position-independent executable's bias;
- `NT_FILE`: the mapped files with their address ranges, so the core
  names its program (the file where `AT_ENTRY` is).

Whether a core is written at all depends on `ulimit -c` and
`/proc/sys/kernel/core_pattern` (a pipe to systemd-coredump on many
systems: then `coredumpctl dump` gives the file).

**IRIX** writes a format of its own (`<core.out.h>`), not ELF:

- `struct coreout`: a magic number (`0xbabec0bb` n32, `0xdeadad40`
  64-bit, `0xdeadadb0` o32, not read), the offset and number of the
  vmaps, the program's name (`c_name`, 80 bytes, no path), the signal
  (`c_sigcause`) and 32 descriptors (offset, length, flags; flag 1 is
  valid).
- The vmaps: `struct vmap` (16 bytes, 32-bit fields) for n32, `struct
  vmap64` (32 bytes) for 64-bit programs: address, length, file offset,
  flags and type. `VDUMPED` ones are in the file; text is `VPARTIAL`,
  its first page only.
- Descriptor 0 (`I_GPREGS`) is the 32 general registers, 8 bytes each
  for both ABIs; descriptor 2 (`I_SPECREGS`) starts with EPC, then
  CAUSE, BADVADDR, MDHI, MDLO, the FP status. Descriptor 5 holds the
  other threads' (not read yet).

The program has to be named for an IRIX core; td checks that its name is
the one the core gives (Linux's is cut to 15 characters).

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

Checked on 6.5.22: programs export `__rld_obj_head` (the dynamic
section's `DT_MIPS_RLD_MAP` points to it), whose list is of
`Elf32_Obj_Info` records (`<objlist.h>`: path, where the ELF header is
and where it was linked, so the bias), and rld calls `__dbx_link` once
per dlopen. Failing that, `PIOCMAP` and `PIOCOPENM` list every mapped
object without rld's help.

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

### 5.4 libdwarf

td reads DWARF through the current libdwarf
(https://github.com/davea42/libdwarf-code, LGPL 2.1, about 62,000 lines
of C in 64 files), vendored in `third_party/libdwarf` as a pinned
submodule and built the same way on Linux and IRIX. Decided 2026-10-24.

IRIX also ships SGI's own 2001 libdwarf (`/usr/lib32/libdwarf.a`,
`libdwarf_sgi.a`, `libelf.a`, headers in `/usr/include`). td does not
use it:

- It reads only DWARF 2.
- Its API differs: compared against commit 71f0401, the calls for opening
  a file, walking compilation units, siblings, line tables, address
  ranges, register rules and location lists were all renamed with more
  arguments since, and the old names are gone. Using it would mean a shim
  and a second code path to test.
- It lacks 25 years of fixes for malformed input, which a debugger meets.
- We cannot ship its source (below).

It stays useful as a cross-check: SGI's `dwarfdump` and dbx on IRIX
confirm that td reads MIPSpro's output correctly.

About the vendored libdwarf:

- **It builds with MIPSpro.** Tried on 2026-10-24 with commit 71f0401
  (`cc -c99 -n32`, a hand-written `config.h` with `WORDS_BIGENDIAN`):
  62 of the 64 files compile. The other two, `dwarf_line.c` and
  `dwarf_print_lines.c`, fail on one line: an `#ifdef __sgi` block in
  `dwarf_line_table_reader_common.h` sets a field, `li_offset`, that the
  line structure no longer has. That code went stale because nobody
  builds it on IRIX any more. The fix is one line: report it upstream and
  pin a commit with the fix. Until then the wrapper file can `#undef
  __sgi` around that one source file (the system headers it needs are
  already included by then).
- **It reads all of it:**
  - DWARF 2 to 5.
  - SGI's `.debug_funcnames`, `.debug_varnames` and `.debug_typenames`
    (`dwarf_get_funcs`, `dwarf_get_vars`, `dwarf_get_types`).
  - Line tables (`dwarf_srclines_b`).
  - Location lists (`dwarf_get_loclist_c`).
  - Frame tables: the FDE for a PC (`dwarf_get_fde_at_pc`) and every
    register's rule there (`dwarf_get_fde_info_for_all_regs3`).
  - zlib-compressed sections (zlib is already in `third_party/`), and
    zstd ones when built with zstd.
- **`dwarf_object_init_b`** lets us supply the object's sections
  ourselves: from a file, a core file, or another machine's memory.
- **Its API is C.** td wraps it, and nothing above td sees it.

Licensing: ttr is BSD-3, and the LGPL 2.1 allows linking libdwarf into
it. What section 6 of the LGPL asks of a binary we distribute with
libdwarf in it (not legal advice; this is the plan):

- **The source of the exact libdwarf version** has to be available. Our
  tardists and packages come from the GitHub repository's releases, and
  section 6(d) is met by offering the source "from the same place". The
  catch: GitHub's automatic "Source code" archives leave out submodules.
  So each release also carries a source archive that includes
  `third_party/` (`git archive` plus the submodules), and the release's
  tag pins the exact libdwarf commit.
- **The user must be able to relink with a changed libdwarf.** ttr's own
  source is public, which allows it either way. A shared `libdwarf.so`
  makes it easy as well.
- **A prominent notice that libdwarf is used and is under the LGPL, and
  a copy of the LGPL.** The tardist and `make install` put the LGPL text
  next to ttr's licence (`/usr/share/ttr/licenses/`). README's License
  section lists libdwarf and the other third-party code with their
  licences.
- **Programs that show copyright notices show libdwarf's too.**
  ttrcode's About dialog lists it with its licence and the repository
  address, and so does `ttrdb --version`.

SGI's libdwarf was LGPL 2.1 too: the guest's `/usr/include/libdwarf.h`
says "Copyright (C) 2000, 2001, 2002 Silicon Graphics, Inc." under the
LGPL 2.1, and SGI published the source on oss.sgi.com. That site is gone,
and the libdwarf repository's oldest release tag is from 2011, so the
source of the exact version in `/usr/lib32/libdwarf.a` is not at hand,
another reason not to link it into anything we ship.

(Current libdwarf's files carry SGI copyrights from 2000 to 2010. Three
files, `dwarf.h`, `libdwarf.h` and `dwarf_form.c`, also say "Copyright
2007-2010 Sun Microsystems": contributions, not joint ownership, as far
as the source shows.)

*Pending:* libdwarf's policy on taking the `__sgi` fix and, more
generally, IRIX build fixes upstream.

`dwarfdump` (GPL, in the same repository) is useful for checking our
results but is not linked into anything.

### 5.5 Names

C++ names are demangled for display and for breakpoints by name
(`td/demangle.h`), when a module loads: each symbol keeps its mangled
name and gets one as written. Both schemes are read on either system
(cores, cross debugging later); cfront's is tried in MIPS files only, as
C names may hold "__".

- **Itanium C++ ABI** (GCC and Clang): td's own demangler, as
  `abi::__cxa_demangle` would bring libstdc++ in. Types as small trees
  printed left and right of the declarator (`void (*)(int)`, `char const
  (&) [64]`), the substitution table, template parameters looked up
  where they are printed, packs and their expansions, reference
  collapsing, lambdas, ABI tags, GCC's clones. Its output is c++filt's,
  except `std::string` for `Ss` (gdb's; c++filt's long form with
  `TD_DEMANGLE_VERBOSE`). Checked against c++filt: libstdc++'s 5,973
  names all equal; libLLVM's 45,771 all but 199 (76 left mangled:
  decltype and expressions; the rest lambdas within template arguments,
  where c++filt and LLVM's demangler scope template parameters in ways
  td does not copy). Bad input fails: types 200 deep, texts 64 KB, no
  exponential output; fuzzed under AddressSanitizer.
- **MIPSpro** (EDG writing cfront's scheme): `get__1SCGv` is `S::get()
  const`. Classes and namespaces `<length><name>` or `Q<n>_...`; `G`
  where cfront had `F` for functions; templates `name__pt__<n>_<args>`
  with values as `X<type>L<digits' count><digits>`; function templates
  `Z<n>Z` for their parameters and `_<return type>`; `T<n>` and
  `N<count><n>` repeat parameters. Written as IRIX's tools do (`const
  S&`). IRIX's own `demangle()` (`<dem.h>`) is in libC.so and libexc.so
  but not exported (neither links nor `dlsym` finds it), so td has its
  own, checked by hand on a sample of every construct and on libC's
  names.
- MIPSpro emits inline member functions as local symbols with plain
  names (`area`, size 0) and its DWARF has no namespaces: td names such
  functions from the DWARF (the class they are in, or the declaration a
  definition completes; also where no symbol covers the address), and a
  breakpoint on `geo::Rect::area` finds `Rect::area`, the name it then
  shows. Size-0 functions run to the next symbol.
- Breakpoints by name: the mangled name, the name as written with or
  without its parameters (`geo::scale(int, int)`, `geo::scale`), after
  a template's return type and with or without its arguments (`twice`,
  `twice<int>`), or without its classes and namespaces (`area`).

### 5.6 Unwinding

Unwinding a stopped thread is a loop:

1. Take the frame's PC.
2. Ask libdwarf for the rules at that PC (`.debug_frame`, or `.eh_frame`
   on Linux).
3. Compute the frame's address (the CFA), and read the saved registers
   through the Target.
4. The return address gives the next frame's PC. Repeat.

That loop is a few hundred lines in td, the same on both systems.

td has an Unwinder interface with two implementations, chosen when td is
built:

- **libunwind** (https://github.com/libunwind/libunwind, MIT), on Linux,
  when the system has it: `libunwind-ptrace` unwinds another process
  through ptrace, and `libunwind-coredump` a core file. The Makefile
  looks for `libunwind-ptrace.h` and `pkg-config libunwind-ptrace`
  (`TD_UNWIND=auto`, the default; `TD_UNWIND=own` turns it off). It is
  not vendored: it is used when present and missed when not.
- **td's own loop** above: on IRIX always, and on Linux when libunwind is
  missing or `TD_UNWIND=own`. It has to work on its own on both systems,
  so the tests run it on Linux too, against libunwind's answers where
  both exist.

What the own loop needs besides libdwarf:

- **No CFI for a function** (hand-written assembly, some IRIX system
  libraries): on MIPS, scan back for the prologue (`addiu sp, sp, -n`,
  `sw ra, n(sp)`), as dbx does. On x86-64, follow the frame pointer.
- **Signal frames:** IRIX's `sigcontext` and Linux's `rt_sigframe`, which
  `.eh_frame` describes for the signal trampoline. *To verify* on IRIX.

Why libunwind does not cover IRIX: it supports Linux, FreeBSD, QNX,
Solaris and HP-UX. Its MIPS port is for Linux, and 26 of its files use
C11 atomics or `__sync` builtins, which MIPSpro does not have. *Pending:*
asking libunwind's maintainers about an IRIX port. If one appears, IRIX
can use it the same way.

Not an option: **IRIX's libexc** (`trace_back_stack`, `exc_dladdr`, in
`<libexc.h>`) unwinds only the calling program's own stack. It is useful
for crash reports in ttr's own programs, not for a debugger.

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
that step: they stay stopped (decided: a single step runs one thread on
both systems).

### 6.1 Disassembly

`td/disasm.h`: one instruction at an address from its bytes, as a
mnemonic and operands, with its length, its kind (jump, branch, call,
return), its target, and MIPS's delay slot. The Session reads the
program's memory (breakpoints are out of it while it is stopped), names
addresses through the symbols, and steps over a call by instruction
(`next_instruction`: to the return address, past MIPS's delay slot).

- **x86-64: Zydis** (`third_party/zydis` at v4.1.1 and its
  `third_party/zycore`, MIT), compiled through `td/third/zydis*.c`
  without its encoder and Xeon Phi tables, about 450 KB of code. Chosen
  2026-10-29 over Capstone v4 (frozen, its x86 tables from an old LLVM:
  part of AVX-512 only), udis86 (2014, no AVX-512) and diStorm (2021, no
  AVX-512): glibc runs AVX-512 versions of `memcpy` and `strlen` where
  the processor has them. Zydis is C99, which the rest of td is not, so
  it is built only where x86-64 programs run; IRIX links
  `td/disasm_nox86.cpp` (`TD_X86`). Intel syntax as Visual C++ wrote it:
  `mov dword ptr [rbp-0x4], eax`, `je`, `ja` (Zydis says `jz`, `jnbe`).
  Checked against GNU objdump: the same instruction lengths for all of
  libc (387,410 instructions) and libLLVM 19 (21.5 million); 21 of
  libcrypto's 880,454 differ, redundant REX prefixes in data in `.text`.
- **MIPS: td's own** (`td/disasm_mips.cpp`), MIPS I to IV with the
  floating point unit and COP1X, about 600 lines, written as IRIX's
  `dis` does (`lw a0,16(sp)`, with the short forms `move`, `li`, `b`,
  `beqz`, `neg`). Checked against llvm-objdump: the same text for IRIX
  6.5's n32 and 64-bit `libc` and mips4 `libm`, `libGLU` and a
  MIPSpro-built ttrcode, 780,000 instructions in all.
- **Names**: `call square (0x401136)`, `jg fact+0x1c (0x40118c)`,
  `printf@plt` for the procedure linkage table's entries (x86-64:
  `.rela.plt` with `.plt` or `.plt.sec`; they are names only, so stepping
  into a library still goes through them as code without lines). MIPS
  code calls through the global offset table (`lw t9,-32740(gp)` then
  `jalr t9`): with the frame's gp, td reads the word and names it.

## 7. Session

- **The stop-run loop:** resume, wait for an event, classify it. The
  event is a breakpoint, a finished step, a signal, a new or finished
  thread, a library change, exec, or exit. The Session then either
  answers it internally (a conditional breakpoint whose condition is
  false, a library event) or reports a stop to the user interface.
- **All threads stop together** when one stops, Visual C++ style, and
  run together; a single step runs only the current thread. The current
  thread (the stack, the values, the steps) is the one that stopped,
  until `select_thread()`. Stepping off a breakpoint steps the thread
  that stopped at it (and the current one, if it is at one too): other
  threads that reached one while being stopped were moved back to it.
  `run_to` (step over, step out) waits for its thread: a temporary
  breakpoint another thread reaches is passed, unless its stack pointer
  says it is the same frame (IRIX's pthreads change kernel threads). A
  watched local belongs to its thread's frame. `thread_number()` gives
  threads small numbers, 1 for the first.
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
- **Expressions:** a small C and C++ expression parser and evaluator
  (`td/expr.cpp`). Variables (the frame's, then globals, this module's
  first), enumerators, functions, members (`.` and `->`), indexing, `*`
  and `&`, casts to C's base types and to known struct, union, enum and
  typedef names, arithmetic with pointers, comparison, `&&`, `||`,
  `?:`, `sizeof`, registers as `$pc`, `$sp` or `@rip`, and VC6's format
  suffixes (`,x`, `,d`, `,N`). No function calls and no assignments.
- **Values** (`td/value.h`): where a value is (memory, a register, or
  computed), its type, a one-line text and its children. Locations come
  from the DWARF of the frame's function at its pc (a caller's at the
  call); a register a caller's frame did not save is "not available".
- **Watchpoints** (`td/watch.cpp`): an expression evaluated once to the
  bytes it is in (a bit field: the bytes holding its bits; 256 bytes at
  most), handed to the Target. A watched write that leaves the value as
  it was (the same bytes, or a neighbouring bit field) is no stop: the
  run goes on, and a single step ends as a step. One whose expression
  used a frame's variable (not a function's `static`) belongs to that
  frame: an internal breakpoint where the frame returns, and at every
  stop the stack pointer compared with the caller's, end it with a stop
  of its own (`EV_WATCH_ENDED`), before the stack is used for something
  else. The stop says which watchpoint, and its old and new values.
- **Core files** (`Session::open_core`): the same calls on a core Target
  (section 3.4); those that would run the program return
  `EV_ERROR`.
- **Events reach the user interface through a callback interface**, not
  by blocking. ttrcode runs one Xt event loop, so the Session must never
  block (section 8.2).

## 8. The user interfaces

### 8.1 ttrdb

A line-oriented program for trying td and for tests, scriptable like the
ttrc tests (`tl` reads its screen, or plain text on a pipe):

```
ttrdb ./prog args          ttrdb -p PID          ttrdb -c core [./prog]
```

| Command | Does |
|---|---|
| `r` | run (start) |
| `c` | continue |
| `s`, `n`, `f` | step into, step over, step out (finish) |
| `si`, `ni` | step one instruction, over calls |
| `b file:line`, `b func`, `b *addr` | set a breakpoint; `b` lists them; `d N` deletes one |
| `bt`, `up`, `down`, `frame N` | the call stack (`fr N` chooses the frame values come from) |
| `p expr` | print a value (`expr,x` in hex, `p,10` ten elements) |
| `locals` | the frame's arguments and variables in scope |
| `e expr` | a value's members, elements or what it points to |
| `pt expr` | its type, and a struct's members |
| `l` | list the source around the current line |
| `x addr [n]` | dump memory |
| `di`, `di func`, `di addr [n]` | disassemble the frame's function (`=>` at the pc), another one, or n instructions |
| `regs` | the registers |
| `t`, `t N` | list threads, switch to one |
| `mods` | the loaded objects and whether they have debug information |
| `w expr` | stop when its value changes (a watchpoint; `d N` deletes it) |
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
  dots for breakpoints. **DataTips:** resting the mouse on a variable for
  half a second shows `name = value` in a pale yellow window, evaluated
  in the frame shown: the word under the mouse with the members it is
  reached through (`p->next->val` over `val`), or the selection when the
  mouse is on it; not for keywords, numbers or functions called. Moving
  off it, a key, a click or a run hides it (`src/ttrcode/datatip.cpp`).
- **Variables** shows locals and "auto" values (the variables of the
  current line and the previous one), as VC6 did, for the frame chosen
  in its Context menu.
- **Watch** lists expressions, which can be edited in place. Structures
  and arrays open as a tree.
- These windows are CellLists (`src/ttrcode/celllist.h`): rows in
  columns on a TwGrid, the editor's character grid, rather than
  XmLists, for columns that line up, a tree's + and -, single values in
  red and editing in place. What is open and what each value was at the
  last stop are kept by expression (`src/ttrcode/valuetree.h`), so a
  step keeps the tree as it was and shows what changed.
- **Call Stack**: double-clicking a frame shows its source and variables.
- **Registers, Memory**: changed values are shown in red.
- **Disassembly** (`src/ttrcode/disassembly.cpp`): the function of the
  frame Call Stack shows, each source line followed by its instructions
  (VC6's Source Annotation), the yellow arrow at the current instruction,
  the green one where a caller's call returns, red dots at breakpoints
  (any breakpoint at that address: a line's, a function's, its own).
  With it focused, F11 and F10 step one instruction (F10 over calls),
  Ctrl+F10 runs to the selected instruction and F9 sets or clears a
  breakpoint there (`*0x401020`, kept with the others); a command given
  there leaves the focus there. A stop where the frame shown has no
  source opens it (VC6 offered to).
- **Output**: the program's own output (it runs on a pty, like the
  terminal pane), and debugger messages.
- **Threads**: a pane (VC6 had a dialog), every thread with its number,
  its id and where it is, the current one with the yellow arrow.
  Double-click switches to it (VC6's Switch To): its stack, values and
  registers; one waiting in the system's code shows its first frame with
  source. Registers in red compare with that thread's last stop only.
  **Modules** is a dialog, as in VC6 (not yet).
- **Attach to Process** (Debug menu; VC6's Build > Start Debug): the
  user's processes, newest last (`Target::processes()`: Linux's `/proc`,
  IRIX's `/proc/pinfo` with `PIOCPSINFO`); Attach stops one and shows
  where it is, its first frame with source. Stop Debugging detaches it
  (VC6 killed it), and the status bar says it runs on.
- **QuickWatch** (Shift+F9): an expression and its tree in a dialog,
  with Add Watch.
- **Breakpoints** (Debug > Breakpoints; VC6's Edit > Breakpoints,
  Alt+F9, a key the window manager has): every breakpoint in a list, one
  page instead of VC6's tabs. "Break at" takes `file:line` or a function,
  "Break when this expression changes" a data breakpoint (td's
  watchpoints). Data breakpoints are kept in the settings with the
  breakpoints and set when debugging starts; one that cannot be set then
  (a local) says why in the list. One on a local is set while its frame
  is there and leaves the list when the frame returns. A data
  breakpoint's stop says in the status bar what changed, from what to
  what (VC6 showed a message box).
- **Open Core File** (Debug menu): a core file in the same windows, with
  F5 and the steps refused. The program is the one the core names
  (Linux), else Debug > Settings' program.

How this fits ttrcode:

- The debugger windows are tool panes in ttrcode's dock areas
  (ttrcode/DESIGN.md 4.1), in a debugging layout of their own.
- The Session runs inside ttrcode's single Xt event loop: while the
  program runs, td polls it (`Target::poll`: `waitpid` with `WNOHANG` on
  Linux, `PIOCSTATUS` on IRIX) between 20 ms of Xt events (ttrcode's
  idle function), so the window goes on and Debug > Break works.
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
  - libdwarf's `dwarfdump` or `llvm-dwarfdump` produce the expected
    values once.
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
3. **F11.** Decided 2026-10-26: F11 steps into, as in Visual C++, and
   full screen moved to Shift+Alt+Enter.
4. **libdwarf** as a `third_party` submodule (section 5.4), built as a
   shared library for the LGPL (decided: the vendored one on both systems).
   Pending: libdwarf's upstreaming policy. And **zstd** for compressed
   debug sections on Linux: a submodule too, or leave such files unread
   for now (our own builds don't compress them).
5. **Alt+F9.** Decided 2026-10-28: VC6's Breakpoints dialog key is the
   window manager's minimize key in ttr, so the dialog is reached from
   the Debug menu only (Debug > Breakpoints).
6. **libunwind on IRIX:** pending a question to its maintainers. Until
   then IRIX uses td's own unwinder (section 5.6).

## 12. Milestones

1. **ttrdb on Linux x86-64, with lines and frames only.**
   - Start a program, breakpoints by line and function, continue.
   - Step by line and by instruction, backtrace, registers, memory.
   - Shared libraries.
   - Built from: libdwarf (vendored), the ptrace Target, the x86-64
     Arch, and both unwinders (libunwind when present, td's own loop).
2. **The same on IRIX.**
   - The /proc Target and the MIPS Arch, with delay slots.
   - Tested on the guest and on the Octane, with MIPSpro-built programs.
   - The *to verify* items of sections 3 to 6 get answered here.
3. **Values.**
   - DWARF types and locations, locals and arguments. *Done.*
   - The expression evaluator. *Done.*
   - Watchpoints (debug registers on Linux, /proc watchpoints on IRIX).
     *Done.*
   - Core files. *Done* (IRIX's other threads in them: with threads).
4. **ttrcode.**
   - The keys and windows of section 8.2.
   - The Session in the Xt loop.
   - The program's output on a pty.
5. **Threads and polish.**
   - Threads on both systems. *Done* (IRIX: kernel threads, section 3.2).
   - Attaching to a running program. *Done.*
   - Demangling for both ABIs. *Done.*
   - Hover tips. *Done* (DataTips).
6. **Disassembly.** *Done* (section 6.1): Zydis for x86-64, td's own
   MIPS decoder, ttrdb's `di` and `ni`, ttrcode's Disassembly window.
