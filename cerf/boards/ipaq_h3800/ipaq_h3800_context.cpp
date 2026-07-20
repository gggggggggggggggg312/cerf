#include "../board_context.h"

#include "../../core/cerf_emulator.h"

namespace {

class IpaqH3800Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board       GetBoard()  const override { return Board::CompaqIpaqH3800; }
    SocFamily   GetSoc()    const override { return SocFamily::SA1110; }
    CpuArch     GetCpuArch() const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }

    const char* GetShortBoardName() const override {
        return "iPAQ H3800";
    }

    uint32_t GetGuestAdditionsColorDepth() const override { return 24u; }

    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{240u, 320u};
    }
};

}  // namespace

REGISTER_SERVICE_AS(IpaqH3800Context, BoardContext);
