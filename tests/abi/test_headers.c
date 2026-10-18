/* Compile-only check that both public headers are valid C99/C11. */
#include <clover/clover.h>
#include <clover/clover_tile.h>

static struct clover_mma_desc mma_desc;
static struct clover_kernel_args kernel_args;
static struct clover_cmd commands[4];

int clover_header_compile_check(void)
{
    return (int)(mma_desc.magic + kernel_args.magic + commands[0].opcode);
}
