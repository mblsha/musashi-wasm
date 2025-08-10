# Perfetto Memory Management Analysis

## Question: Can Perfetto be disabled to avoid memory usage when not actively tracing?

### Answer: YES - Memory usage is well controlled

## Memory Behavior

### 1. When ENABLE_PERFETTO=OFF (Compile-time)
- **Zero memory overhead** - Perfetto code not compiled
- All functions are inline no-ops
- No allocations whatsoever

### 2. When ENABLE_PERFETTO=ON but m68k_perfetto_init() not called
- **Zero memory overhead** - No objects created
- Perfetto code is linked but dormant
- No static/global allocations

### 3. When m68k_perfetto_init() called but tracing disabled
```c
m68k_perfetto_init("MyEmulator");  // Creates tracer
m68k_perfetto_enable_flow(0);      // But don't enable tracing
m68k_perfetto_enable_memory(0);
m68k_perfetto_enable_instructions(0);
```

**Small fixed overhead (~10-20 KB):**
- PerfettoTraceBuilder object created
- Initial Trace protobuf message
- 6 track descriptor packets (metadata only):
  - Process descriptor (1 packet)
  - Thread tracks: Flow, Jumps, Instructions, Writes (4 packets)
  - Counter tracks: Memory_Access, CPU_Cycles (2 packets)
- **NO EVENT ACCUMULATION** - Early-exit checks prevent event creation

### 4. When tracing is enabled
```c
m68k_perfetto_enable_flow(1);      // Events will accumulate
m68k_perfetto_enable_memory(1);
m68k_perfetto_enable_instructions(1);
```

**Memory grows with events:**
- Each instruction: ~100-200 bytes
- Each memory access: ~50-100 bytes
- Each flow event (call/return): ~200-400 bytes
- Can grow to MB/GB depending on trace duration

## Implementation Details

### Early-Exit Pattern
All event handlers check enabled flags first:

```cpp
int M68kPerfettoTracer::handle_flow_event(...) {
    if (!flow_enabled_) {
        return 0;  // No allocation, immediate return
    }
    // ... create event only if enabled
}

int M68kPerfettoTracer::handle_instruction_event(...) {
    if (!instruction_enabled_) {
        return 0;  // No allocation, immediate return
    }
    // ... create event only if enabled
}

int M68kPerfettoTracer::handle_memory_event(...) {
    if (!memory_enabled_) {
        return 0;  // No allocation, immediate return
    }
    // ... create event only if enabled
}
```

### Memory Allocation Points

1. **On m68k_perfetto_init():**
   - Allocates PerfettoTraceBuilder
   - Creates initial protobuf Trace message
   - Adds ~7 metadata packets (track descriptors)
   - Total: ~10-20 KB

2. **During tracing (only if enabled):**
   - Each trace_builder_->begin_slice() adds a packet
   - Each trace_builder_->add_instant_event() adds a packet
   - Each trace_builder_->update_counter() adds a packet
   - Packets accumulate in memory until exported

3. **No allocation when disabled:**
   - Early-exit checks prevent any event creation
   - Only the initial tracer object remains in memory

## Best Practices

### For Minimal Memory Usage

1. **Don't initialize unless needed:**
```c
// Only initialize when user requests tracing
if (user_wants_tracing) {
    m68k_perfetto_init("MyEmulator");
}
```

2. **Lazy initialization pattern:**
```c
static bool perfetto_initialized = false;

void start_tracing() {
    if (!perfetto_initialized) {
        m68k_perfetto_init("MyEmulator");
        perfetto_initialized = true;
    }
    m68k_perfetto_enable_flow(1);
    // ... enable other features
}

void stop_tracing() {
    m68k_perfetto_enable_flow(0);
    m68k_perfetto_enable_memory(0);
    m68k_perfetto_enable_instructions(0);
    // Events stop accumulating immediately
}
```

3. **Periodic export to free memory:**
```c
// Export and clear trace periodically
void export_and_clear() {
    uint8_t* data;
    size_t size;
    if (m68k_perfetto_export_trace(&data, &size) == 0) {
        save_to_file(data, size);
        m68k_perfetto_free_trace_data(data);
        
        // Destroy and reinit to clear internal buffers
        m68k_perfetto_destroy();
        m68k_perfetto_init("MyEmulator");
    }
}
```

## Memory Usage Summary

| State | Memory Usage | Growth |
|-------|-------------|--------|
| ENABLE_PERFETTO=OFF | 0 bytes | None |
| Compiled but not initialized | 0 bytes | None |
| Initialized but tracing disabled | ~10-20 KB | None (fixed) |
| Tracing enabled | 10-20 KB + events | Continuous |

## Conclusion

**Yes, Perfetto can be effectively disabled to prevent memory usage:**

1. **Complete disable**: Don't call `m68k_perfetto_init()` - zero memory
2. **Runtime disable**: Call init but disable tracing - minimal fixed memory (~10-20 KB)
3. **Smart pattern**: Only initialize when user requests tracing
4. **Memory control**: Events only accumulate when explicitly enabled

The design ensures:
- No memory waste when not tracing
- Immediate stop of memory growth when disabled
- Clean separation between initialization and activation
- Predictable memory behavior