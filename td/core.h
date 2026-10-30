/*
 * td/core.h - a core file as a Target (td/DESIGN.md section 3.4): the
 * registers and memory a crashed program left, read-only. Two formats,
 * either read on any system: Linux's ELF core files, and IRIX's own
 * (<core.out.h>: a struct coreout, its vmaps and register descriptors).
 *
 * Neither holds the program's code, only its first page at most: code is
 * read from the executable's and shared objects' files (add_image).
 */
#ifndef TD_CORE_H
#define TD_CORE_H

#include "elf.h"
#include "target.h"
#include "td.h"

#include "core/str.h"
#include "core/vector.h"

namespace td {

class CoreTarget : public Target {
public:
    /* 0 with the reason in err if it is not a core file td reads. */
    static CoreTarget* open(const char* path, ttr::Str* err);
    ~CoreTarget();

    /* The program's file: the user's, or from the core (Linux's list of
     * mapped files) when not given. */
    void set_exe(const char* path) { exe_ = path; }
    /* A loaded object's file, for the memory the core leaves out. */
    void add_image(const ElfFile* f, Addr bias);
    /* The signal that killed it, and its name as the core gives it. */
    int signal() const { return sig_; }
    const char* program_name() const { return name_.c_str(); }

    /* Target. */
    bool launch(char* const*, const LaunchOptions&, ttr::Str* err);
    bool attach(int, ttr::Str* err);
    const Arch* arch() const { return arch_; }
    int pid() const { return pid_; }
    ttr::Str exe_path();
    Addr exe_bias(Addr elf_entry);
    bool shared_objects(Addr dynamic, ttr::Vector<SharedObject>* out);
    bool loader_hook(Addr, SharedObject*, Addr*, ttr::Str*) { return false; }
    bool resume(bool, int) { return false; }
    bool wait(Event* ev);
    bool poll(Event* ev, bool* got);
    bool interrupt() { return false; }
    void kill() {}
    void detach() {}
    bool get_regs(Regs* r);
    bool set_pc(Addr) { return false; }
    bool read(Addr a, void* buf, size_t n);
    bool write(Addr, const void*, size_t) { return false; }
    int add_watch(Addr, size_t, ttr::Str* err);
    void remove_watch(int) {}

private:
    CoreTarget();
    bool read_file(uint64_t off, void* buf, size_t n);
    bool open_elf(const unsigned char* h, ttr::Str* err);
    bool open_irix(const unsigned char* h, ttr::Str* err);
    void elf_notes(uint64_t off, uint64_t size);

    struct Segment {
        Addr vaddr;
        uint64_t size;      /* what the file holds */
        uint64_t offset;
    };
    struct Image {
        const ElfFile* file;
        Addr bias;
    };
    struct Mapped {         /* Linux's NT_FILE */
        Addr start, end;
        ttr::Str path;
    };

    int fd_;
    const Arch* arch_;
    ttr::Vector<Segment> segs_;
    ttr::Vector<Image> images_;
    ttr::Vector<Mapped> files_;
    Regs regs_;
    bool have_regs_;
    int sig_;
    int pid_;
    Addr entry_;            /* AT_ENTRY (Linux), 0 if not known */
    ttr::Str name_;
    ttr::Str exe_;

    CoreTarget(const CoreTarget&);
    CoreTarget& operator=(const CoreTarget&);
};

} /* namespace td */

#endif
