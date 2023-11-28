#pragma once

#include "LegacyEditor/LCE/Chunk/ChunkData.hpp"


namespace universal {


    /// "Elytra" chunks.
    class V11Chunk {
    private:
        static constexpr u32 GRID_HEADER_SIZE = 1024;

    public:
        ChunkData* chunkData = nullptr;
        DataManager* dataManager = nullptr;

        MU void readChunk(ChunkData* chunkDataIn, DataManager* managerIn, DIM dim);
        MU void writeChunk(ChunkData* chunkDataIn, DataManager* managerOut, DIM dim);

        V11Chunk() = default;

    private:
        MU void readBlocks() const;
        template<size_t BitsPerBlock>
        MU bool readGrid(u8 const* buffer, u8 grid[128]) const;
        MU void readData() const;
    };


}