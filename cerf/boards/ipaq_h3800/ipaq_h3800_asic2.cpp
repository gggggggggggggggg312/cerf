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
}

uint8_t IpaqH3800Asic2::ReadByte(uint32_t addr) {
    return static_cast<uint8_t>(ReadWord(addr & ~3u) >> ((addr & 3u) * 8));
}

uint32_t IpaqH3800Asic2::ReadWord(uint32_t addr) {
    const uint32_t off = addr - kAsic2Base;

    LOG(Periph, "[H3800 ASIC2] R32 %08X\n", off);

    /* For now return 0 for all registers.
       The ROM will probe IRQ/GPIO/SD/USB status registers here. */
    return dummy_.load(std::memory_order_acquire);
}

void IpaqH3800Asic2::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t aligned = addr & ~3u;
    const uint32_t shift   = (addr & 3u) * 8;

    uint32_t cur = ReadWord(aligned);
    cur = (cur & ~(0xFFu << shift)) | (static_cast<uint32_t>(value) << shift);

    WriteWord(aligned, cur);
}

void IpaqH3800Asic2::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - kAsic2Base;

    LOG(Periph, "[H3800 ASIC2] W32 %08X = %08X\n", off, value);

    dummy_.store(value, std::memory_order_release);
}

void IpaqH3800Asic2::SaveState(StateWriter& w) {
    w.Write(dummy_.load(std::memory_order_acquire));
}

void IpaqH3800Asic2::RestoreState(StateReader& r) {
    uint32_t v = 0;
    r.Read(v);
    dummy_.store(v, std::memory_order_release);
}

REGISTER_SERVICE(IpaqH3800Asic2);
