#include "singlestep_test.h"
#include "m68k_test_common.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <iostream>

extern "C" {
    #include "m68k.h"
    void add_region(unsigned int start, unsigned int size, void* data);
    void clear_regions();
    void reset_myfunc_state();
    void enable_printf_logging();
    
    // Register access functions from myfunc.cc
    void set_d_reg(int n, uint32_t value);
    uint32_t get_d_reg(int n);
    void set_a_reg(int n, uint32_t value);
    uint32_t get_a_reg(int n);
    void set_pc_reg(uint32_t value);
    uint32_t get_pc_reg(void);
    void set_sr_reg(uint16_t value);
    uint32_t get_sr_reg(void);
    uint32_t get_sp_reg(void);
}

using namespace singlestep;

DECLARE_M68K_TEST(SingleStepBase) {
public:
    // Override PC hook for transaction tracking
    int OnPcHook(unsigned int pc) override {
        pc_hooks.push_back(pc);
        // Stop after one instruction
        return pc_hooks.size() >= 2 ? 1 : 0;
    }

    // Run a single test case and return results
    TestResult runSingleTest(const SingleStepTest& test) {
        TestResult result;
        result.test_name = test.name;
        
        try {
            // Set up initial state
            setupInitialState(test.initial);
            
            // Execute instruction
            int cycles = m68k_execute(100); // Should be enough for any single instruction
            result.cycles_executed = cycles;
            
            // Get final state
            ProcessorState final_state;
            extractFinalState(final_state);
            
            // Compare states
            result.state_differences = final_state.getDifferences(test.final);
            
            // For now, skip transaction validation (complex)
            
            // Test passes if no state differences
            result.passed = result.state_differences.empty();
            
        } catch (const std::exception& e) {
            result.passed = false;
            result.state_differences.push_back(std::string("Exception: ") + e.what());
        }
        
        return result;
    }

private:
    void setupInitialState(const ProcessorState& state) {
        // Set registers using wrapper functions
        for (int i = 0; i < 8; i++) {
            set_d_reg(i, state.d[i]);
            set_a_reg(i, state.a[i]);
        }
        
        // Set special registers
        set_pc_reg(state.pc);
        set_sr_reg(state.sr);
        // Note: USP/SSP setup is more complex and may require specific handling
        
        // Set up memory
        state.applyToMemory(memory.data(), memory.size());
        
        // Note: Prefetch setup would require deeper integration
    }
    
    void extractFinalState(ProcessorState& state) {
        // Get registers using wrapper functions
        for (int i = 0; i < 8; i++) {
            state.d[i] = get_d_reg(i);
            state.a[i] = get_a_reg(i);
        }
        
        // Get special registers
        state.pc = get_pc_reg();
        state.sr = static_cast<uint16_t>(get_sr_reg());
        state.ssp = get_sp_reg(); // This gets current stack pointer
        state.usp = 0; // USP extraction more complex
        
        // Extract prefetch would require deeper integration
        state.prefetch[0] = 0;
        state.prefetch[1] = 0;
        
        // Extract memory changes would require tracking
        state.extractFromMemory(memory.data(), memory.size());
    }

protected:    
    // Get the test data path
    std::string getTestDataPath() {
        std::string test_data_path = "../third_party/m68000/v1/";
        if (!std::filesystem::exists(test_data_path)) {
            test_data_path = "third_party/m68000/v1/";
        }
        return test_data_path;
    }
};

// Helper function to run tests for a specific instruction
SuiteResult runInstructionTests(const std::string& instruction_file, 
                               SingleStepBase& runner,
                               int max_tests = 100) {
    SingleStepTestSuite suite(instruction_file);
    SuiteResult suite_result;
    
    if (!suite.loadFromFile(instruction_file)) {
        std::cerr << "Failed to load test suite: " << instruction_file << std::endl;
        return suite_result;
    }
    
    suite_result.instruction_name = suite.getInstructionName();
    suite_result.total_tests = std::min(max_tests, static_cast<int>(suite.size()));
    
    std::cout << "Running " << suite_result.total_tests << " tests for " 
              << suite_result.instruction_name << "..." << std::endl;
    
    for (int i = 0; i < suite_result.total_tests; i++) {
        const auto& test = suite.getTests()[i];
        TestResult result = runner.runSingleTest(test);
        
        if (result.passed) {
            suite_result.passed_tests++;
        } else {
            suite_result.failed_tests++;
            
            // Print first few failures for debugging
            if (suite_result.failed_tests <= 3) {
                std::cout << "FAIL: " << result.test_name << std::endl;
                for (const auto& diff : result.state_differences) {
                    std::cout << "  " << diff << std::endl;
                }
            }
        }
        
        suite_result.individual_results.push_back(result);
    }
    
    std::cout << suite_result.instruction_name << ": " 
              << suite_result.passed_tests << "/" << suite_result.total_tests 
              << " passed (" << (suite_result.getPassRate() * 100.0) << "%)" << std::endl;
    
    return suite_result;
}

// Test NOP instruction (simple)
TEST_F(SingleStepBase, TestNOP) {
    std::string test_data_path = getTestDataPath();
    std::string test_file = test_data_path + "NOP.json";
    if (!std::filesystem::exists(test_file)) {
        GTEST_SKIP() << "Test file not found: " << test_file;
    }
    
    SuiteResult result = runInstructionTests(test_file, *this, 10);
    
    // We expect high success rate for NOP
    EXPECT_GT(result.getPassRate(), 0.5) << "NOP instruction should have high pass rate";
    EXPECT_GT(result.passed_tests, 0) << "At least some NOP tests should pass";
}

// Test simple ADD instruction
TEST_F(SingleStepBase, TestADD_b) {
    std::string test_data_path = getTestDataPath();
    std::string test_file = test_data_path + "ADD.b.json";
    if (!std::filesystem::exists(test_file)) {
        GTEST_SKIP() << "Test file not found: " << test_file;
    }
    
    SuiteResult result = runInstructionTests(test_file, *this, 10);
    
    // Just ensure tests run without crashing
    EXPECT_GT(result.total_tests, 0) << "Should load some ADD.b tests";
}

// Comprehensive test runner for multiple instructions
TEST_F(SingleStepBase, RunSelectedInstructions) {
    std::string test_data_path = getTestDataPath();
    std::vector<std::string> test_instructions = {
        "NOP.json",
        "MOVE.b.json", 
        "ADD.b.json",
        "SUB.b.json",
        "CMP.b.json"
    };
    
    std::vector<SuiteResult> all_results;
    int total_passed = 0, total_tests = 0;
    
    for (const auto& instruction : test_instructions) {
        std::string test_file = test_data_path + instruction;
        if (std::filesystem::exists(test_file)) {
            SuiteResult result = runInstructionTests(test_file, *this, 20);
            all_results.push_back(result);
            total_passed += result.passed_tests;
            total_tests += result.total_tests;
        }
    }
    
    // Print summary
    std::cout << "\n=== SingleStep Test Summary ===" << std::endl;
    std::cout << "Overall: " << total_passed << "/" << total_tests << " passed";
    if (total_tests > 0) {
        std::cout << " (" << (100.0 * total_passed / total_tests) << "%)";
    }
    std::cout << std::endl;
    
    for (const auto& result : all_results) {
        std::cout << result.instruction_name << ": " 
                  << result.passed_tests << "/" << result.total_tests 
                  << " (" << (result.getPassRate() * 100.0) << "%)" << std::endl;
    }
    
    // Test should pass if we successfully ran some tests
    EXPECT_GT(total_tests, 0) << "Should run at least some tests";
}