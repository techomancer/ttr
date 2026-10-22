/*
 * tf/sys.h - what tf needs that differs per system, one implementation
 * file each (sys_linux.cpp, sys_irix.cpp), chosen by the Makefile's
 * TF_SYS, as in tp/.
 */
#ifndef TF_SYS_H
#define TF_SYS_H

#include "tf.h"

namespace tf {

/* Sizes and the file system type ("xfs", "nfs", "ext4") of the file
 * system holding path. 0 or an errno value. */
int sys_space(const char* path, Space* out);

} /* namespace tf */

#endif
