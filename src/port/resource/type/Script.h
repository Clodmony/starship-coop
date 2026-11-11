#pragma once

#include <cstdint>
#include <Resource.h>

namespace SF64 {
class Script : public Ship::Resource<uint16_t*> {
  public:
    using Resource::Resource;

    uint16_t** GetPointer() override;
    size_t GetPointerSize() override;

    std::vector<uint16_t*> mScripts;
    // COOP: the level's scripts in one block, in the order the ROM holds them; mScripts points into it (#112)
    std::vector<uint16_t> mBlock;
};

class ScriptCMDs : public Ship::Resource<uint16_t> {
  public:
    using Resource::Resource;

    uint16_t* GetPointer() override;
    size_t GetPointerSize() override;

    std::vector<uint16_t> mCommands;
};
}