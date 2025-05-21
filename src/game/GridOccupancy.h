// GridOccupancy.h

#pragma once
#include <bitset>
#include <glm/glm.hpp>

/* Compact 8×8 occupancy helper                                   */
class GridOccupancy {
public:
    // col 0-7, row 0-7
    static constexpr int COLS = 8;
    static constexpr int ROWS = 8;

    void  set(int c,int r)   { bits.set(index(c,r)); }
    void  clear(int c,int r) { bits.reset(index(c,r)); }
    bool  test(int c,int r) const { return bits.test(index(c,r)); }

    void reset() { bits.reset(); }

    // Adaptor for old uint32_t key (col | row<<16)
    static int col(uint32_t key) { return key & 0xFFFF; }
    static int row(uint32_t key) { return key >> 16;    }

private:
    std::bitset<COLS*ROWS> bits;

    static constexpr int index(int c,int r){ return r*COLS + c; }
};
