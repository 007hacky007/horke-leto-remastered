#pragma once
#include "data/result.h"
#include <array>
#include <cstdint>
#include <optional>
namespace hl {
enum class InstructionFlow {next, enter, jump, conditional, declaration, terminate};
struct OpcodeInfo {uint32_t handler_address{};uint8_t operand_bytes{};bool signed_operand{};InstructionFlow flow{InstructionFlow::next};};
using OpcodeMap=std::array<OpcodeInfo,256>;
struct Instruction {
 uint8_t opcode{};size_t offset{},next_offset{};int64_t operand{};
 InstructionFlow flow{};std::optional<size_t> branch_target;
};
OpcodeMap dos_opcode_map();
bool native_opcode_bound(unsigned opcode);
Result<Instruction> decode_instruction(ByteView code,size_t offset,const OpcodeMap& map,std::string_view object_name="object");
}
