#include "script/opcode.h"
#include <bit>
namespace hl {
bool native_opcode_bound(unsigned op) {
 if(op>=170||!dos_opcode_map()[op].handler_address)return false;
 if(op<=60)return true;
 switch(op){
 case 61:case 63:case 64:case 65:case 66:case 67:case 68:case 69:case 70:case 71:case 72:case 73:case 74:case 75:
 case 77:case 79:case 80:case 82:case 83:case 84:case 85:case 86:case 87:case 88:case 89:case 90:case 91:case 92:case 93:case 94:
 case 95:case 96:case 97:case 98:case 99:case 100:case 101:case 102:case 103:case 104:case 105:case 106:case 107:case 108:case 109:case 110:case 111:case 112:case 113:case 114:case 115:case 117:case 120:case 121:
 case 122:case 123:case 124:case 125:case 126:case 127:case 128:case 129:case 130:case 131:case 132:case 133:case 134:case 135:
 case 137:case 138:case 139:case 140:case 141:case 142:case 143:case 144:case 145:case 146:case 147:case 148:case 149:case 150:case 151:case 152:case 155:case 156:case 153:case 154:case 157:case 158:case 159:case 160:case 161:
 case 162:case 163:case 164:case 165:case 166:case 167:case 168:case 169:return true;
 default:return false;
 }
}
OpcodeMap dos_opcode_map() {
 // Verified ELF switch at 0x27c4d; REA interpreter-assembly.json and m2-interpreter.json.
 static constexpr uint32_t handlers[170]={
  0x286e8, 0x29a19, 0x29a19, 0x27fe9, 0x2801b, 0x27fc3, 0x27fb4, 0x27fcd, 0x27fe0, 0x27f51,
  0x27f2f, 0x27f0f, 0x27f72, 0x27f94, 0x28044, 0x28058, 0x28084, 0x280ad, 0x280dc, 0x2813a,
  0x2810b, 0x28198, 0x28169, 0x281c7, 0x281e1, 0x281fb, 0x28216, 0x2825c, 0x282a4, 0x282be,
  0x282d8, 0x283ac, 0x282f2, 0x2830e, 0x2832a, 0x28369, 0x283bd, 0x283ec, 0x283fd, 0x2842b,
  0x28459, 0x28487, 0x284b5, 0x284d9, 0x284fd, 0x2852c, 0x2855b, 0x2858b, 0x285c3, 0x285f9,
  0x2862a, 0x2865b, 0x2868a, 0x286b9, 0x28891, 0x28924, 0x28968, 0x289ac, 0x28a92, 0x289f0,
  0x28a34, 0x2880d, 0x29a19, 0x2882c, 0x287b1, 0x287d3, 0x28799, 0x2885f, 0x28872, 0x28bd7,
  0x2884b, 0x2872a, 0x28742, 0x28bf7, 0x28769, 0x28781, 0x29a19, 0x28c16, 0x28c42, 0x28c6e,
  0x28c9a, 0x28d2f, 0x28cde, 0x28d0a, 0x28d43, 0x28d57, 0x28d6b, 0x28d7f, 0x28d93, 0x28dec,
  0x28e34, 0x28e71, 0x28e96, 0x28ec2, 0x28ecc, 0x29807, 0x2981b, 0x296ba, 0x296d9, 0x296f8,
  0x29702, 0x2970c, 0x2971f, 0x29729, 0x2973d, 0x29751, 0x28712, 0x2975b, 0x2977a, 0x29789,
  0x29793, 0x2979d, 0x297a7, 0x297d7, 0x2982f, 0x29428, 0x294d3, 0x29594, 0x29634, 0x2966b,
  0x29428, 0x294d3, 0x287f8, 0x29841, 0x29904, 0x29919, 0x29943, 0x2996d, 0x29982, 0x29997,
  0x299bc, 0x299ec, 0x298ef, 0x28eea, 0x28f43, 0x28f9f, 0x2910b, 0x28ffd, 0x29050, 0x2909a,
  0x290ad, 0x290d9, 0x290f2, 0x2911a, 0x29133, 0x2914c, 0x290c0, 0x29165, 0x291b1, 0x291fd,
  0x29249, 0x29295, 0x292a8, 0x29342, 0x2934c, 0x292bb, 0x292d5, 0x29307, 0x29311, 0x2931b,
  0x29356, 0x293bf, 0x29325, 0x29338, 0x29081, 0x2992e, 0x29a08, 0x29958, 0x28ed6, 0x28ee0
 };
 OpcodeMap out{};
 for(size_t i=0;i<170;++i)if(handlers[i]!=0x29a19)out[i].handler_address=handlers[i];
 for(unsigned op:{3,4,10,12,54,55,56,57,58,59,60}) {out[op].operand_bytes=2;out[op].signed_operand=true;}
 out[9].operand_bytes=4;out[9].signed_operand=true;
 for(unsigned op:{11,13}) {out[op].operand_bytes=1;out[op].signed_operand=true;}
 out[15].operand_bytes=2;out[16].operand_bytes=1;
 out[0].operand_bytes=4;out[0].flow=InstructionFlow::enter;
 out[3].flow=InstructionFlow::conditional;out[4].flow=InstructionFlow::jump;
 out[7].flow=InstructionFlow::terminate;
 for(unsigned op=54;op<=60;++op)out[op].flow=InstructionFlow::declaration;
 return out;
}
Result<Instruction> decode_instruction(ByteView code,size_t offset,const OpcodeMap& map,std::string_view name) {
 auto fail=[&](std::string why)->Result<Instruction>{return DataError{"",std::string(name),offset,std::move(why)};};
 if(offset>=code.size())return fail("instruction offset outside code");
 auto op=std::to_integer<uint8_t>(code[offset]);const auto& info=map[op];
 if(!info.handler_address)return fail("unknown opcode "+std::to_string(op));
 if(info.operand_bytes>code.size()-offset-1)return fail("truncated opcode operand");
 Instruction out{op,offset,offset+1+info.operand_bytes,0,info.flow,{}};
 uint32_t raw=0;
 for(unsigned i=0;i<info.operand_bytes;++i)raw|=uint32_t(std::to_integer<uint8_t>(code[offset+1+i]))<<(8*i);
 if(info.signed_operand) {
  if(info.operand_bytes==1)out.operand=std::bit_cast<int8_t>(uint8_t(raw));
  else if(info.operand_bytes==2)out.operand=std::bit_cast<int16_t>(uint16_t(raw));
  else out.operand=std::bit_cast<int32_t>(raw);
 } else out.operand=raw;
 if(info.flow==InstructionFlow::enter) {
  if((raw&65535)!=0xdead)return fail("invalid Enter marker");
  size_t length=raw>>16;
  if(length>code.size()-out.next_offset)return fail("truncated Enter metadata");
  out.operand=length;out.next_offset+=length;
 }
 if(info.flow==InstructionFlow::jump||info.flow==InstructionFlow::conditional||info.flow==InstructionFlow::declaration) {
  int64_t target=int64_t(out.next_offset)+out.operand;
  if(target<0)return fail("branch before code");
  out.branch_target=size_t(target);
 }
 return out;
}
}
