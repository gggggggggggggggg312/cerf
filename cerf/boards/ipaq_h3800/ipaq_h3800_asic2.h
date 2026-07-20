#pragma once

#include "../../peripherals/peripheral_base.h"
#include <atomic>

class StateWriter;
class StateReader;

class IpaqH3800Asic2 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    static constexpr uint32_t kAsic2Base = 0x4B000000u;

    uint32_t MmioBase() const override { return kAsic2Base; }
    uint32_t MmioSize() const override { return 0x00100000u; }

    uint8_t  ReadByte(uint32_t addr) override;
    uint32_t ReadWord(uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    std::atomic<uint32_t> dummy_{0};
};
