# JIT - guest ISA → host x86 block translator

The JIT translates guest code to native host x86 at first execution
of each block and caches the translation. Two guest ISAs are
implemented: ARM (ARM-mode + Thumb) and MIPS (MIPS IV, 64-bit). A
board runs exactly one of them, picked by its CPU architecture.

## The `GuestEngine` seam

`JitRunner` drives an abstract `GuestEngine` service
(`cerf/jit/guest_engine.h`) and never names a concrete engine.
`ArmJit` and `MipsJit` each `REGISTER_SERVICE_AS(<engine>,
GuestEngine)`; the winner is selected by
`BoardContext::GetCpuArch()` (`enum class CpuArch { Arm, Mips }`),
so `ArmJit::ShouldRegister` returns true on `CpuArch::Arm` and
`MipsJit::ShouldRegister` on `CpuArch::Mips`. Every ARM-engine
service (`ArmCpu`, `ArmMmu`, `ArmDecoder`, `ArmCp15SctlrHandler`)
likewise gates `ShouldRegister` on `CpuArch::Arm`; the MIPS engine's
internals gate on `CpuArch::Mips`. A board therefore materializes one
engine and none of the other ISA's services.

The seam surface (all ISA-neutral):

- `Run` / `Pc` / `DeepSleep` / `ResetPending` - the dispatch loop and
  CPU-status reads `JitRunner` polls.
- `SaveCpuState` / `RestoreCpuState` / `SaveMmuState` /
  `RestoreMmuState` - the hibernation `Cpu` and `Mmu` `.img` sections
  route here, so the state layer is ISA-agnostic.
- `SetResetPending(is_resume)` - `GuestCpuReset` / `GuestColdBoot`
  pend a CPU reset here; `is_resume` selects the deep-sleep-wake
  notification over the reboot one.
- `ResyncInterruptPoll`, `FlushTranslationCache`, `SetInjectionBand`
  (guest-additions overlay band), `PeekGuestVa` /
  `ResolveGuestVaToHost` (diagnostics + ROM placement).

## Boundary

- **Host C++ in the JIT covers**: translation (decode → emit), the
  dispatch loop, the MMU + coprocessor (cp15 / CP0) state, per-SoC
  configuration strategies, exception entry, the cross-thread
  interrupt channel, and the SEH fault wrapper. That's it.
- **NOT covered by host C++**: any CE userspace or kernel behavior.
  Those are guest code running in the cache. If `coredll.dll` does
  something wrong, the fix is not in the JIT.

## Shared infrastructure (both engines)

- **`JitCodeArena`** (`cerf/jit/jit_code_arena.{h,cpp}`) - one large
  `VirtualAlloc PAGE_EXECUTE_READWRITE` region per engine instance,
  bump-allocated. Allocation failure means the region is full -
  caller flushes everything and retries. `Flush` drops every block
  but keeps the region committed; the unused tail consumes no
  physical RAM under Windows overcommit.
- **`IsaBlockSpace`** (`cerf/jit/isa_block_space.h`) - the per-ISA
  translation index, modeled on QEMU's TCG block cache. It holds a
  VA-indexed jump cache (`tb_jmp_cache`), a `global` `JitBlockIndex`
  for nG=0 kernel/shared blocks plus 256 per-ASID `JitBlockIndex`
  trees for nG=1 user blocks, and a per-physical-page intrusive list
  of outer blocks (`PageDesc.first_tb`) so SMC `RemoveRange` walks
  one page's list instead of the whole VA tree. The block index is
  phys-keyed; the jump cache is keyed by FCSE-folded VA and dropped
  on context switch / SMC / full flush. The ARM engine owns two
  spaces (ARM, Thumb); the MIPS engine owns one.
