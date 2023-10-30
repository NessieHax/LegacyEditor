#pragma once

#include <chrono>
#include <iostream>
#include <optional>

#include "LegacyEditor/LCE/FileInfo.hpp"
#include "LegacyEditor/utils/dataManager.hpp"


struct StfsFileEntry {
    u32 entryIndex{};
    std::string name;
    u8 nameLen{};
    u8 flags{};
    int blocksForFile{};
    int startingBlockNum{};
    u16 pathIndicator{};
    u32 fileSize{};
    u32 createdTimeStamp{};
    u32 accessTimeStamp{};
    u32 fileEntryAddress{};
    std::vector<int> blockChain;
};


struct StfsFileListing {
    std::vector<StfsFileEntry> fileEntries;
    std::vector<StfsFileListing> folderEntries;
    StfsFileEntry folder;
};


struct StfsVD {
    u8 size;
    //u8 reserved;
    u8 blockSeparation;
    u16 fileTableBlockCount;
    int fileTableBlockNum;
    //u8 topHashTableHash[0x14];
    u32 allocatedBlockCount;
    u32 unallocatedBlockCount;

    void readStfsVD(DataManager& input) {
        this->size = input.readByte();
        input.readByte();// reserved
        this->blockSeparation = input.readByte();
        this->fileTableBlockCount = input.readShort();
        this->fileTableBlockNum = input.readInt24();
        input.incrementPointer(0x14);// skip the hash
        input.setLittleEndian();
        this->allocatedBlockCount = input.readInt();
        this->unallocatedBlockCount = input.readInt();
    }
};


#pragma pack(push, 1)
struct HashEntry {
    u8 blockHash[0x14];
    u8 status;
    u32 nextBlock;
};
#pragma pack(pop)


struct HashTable {
    u8 level;
    u32 trueBlockNumber;
    u32 entryCount;
    HashEntry entries[0xAA];
    u32 addressInFile;
};


class BINHeader {
public:
    u32 headerSize{};
    StfsVD stfsVD{};
    std::wstring displayName;
    DataManager thumbnailImage = DataManager();


    int readHeader(DataManager& binFile) {

        binFile.seek(0x340);
        this->headerSize = binFile.readInt();

        //content type, 1 is savegame
        if (binFile.readInt() != 1) {
            printf(".bin file is not a savegame, exiting\n");
            return 0;
        }

        //file system
        binFile.seek(0x3A9);
        if (binFile.readInt()) {
            printf(".bin file is not in STFS format, exiting\n");
            return 0;
        }

        binFile.seek(0x0379);
        this->stfsVD.readStfsVD(binFile);
        binFile.seek(0x0411);

        //readBytes the savegame name
        displayName = binFile.readWString();

        //skip all the irrelevant data to extract the savegame
        binFile.seek(0x1712);
        //get thumbnail image, if not present, use the title one if present
        u8* thumbnail = nullptr;
        u32 thumbnailImageSize = binFile.readInt();
        if (thumbnailImageSize) {
            binFile.incrementPointer(4);//readBytes the other size but it will not be used
            u8* thumbnailImageData = binFile.readBytes(thumbnailImageSize);
            this->thumbnailImage = DataManager(thumbnailImageData, thumbnailImageSize);
        } else {
            u32 titleThumbnailImageSize = binFile.readInt();
            if (titleThumbnailImageSize) {
                binFile.seek(0x571A);
                u8* titleThumbnailImageData = binFile.readBytes(thumbnailImageSize);
                this->thumbnailImage = DataManager(titleThumbnailImageData, titleThumbnailImageSize);
            }
        }
        return 1;
    }
};


/// extract a file (by FileEntry) to a designated file path
class StfsPackage {
public:
    explicit StfsPackage(DataManager& input) : data(input) {}

    StfsFileListing GetFileListing() { return fileListing; }

