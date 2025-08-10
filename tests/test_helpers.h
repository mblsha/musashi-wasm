/* Helper functions for tests to work both locally and in CI */

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <string>
#include <fstream>

/* Try multiple paths to find test files - works both locally and in CI */
inline std::string FindTestFile(const std::string& filename) {
    // Try paths in order of likelihood
    const std::string paths[] = {
        "tests/" + filename,           // CI build directory
        "../tests/" + filename,        // Local build from build/
        filename,                       // Current directory
        "build/tests/" + filename      // Alternative
    };
    
    for (const auto& path : paths) {
        std::ifstream test(path);
        if (test.good()) {
            return path;
        }
    }
    
    // Return first path as fallback
    return paths[0];
}

#endif // TEST_HELPERS_H