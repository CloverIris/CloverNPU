#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace clover {

struct IssOptions {
    uint32_t load_base = 0x1000'0000u;
    uint32_t entry = 0x1000'0000u;
    uint32_t stack_top = 0x0003'FFF0u;
    uint32_t bss_size = 0;
    uint64_t max_steps = 10'000'000ull;
    bool trace = false;
    bool echo_console = false;
};

struct IssResult {
    bool halted = false;
    bool faulted = false;
    int exit_code = 0;
    uint64_t cycles = 0;
    uint64_t instructions = 0;
    uint32_t pc = 0;
    std::string error;
    std::string console;
};

class Iss {
  public:
    explicit Iss(IssOptions options = {});

    bool load_image(const std::vector<uint8_t> &image, std::string &error);
    bool load_image_at(uint32_t base, const std::vector<uint8_t> &image,
                       std::string &error);
    IssResult run(std::string *trace_output = nullptr);

    void boot(uint32_t entry, uint32_t sp, uint32_t arg);
    uint32_t reg(uint32_t index) const;
    uint32_t pc() const { return pc_; }
    uint32_t csr(uint32_t address) const;
    const std::string &console() const { return console_; }

    bool read_memory(uint32_t address, uint32_t size, uint32_t &value) const;
    bool write_memory(uint32_t address, uint32_t size, uint32_t value);

  private:
    static constexpr uint32_t kScratchSize = 256u * 1024u;
    static constexpr uint32_t kDramBase = 0x1000'0000u;
    static constexpr uint32_t kDramSize = 64u * 1024u * 1024u;
    static constexpr uint32_t kMmioBase = 0x0007'0000u;
    static constexpr uint32_t kMmioSize = 0x0000'8000u;

    bool access(uint32_t address, uint32_t size, bool write, uint32_t &value);
    bool fetch(uint32_t address, uint32_t &raw);
    void set_reg(uint32_t index, uint32_t value);
    void next(uint32_t target);

    void raise(uint32_t cause, uint32_t tval);
    void syscall();
    void execute(uint32_t raw);

    bool read_elem(uint32_t address, uint32_t dtype, float &value,
                   std::string &error);
    bool write_elem(uint32_t address, uint32_t dtype, float value,
                    std::string &error);
    bool tile_mma(uint32_t descriptor, std::string &error);
    bool tile_vec(uint32_t descriptor, std::string &error);
    bool tile_dma(uint32_t descriptor, std::string &error);

    IssOptions options_;
    std::vector<uint8_t> scratch_;
    std::vector<uint8_t> dram_;
    uint32_t regs_[32] = {};
    uint32_t csrs_[4096] = {};
    uint32_t pc_ = 0;
    uint32_t next_pc_ = 0;
    uint64_t cycles_ = 0;
    uint64_t instructions_ = 0;
    bool halted_ = false;
    bool faulted_ = false;
    bool trapped_ = false;
    int exit_code_ = 0;
    std::string error_;
    std::string console_;
};

}  // namespace clover
