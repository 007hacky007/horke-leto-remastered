#pragma once
#include "data/adb.h"
#include "script/opcode.h"
namespace hl {
struct ScriptHandler {uint8_t declaration{};size_t entry{},end{};};
struct ScriptObject {
 std::string name;ObjectLayout layout;std::vector<std::byte> code;
 size_t executable_end{};std::vector<Instruction> instructions;std::vector<ScriptHandler> handlers;
};
Result<ScriptObject> load_object(ByteView bytes,std::string_view name="object");
}
