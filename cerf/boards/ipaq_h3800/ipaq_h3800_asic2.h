#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>

class StateWriter;
class StateReader;

/* Minimal H3800 ASIC2 interrupt controller (GPIO/SD/USB companion block).
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

private:
    /* Provisional interrupt-register map used for bring-up tracing. */
    static constexpr uint32_t kIntStatusOffset = 0x10u;
    static constexpr uint32_t kIntEnableOffset = 0x14u;
    static constexpr uint32_t kIntAckOffset    = 0x18u;

    /* Provisional GPIO status register (SD/USB detect inputs). */
    static constexpr uint32_t kGpioStatusOffset = 0x20u;

    enum : uint8_t {
        kIrqSdDetect  = 0,
        kIrqUsbDetect = 1,
        kIrqEarphone  = 2,
    };

    void RaiseIrq(uint8_t source);
    void LowerIrq(uint8_t source);

    uint32_t raw_    = 0;  /* live input levels */
    uint32_t detect_ = 0;  /* previous levels for edge detect */
    uint32_t status_ = 0;  /* latched pending interrupts */
    uint32_t enable_ = 0;  /* interrupt enable mask */

    /* Input GPIO state (1 = inactive/high for the *_N signals). */
    uint32_t gpio_status_ = 0;
};
