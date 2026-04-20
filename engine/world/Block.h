#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

enum BlockFlags : uint32_t {
    BLOCK_FLAG_NONE = 0,
    BLOCK_FLAG_SOLID = 1 << 0,
    BLOCK_FLAG_WALKABLE = 1 << 1,
    BLOCK_FLAG_TRANSPARENT = 1 << 2
};

struct BlockData {
    std::string name = "Unknown";
    uint32_t flags = BLOCK_FLAG_NONE;
    int lightLevel = 0;
    int toughness = 0; // -1 for unbreakable

    bool isSolid() const { return (flags & BLOCK_FLAG_SOLID) != 0; }
    bool isWalkable() const { return (flags & BLOCK_FLAG_WALKABLE) != 0; }
    bool isTransparent() const { return (flags & BLOCK_FLAG_TRANSPARENT) != 0; }
};

class BlockRegistry {
public:
    class Builder {
    public:
        Builder(std::uint8_t id, const std::string& name) : id_(id) {
            data_.name = name;
        }

        Builder& makeSolid() { data_.flags |= BLOCK_FLAG_SOLID; return *this; }
        Builder& makeWalkable() { data_.flags |= BLOCK_FLAG_WALKABLE; return *this; }
        Builder& makeTransparent() { data_.flags |= BLOCK_FLAG_TRANSPARENT; return *this; }

        Builder& setLightLevel(int level) { data_.lightLevel = level; return *this; }
        Builder& setToughness(int toughness) { data_.toughness = toughness; return *this; }

        ~Builder() {
            BlockRegistry::blocks_[id_] = data_;
        }

    private:
        std::uint8_t id_;
        BlockData data_;
    };

    static void init() {
        registerBlock(0, "Air").makeTransparent();
        registerBlock(1, "Stone").makeSolid().makeWalkable().setToughness(10);
        registerBlock(2, "Dirt").makeSolid().makeWalkable();
        registerBlock(3, "Water").makeTransparent();
        registerBlock(4, "Bedrock").makeSolid().makeWalkable().setToughness(-1);
        registerBlock(5, "Grass_Block").makeSolid().makeWalkable().setToughness(2);
        registerBlock(6, "Sand").makeSolid().makeWalkable().setToughness(1);
        registerBlock(7, "Sandstone").makeSolid().makeWalkable().setToughness(4);
        registerBlock(8, "Gravel").makeSolid().makeWalkable().setToughness(2);
        registerBlock(9, "Clay").makeSolid().makeWalkable().setToughness(3);
        registerBlock(10, "Snow").makeSolid().makeWalkable().setToughness(1);
        registerBlock(11, "Packed_Ice").makeSolid().makeWalkable().setToughness(5);
    }

    static Builder registerBlock(std::uint8_t id, const std::string& name) {
        return Builder(id, name);
    }

    static const BlockData& get(std::uint8_t id) {
        return blocks_[id];
    }

private:
    static inline BlockData blocks_[256];
};
