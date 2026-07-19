#pragma once

#include <cstdint>

class JitBlockIndex;

struct JitBlock {
    uint32_t   guest_start;   /* FCSE-folded VA - RB index key + jump-cache key */
    uint32_t   guest_end;     /* folded VA, inclusive last byte */
    void*      native_start;
    void*      native_end;
    uint32_t   flags_needed;
    JitBlock*  sub_block;
    uint32_t   phys_start;    /* fetch PA of start - dual-key phys validation + SMC match */

    /* Per-physical-page intrusive lists (QEMU PageDesc.first_tb, tb_link_page):
       slot 0 chains index_start's page, slot 1 chains index_start2's page.
       owner = the RB tree to RbDelete from. */
    JitBlock*       page_next[2];
    JitBlockIndex*  owner;

    /* page_heads[] key: the backing-store offset (QEMU ram_addr), so every
       alias of a byte in a repeating decode window shares one bucket. */
    uint32_t        index_start;

    /* QEMU tb->page_addr[1]: index_split = byte offset where the second
       translation page begins (0 = single-page block); index_start2 = that
       tail's own backing-store offset. */
    uint32_t        index_split;
    uint32_t        index_start2;
};
