#include "ScriptFactory.h"
#include "../type/Script.h"
#include "spdlog/spdlog.h"
#include "ResourceUtil.h"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>

namespace SF64 {
// COOP: (#112) the N64 holds a level's scripts in one run of commands, so a branch past a script's last command ran on
// into the next script, its targets still counted from the first one (Venom 1's script 40 does that when its wingman
// is not there). Here each script is a buffer of its own, and the interpreter read past its end. tools/Torch cuts the
// scripts from that run and names each `<table>_cmd_<n>`, n in ROM order (ScriptBinaryExporter), so this joins them
// again in that order and points the table into the block. Behind the last script come STOP_SCRIPT commands, enough
// for any command target (below EV_CHANGE_AI, 200) of any script: where the N64 read the table itself, the port stops.
// Entries named or typed otherwise, or a gap in the numbers (a mod), keep their own buffers, as before.
static constexpr uint16_t kStopScript[2] = { 0xFE00, 0 }; // EVENT_STOP_SCRIPT(), include/sf64event.h
static constexpr size_t kTargetLimit = 200;               // EV_CHANGE_AI

static int CommandsEntryNumber(const char* path) {
    const char* tail = (path != nullptr) ? strrchr(path, '_') : nullptr;

    if ((tail == nullptr) || (tail - path < 4) || (strncmp(tail - 4, "_cmd_", 5) != 0) || !isdigit(tail[1])) {
        return -1;
    }
    return atoi(tail + 1);
}

static void LayOutInRomOrder(Script* script, const std::vector<uint64_t>& crcs) {
    std::map<int, std::shared_ptr<ScriptCMDs>> entries;
    std::vector<int> numberOfSlot;
    std::map<int, size_t> offsetOf;
    std::vector<uint16_t> block;

    for (uint64_t crc : crcs) {
        const char* path = (crc != 0) ? ResourceGetNameByCrc(crc) : nullptr;
        int number = CommandsEntryNumber(path);
        auto commands = (number >= 0)
                            ? std::dynamic_pointer_cast<ScriptCMDs>(
                                  Ship::Context::GetInstance()->GetResourceManager()->LoadResourceProcess(path))
                            : nullptr;

        if (commands == nullptr) {
            return;
        }
        entries[number] = commands;
        numberOfSlot.push_back(number);
    }
    for (auto& [number, commands] : entries) {
        if (number != (int) offsetOf.size()) {
            return;
        }
        offsetOf[number] = block.size();
        block.insert(block.end(), commands->mCommands.begin(), commands->mCommands.end());
    }
    for (size_t i = 0; i < kTargetLimit; i++) {
        block.insert(block.end(), kStopScript, kStopScript + 2);
    }
    script->mBlock = std::move(block);
    for (size_t slot = 0; slot < numberOfSlot.size(); slot++) {
        script->mScripts[slot] = script->mBlock.data() + offsetOf[numberOfSlot[slot]];
    }
}

std::shared_ptr<Ship::IResource> ResourceFactoryBinaryScriptV0::ReadResource(std::shared_ptr<Ship::File> file,
                                                                             std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto script = std::make_shared<Script>(initData);
    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);

    auto size = reader->ReadUInt32();

    std::vector<uint64_t> crcs; // COOP: for LayOutInRomOrder (#112)

    for (uint32_t i = 0; i < size; i++) {
        crcs.push_back(reader->ReadUInt64()); // COOP: kept (#112)
        script->mScripts.push_back(LoadChild<uint16_t*>(crcs.back()));
    }
    LayOutInRomOrder(script.get(), crcs); // COOP: a branch past a script's end reads what the N64 read (#112)

    return script;
}
} // namespace LUS
