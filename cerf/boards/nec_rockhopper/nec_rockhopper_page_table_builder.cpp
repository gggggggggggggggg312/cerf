#include "../page_table_builder.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../board_context.h"

#include <cstdint>
#include <vector>

namespace {

/* 128 MB DRAM at PA 0x00000000-0x07FFFFFF (boot.bib: "Rockhopper CPU VR5500
   board has 128MB DRAM 0x00000000-0x07FFFFFF"). MIPS kseg0 (0x80000000 cached)
   and kseg1 (0xA0000000 uncached) are fixed unmapped windows: PA = VA &
   0x1FFFFFFF. The XIP loads at kseg0 VA 0x80800000 -> PA 0x00800000. */
constexpr uint32_t kDramPaBase = 0x00000000u;
constexpr uint32_t kDramSize   = 0x08000000u;   /* 128 MB */
constexpr uint32_t kKseg0Base  = 0x80000000u;
constexpr uint32_t kKseg2Base  = 0xC0000000u;
constexpr uint32_t kUnmaskKseg = 0x1FFFFFFFu;

class NecRockhopperPageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::NecRockhopper;
    }

    uint32_t VaToPa(uint32_t va) const override {
        if (va >= kKseg0Base && va < kKseg2Base) {
            return va & kUnmaskKseg;
        }
        LOG(Caution, "NecRockhopperPageTableBuilder::VaToPa: VA 0x%08X is "
                "outside the kseg0/kseg1 unmapped windows (ROM placement only "
                "feeds kseg VAs)\n", va);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    std::vector<DramRegion> CachedDramRegions() const override {
        return { { kKseg0Base, kDramPaBase, kDramSize } };
    }

    std::vector<BackedRegion> BackedMemoryRegions() const override {
        return { { kKseg0Base, kDramPaBase, kDramSize, PAGE_READWRITE } };
    }

    std::vector<DramRegion> MappedVaSpans() const override {
        return { { kKseg0Base, kDramPaBase, kDramSize } };
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecRockhopperPageTableBuilder, PageTableBuilder);