- **`JitBlockIndex`** (`cerf/jit/jit_block_index.{h,cpp}`) - one RB
  tree keyed on the post-fold guest VA. Outer entries cover
  `[guest_start, guest_end]` ranges; lookup primitives are
  find-exact, find-containing, find-next-after, range-intersects.
  Sub-entries (linked off the outer's `sub_block` chain) handle
  re-entering the same outer block at a non-start instruction
  without fragmenting the tree.
- **`x86_emit.h`** - header-only x86 encoding helpers. Style: free
  functions taking `uint8_t*& cursor`, advancing the cursor past
  emitted bytes. The encoding source of truth is Intel SDM Vol. 2.

  **`rel8` vs `rel32` trap.** Short conditional jumps (`Jcc rel8` /
  `JMP rel8`) carry a signed-byte displacement (±127). When the emit
  between the jump and its target grows past that range - usually
  because the surrounding instruction's code body grew - the
  back-patch silently truncates and the CPU jumps to garbage. The
  `FixupLabel` helper catches the overflow at emit time with a fatal
  log naming the jump opcode and the actual displacement; the fix is
  to switch the affected jump to its `*32` cousin (`EmitJzLabel32`,
  `EmitJnzLabel32`, `EmitJmpLabel32`, `FixupLabel32`).

**Block physical identity comes from the fetch, never a re-walk.**
When a block is keyed or validated by physical address, that PA must
be captured from the same translation that fetched the block's bytes
- an independent later page-table walk diverges from the fetch during
transitional MMU states (a TLB-cached mapping the fresh walk can't
see, or a partially-set-up page table) and will mis-key or spuriously
fault the block.

## The `place_fn` contract

The single most important convention in the JIT, shared in shape by
both engines. The decoder fills a decoded-instruction record and
assigns a function pointer; the emit phase invokes that function
once per guest instruction. The ARM form:

```cpp
using ArmPlaceFn = uint8_t* (*)(uint8_t*       cursor,
                                DecodedInsn*   d,
                                BlockContext*  ctx);
```

`MipsPlaceFn` has the same shape over `MipsDecodedInsn` /
`MipsBlockContext`. Each `place_fn` writes host machine code at
`cursor` and returns the advanced cursor. ARM place fns live in
`cerf/jit/arm/place/`, MIPS place fns in `cerf/jit/mips/place/` -
one file per function. Adding a guest instruction is "add a decoder
mapping + add a `place/<name>.cpp`"; no build-script or project-file
edit (`cerf.vcxproj` globs `**\*.cpp`).

The block context carries per-block emit state: the
decoded-instruction array, the owning engine back-pointer for
service access, the addresses of the per-instance JIT trampolines,
and per-block caches. Place fns reach per-instance services through
`ctx->jit`.

## Pinned-register dispatcher

Every translated block runs with the guest CPU-state pointer pinned
in `ESI` across the whole block; ARM additionally pins the MMU-state
pointer in `EBX`. Place fns address state fields as `[<pinned-reg> +
byte-offset]` without ever recomputing the base. The assignment is
invariant across every place fn and every JIT helper - changing it
would require touching every emit site. Helpers documented as
`__fastcall(va, …, jit)` follow that convention because emit code at
the call site already has the args in the right registers.

---

# ARM engine (`ArmJit`)

## Service set

The ARM engine is a small constellation of services. Each owns one
responsibility:

- **`ArmJit`** - the translation cache, the dispatcher, the compile
  pipeline; implements `GuestEngine`. Hot-path runtime helpers
  called from emitted code live here as static methods so emit code
  can bake a stable function-pointer address.
- **`ArmCpu`** - owns `ArmCpuState` (GPRs, CPSR, banked SPSR/R13/R14
  per privileged mode). Hosts the mode-bank / CPSR-write helpers and
  the exception-entry methods (Undefined, AbortData, AbortPrefetch,
  IRQ, SWI, Reset).
- **`ArmMmu`** - owns the cp15 register file, the I-TLB and D-TLB,
  the page-table walker. `Translate{Read,Write,ReadWrite,Execute}`
  are the public surface; on a peripheral PA they return `nullptr`
  and stash the PA in an I/O-pending slot the JIT helper reads to
  route the access to `PeripheralDispatcher`.
- **`ArmDecoder`** - ARM/Thumb opcode → `DecodedInsn`. Thumb decoders
  synthesize an ARM equivalent and re-issue through the ARM decoder
  so register/operand placement has one canonical source.
- **`ArmProcessorConfig`** - per-SoC strategy. PC store offset,
  base-restored-abort model, memory-before-writeback model,
  cache-line size, MIDR/CTR, syscall presence, DSP / LDRD-STRD
  optionality. Base in `cerf/cpu/arm_processor_config.h`, concretes
  under `cerf/cpu/<core>/`, selected by `GetSoc()`.
- **`CoprocEmitter`** - per-SoC MCR/MRC/CDP/LDC/STC emit strategy.
  Base in `cerf/jit/arm/coproc_emitter.h`, concretes under
  `cerf/cpu/<core>/`.
- **`ArmCp15SctlrHandler`** - owns the per-instance trampoline that
  cp15 c1 (SCTLR) writes JMP to. SCTLR changes flip MMU on/off,
  invalidating every cached block - the handler flushes the cache
  before returning so JIT-emitted code never re-enters a freed
  block.

A shared-capable ISA capability (VFP, NEON, DSP, …) lives in the
shared ARM decode/emit path behind an `ArmProcessorConfig::HasX()`
flag, never localized in one SoC's `CoprocEmitter`.

## Compile pipeline

`ArmJit::JitCompile(guest_pc)` runs a multi-phase pipeline:

1. **Decode forward** from `guest_pc` until a kernel boundary, a
   prefetch-abort PA, or a per-block instruction cap. ARM vs Thumb
   picked from CPSR.T.
2. **Locate entrypoints + flag-eliminate**. Every R15-modifying
   insn terminates an entrypoint; in-stream branches create new
   entrypoints at their destinations. A back-to-front sweep drops
   per-insn flag packs the next consumer overwrites.
3. **Allocate + register entrypoints**. Bump-allocate one slab from
   `JitCodeArena`, place the records, insert into the ISA's
   `IsaBlockSpace`. Outer entrypoints become new ranges; re-entries
   into an existing range become sub-entries.
4. **Generate code**. Walk the decoded stream, emit per-entrypoint
   prologue + per-condition guard + per-instruction `place_fn`.
5. **Apply fixups**. Back-patch intra-batch forward branches whose
   destination native address wasn't known when the JMP was emitted.
6. **Flush host instruction cache** for the emitted range.

On translation-cache exhaustion: full flush + retry with a fresh
slab.

## FCSE fold

Guest VAs below 32 MB are private to the current process (ARM
Fast-Context-Switch Extension - the OS plants the active process's
fold base in cp13). The block-index key, the TLB key, and the
shadow-stack key all use the post-fold VA; `DecodedInsn::guest_address`
keeps the raw VA for diagnostics and for instruction-stream
re-entry.

**FCSE fold is identity on ASID kernels.** cp13 FCSE `process_id`
separates per-process low VAs only on FCSE kernels (CE5 / ARMv5).
CE6 / CE7 set `process_id = 0` and distinguish address spaces by ASID
(CONTEXTIDR) instead, so the fold is a no-op there. Any VA-keyed
structure that must stay process-private therefore has to incorporate
the ASID rather than rely on the fold.

## Trampoline pattern

For cross-block control transfers (cp15 cache-op-induced flush,
R15-modified resolve, branch resolve, BL push, BX-LR pop,
data-abort raise, interrupt poll, …) the JIT emits a JMP/CALL to a
per-instance trampoline rather than inlining the resolve. Each
trampoline is a small naked-machine-code body in writable host
memory, owned by `ArmJit`. Self-modifying trampolines (notably the
interrupt-poll one) let peripheral threads change JIT-thread
behavior without a lock on the hot path.

## Shadow stack

`BL` (and `BL`-shaped patterns the decoder recognizes) push a
`(guest_return_addr, cached_native_dest)` pair onto a per-instance
shadow stack. `BX LR` / `MOV PC, LR` pop and compare; on a
guest-return-address match the JIT JMPs straight to the cached
native destination, skipping the R15-modified-helper round trip.
Cleared on every JIT cache flush - the cached pointers became
stale the moment the arena was reused.

## I/O routing

`ArmMmu::Translate*` returns `nullptr` for two reasons: (1) a real
fault (TLB miss + page-table fault), and (2) the resolved PA lies
in peripheral I/O space. The JIT helper distinguishes by reading
the MMU's I/O-pending slot - non-zero means "PA is here, route to
`PeripheralDispatcher`," zero means "raise the abort." The slot is
per-`ArmMmu` instance.

## Interrupt delivery (cross-thread)

Peripheral threads call `ArmJit::SetInterruptPending` (or
`ClearInterruptPending`) under `InterruptLock`. The lock guards
both the pending bit and the byte of the interrupt-poll trampoline
that the JIT thread CALLs at every guest-block exit. A pending IRQ
patches the byte to NOP (fall through to delivery); no pending
patches it to RETN (return to block immediately). `SetEvent` wakes
the JIT thread if it's parked in an idle-loop `WaitForSingleObject`.
Idle-wake is fire-and-forget outside the lock - wake-up never hurts;
missing one (because the peripheral asserted just as the JIT thread
was about to park) is self-correcting on the next poll.

## Reset

- **Cold power-on**: the boot path resolves the entry VA, writes
  the bootloader-handoff SP via `ArmCpu::SetInitialStackPointer`,
  then calls `RaiseResetException(initial_pc)`. The initial PC is
  cached for subsequent soft resets.
- **Soft reset** (watchdog expiry, OAL request, …): peripheral
  calls `ArmJit::SetResetPending`; on the next IRQ-delivery
  dispatch the JIT routes through `ArmCpu::RaiseResetException()`
  (no-arg overload, uses the cached entry VA).

## SEH fault filter

`ArmJit::Run` wraps `Dispatch` in `__try`/`__except`. The filter
dumps the host EIP context (host x86 registers + a window of
JIT-emitted bytes around the fault), the guest ARM register file,
and the host symbol resolved via `dbghelp`, then `CerfFatalExit`s.
The filter only runs on actual hardware exceptions - zero hot-path
cost on the dispatch path.

---

# MIPS engine (`MipsJit`)

`MipsJit` (`cerf/jit/mips/`) is the MIPS IV / 64-bit engine,
structured in parallel to `ArmJit`: it owns a `JitCodeArena`, a
single `IsaBlockSpace` (one ISA - no ARM/Thumb split), a
`MipsDecoder`, a `MipsMmu`, a 64-bit `MipsCpuState`, and resolves a
`MipsProcessorConfig` + `MipsCp0Emitter`. `ESI` is pinned to
`MipsCpuState*`; every emitted block addresses GPR / CP0 / TLB
fields off `ESI`.

## Service set

- **`MipsJit`** - translation cache, dispatcher, compile pipeline;
  implements `GuestEngine`. Memory access, CP0 side effects, TLB
  ops, exception delivery, and the wide 64-bit arithmetic that
  cannot emit inline are `static __fastcall` helpers it bakes into
  emitted code.
- **`MipsMmu`** (`cerf/jit/mips/mips_mmu.{h,cpp}`) - the kseg fold +
  software joint-TLB. `kseg0/kseg1` map directly (unmapped/cached
  vs uncached); mapped segments walk the software TLB. `TLBWI` /
  `TLBWR` / `TLBP` / `TLBR` drive the indexed / random / probe /
  read TLB ops (and their block-cache invalidation).
- **`MipsDecoder`** - MIPS opcode → `MipsDecodedInsn`.
- **`MipsProcessorConfig`** (`cerf/cpu/mips_processor_config.h`) -
  per-SoC strategy, the MIPS analog of `ArmProcessorConfig`: `Prid`,
  `TlbSize`, `IsaLevel`, and the silicon-capability gates `HasFpu`
  (CP1), `HasLlsc` (LL/SC), `HasCounter` (CP0 Count/Compare),
  `HasWatch` (CP0 WatchLo/Hi) - the cpuinfo_mips option set. Base in
  `cerf/cpu/`, concretes under `cerf/cpu/<core>/`, selected by
  `GetSoc()`.
- **`MipsCp0Emitter`** (`cerf/jit/mips/mips_cp0_emitter.h`) - the
  per-SoC MFC0 / MTC0 / DMFC0 / DMTC0 emit strategy (the CP0 moves; TLB
  ops and timer side-effects are `MipsJit` helpers), the MIPS analog of
  `CoprocEmitter`. Base in `cerf/jit/mips/`, concretes under
  `cerf/cpu/<core>/`.

## CP0 exception model

Synchronous CP0 exceptions follow QEMU target/mips
`mips_cpu_do_interrupt`. The common entry sets EPC / EXL / BD (iff
!EXL), `Cause.ExcCode`, and the vector PC - the `0x000` TLB-refill
offset when EXL was clear, else `0x180` general. `ERET` returns to
`ErrorEPC` (if `Status.ERL`) else `EPC`, clears the level bit and
LLbit. Covered causes: TLB load/store/modify (`TLBL` / `TLBS` /
`Mod`), address error (`AdEL` / `AdES`), integer overflow,
`SYSCALL` / `BREAK`. A guest exception raised from inside a
memory/arith helper unwinds to `Run`'s `__except` via a
customer-defined NTSTATUS (`kGuestExceptionCode`), which resumes at
the already-set vector PC.

**Unimplemented MIPS paths loud-fatal, never silent-UND.** A decoder
reject or a not-yet-built place fn emits `PlaceMipsUndefined`, which
logs op + PC and `CerfFatalExit`s; trapping arithmetic overflow and
unbuilt MMIO/CP0 paths fatal the same way.

## In-core timer + interrupts

The CP0 Count/Compare timer is polled at the top of `Run` on the JIT
thread: Count advances by the guest cycles elapsed since the last
poll and, when armed and Count reaches Compare, raises IP7 (the
scheduler tick) - QEMU `cp0_timer.c`. The board INTC pushes the live
`Cause.IP[5:2]` external-interrupt LEVEL (not a latch) via
`SetExternalInterruptLevel`; `Run` reconciles `cp0_cause` from it on
the JIT thread. Passing the full current level every time is
mandatory - a missed deassert leaves an IP bit stuck and the guest
re-enters its ISR forever. An interrupt is deliverable iff
`Status.IE && !EXL && !ERL` and some `(Cause.IP & Status.IM)` bit is
set.

## Address spaces + wide ops

MIPS distinguishes address spaces by EntryHi ASID; an ASID-field
change flushes the VA jump cache (`Mtc0EntryHiHelper` →
`tlb_flush`). The 64-bit operations too wide for inline x86-32 emit
go through helpers: doubleword loads/stores, the unaligned
`LWL/LWR/LDL/LDR` + `SWL/SWR/SDL/SDR` merges, `DMULT/DMULTU` 128-bit
products, `DDIV/DDIVU`, and the variable doubleword shifts
`DSLLV/DSRLV/DSRAV`. Memory access is per-width all-in-one helpers
(`__fastcall` VA in ECX) that fold kseg or walk the software TLB,
then perform the access or route a non-RAM PA to
`PeripheralDispatcher`.
