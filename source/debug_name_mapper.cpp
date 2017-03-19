// Copyright (c) 2015-2016 The Khronos Group Inc.
// Copyright (c) 2016 Google Inc.
// Copyright (c) 2016 Florian Ziesche
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "name_mapper.h"

#include <cassert>
#include <algorithm>
#include <string>
#include <unordered_map>

#include "spirv-tools/libspirv.h"
#include "spirv/1.1/spirv.h"
#include "util/hex_float.h"

namespace spvtools {

DebugNameMapper::DebugNameMapper(const spv_const_context context,
                                 const uint32_t *code, const size_t wordCount)
    : spvtools::FriendlyNameMapper(context, code, wordCount, false) {
  // add a few more allowed characters that we need for nicer type names
  valid_chars_ += ".:,_-[]()*<>#@";

  // run the parsing step
  spv_diagnostic diag = nullptr;
  // We don't care if the parse fails.
  spvBinaryParse(context, this, code, wordCount, nullptr,
                 DebugParseInstructionForwarder, &diag);
  spvDiagnosticDestroy(diag);
}

void DebugNameMapper::SaveName(uint32_t id, const std::string &suggested_name) {
  FriendlyNameMapper::SaveName(id, suggested_name);
  max_name_length_ =
      std::max(max_name_length_, (uint32_t)suggested_name.length());
}

// NOTE: initial code taken from FriendlyNameMapper::ParseInstruction
spv_result_t
DebugNameMapper::ParseInstruction(const spv_parsed_instruction_t &inst) {
  const auto result_id = inst.result_id;
  switch (inst.opcode) {
  // TODO: handle OpMemberName
  case SpvOpName:
    SaveName(inst.words[1], reinterpret_cast<const char *>(inst.words + 2));
    break;
  // TODO: handle OpGroupDecorate
  case SpvOpDecorate:
    // Decorations come after OpName.  So OpName will take precedence over
    // decorations.
    if (inst.words[2] == SpvDecorationBuiltIn) {
      assert(inst.num_words > 3);
      SaveBuiltInName(inst.words[1], inst.words[3]);
    }
    break;
  case SpvOpTypeVoid:
    SaveName(result_id, "void");
    break;
  case SpvOpTypeBool:
    SaveName(result_id, "bool");
    break;
  case SpvOpTypeInt: {
    std::string root;
    const auto bit_width = inst.words[2];
    switch (bit_width) {
    case 8:
      root = "char";
      break;
    case 16:
      root = "short";
      break;
    case 32:
      root = "int";
      break;
    case 64:
      root = "long";
      break;
    default:
      root = std::to_string(bit_width);
      break;
    }
    SaveName(result_id, (0 == inst.words[3] ? "u" : "i") + root);
  } break;
  case SpvOpTypeFloat: {
    const auto bit_width = inst.words[2];
    switch (bit_width) {
    case 16:
      SaveName(result_id, "half");
      break;
    case 32:
      SaveName(result_id, "float");
      break;
    case 64:
      SaveName(result_id, "double");
      break;
    default:
      SaveName(result_id, std::string("fp") + std::to_string(bit_width));
      break;
    }
  } break;
  case SpvOpTypeVector:
    SaveName(result_id, "<" + std::to_string(inst.words[3]) + "x" +
                            NameForId(inst.words[2]) + ">");
    break;
  case SpvOpTypeMatrix:
    // TODO: nicer name
    SaveName(result_id, std::string("mat") + std::to_string(inst.words[3]) +
                            NameForId(inst.words[2]));
    break;
  case SpvOpTypeArray:
    SaveName(result_id,
             NameForId(inst.words[2]) + "[" + NameForId(inst.words[3]) + "]");
    break;
  case SpvOpTypeRuntimeArray:
    SaveName(result_id, NameForId(inst.words[2]) + "[]");
    break;
  case SpvOpTypePointer:
    SaveName(result_id, "(" + NameForEnumOperand(SPV_OPERAND_TYPE_STORAGE_CLASS,
                                                 inst.words[2]) +
                            ")" + NameForId(inst.words[3]) + "*");
    break;
  case SpvOpTypePipe:
    SaveName(result_id,
             std::string("Pipe") +
                 NameForEnumOperand(SPV_OPERAND_TYPE_ACCESS_QUALIFIER,
                                    inst.words[2]));
    break;
  case SpvOpTypeEvent:
    SaveName(result_id, "Event");
    break;
  case SpvOpTypeDeviceEvent:
    SaveName(result_id, "DeviceEvent");
    break;
  case SpvOpTypeReserveId:
    SaveName(result_id, "ReserveId");
    break;
  case SpvOpTypeQueue:
    SaveName(result_id, "Queue");
    break;
  case SpvOpTypeOpaque:
    SaveName(result_id,
             std::string("Opaque_") +
                 Sanitize(reinterpret_cast<const char *>(inst.words + 2)));
    break;
  case SpvOpTypePipeStorage:
    SaveName(result_id, "PipeStorage");
    break;
  case SpvOpTypeNamedBarrier:
    SaveName(result_id, "NamedBarrier");
    break;
  case SpvOpTypeSampler:
    SaveName(result_id, "Sampler");
    break;
  case SpvOpTypeImage:
    SaveName(result_id, "Image");
    break;
  case SpvOpTypeSampledImage:
    SaveName(result_id, "SampledImage");
    break;
  case SpvOpTypeStruct:
    // Structs are mapped rather simplistically. Just indicate that they
    // are a struct and then give the raw Id number.
    SaveName(result_id, "struct." + std::to_string(result_id));
    break;
  case SpvOpTypeFunction: {
#if 0 // NOTE: would be nice to have this, but easily leads to *very* long names
    std::string param_list = "(";
    for (uint16_t i = 3; i < inst.num_words; ++i) {
      param_list += NameForId(inst.words[i]);
      if (i + 1 != inst.num_words) {
        param_list += ',';
      }
    }
    param_list += ')';
    SaveName(result_id, NameForId(inst.words[2]) + param_list);
#else
    SaveName(result_id, NameForId(inst.words[2]) + "(" +
                            (inst.num_words - 3 > 0
                                 ? "#" + std::to_string(inst.num_words - 3)
                                 : "") +
                            ")");
#endif
    break;
  }
  case SpvOpConstantNull:
    SaveName(result_id, "null");
    break;
  case SpvOpConstantTrue:
    SaveName(result_id, "true");
    break;
  case SpvOpConstantFalse:
    SaveName(result_id, "false");
    break;
  case SpvOpConstant: {
    // NOTE: must be float or integer type
    static const std::unordered_map<std::string,
                                    std::pair<uint32_t, std::string>>
        suffix_lut{
            // standard suffixes
            {"iint", {4, "i"}},
            {"uint", {4, "u"}},
            {"ilong", {8, "l"}},
            {"ulong", {8, "ul"}},
            {"half", {2, "h"}},
            {"float", {4, "f"}},
            {"double", {8, "d"}},
            // no standard suffixes for these, so just use their bit-width
            {"ichar", {1, "i8"}},
            {"uchar", {1, "u8"}},
            {"ishort", {2, "i16"}},
            {"ushort", {2, "u16"}},
        };
    const auto suffix_entry = suffix_lut.find(NameForId(inst.type_id));
    if (suffix_entry != suffix_lut.cend()) {
      // TODO/NOTE: copied from disassemble.cpp, should probably unify into a
      // util function
      const spv_parsed_operand_t &operand = inst.operands[2];
      const uint32_t word = inst.words[operand.offset];
      std::stringstream name;
      if (operand.num_words == 1) {
        switch (operand.number_kind) {
        case SPV_NUMBER_SIGNED_INT:
          name << int32_t(word);
          break;
        case SPV_NUMBER_UNSIGNED_INT:
          name << word;
          break;
        case SPV_NUMBER_FLOATING:
          if (operand.number_bit_width == 16) {
            name << spvtools::utils::FloatProxy<spvtools::utils::Float16>(
                uint16_t(word & 0xFFFF));
          } else {
            name << spvtools::utils::FloatProxy<float>(word);
          }
          if (name.str().find('.') == std::string::npos) {
            name << ".0";
          }
          break;
        default:
          assert(false && "Unreachable");
        }
      } else if (operand.num_words == 2) {
        // Multi-word numbers are presented with lower order words first.
        uint64_t bits =
            uint64_t(word) | (uint64_t(inst.words[operand.offset + 1]) << 32);
        switch (operand.number_kind) {
        case SPV_NUMBER_SIGNED_INT:
          name << int64_t(bits);
          break;
        case SPV_NUMBER_UNSIGNED_INT:
          name << bits;
          break;
        case SPV_NUMBER_FLOATING:
          name << spvtools::utils::FloatProxy<double>(bits);
          break;
        default:
          assert(false && "Unreachable");
        }
      }
      name << suffix_entry->second.second;
      SaveName(result_id, name.str());
      break;
    }
// else: fallthrough to normal id handling
#if __cplusplus > 201402L
    [[fallthrough]];
#elif defined(__clang__)
    [[clang::fallthrough]];
#endif
  }
  default:
    // If this instruction otherwise defines an Id, then save a mapping for
    // it.  This is needed to ensure uniqueness in there is an OpName with
    // string something like "1" that might collide with this result_id.
    // We should only do this if a name hasn't already been registered by some
    // previous forward reference.
    if (result_id && name_for_id_.find(result_id) == name_for_id_.end())
      SaveName(result_id, std::to_string(result_id));
    break;
  }
  return SPV_SUCCESS;
}

} // namespace spvtools
