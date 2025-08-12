# Musashi WASM Node.js Integration Test

This is a self-contained test project to verify the functionality of the Musashi M68k WebAssembly module in a Node.js environment. It uses the Jest testing framework.

The test validates the core JavaScript-to-WASM API, ensuring that functions for CPU control, register access, memory management, and instruction hooking are correctly exported and operational.

## Prerequisites

1. **Node.js and npm:** You must have Node.js (version 16 or later) and npm installed.
2. **Musashi WASM Build:** You must have successfully built the Musashi project. This test specifically targets the Node.js artifact.

## Setup

1. **Build Musashi:** Navigate to the root of the Musashi repository and run the build command:
   ```bash
   make wasm
   # or ./build.fish
   ```
   This will generate `musashi-node.out.js` and `musashi-node.out.wasm` in the root directory.

2. **Install Dependencies:** Navigate into this directory and install the testing framework.
   ```bash
   cd musashi-wasm-test
   npm install
   ```

## Running the Test

To run the integration test, simply execute the `test` script defined in `package.json`:

```bash
npm test
```

You should see output from Jest indicating that the tests have passed.

```
PASS  tests/wasm_integration.test.js
  Musashi WASM Node.js Integration Test
    ✓ should correctly execute a simple program using the full API bridge
    ✓ should disassemble an instruction correctly
    ✓ should handle memory regions correctly
    ✓ should track cycle counts correctly
    ✓ should support all exported register access functions
    ✓ should handle instruction hook interruption correctly

Test Suites: 1 passed, 1 total
Tests:       6 passed, 6 total
Snapshots:   0 total
Time:        X.XXX s
Ran all test suites.
```

## Test Coverage

The integration test suite validates:

1. **Module Loading and Instantiation** - Verifies the WASM module loads correctly in Node.js
2. **Memory Management** - Tests memory regions and callback functions
3. **CPU Control** - Tests reset, execution, and cycle counting
4. **Register Access** - Validates reading and writing CPU registers
5. **Instruction Hooking** - Tests PC hook functionality and execution interruption
6. **Disassembly** - Verifies the disassembler is accessible from JavaScript

## Test Scenarios

### Main Test Scenario
The primary test executes a simple M68k program:
```assembly
MOVE.L #$DEADBEEF, D0  ; Load immediate value to D0
MOVE.L D0, $10000      ; Store D0 to memory address
STOP #$2700            ; Stop execution
```

This tests:
- Memory region mapping
- Instruction execution
- Register operations
- Memory callbacks for addresses outside mapped regions
- PC hook tracking

### Additional Tests
- **Disassembly**: Tests NOP instruction disassembly
- **Memory Regions**: Validates region-based memory access vs callback-based
- **Cycle Counting**: Verifies cycle tracking functionality
- **Register Access**: Tests all register getter/setter functions
- **Hook Interruption**: Tests early termination via PC hook return value

## Troubleshooting

If the tests fail:

1. **Module Not Found**: Ensure you've built the WASM module with `make wasm` or `./build.fish`
2. **Function Not Exported**: Check that the build script includes all necessary exports
3. **Memory Issues**: Verify that memory allocation and cleanup are working correctly
4. **Hook Failures**: Ensure function pointers are correctly managed with `addFunction`/`removeFunction`

## CI/CD Integration

This test suite is automatically run in GitHub Actions as part of the WASM CI pipeline:

1. **Automatic Execution**: Tests run after every WASM build in CI
2. **Test Reporting**: JUnit XML results are generated and uploaded as artifacts
3. **Visual Results**: Test results appear in the GitHub Actions UI with pass/fail status
4. **Both Build Variants**: Tests run for both standard and Perfetto-enabled builds

The CI configuration can be found in `.github/workflows/wasm-ci.yml`.

## Directory Structure

```
musashi/
├── musashi-node.out.js        # Generated WASM module (Node.js version)
├── musashi-node.out.wasm      # Generated WASM binary
├── musashi-wasm-test/         # This test directory
│   ├── package.json           # Project configuration
│   ├── README.md              # This file
│   ├── load-musashi.js        # Module loader wrapper (handles mixed exports)
│   ├── test-results.xml       # JUnit test results (generated after test run)
│   └── tests/
│       └── wasm_integration.test.js  # Main test suite
└── ... (other Musashi source files)
```