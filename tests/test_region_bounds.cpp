// Tests for region end-bound access safety

#include "m68k_test_common.h"

extern "C" {
    void add_region(unsigned int start, unsigned int size, void* data);
    void clear_regions();
    void m68k_write_memory_16(unsigned int address, unsigned int value);
    void m68k_write_memory_32(unsigned int address, unsigned int value);
}

DECLARE_M68K_TEST(RegionBoundsTest) {
protected:
    void OnTearDown() override {
        clear_regions();
    }
};

// Writing a 16-bit value at the last byte of a region must not touch memory
// beyond the region boundary.
TEST_F(RegionBoundsTest, NoWritePastEndOnWord) {
    // Allocate larger backing to place sentinels after the region
    const size_t backingSize = 64;
    uint8_t* backing = new uint8_t[backingSize];
    memset(backing, 0, backingSize);

    // Place a region starting at offset 8 with size 16
    const unsigned int regionBase = 0x2000;
    const unsigned int regionSize = 16; // valid addresses: [0..15]
    // Put sentinels immediately after region end inside the same allocation
    const size_t regionOffset = 8;
    uint8_t* regionPtr = backing + regionOffset;
    const size_t sentinelIndex = regionOffset + regionSize; // first byte past end
    backing[sentinelIndex] = 0xEE; // sentinel

    add_region(regionBase, regionSize, regionPtr);

    // Attempt to write 16-bit at last byte in region
    // If implementation is incorrect, it will overwrite sentinel at sentinelIndex
    m68k_write_memory_16(regionBase + regionSize - 1, 0xA1B2);

    // Validate sentinel untouched
    EXPECT_EQ(backing[sentinelIndex], 0xEE) << "word write spilled past region end";

    delete[] backing;
}

// Writing a 32-bit value near the end must not cross the boundary
TEST_F(RegionBoundsTest, NoWritePastEndOnLong) {
    const size_t backingSize = 64;
    uint8_t* backing = new uint8_t[backingSize];
    memset(backing, 0, backingSize);

    const unsigned int regionBase = 0x3000;
    const unsigned int regionSize = 8; // small region
    const size_t regionOffset = 4;
    uint8_t* regionPtr = backing + regionOffset;
    const size_t sentinelIndex = regionOffset + regionSize; // first byte past end
    backing[sentinelIndex + 0] = 0xAA;
    backing[sentinelIndex + 1] = 0xBB;
    backing[sentinelIndex + 2] = 0xCC;
    backing[sentinelIndex + 3] = 0xDD;

    add_region(regionBase, regionSize, regionPtr);

    // Attempt multiple long writes at positions that would overflow
    m68k_write_memory_32(regionBase + regionSize - 1, 0x11223344);
    m68k_write_memory_32(regionBase + regionSize - 2, 0x55667788);
    m68k_write_memory_32(regionBase + regionSize - 3, 0x99AABBCC);

    // Sentinels must remain intact
    EXPECT_EQ(backing[sentinelIndex + 0], 0xAA);
    EXPECT_EQ(backing[sentinelIndex + 1], 0xBB);
    EXPECT_EQ(backing[sentinelIndex + 2], 0xCC);
    EXPECT_EQ(backing[sentinelIndex + 3], 0xDD);

    delete[] backing;
}

