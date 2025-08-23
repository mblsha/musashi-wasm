#include "singlestep_test.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cassert>
#include <cstring>

namespace singlestep {

// Simple JSON parsing helpers
namespace {
    std::string trim(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }
    
    std::string extractStringValue(const std::string& line) {
        size_t start = line.find('"');
        if (start == std::string::npos) return "";
        start++; // Skip opening quote
        size_t end = line.find('"', start);
        if (end == std::string::npos) return "";
        return line.substr(start, end - start);
    }
    
    uint32_t extractIntValue(const std::string& line) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) return 0;
        std::string value = trim(line.substr(colon + 1));
        if (value.back() == ',') value.pop_back(); // Remove trailing comma
        return std::stoul(value);
    }
    
    std::vector<uint32_t> parseArray(const std::string& line) {
        std::vector<uint32_t> result;
        size_t start = line.find('[');
        size_t end = line.find(']');
        if (start == std::string::npos || end == std::string::npos) return result;
        
        std::string content = line.substr(start + 1, end - start - 1);
        std::istringstream ss(content);
        std::string item;
        
        while (std::getline(ss, item, ',')) {
            item = trim(item);
            if (!item.empty()) {
                result.push_back(std::stoul(item));
            }
        }
        return result;
    }
}

void ProcessorState::applyToMemory(uint8_t* memory, size_t memory_size) const {
    for (const auto& ram_entry : ram) {
        uint32_t addr = ram_entry.first;
        uint8_t value = ram_entry.second;
        if (addr < memory_size) {
            memory[addr] = value;
        }
    }
}

void ProcessorState::extractFromMemory(const uint8_t* memory, size_t memory_size) {
    ram.clear();
    // For comparison, we'd need to know which addresses to check
    // This would be populated based on the expected final state
}

bool ProcessorState::operator==(const ProcessorState& other) const {
    // Compare registers
    for (int i = 0; i < 8; i++) {
        if (d[i] != other.d[i] || a[i] != other.a[i]) return false;
    }
    
    if (usp != other.usp || ssp != other.ssp || sr != other.sr || pc != other.pc) {
        return false;
    }
    
    if (prefetch[0] != other.prefetch[0] || prefetch[1] != other.prefetch[1]) {
        return false;
    }
    
    // Compare RAM (this is complex - for now just check size)
    return ram.size() == other.ram.size();
}

std::vector<std::string> ProcessorState::getDifferences(const ProcessorState& other) const {
    std::vector<std::string> differences;
    
    // Check data registers
    for (int i = 0; i < 8; i++) {
        if (d[i] != other.d[i]) {
            differences.push_back("D" + std::to_string(i) + ": expected " + 
                std::to_string(other.d[i]) + ", got " + std::to_string(d[i]));
        }
        if (a[i] != other.a[i]) {
            differences.push_back("A" + std::to_string(i) + ": expected " + 
                std::to_string(other.a[i]) + ", got " + std::to_string(a[i]));
        }
    }
    
    // Check special registers
    if (usp != other.usp) {
        differences.push_back("USP: expected " + std::to_string(other.usp) + 
                            ", got " + std::to_string(usp));
    }
    if (ssp != other.ssp) {
        differences.push_back("SSP: expected " + std::to_string(other.ssp) + 
                            ", got " + std::to_string(ssp));
    }
    if (sr != other.sr) {
        differences.push_back("SR: expected " + std::to_string(other.sr) + 
                            ", got " + std::to_string(sr));
    }
    if (pc != other.pc) {
        differences.push_back("PC: expected " + std::to_string(other.pc) + 
                            ", got " + std::to_string(pc));
    }
    
    // Check prefetch
    if (prefetch[0] != other.prefetch[0]) {
        differences.push_back("Prefetch[0]: expected " + std::to_string(other.prefetch[0]) + 
                            ", got " + std::to_string(prefetch[0]));
    }
    if (prefetch[1] != other.prefetch[1]) {
        differences.push_back("Prefetch[1]: expected " + std::to_string(other.prefetch[1]) + 
                            ", got " + std::to_string(prefetch[1]));
    }
    
    return differences;
}