    void Extract(StfsFileEntry* entry, DataManager& out) {
        if (entry->nameLen == 0) { entry->name = "default"; }

        // get the file size that we are extracting
        u32 fileSize = entry->fileSize;
        if (fileSize == 0) { return; }

        // check if all the blocks are consecutive
        if (entry->flags & 1) {
            // allocate 0xAA blocks of memory, for maximum efficiency, yo
            auto* buffer = new u8[0xAA000];

            // seek to the beginning of the file
            u32 startAddress = BlockToAddress(entry->startingBlockNum);
            data.seek(startAddress);

            // calculateOffset the number of blocks to readBytes before we hit a table
            u32 blockCount = (ComputeLevel0BackingHashBlockNumber(entry->startingBlockNum) + blockStep[0]) -
                                  ((startAddress - firstHashTableAddress) >> 0xC);

            // pick up the change at the beginning, until we hit a hash table
            if ((u32) entry->blocksForFile <= blockCount) {
                data.readOntoData(entry->fileSize, buffer);
                out.write(buffer, entry->fileSize);

                //out.Close();

                // free the temp buffer
                delete[] buffer;
                return;
            } else {
                data.readOntoData(blockCount << 0xC, buffer);
                out.write(buffer, blockCount << 0xC);
            }

            // extract the blocks in between the tables
            u32 tempSize = (entry->fileSize - (blockCount << 0xC));
            while (tempSize >= 0xAA000) {
                // skip past the hash table(s)
                u32 currentPos = data.getPosition();
                data.seek(currentPos + GetHashTableSkipSize(currentPos));

                // readBytes in the 0xAA blocks between the tables
                data.readOntoData(0xAA000, buffer);

                // Write the bytes to the out file
                out.write(buffer, 0xAA000);

                tempSize -= 0xAA000;
                blockCount += 0xAA;
            }

            // pick up the change at the end
            if (tempSize != 0) {
                // skip past the hash table(s)
                u32 currentPos = data.getPosition();
                data.seek(currentPos + GetHashTableSkipSize(currentPos));

                // readBytes in the extra crap
                data.readOntoData(tempSize, buffer);

                // Write it to the out file
                out.write(buffer, tempSize);
            }

            // free the temp buffer
            delete[] buffer;
        } else {
            // generate the blockchain which we have to extract
            u32 fullReadCounts = fileSize / 0x1000;

            fileSize -= (fullReadCounts * 0x1000);

            u32 block = entry->startingBlockNum;

            // allocate data for the blocks
            u8 block_data[0x1000];

            // readBytes all the full blocks the file allocates
            for (u32 i = 0; i < fullReadCounts; i++) {
                ExtractBlock(block, block_data);
                out.write(block_data, 0x1000);

                block = GetBlockHashEntry(block).nextBlock;
            }

            // readBytes the remaining data
            if (fileSize != 0) {
                ExtractBlock(block, block_data, fileSize);
                out.write(block_data, (int) fileSize);
            }
        }
    }

    /// convert a block into an address in the file
    u32 BlockToAddress(u32 blockNum) {
        // check for invalid block number
        if (blockNum >= 0xFFFFFF) throw std::runtime_error("STFS: Block number must be less than 0xFFFFFF.\n");
        return (ComputeBackingDataBlockNumber(blockNum) << 0x0C) + firstHashTableAddress;
    }

    /// get the address of a hash for a data block
    u32 GetHashAddressOfBlock(u32 blockNum) {
        if (blockNum >= metaData.stfsVD.allocatedBlockCount)
            throw std::runtime_error("STFS: Reference to illegal block number.\n");

        u32 hashAddr = (ComputeLevel0BackingHashBlockNumber(blockNum) << 0xC) + firstHashTableAddress;
        hashAddr += (blockNum % 0xAA) * 0x18;

        switch (topLevel) {
            case 0:
                hashAddr += ((metaData.stfsVD.blockSeparation & 2) << 0xB);
                break;
            case 1:
                hashAddr += ((topTable.entries[blockNum / 0xAA].status & 0x40) << 6);
                break;
            case 2:
                u32 level1Off = ((topTable.entries[blockNum / 0x70E4].status & 0x40) << 6);
                u32 pos =
                        ((ComputeLevel1BackingHashBlockNumber(blockNum) << 0xC) + firstHashTableAddress + level1Off) +
                        ((blockNum % 0xAA) * 0x18);
                data.seek(pos + 0x14);
                hashAddr += ((data.readByte() & 0x40) << 6);
                break;
        }
        return hashAddr;
    }

    ~StfsPackage() = default;

    BINHeader GetMetaData() { return metaData; }

private:
    BINHeader metaData;

    StfsFileListing fileListing;
    //StfsFileListing writtenToFile;
    DataManager& data;

    u8 packageSex{};//0 female, 1 male
    u32 blockStep[2]{};
    u32 firstHashTableAddress{};
    u8 topLevel{};
    HashTable topTable{};
    u32 tablesPerLevel[3]{};

