# Unified Memory Mapping and Mirroring (Maintainers Guide)

This document proposes a small, generic enhancement to the JS/TS wrapper that makes the emulator’s memory view configurable and capable of mirroring ROM regions. The goal is to help downstream users align the wrapper’s unified memory with their platform’s expected address map without maintaining a local fork or editing built artifacts.

The approach is generic: consumers describe their desired address regions and optional mirrors; the wrapper allocates a single JS `Uint8Array` large enough to cover those regions and copies/mirrors bytes from the provided ROM and RAM sources accordingly.

## Motivation

Many emulator integrations expect a specific memory map, including multiple ROM windows, banked regions, or simple mirrors. Downstream projects often approximate this by post‑processing the ROM or patching the built wrapper. A small, well‑scoped API in the wrapper avoids those workarounds:

- Allows consumers to define ROM/RAM placement in address space.
- Supports mirroring one contiguous region into another address window.
- Keeps backward compatibility when options are not supplied.

## Summary of Changes

1) Expand unified memory to the maximum end address of configured regions (instead of a fixed size).
2) Add an optional `memoryLayout` parameter to the system factory so consumers can define regions and mirrors.
3) Initialize the unified memory by copying ROM/RAM bytes into each region and applying mirrors.
4) Keep the existing PC hook, trace, and run/step semantics unchanged.

## Proposed API (TypeScript)

Extend the `createSystem` factory with an optional `memoryLayout` argument. Backward‑compatible: when omitted, existing behavior is preserved.

```ts
type MemoryRegion = {
  // Absolute address where the region is mapped into the unified memory
  start: number;
  // Region length in bytes
  length: number;
  // Source for initial bytes
  source: 'rom' | 'ram' | 'zero';
  // Byte offset within the source array, if applicable (defaults to 0)
  sourceOffset?: number;
};

type MirrorRegion = {
  // Destination address to populate by mirroring bytes
  start: number;
  // Mirror length in bytes
  length: number;
  // Address in unified memory to mirror from (already initialized region)
  mirrorFrom: number;
};

type MemoryLayout = {
  // Direct regions copied from ROM/RAM/zero
  regions?: MemoryRegion[];
  // Mirrors that duplicate an initialized span into another address range
  mirrors?: MirrorRegion[];
  // Optional minimum capacity for the unified memory buffer
  minimumCapacity?: number; // default: max end of regions/mirrors
};

type CreateSystemOptions = {
  rom: Uint8Array;
  ramSize: number;
  memoryLayout?: MemoryLayout;
};
```

Notes:
- `regions` cover the initial placement (e.g., mapping a contiguous portion of the ROM at some address, or seeding a RAM window).
- `mirrors` copy bytes from one already‑initialized span to another (same length), enabling simple banked/mirrored windows without duplicating storage in the source.
- If a `minimumCapacity` is provided, allocate at least that many bytes even if the regions are smaller.

## Implementation Outline

File: `packages/core/src/musashi-wrapper.ts`

1) Compute unified memory capacity
   - If `memoryLayout` is provided, compute the maximum end offset across `regions` and `mirrors` and take the max of that and `minimumCapacity`.
   - Otherwise, keep the current default (e.g., 2 MB) for backward compatibility.

2) Allocate unified memory
   - Replace the fixed `new Uint8Array(N)` with a capacity computed as above.

3) Initialize base regions
   - For each `MemoryRegion`:
     - Validate bounds (`start >= 0`, `length > 0`, `start + length <= capacity`).
     - Resolve source bytes: `rom.subarray(sourceOffset, sourceOffset + length)`, a RAM view, or zeros.
     - Copy into `unifiedMemory.subarray(start, start + length)`.

4) Apply mirrors
   - For each `MirrorRegion`:
     - Validate bounds for `mirrorFrom..mirrorFrom+length` and `start..start+length`.
     - Copy from `unifiedMemory.subarray(mirrorFrom, mirrorFrom + length)` into `unifiedMemory.subarray(start, start + length)`.
   - Mirrors must run after base regions have been copied to ensure the source spans are initialized.

5) Initialize RAM backing
   - Continue to maintain the separate `system.ram` buffer for writes and reflect writes into unified memory within the configured RAM window(s).
   - If `memoryLayout` was not provided for RAM, preserve the existing default RAM window (e.g., starting at a fixed address), maintaining backward compatibility.

6) Reset vectors and CPU init
   - Keep existing reset vector writes (or use a callback to allow consumers to customize). These are independent of the mapping.

7) Memtrace and hooks
   - No change required. The memory trace bridge should continue to report accesses at absolute addresses in unified memory.

## Backward Compatibility

When `memoryLayout` is omitted:
- Allocate the current default unified memory size.
- Map ROM and RAM using existing defaults.
- Behavior of `getRegisters`, `step`, `execute`, and `onMemoryRead/Write` remains unchanged.

When `memoryLayout` is provided:
- Respect the provided regions/mirrors for initial content.
- Preserve current APIs and event shapes.

## Example Usage (Illustrative)

The following shows how a consumer might map two contiguous ROM windows and mirror one into another window. Addresses and sizes are illustrative only.

```ts
const memoryLayout: MemoryLayout = {
  regions: [
    { start: 0x000000, length: 0x100000, source: 'rom', sourceOffset: 0x000000 },
    { start: 0x100000, length: 0x100000, source: 'ram' },
    // Optionally map a third window from a later offset in ROM
    { start: 0x300000, length: 0x080000, source: 'rom', sourceOffset: 0x180000 },
  ],
  mirrors: [
    // Mirror the first 1MB of ROM into another address window
    { start: 0x200000, length: 0x100000, mirrorFrom: 0x000000 },
  ],
  minimumCapacity: 0x400000,
};

const sys = await createSystem({ rom, ramSize: 0x100000, memoryLayout });
```

## Validation Strategy

Add unit tests under `packages/core/src/` covering:
- Mapping: reads at the start/end of each region return the expected bytes from the proper source offsets.
- Mirroring: reads within a mirror window equal the corresponding source window bytes.
- Capacity: unified memory is at least as large as the maximum covered address.
- Backward compatibility: constructing without `memoryLayout` behaves as before.

## Performance Considerations

- All initialization occurs once during `createSystem`; steady‑state reads are index lookups into a single `Uint8Array`.
- Mirroring uses a single `set` (typed array copy) per mirror region.
- RAM writes already update the external RAM buffer and can continue to update unified memory in place to keep both views consistent.

## Error Handling

- Validate every region/mirror for bounds before copying. Throw a descriptive error on misconfiguration.
- If overlapping regions are specified, the last writer wins; document this explicitly or forbid overlaps.

## Incremental Adoption Plan

1) Add the types and new `memoryLayout` option to the factory.
2) Implement capacity computation + region/mirror application in the wrapper.
3) Add unit tests.
4) Publish a minor version with release notes highlighting the optional feature and compatibility.

This small, generic facility enables downstreams to align the wrapper’s memory view with their address map requirements—without hardcoding platform‑specific assumptions into the core.

