#include "ipaq_h3800_asic1.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

bool IpaqH3800Asic1::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::CompaqIpaqH3800;
}

void IpaqH3800Asic1::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint8_t IpaqH3800Asic1::ReadByte(uint32_t addr) {
    return static_cast<uint8_t>(ReadWord(addr & ~3u) >> ((addr & 3u) * 8));
}

uint32_t IpaqH3800Asic1::ReadWord(uint32_t addr) {
    const uint32_t off = addr - kAsic1Base;

    switch (off) {
    case kGpioOutOffset:  return gpio_out_.load(std::memory_order_acquire);
    case kGpioDirOffset:  return gpio_dir_.load(std::memory_order_acquire);
    case kGpioMaskOffset: return gpio_mask_.load(std::memory_order_acquire);
    default:
        LOG(Periph, "[H3800 ASIC1] unknown R32 %08X\n", off);
        return 0;
    }
}

void IpaqH3800Asic1::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t aligned = addr & ~3u;
    const uint32_t shift   = (addr & 3u) * 8;
    uint32_t cur = ReadWord(aligned);
    cur = (cur & ~(0xFFu << shift)) | (static_cast<uint32_t>(value) << shift);
    WriteWord(aligned, cur);
}

void IpaqH3800Asic1::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - kAsic1Base;

    switch (off) {
    case kGpioOutOffset:
        gpio_out_.store(value, std::memory_order_release);
        LOG(Periph, "[H3800 ASIC1] GPIO_OUT = %08X\n", value);
        break;

    case kGpioDirOffset:
        gpio_dir_.store(value, std::memory_order_release);
        LOG(Periph, "[H3800 ASIC1] GPIO_DIR = %08X\n", value);
        break;

    case kGpioMaskOffset:
        gpio_mask_.store(value, std::memory_order_release);
        LOG(Periph, "[H3800 ASIC1] GPIO_MASK = %08X\n", value);
        break;

    default:
        LOG(Periph, "[H3800 ASIC1] unknown W32 %08X = %08X\n", off, value);
        break;
    }
}

void IpaqH3800Asic1::SaveState(StateWriter& w) {
    w.Write(gpio_out_.load(std::memory_order_acquire));
    w.Write(gpio_dir_.load(std::memory_order_acquire));
    w.Write(gpio_mask_.load(std::memory_order_acquire));
}

void IpaqH3800Asic1::RestoreState(StateReader& r) {
    uint32_t v;
    r.Read(v); gpio_out_.store(v, std::memory_order_release);
    r.Read(v); gpio_dir_.store(v, std::memory_order_release);
    r.Read(v); gpio_mask_.store(v, std::memory_order_release);
}

REGISTER_SERVICE(IpaqH3800Asic1);
