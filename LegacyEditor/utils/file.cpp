#include "file.hpp"


File::~File() {
    if (nbt == nullptr) return;
    if (nbt->data == nullptr) return;
    getNBTCompound()->deleteAll();
    nbt->data = nullptr;
    delete nbt;
    nbt = nullptr;
}


NBTTagCompound* File::createNBTTagCompound() {
    if (nbt != nullptr)
        return nullptr;
    nbt = new NBTBase(new NBTTagCompound(), TAG_COMPOUND);
    return static_cast<NBTTagCompound*>(nbt->data);
}


ND NBTTagCompound* File::getNBTCompound() const {
    if (nbt == nullptr)
        return nullptr;
    if (nbt->data == nullptr)
        return nullptr;
    return static_cast<NBTTagCompound*>(nbt->data);
}


std::string File::constructFileName(MU CONSOLE console) const {
    static std::unordered_map<FileType, std::string> FileTypeNames{
        {FileType::VILLAGE, "data/villages.dat"},
        {FileType::DATA_MAPPING, "data/largeMapDataMappings.dat"},
        {FileType::LEVEL, "level.dat"},
        {FileType::GRF, "requiredGameRules.grf"},
        {FileType::ENTITY_NETHER, "DIM-1entities.dat"},
        {FileType::ENTITY_OVERWORLD, "entities.dat"},
        {FileType::ENTITY_END, "DIM1/entities.dat"}
    };
    std::string name;
    switch (fileType) {
        using std::to_string;
        case FileType::VILLAGE:
        case FileType::DATA_MAPPING:
        case FileType::LEVEL:
        case FileType::GRF:
        case FileType::ENTITY_NETHER:
        case FileType::ENTITY_OVERWORLD:
        case FileType::ENTITY_END:
            name = FileTypeNames[fileType];
            break;
        case FileType::NONE: {
            name = "NONE";
            printf("file encountered with no type, possibly an error?");
            break;
        }
        case FileType::STRUCTURE: {
            auto* compound = getNBTCompound();
            if (compound == nullptr) {
                name = "NULL";
                break;
            }
            name = getNBTCompound()->getString("filename");
            break;
        }
        case FileType::MAP: {
            auto tag = getNBTCompound()->getTag("#");
            i16 mapNum = tag.toPrimitiveType<i16>();
            name = "data/map" + to_string(mapNum) + ".dat";
            break;
        }
        case FileType::PLAYER:
            name = getNBTCompound()->getString("filename");
            break;
        case FileType::REGION_NETHER:
        case FileType::REGION_OVERWORLD:
        case FileType::REGION_END: {
            i16 x = getNBTCompound()->getTag("x").toPrimitiveType<i16>();
            i16 z = getNBTCompound()->getTag("z").toPrimitiveType<i16>();
            if (fileType == FileType::REGION_NETHER) {
                name = "DIM-1";
            } else if (fileType == FileType::REGION_OVERWORLD) {
                name = "";
            } else {
                name = "DIM1/";
            }
            name += "r." + to_string(x) + "." + to_string(z) + ".mcr";
            break;
        }
    }
    return name;
}