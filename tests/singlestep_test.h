#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace singlestep {

/**
 * Represents a memory access transaction during instruction execution.
 * Format: [type, cycles, byte_enable, address, size_suffix, data, uds, lds]
 */
struct Transaction {
    std::string type;      // "r" (read), "w" (write), "n" (no-op/internal), etc.
    int cycles;           // Number of cycles for this transaction
    int byte_enable;      // Byte enable flags
    uint32_t address;     // Memory address (0 for internal operations)
    std::string size;     // ".b", ".w", ".l" or "" for internal
    uint32_t data;        // Data value (0 for internal operations)
    int uds;              // Upper data strobe
    int lds;              // Lower data strobe
    
    Transaction() : cycles(0), byte_enable(0), address(0), data(0), uds(0), lds(0) {}
};

/**
 * Represents CPU state (registers and memory) at a point in time.
 */
struct ProcessorState {
    // Data registers D0-D7
    uint32_t d[8];
    
    // Address registers A0-A7
    uint32_t a[8];
    
    // Special registers
    uint32_t usp;           // User stack pointer
    uint32_t ssp;           // Supervisor stack pointer  
    uint16_t sr;            // Status register
    uint32_t pc;            // Program counter
    
    // Prefetch queue (2 words)
    uint16_t prefetch[2];
    
    // Memory contents: vector of [address, byte_value] pairs
    std::vector<std::pair<uint32_t, uint8_t>> ram;
    
    ProcessorState() : usp(0), ssp(0), sr(0), pc(0) {
        for (int i = 0; i < 8; i++) {
            d[i] = 0;
            a[i] = 0;
        }
        prefetch[0] = prefetch[1] = 0;
    }
    
    // Apply RAM contents to a memory buffer
    void applyToMemory(uint8_t* memory, size_t memory_size) const;
    
    // Extract RAM contents from a memory buffer for comparison
    void extractFromMemory(const uint8_t* memory, size_t memory_size);
    
    // Compare two processor states
    bool operator==(const ProcessorState& other) const;
    bool operator!=(const ProcessorState& other) const { return !(*this == other); }
    
    // Get differences between states for debugging
    std::vector<std::string> getDifferences(const ProcessorState& other) const;
};

/**
 * Represents a single M68000 instruction test case.
 */
struct SingleStepTest {
    std::string name;                    // Test name (e.g. "000 NOP 4e71")
    ProcessorState initial;              // Initial CPU state
    ProcessorState final;                // Expected final CPU state
    std::vector<Transaction> transactions; // Expected bus transactions
    int length;                         // Instruction length in cycles
    
    // Parse a test case from JSON object string
    static SingleStepTest parseFromJson(const std::string& json_object);
    
    // Helper functions for JSON parsing
    static void parseProcessorState(const std::string& section, ProcessorState& state);
    static void parseRegisterValue(const std::string& section, const std::string& pattern, uint32_t& value);
    
    // Get instruction opcode from test name
    uint16_t getOpcode() const;
    
    // Get instruction mnemonic
    std::string getMnemonic() const;
};

/**
 * Collection of test cases for a single instruction type.
 */
class SingleStepTestSuite {
private:
    std::string instruction_name_;
    std::vector<SingleStepTest> tests_;
    
public:
    explicit SingleStepTestSuite(const std::string& instruction_name) 
        : instruction_name_(instruction_name) {}
    
    // Load test cases from JSON file
    bool loadFromFile(const std::string& filename);
    
    // Get all test cases
    const std::vector<SingleStepTest>& getTests() const { return tests_; }
    
    // Get instruction name
    const std::string& getInstructionName() const { return instruction_name_; }
    
    // Get number of test cases
    size_t size() const { return tests_.size(); }
};

/**
 * Results from running a single test case.
 */
struct TestResult {
    bool passed;
    std::string test_name;
    std::vector<std::string> state_differences;
    std::vector<std::string> transaction_differences;
    int cycles_executed;
    
    TestResult() : passed(false), cycles_executed(0) {}
};

/**
 * Results from running an entire test suite.
 */
struct SuiteResult {
    std::string instruction_name;
    int total_tests;
    int passed_tests;
    int failed_tests;
    std::vector<TestResult> individual_results;
    
    SuiteResult() : total_tests(0), passed_tests(0), failed_tests(0) {}
    
    double getPassRate() const {
        return total_tests > 0 ? (double)passed_tests / total_tests : 0.0;
    }
};

} // namespace singlestep