    /// readBytes the file listing from the file
    void ReadFileListing() {
        fileListing.fileEntries.clear();
        fileListing.folderEntries.clear();

        // set up the entry for the blockchain
        StfsFileEntry entry;
        entry.startingBlockNum = metaData.stfsVD.fileTableBlockNum;
        entry.fileSize = (metaData.stfsVD.fileTableBlockCount * 0x1000);

        // generate a blockchain for the full file listing
        u32 block = entry.startingBlockNum;

        StfsFileListing fl;
        u32 currentAddr;
        for (u32 x = 0; x < metaData.stfsVD.fileTableBlockCount; x++) {
            currentAddr = BlockToAddress(block);
            data.seek(currentAddr);

            for (u32 i = 0; i < 0x40; i++) {
                StfsFileEntry fe;

                // set the current position
                fe.fileEntryAddress = currentAddr + (i * 0x40);

                // calculateOffset the entry index (in the file listing)
                fe.entryIndex = (x * 0x40) + i;

                // readBytes the name, if the length is 0 then break
                fe.name = data.readString(0x28);

                // readBytes the name length
                fe.nameLen = data.readByte();
                if ((fe.nameLen & 0x3F) == 0) {
                    data.seek(currentAddr + ((i + 1) * 0x40));
                    continue;
                } else if (fe.name.length() == 0) {
                    break;
                }

                // check for a mismatch in the total allocated blocks for the file
                fe.blocksForFile = data.readInt24(true);
                data.incrementPointer(3);

                // readBytes more information
                fe.startingBlockNum = data.readInt24(true);
                fe.pathIndicator = data.readShort();
                fe.fileSize = data.readInt();
                fe.createdTimeStamp = data.readInt();
                fe.accessTimeStamp = data.readInt();

                // get the flags
                fe.flags = fe.nameLen >> 6;

                // bits 6 and 7 are flags, clear them
                fe.nameLen &= 0x3F;

                fl.fileEntries.push_back(fe);
            }

            block = GetBlockHashEntry(block).nextBlock;
        }

        // sort the file listing
        AddToListing(&fl, &fileListing);
        //writtenToFile = fileListing;
    }

    /// extract a block's data
    void ExtractBlock(u32 blockNum, u8* inputData, u32 length = 0x1000) {
        if (blockNum >= metaData.stfsVD.allocatedBlockCount)
            throw std::runtime_error("STFS: Reference to illegal block number.\n");

        // check for an invalid block length
        if (length > 0x1000) throw std::runtime_error("STFS: length cannot be greater 0x1000.\n");

        // go to the block's position
        data.seek(BlockToAddress(blockNum));

        // readBytes the data, and return
        data.readOntoData(length, inputData);
    }

    /// convert a block number into a true block number, where the first block is the first hash table
    ND u32 ComputeBackingDataBlockNumber(u32 blockNum) const {
        u32 toReturn = (((blockNum + 0xAA) / 0xAA) << packageSex) + blockNum;
        if (blockNum < 0xAA) return toReturn;
        else if (blockNum < 0x70E4)
            return toReturn + (((blockNum + 0x70E4) / 0x70E4) << packageSex);
        else
            return (1 << packageSex) + (toReturn + (((blockNum + 0x70E4) / 0x70E4) << packageSex));
    }

    /// get a block's hash entry
    HashEntry GetBlockHashEntry(u32 blockNum) {
        if (blockNum >= metaData.stfsVD.allocatedBlockCount) {
            throw std::runtime_error("STFS: Reference to illegal block number.\n");
        }

        // go to the position of the hash address
        data.seek(GetHashAddressOfBlock(blockNum));

        // readBytes the hash entry
        HashEntry he{};
        data.readOntoData(0x14, he.blockHash);
        he.status = data.readByte();
        he.nextBlock = data.readInt24();

        return he;
    }

    /// get the true block number for the hash table that hashes the block at the level passed in
    u32 ComputeLevelNBackingHashBlockNumber(u32 blockNum, u8 level) {
        switch (level) {
            case 0:
                return ComputeLevel0BackingHashBlockNumber(blockNum);

            case 1:
                return ComputeLevel1BackingHashBlockNumber(blockNum);

            case 2:
                return ComputeLevel2BackingHashBlockNumber(blockNum);

            default:
                throw std::runtime_error("STFS: Invalid level.\n");
        }
    }