SingleStepTest SingleStepTest::parseFromJson(const std::string& json_object) {
    SingleStepTest test;
    std::istringstream ss(json_object);
    std::string line;
    
    enum ParseState { NONE, INITIAL, FINAL, TRANSACTIONS } state = NONE;
    
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty() || line == "{" || line == "}" || line == ",") continue;
        
        // Parse test name
        if (line.find("\"name\"") != std::string::npos) {
            test.name = extractStringValue(line);
        }
        
        // Parse length
        if (line.find("\"length\"") != std::string::npos) {
            test.length = extractIntValue(line);
        }
        
        // State parsing
        if (line.find("\"initial\"") != std::string::npos) {
            state = INITIAL;
            continue;
        }
        if (line.find("\"final\"") != std::string::npos) {
            state = FINAL;
            continue;
        }
        if (line.find("\"transactions\"") != std::string::npos) {
            state = TRANSACTIONS;
            continue;
        }
        
        // Parse processor state fields
        ProcessorState* current_state = nullptr;
        if (state == INITIAL) current_state = &test.initial;
        else if (state == FINAL) current_state = &test.final;
        
        if (current_state) {
            // Parse registers
            for (int i = 0; i < 8; i++) {
                if (line.find("\"d" + std::to_string(i) + "\"") != std::string::npos) {
                    current_state->d[i] = extractIntValue(line);
                }
                if (line.find("\"a" + std::to_string(i) + "\"") != std::string::npos) {
                    current_state->a[i] = extractIntValue(line);
                }
            }
            
            if (line.find("\"usp\"") != std::string::npos) {
                current_state->usp = extractIntValue(line);
            }
            if (line.find("\"ssp\"") != std::string::npos) {
                current_state->ssp = extractIntValue(line);
            }
            if (line.find("\"sr\"") != std::string::npos) {
                current_state->sr = static_cast<uint16_t>(extractIntValue(line));
            }
            if (line.find("\"pc\"") != std::string::npos) {
                current_state->pc = extractIntValue(line);
            }
            
            // Parse prefetch
            if (line.find("\"prefetch\"") != std::string::npos) {
                auto values = parseArray(line);
                if (values.size() >= 2) {
                    current_state->prefetch[0] = static_cast<uint16_t>(values[0]);
                    current_state->prefetch[1] = static_cast<uint16_t>(values[1]);
                }
            }
            
            // Parse RAM entries (simplified for now)
            if (line.find("\"ram\"") != std::string::npos) {
                // This is complex - would need to parse nested arrays
                // For now, skip detailed RAM parsing
            }
        }
        
        // Parse transactions (simplified)
        if (state == TRANSACTIONS) {
            // Transaction parsing would go here
            // Format: [type, cycles, byte_enable, address, size, data, uds, lds]
            // This is also complex and would need careful parsing
        }
    }
    
    return test;
}

uint16_t SingleStepTest::getOpcode() const {
    // Extract opcode from test name (e.g. "000 NOP 4e71" -> 0x4e71)
    size_t pos = name.find_last_of(' ');
    if (pos != std::string::npos) {
        std::string opcode_str = name.substr(pos + 1);
        return static_cast<uint16_t>(std::stoul(opcode_str, nullptr, 16));
    }
    return 0;
}

std::string SingleStepTest::getMnemonic() const {
    // Extract mnemonic from test name (e.g. "000 NOP 4e71" -> "NOP")
    size_t first_space = name.find(' ');
    if (first_space != std::string::npos) {
        size_t second_space = name.find(' ', first_space + 1);
        if (second_space != std::string::npos) {
            return name.substr(first_space + 1, second_space - first_space - 1);
        }
    }
    return "";
}

bool SingleStepTestSuite::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    tests_.clear();
    
    std::string line;
    std::string current_test;
    bool in_test = false;
    int brace_count = 0;
    
    while (std::getline(file, line)) {
        line = trim(line);
        
        if (line == "[" || line.empty()) continue;
        
        if (line == "{") {
            if (!in_test) {
                in_test = true;
                current_test = "{\n";
                brace_count = 1;
            } else {
                current_test += line + "\n";
                brace_count++;
            }
        } else if (line == "}," || line == "}") {
            current_test += "}\n";
            brace_count--;
            
            if (brace_count == 0) {
                // End of test case
                SingleStepTest test = SingleStepTest::parseFromJson(current_test);
                if (!test.name.empty()) {
                    tests_.push_back(test);
                }
                in_test = false;
                current_test.clear();
            }
        } else if (in_test) {
            current_test += line + "\n";
        }
    }
    
    return !tests_.empty();
}

} // namespace singlestep