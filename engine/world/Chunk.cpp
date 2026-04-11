#include "Chunk.h"

Chunk::Chunk() : blocks_(kSizeX * kSizeY * kSizeZ, 0u) {
}

std::uint8_t Chunk::getBlock(int x, int y, int z) const {
    return blocks_[indexOf(x, y, z)];
}

void Chunk::setBlock(int x, int y, int z, std::uint8_t id) {
    blocks_[indexOf(x, y, z)] = id;
}

int Chunk::indexOf(int x, int y, int z) {
    return x + (z * kSizeX) + (y * kSizeX * kSizeZ);
}
