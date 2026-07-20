#pragma once

#include "../../peripherals/peripheral_base.h"

#include <atomic>
#include <cstdint>

class StateWriter;
class StateReader;

/* H3800 ASIC1 - power-management / GPIO companion block.
   Real base from Compaq h3600_asic.h:
       H3800_ASIC1_BASE = 0x4C000000
*/
class IpaqH3800Asic1 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    static constexpr uint32_t kAsic1Base = 0x4C000000u;

    /* Real GPIO register offsets from h3600_asic.h */
    static constexpr uint32_t kGpioOutOffset  = 0x68u;
    static constexpr uint32_t kGpioDirOffset  = 0x6Cu;
    static constexpr uint32_t kGpioMaskOffset = 0x70u;

    /* GPIO1 power-control bits (Linux H3800 BSP names). */
    static constexpr uint32_t kLcdOnBit    = 1u << 2;
    static constexpr uint32_t kVglOnBit    = 1u << 3;
    static constexpr uint32_t kVghOnBit    = 1u << 4;
    static constexpr uint32_t kLcd5vOnBit  = 1u << 5;
    static constexpr uint32_t kIrOnBit     = 1u << 6;
    static constexpr uint32_t kRs232OnBit  = 1u << 7;

    uint32_t MmioBase() const override { return kAsic1Base; }
    uint32_t MmioSize() const override { return 0x00100000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    std::atomic<uint32_t> gpio_out_{0};
    std::atomic<uint32_t> gpio_dir_{0};
    std::atomic<uint32_t> gpio_mask_{0};
};
