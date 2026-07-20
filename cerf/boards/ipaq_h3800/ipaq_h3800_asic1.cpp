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
    const uint32_t off = addr - MmioBase();
    return static_cast<uint8_t>(
        (gpio_out_.load(std::memory_order_acquire) >> (8 * off)) & 0xFFu);
}

uint32_t IpaqH3800Asic1::ReadWord(uint32_t addr) {
    LOG(Periph, "[H3800 ASIC1] R32 %08X\n", addr);
    return gpio_out_.load(std::memory_order_acquire);
}

void IpaqH3800Asic1::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off   = addr - MmioBase();
    const uint32_t shift = 8 * off;
    const uint32_t mask  = 0xFFu << shift;

    uint32_t before = gpio_out_.load(std::memory_order_acquire);
    uint32_t after  = (before & ~mask) |
                      (static_cast<uint32_t>(value) << shift);

    gpio_out_.store(after, std::memory_order_release);

    LOG(Periph, "[H3800 ASIC1] W8 +%u = %02X -> %08X\n",
        off, value, after);
}

void IpaqH3800Asic1::WriteWord(uint32_t addr, uint32_t value) {
    gpio_out_.store(value, std::memory_order_release);

    LOG(Periph, "[H3800 ASIC1] W32 %08X = %08X\n", addr, value);
}

void IpaqH3800Asic1::SaveState(StateWriter& w) {
    w.Write(gpio_out_.load(std::memory_order_acquire));
}

void IpaqH3800Asic1::RestoreState(StateReader& r) {
    uint32_t v = 0;
    r.Read(v);
    gpio_out_.store(v, std::memory_order_release);
}

REGISTER_SERVICE(IpaqH3800Asic1);
