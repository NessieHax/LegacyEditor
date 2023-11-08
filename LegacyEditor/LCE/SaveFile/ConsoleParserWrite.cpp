#include "ConsoleParser.hpp"

#include <cstdio>
#include "LegacyEditor/utils/RLEVITA/rlevita.hpp"
#include "LegacyEditor/utils/LZX/XboxCompression.hpp"
#include "LegacyEditor/utils/processor.hpp"
#include "LegacyEditor/utils/tinf/tinf.h"
#include "LegacyEditor/utils/zlib-1.2.12/zlib.h"

#include "LegacyEditor/utils/endian.hpp"


int ConsoleParser::saveConsoleFile(const std::string& outfileStr) {
    return 0;
}


int ConsoleParser::saveWiiU(const std::string& outfileStr, Data& dataOut) {
    DataManager managerOut(dataOut);
    u64 src_size = managerOut.size;

    FILE* f_out = fopen(outfileStr.c_str(), "wb");
    if (!f_out) { return -1; }

    // Write src_size to the file
    uLong compressedSize = compressBound(src_size);
    printf("compressed bound: %lu\n", compressedSize);

    u8_vec compressedData(compressedSize);
    if (compress(compressedData.data(), &compressedSize,
                 managerOut.data, managerOut.size) != Z_OK) {
        return {};
    }
    compressedData.resize(compressedSize);

    if (isSystemLittleEndian()) {
        src_size = swapEndian64(src_size);
    }

    fwrite(&src_size, sizeof(u64), 1, f_out);
    printf("Writing final size: %zu\n", compressedData.size());

    fwrite(compressedData.data(), 1, compressedData.size(), f_out);

    fclose(f_out);


    return 0;
}


int ConsoleParser::saveVita(const std::string& outFileStr, Data& dataOut) {

    FILE* f_out = fopen(outFileStr.c_str(), "wb");
    if (!f_out) {
        printf("failed to open file '%s'", outFileStr.c_str());
        return -1;
    }

    allocate(dataOut.size + 2);

    size = RLEVITA_COMPRESS(dataOut.data, dataOut.size, data, size);

    int num = 0;
    fwrite(&num, sizeof(u32), 1, f_out);


    u32 val;
    std::memcpy(&val, &data[0], 4);
    val += 0x0900;

    // might need to swap endianness
    fwrite(&val, sizeof(u32), 1, f_out);
    fwrite(data, sizeof(u8), size, f_out);
    fclose(f_out);

    return 0;
}



int ConsoleParser::savePS3Uncompressed() { return 0; }


int ConsoleParser::savePS3Compressed() { return 0; }


int ConsoleParser::saveXbox360_DAT() { return 0; }


int ConsoleParser::saveXbox360_BIN() { return 0; }