    /// get the true block number for the hash table that hashes the block at level 0
    u32 ComputeLevel0BackingHashBlockNumber(u32 blockNum) {
        if (blockNum < 0xAA) return 0;

        u32 num = (blockNum / 0xAA) * blockStep[0];
        num += ((blockNum / 0x70E4) + 1) << ((u8) packageSex);

        if (blockNum / 0x70E4 == 0) return num;

        return num + (1 << (u8) packageSex);
    }

    /// get the true block number for the hash table that hashes the block at level 1 (female)
    u32 ComputeLevel1BackingHashBlockNumber(u32 blockNum) {
        if (blockNum < 0x70E4) return blockStep[0];
        return (1 << (u8) packageSex) + (blockNum / 0x70E4) * blockStep[1];
    }

    /// get the true block number for the hash table that hashes the block at level 2
    u32 ComputeLevel2BackingHashBlockNumber(u32 blockNum) { return blockStep[1]; }

    /// add the file entry to the file listing
    void AddToListing(StfsFileListing* fullListing, StfsFileListing* out) {
        for (auto &fileEntry: fullListing->fileEntries) {
            // check if the file is a directory
            bool isDirectory = (fileEntry.flags & 2);

            // make sure the file belongs to the current folder
            if (fileEntry.pathIndicator == out->folder.entryIndex) {
                // add it if it's a file
                if (!isDirectory) out->fileEntries.push_back(fileEntry);
                // if it's a directory and not the current directory, then add it
                else if (isDirectory && fileEntry.entryIndex != out->folder.entryIndex) {
                    StfsFileListing fl;
                    fl.folder = fileEntry;
                    out->folderEntries.push_back(fl);
                }
            }
        }

        // for every folder added, add the files to them
        for (auto &folderEntry: out->folderEntries) {
            AddToListing(fullListing, &folderEntry);
        }
    }

    /// calculateOffset the level of the topmost hash table
    int CalculateTopLevel() const {
        if (metaData.stfsVD.allocatedBlockCount <= 0xAA) return 0;
        else if (metaData.stfsVD.allocatedBlockCount <= 0x70E4)
            return 1;
        else if (metaData.stfsVD.allocatedBlockCount <= 0x4AF768)
            return 2;
        else
            throw std::runtime_error("STFS: Invalid number of allocated blocks.\n");
    }

    /// get the number of bytes to skip over the hash table
    u32 GetHashTableSkipSize(u32 tableAddress) {
        // convert the address to a true block number
        u32 trueBlockNumber = (tableAddress - firstHashTableAddress) >> 0xC;

        // check if it's the first hash table
        if (trueBlockNumber == 0) return (0x1000 << packageSex);

        // check if it's the level 2 table, or above
        if (trueBlockNumber == blockStep[1]) return (0x3000 << packageSex);
        else if (trueBlockNumber > blockStep[1])
            trueBlockNumber -= (blockStep[1] + (1 << packageSex));

        // check if it's at a level 1 table
        if (trueBlockNumber == blockStep[0] ||
            trueBlockNumber % blockStep[1] == 0)
            return (0x2000 << packageSex);

        // otherwise, assume it's at a level 0 table
        return (0x1000 << packageSex);
    }

public:
    /// parse the file
    void Parse() {
        BINHeader header;
        int result = header.readHeader(data);
        if (!result) {
            //free(inputData);
            return;//FileInfo();
        }
        metaData = header;
        packageSex = ((~metaData.stfsVD.blockSeparation) & 1);

        if (packageSex == 0) { //female
            blockStep[0] = 0xAB;
            blockStep[1] = 0x718F;
        } else { //male
            blockStep[0] = 0xAC;
            blockStep[1] = 0x723A;
        }

        // address of the first hash table in the package, comes right after the header
        firstHashTableAddress = (metaData.headerSize + 0x0FFF) & 0xFFFFF000;

        // calculateOffset the number of tables per level
        tablesPerLevel[0] = (metaData.stfsVD.allocatedBlockCount / 0xAA) +
                            ((metaData.stfsVD.allocatedBlockCount % 0xAA != 0) ? 1 : 0);
        tablesPerLevel[1] = (tablesPerLevel[0] / 0xAA) +
                            ((tablesPerLevel[0] % 0xAA != 0 &&
                              metaData.stfsVD.allocatedBlockCount > 0xAA) ? 1 : 0);
        tablesPerLevel[2] = (tablesPerLevel[1] / 0xAA) +
                            ((tablesPerLevel[1] % 0xAA != 0 &&
                              metaData.stfsVD.allocatedBlockCount > 0x70E4) ? 1 : 0);

        // calculateOffset the level of the top table
        topLevel = CalculateTopLevel();

        // readBytes in the top hash table
        topTable.trueBlockNumber = ComputeLevelNBackingHashBlockNumber(0, topLevel);
        topTable.level = topLevel;

        u32 baseAddress = (topTable.trueBlockNumber << 0xC) + firstHashTableAddress;
        topTable.addressInFile = baseAddress + ((metaData.stfsVD.blockSeparation & 2) << 0xB);
        data.seek(topTable.addressInFile);

        u32 dataBlocksPerHashTreeLevel[3] = {1, 0xAA, 0x70E4};

        // load the information
        topTable.entryCount = metaData.stfsVD.allocatedBlockCount / dataBlocksPerHashTreeLevel[topLevel];
        if (metaData.stfsVD.allocatedBlockCount > 0x70E4 &&
            (metaData.stfsVD.allocatedBlockCount % 0x70E4 != 0))
            topTable.entryCount++;
        else if (metaData.stfsVD.allocatedBlockCount > 0xAA
                 && (metaData.stfsVD.allocatedBlockCount % 0xAA != 0))
            topTable.entryCount++;

        for (u32 i = 0; i < topTable.entryCount; i++) {
            data.readOntoData(0x14, topTable.entries[i].blockHash);
            topTable.entries[i].status = data.readByte();
            topTable.entries[i].nextBlock = data.readInt24();
        }

        // set default values for the root of the file listing
        StfsFileEntry fe;
        fe.pathIndicator = 0xFFFF;
        fe.name = "Root";
        fe.entryIndex = 0xFFFF;
        fileListing.folder = fe;

        ReadFileListing();
    }
};


