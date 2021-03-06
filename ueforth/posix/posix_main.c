#include <dlfcn.h>
#include <sys/mman.h>

#include "common/opcodes.h"
#include "common/calling.h"

#define HEAP_SIZE (10 * 1024 * 1024)
#define STACK_SIZE (16 * 1024)

#define PLATFORM_OPCODE_LIST \
  X("DLSYM", DLSYM, tos = (cell_t) dlsym((void *) *sp, (void *) tos); --sp) \
  CALLING_OPCODE_LIST \

#include "common/core.h"
#include "common/interp.h"

#include "gen/posix_boot.h"

int main(int argc, char *argv[]) {
  void *heap = mmap(0, HEAP_SIZE, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ueforth_init(argc, argv, heap, boot, sizeof(boot));
  for (;;) { g_sys.rp = ueforth_run(g_sys.rp); }
  return 1;
}
