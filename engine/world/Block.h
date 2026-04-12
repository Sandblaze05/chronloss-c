#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct BlockData {
    std::string name;
    bool isSolid;       // Does it block the camera / meshing?
    bool isWalkable;    
};

class BlockRegistry {
public:
    static void init() {
        // ID 0: Air
        blocks_[0] = { "Air", false, false };

        // ID 1: Dirt
        blocks_[1] = { "Dirt", true, true };

        // ID 2: Water
        blocks_[2] = { "Water", false, false };

        // ID 3: Tall Grass
        blocks_[3] = { "Stone", true, true };
    }

    static const BlockData& get(std::uint8_t id) {
        return blocks_[id];
    }

private:
    static inline BlockData blocks_[256];
};
