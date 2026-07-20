#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>

class StateWriter;
class StateReader;

/* H3800 ASIC2 companion block (GPIO / SD / USB / interrupt logic).
   Real base from Compaq h3600_asic.h:
       H3800_ASIC2_BASE = 0x4B000000
*/
class IpaqH3800Asic2 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    static constexpr uint32_t kAsic2Base = 0x4B000000u;

    uint32_t MmioBase() const override { return kAsic2Base; }
    uint32_t MmioSize() const override { return 0x00100000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    bool OutputAsserted() const { return (status_ & enable_) != 0; }

private:
    /* Provisional bring-up register map. */
    static constexpr uint32_t kIntStatusOffset  = 0x10u;
    static constexpr uint32_t kIntEnableOffset  = 0x14u;
    static constexpr uint32_t kIntAckOffset     = 0x18u;
    static constexpr uint32_t kGpioStatusOffset = 0x20u;

    /* IRQ bits. */
    static constexpr uint32_t kIrqSdDetectBit  = 1u << 0;
    static constexpr uint32_t kIrqUsbDetectBit = 1u << 1;
    static constexpr uint32_t kIrqEarphoneBit  = 1u << 2;

    void RaiseIrq(uint32_t bit);
    void LowerIrq(uint32_t bit);

    uint32_t raw_         = 0;  /* live input levels */
    uint32_t detect_      = 0;  /* previous levels for edge detect */
    uint32_t status_      = 0;  /* latched pending interrupts */
    uint32_t enable_      = 0;  /* interrupt enable mask */

    /* GPIO input state (1 = inactive/high for *_N signals). */
    uint32_t gpio_status_ = 0;
};