struct TextChunk {
    std::string keyword;
    std::string text;
};


static StfsFileEntry* FindSavegameFileEntry(StfsFileListing& listing) {
    for (StfsFileEntry& file: listing.fileEntries) {
        if (file.name == "savegame.dat") { return &file; }
    }
    for (StfsFileListing& folder: listing.folderEntries) {
        if (StfsFileEntry* entry = FindSavegameFileEntry(folder); entry) { return entry; }
    }
    return nullptr;
}


static u32 c2n(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}


static i64 stringToHex(const std::string& str) {
    i64 result = 0;
    size_t i = 0;
    int stringSize = (int)str.size() - 1;//terminating value doesn't count
    for (; i < stringSize; i++) { result = result * 16 + c2n(str[i]); }
    return result;
}


static i64 stringToInt64(const std::string& str) {
    i64 result = 0;
    int sign = 1;
    size_t i = 0;

    if (str[0] == '-') {
        sign = -1;
        i++;
    }
    int stringSize = (int)str.size() - 1;//terminating value doesn't count
    for (; i < stringSize; i++) { result = result * 10 + (str[i] - '0'); }

    return result * sign;
}


static WorldOptions getTagsInImage(DataManager& image) {
    WorldOptions options;
    u8* PNGHeader = image.readBytes(8);
    if (memcmp(PNGHeader, "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8) != 0) {
        printf("File in thumbnail block is not PNG header, the first 8 bytes are:\n");
        for (size_t i = 0; i < 8; i++) { std::cout << std::hex << (int) (PNGHeader[i]) << " "; }
        std::cout << std::endl;
    }
    free(PNGHeader);

    std::vector<TextChunk> chunks;

    while (true) {
        // Check if we've reached the end of the file
        if (image.isEndOfData()) break;
        // Read chunk length
        u32 length = image.readInt();

        // Read chunk type
        char* type = (char*) image.readBytes(4);
        //check if end
        if (std::string(type, 4) == "IEND") {
            free(type);
            break;
        }
        // Check if the chunk is a text chunk
        if (std::string(type, 4) != "tEXt") {
            free(type);
            image.incrementPointer(length + 4);//the extra 4 is the crc
            continue;
        }
        free(type);
        // Read keyword
        i64 chunkLength = length;
        while (chunkLength > 0) {
            std::string keyword;
            u8 c = 0;
            //remove all null bytes in between
            while (c == 0 && chunkLength > 0) {
                chunkLength--;
                c = image.readByte();
                if (image.isEndOfData()) { break; }
            }
            chunkLength++;//chunkLength is added because the next value was tested and subtracted even on the non-null byte
            image.incrementPointer(-1);
            while (c != 0 && chunkLength > 0) {
                c = image.readByte();
                keyword += (char)c;
                chunkLength--;
                if (image.isEndOfData()) { break; }
            }
            //remove all null bytes in between
            while (c == 0 && chunkLength > 0) {
                chunkLength--;
                c = image.readByte();
                if (image.isEndOfData()) { break; }
            }
            chunkLength++;//chunkLength is added because the next value was tested and subtracted even on the non-null byte
            image.incrementPointer(-1);
            std::string text;
            while (c != 0 && chunkLength > 0) {
                c = image.readByte();
                text += (char)c;
                chunkLength--;
                if (image.isEndOfData()) { break; }
            }
            chunks.push_back({keyword, text});
            if (image.isEndOfData()) { break; }
        }

        // Read chunk CRC
        image.readInt();
    }


    //get keys and store them
    for (const TextChunk& chunk: chunks) {
        const char* keyword = chunk.keyword.c_str();
        if (keyword == std::string("4J_SEED")) {
            options.displaySeed = stringToInt64(chunk.text);
        } else if (keyword == std::string("4J_#LOADS")) {
            options.numLoads = stringToHex(chunk.text);
        } else if (keyword == std::string("4J_HOSTOPTIONS")) {
            options.hostOptions = stringToHex(chunk.text);
        } else if (keyword == std::string("4J_TEXTUREPACK")) {
            options.texturePack = stringToHex(chunk.text);
        } else if (keyword == std::string("4J_EXTRADATA")) {
            options.extraData = stringToHex(chunk.text);
        } else if (keyword == std::string("4J_EXPLOREDCHUNKS")) {
            options.numExploredChunks = stringToHex(chunk.text);
        } else if (keyword == std::string("4J_BASESAVENAME")) {
            options.baseSaveName = chunk.text;
        }
    }
    return options;
}


static std::optional<std::chrono::system_clock::time_point> TimePointFromFatTimestamp(u32 fat) {
    u32 year = (fat >> 25) + 1980;
    u32 month = 0xf & (fat >> 21);
    u32 day = 0x1f & (fat >> 16);
    u32 hour = 0x1f & (fat >> 11);
    u32 minute = 0x3f & (fat >> 5);
    u32 second = (0x1f & fat) * 2;

#if defined(__GNUC__)
    std::tm tm{};
    tm.tm_year = (int) year - 1900;
    tm.tm_mon = (int) month - 1;
    tm.tm_mday = (int) day;
    tm.tm_hour = (int) hour;
    tm.tm_min = (int) minute;
    tm.tm_sec = (int) second;
    tm.tm_isdst = 0;

    std::time_t t = _mkgmtime(&tm);

    if (t == (std::time_t) -1) { return std::nullopt; }
    return std::chrono::system_clock::from_time_t(t);
#else
    std::chrono::year_month_day ymd = std::chrono::year(year) / std::chrono::month(month) / std::chrono::day(day);
    if (!ymd.ok()) { return std::nullopt; }
    return std::chrono::sys_days(ymd) + std::chrono::hours(hour) + std::chrono::minutes(minute) +
           std::chrono::seconds(second);
#endif
}

static FileInfo extractSaveGameDat(u8* inputData, i64 inputSize) {
    DataManager binFile(inputData, inputSize);
    StfsPackage stfsInfo(binFile);
    stfsInfo.Parse();
    StfsFileListing listing = stfsInfo.GetFileListing();
    StfsFileEntry* entry = FindSavegameFileEntry(listing);
    if (!entry) { return {}; }

    // TODO IMPORTANT: find upper range of this so it can use a buffer
    std::cout << "because I removed vector based dataManagers, BINSupport.hpp at line 752 needs better way to allocate memory ahead of time" << std::endl;
    DataManager out(23456789);

    stfsInfo.Extract(entry, out);
    out.size = out.ptr - out.start();

    FileInfo savegame;
    savegame.createdTime = TimePointFromFatTimestamp(entry->createdTimeStamp);
    BINHeader meta = stfsInfo.GetMetaData();
    if (meta.thumbnailImage.size) {
        savegame.thumbnailImage = meta.thumbnailImage;
    }

    int savefileSize = (int) out.getSize();
    if (savefileSize) {
        u8* savefile = (u8*) malloc(savefileSize);
        memcpy(savefile, out.start(), savefileSize);
        savegame.saveFileData = DataManager(savefile, savefileSize);
    }

    savegame.saveName = stfsInfo.GetMetaData().displayName;
    savegame.options = getTagsInImage(savegame.thumbnailImage);
    free(inputData);
    return savegame;
}
