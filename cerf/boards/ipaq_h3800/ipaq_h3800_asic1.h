#pragma once

#include "../../peripherals/peripheral_base.h"

#include <atomic>
#include <cstdint>

class StateWriter;
class StateReader;

/* iPAQ H3800 ASIC1 (Glamis) - power / LCD / IR / RS232 GPIO block.
   This replaces the H3600 EGPIO latch. The initial implementation is
   intentionally minimal: a read/write latch so the ROM can probe the
   registers without aborting. */
class IpaqH3800Asic1 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    // TODO: replace with the real ASIC1 base/size from h3600_asic.h
    uint32_t MmioBase() const override { return 0x49000000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    std::atomic<uint32_t> gpio_out_{0};
};
