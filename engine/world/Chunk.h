#pragma once

#include <cstdint>
#include <vector>

class Chunk {
public:
    static constexpr int kSizeX = 16;
    static constexpr int kSizeY = 256;
    static constexpr int kSizeZ = 16;

    Chunk();

    std::uint8_t getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, std::uint8_t id);

private:
    std::vector<std::uint8_t> blocks_;

    static int indexOf(int x, int y, int z);
};
