#include <clover/clover_abi.h>

#include <cstddef>
#include <cstdio>

static_assert(sizeof(struct clover_mma_desc) == 64, "mma desc size");
static_assert(offsetof(struct clover_mma_desc, flags) == 4, "mma flags");
static_assert(offsetof(struct clover_mma_desc, m) == 8, "mma m");
static_assert(offsetof(struct clover_mma_desc, k) == 16, "mma k");
static_assert(offsetof(struct clover_mma_desc, a_addr) == 20, "mma a_addr");
static_assert(offsetof(struct clover_mma_desc, c_addr) == 28, "mma c_addr");
static_assert(offsetof(struct clover_mma_desc, a_stride) == 32, "mma a_stride");
static_assert(offsetof(struct clover_mma_desc, post_bias) == 56, "mma post_bias");
static_assert(alignof(struct clover_mma_desc) == 64, "mma align");

static_assert(sizeof(struct clover_vec_desc) == 64, "vec desc size");
static_assert(offsetof(struct clover_vec_desc, n) == 8, "vec n");
static_assert(offsetof(struct clover_vec_desc, src_type) == 36, "vec src_type");
static_assert(offsetof(struct clover_vec_desc, dst_type) == 40, "vec dst_type");
static_assert(offsetof(struct clover_vec_desc, post_bias) == 56, "vec post_bias");

static_assert(sizeof(struct clover_dma_desc) == 64, "dma desc size");
static_assert(offsetof(struct clover_dma_desc, src_addr) == 8, "dma src");
static_assert(offsetof(struct clover_dma_desc, dst_stride) == 28, "dma stride");

static_assert(sizeof(struct clover_cmd) == 64, "cmd size");
static_assert(offsetof(struct clover_cmd, src) == 8, "cmd src");
static_assert(offsetof(struct clover_cmd, dim0) == 16, "cmd dim0");
static_assert(offsetof(struct clover_cmd, seq) == 44, "cmd seq");
static_assert(offsetof(struct clover_cmd, crc) == 60, "cmd crc");
static_assert(alignof(struct clover_cmd) == 64, "cmd align");

static_assert(sizeof(struct clover_completion) == 16, "completion size");

static_assert(sizeof(struct clover_tensor) == 56, "tensor size");
static_assert(offsetof(struct clover_tensor, dim) == 16, "tensor dim");
static_assert(offsetof(struct clover_tensor, stride) == 32, "tensor stride");
static_assert(offsetof(struct clover_tensor, scale_addr) == 48, "tensor scale");

static_assert(sizeof(struct clover_kernel_args) == 1024, "kargs size");
static_assert(offsetof(struct clover_kernel_args, scalar) == 16, "kargs scalar");
static_assert(offsetof(struct clover_kernel_args, tensors) == 80, "kargs tensors");
static_assert(offsetof(struct clover_kernel_args, reserved) == 976, "kargs reserved");
static_assert(alignof(struct clover_kernel_args) == 64, "kargs align");

static_assert(sizeof(struct clov_header) == 64, "clov header size");

static_assert((CLOVER_MMA_MAGIC & 0xFFu) == 'M', "mma magic byte 0");
static_assert(((CLOVER_MMA_MAGIC >> 8) & 0xFFu) == 'M', "mma magic byte 1");
static_assert(((CLOVER_MMA_MAGIC >> 16) & 0xFFu) == 'A', "mma magic byte 2");
static_assert(((CLOVER_MMA_MAGIC >> 24) & 0xFFu) == '1', "mma magic byte 3");

int main()
{
    std::printf("Clover ABI layout OK (version %d.%d)\n",
                CLOVER_ABI_VERSION_MAJOR, CLOVER_ABI_VERSION_MINOR);
    return 0;
}
