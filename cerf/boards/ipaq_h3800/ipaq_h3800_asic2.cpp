#include "ipaq_h3800_asic2.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

bool IpaqH3800Asic2::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::CompaqIpaqH3800;
}

void IpaqH3800Asic2::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);

    /* Default inputs: no SD card, no USB cable, no earphone.
       Active-low signals therefore idle high. */
    gpio_status_ = (1u << 0) | (1u << 1) | (1u << 2);

    LOG(Periph, "[H3800 ASIC2] ready (base=%08X)\n", kAsic2Base);
}

void IpaqH3800Asic2::RaiseIrq(uint32_t bit) {
    raw_ |= bit;

    const uint32_t level = raw_; /* active-high for now */

    /* Rising-edge latch. */
    status_ |= level & ~detect_;
    detect_ = level;

    if (bit == kIrqSdDetectBit)
        LOG(Periph, "[H3800 ASIC2] SD detect IRQ\n");
    else if (bit == kIrqUsbDetectBit)
        LOG(Periph, "[H3800 ASIC2] USB detect IRQ\n");
    else if (bit == kIrqEarphoneBit)
        LOG(Periph, "[H3800 ASIC2] Earphone IRQ\n");
}

void IpaqH3800Asic2::LowerIrq(uint32_t bit) {
    raw_ &= ~bit;
    detect_ = raw_;

    /* Do NOT clear status_ here - edge-triggered latch. */
}

uint8_t IpaqH3800Asic2::ReadByte(uint32_t addr) {
    return static_cast<uint8_t>(
        ReadWord(addr & ~3u) >> ((addr & 3u) * 8));
}

uint32_t IpaqH3800Asic2::ReadWord(uint32_t addr) {
    const uint32_t off = addr - kAsic2Base;

    switch (off) {
    case kIntStatusOffset:
        LOG(Periph, "[H3800 ASIC2] INT_STATUS -> %08X\n", status_);
        return status_;

    case kIntEnableOffset:
        LOG(Periph, "[H3800 ASIC2] INT_ENABLE -> %08X\n", enable_);
        return enable_;

    case kGpioStatusOffset:
        LOG(Periph, "[H3800 ASIC2] GPIO_STATUS -> %08X\n", gpio_status_);
        return gpio_status_;

    default:
        LOG(Periph, "[H3800 ASIC2] unknown R32 %08X\n", off);
        return 0;
    }
}

void IpaqH3800Asic2::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t aligned = addr & ~3u;
    const uint32_t shift   = (addr & 3u) * 8;

    uint32_t cur = ReadWord(aligned);
    cur = (cur & ~(0xFFu << shift)) |
          (static_cast<uint32_t>(value) << shift);

    WriteWord(aligned, cur);
}

void IpaqH3800Asic2::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - kAsic2Base;

    switch (off) {
    case kIntEnableOffset:
        enable_ = value;
        LOG(Periph, "[H3800 ASIC2] INT_ENABLE = %08X\n", value);
        break;

    case kIntAckOffset:
        status_ &= ~value; /* W1C */
        LOG(Periph, "[H3800 ASIC2] INT_ACK = %08X -> pending %08X\n",
            value, status_);
        break;

    default:
        LOG(Periph, "[H3800 ASIC2] unknown W32 %08X = %08X\n",
            off, value);
        break;
    }
}

void IpaqH3800Asic2::SaveState(StateWriter& w) {
    w.Write(raw_);
    w.Write(detect_);
    w.Write(status_);
    w.Write(enable_);
    w.Write(gpio_status_);
}

void IpaqH3800Asic2::RestoreState(StateReader& r) {
    r.Read(raw_);
    r.Read(detect_);
    r.Read(status_);
    r.Read(enable_);
    r.Read(gpio_status_);
}

void IpaqH3800Asic2::PostRestore() {
    /* Re-drive interrupt state after loading a save state. */
    if (OutputAsserted()) {
        LOG(Periph,
            "[H3800 ASIC2] interrupt output asserted after restore\n");
    }
}

REGISTER_SERVICE(IpaqH3800Asic2);
