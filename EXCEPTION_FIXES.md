# M68000 Exception Handling Fixes Required

Based on failing tests and expert analysis, the Musashi emulator has incorrect M68000 exception handling. This document outlines the required fixes.

## Core Issue: RTE Implementation for 68000

The RTE (Return from Exception) instruction on 68000 is not properly popping the exception frame and restoring state.

### Current Symptoms
1. **Test #1 (IllegalInstructionException)**: PC not restored to next instruction after exception
2. **Test #4 (ExceptionStackFrame)**: Stack pointer off by 6 bytes (one 68000 frame) after RTE

### Required RTE Implementation for 68000

```c
// In m68kops.c or wherever RTE (0x4E73) is implemented
void m68k_op_rte_32(void)
{
    // 1. RTE is privileged - must be in supervisor mode
    if((CPU_SR & 0x2000) == 0) {
        m68ki_exception_privilege_violation();
        return;
    }

    if(CPU_TYPE_IS_000()) {
        // 68000: Simple 6-byte frame (SR + PC)
        
        // 2. Pop SR from supervisor stack
        uint new_sr = m68ki_pull_16();
        
        // 3. Pop PC from supervisor stack  
        uint new_pc = m68ki_pull_32();
        
        // 4. Restore SR (this handles S-bit and stack switching)
        m68ki_set_sr(new_sr);
        
        // 5. Jump to restored PC
        m68ki_jump(new_pc);
    }
    else {
        // 68010+: Handle format word
        // ... existing 68010+ code ...
    }
}
```

### Key Points for RTE Fix
1. **Must pop exactly 6 bytes on 68000**: 2 bytes SR + 4 bytes PC
2. **SR restoration must handle S-bit changes**: If S bit changes from 1→0, switch from SSP to USP
3. **No format word on 68000**: Unlike 68010+, 68000 has no format/vector offset word

## Exception Entry Fixes

### Stacked PC Must Be Next Instruction Address

For these exceptions, the stacked PC should point to the **next instruction** (resume point), not the faulting instruction:
- Illegal instruction (vector 4)
- Privilege violation (vector 8)  
- CHK instruction (vector 6)
- Zero divide (vector 5)

```c
// Example for illegal instruction exception
void m68ki_exception_illegal(void)
{
    uint sr = m68ki_get_sr();
    
    // Switch to supervisor mode
    FLAG_S = 1;
    
    // Push current SR
    m68ki_push_16(sr);
    
    // Push PC of NEXT instruction (not current PC)
    m68ki_push_32(REG_PC);  // REG_PC should already point to next instruction
    
    // Jump to vector
    m68ki_jump(m68ki_read_32(EXCEPTION_ILLEGAL_INSTRUCTION << 2));
}
```

## Stack Pointer Management

### Ensure Proper SSP/USP Switching

When the S-bit changes:
- **S: 0→1**: Save current A7 to USP, load A7 from SSP
- **S: 1→0**: Save current A7 to SSP, load A7 from USP

```c
void m68ki_set_sr(uint value)
{
    uint old_s = FLAG_S;
    uint new_s = (value >> 11) & 1;
    
    // Update SR
    CPU_SR = value;
    FLAG_S = new_s;
    
    // Handle stack switch if S-bit changed
    if(old_s != new_s) {
        if(new_s) {
            // User → Supervisor
            REG_USP = REG_A[7];
            REG_A[7] = REG_SP;  // REG_SP holds SSP when S=1
        } else {
            // Supervisor → User  
            REG_SP = REG_A[7];   // Save SSP
            REG_A[7] = REG_USP;
        }
    }
}
```

## Vector Base Register (VBR)

On 68000, there is no VBR - vectors are always at address 0:

```c
uint m68ki_get_exception_vector_address(uint vector)
{
    if(CPU_TYPE_IS_000()) {
        // 68000: No VBR, vectors at 0
        return vector << 2;
    } else {
        // 68010+: Use VBR
        return (REG_VBR + (vector << 2));
    }
}
```

## Test-Specific Notes

### Test #2 (PrivilegeViolationException)
- **Fixed in test**: Changed expectation to user mode after RTE (unless handler modifies stacked SR)
- The emulator behavior might be correct here once RTE is fixed

### Test #3 (CHKInstructionException)  
- **Fixed in test**: Corrected opcode from 0x4180 to 0x4181
- CHK D1,D0 checks D0 against bound in D1
- Emulator must trigger exception when D0 < 0 or D0 > D1 (unsigned comparison for upper bound)

## Testing After Fixes

After implementing these fixes, run:
```bash
cd build
./test_exceptions
```

Expected results:
- All exception tests should pass
- Stack pointer properly restored
- PC correctly points to instruction after exception
- Mode transitions work correctly

## References
- M68000 Programmer's Reference Manual (Exception Processing section)
- 68000 exception frame format: 6 bytes (SR + PC)
- 68010+ exception frame format: Variable with format word