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

#ifndef LIBSPIRV_SPIRV_CONTAINER_H_
#define LIBSPIRV_SPIRV_CONTAINER_H_

#include <vector>
#include <utility>
#include <algorithm>
#include <iostream>
#include <cstdint>
#include <cstring>
#include <cassert>
#include "tools/io.h"

//! This class implements a simple SPIR-V container format that can be used to
//! easily bundle multiple SPIR-V modules in a single file. Alternatively, it
//! can also wrap a singular SPIR-V binary, thus providing the same interface
//! for both file types.
//!
//! [[SPIR-V container file format]]
//! ## header
//! char[4]: identifier "SPVC"
//! uint32_t: version (currently 2)
//! uint32_t: entry_count
//!
//! ## header entries [entry_count]
//! uint32_t: function_entry_count
//! uint32_t: SPIR-V module word count (word == uint32_t)
//!
//! ## module entries [entry_count]
//! uint32_t[header_entry[i].word_count]: SPIR-V module
//!
//! ## additional metadata [entry_count]
//! uint32_t[function_entry_count]: function types
//! char[function_entry_count][]: function names
//!   -> NOTE: always \0 terminated, with \0 padding to achieve 4-byte alignment
class spirv_container {
public:
  //! current version number of the SPIR-V container file format
  enum : uint32_t { SPIRV_CONTAINER_VERSION = 2u };

  //! module entry in this container
  struct module_t {
    //! pointer to the beginning of a SPIR-V module
    const uint32_t *const data;
    //! size of the SPIR-V module in 32-bit uint words
    const size_t size;
    //! metadata: function types + names
    std::vector<std::pair<uint32_t /* type */, std::string /* name */>> functions;
    module_t(const uint32_t *const data_, const size_t &size_) noexcept
        : data(data_), size(size_) {}
  };

protected:
  std::vector<uint32_t> data;
  std::vector<module_t> modules;
  bool container{false};
  bool valid{true};

public:
  //! construct from a moved vector<uint32_t>
  template <typename F = decltype(std::cerr)>
  spirv_container(std::vector<uint32_t> &&data_, F &diag = std::cerr) noexcept
      : data(std::forward<std::vector<uint32_t>>(data_)) {
    if (!data.empty() && memcmp(&data[0], "SPVC", 4) == 0) {
      // this is a container
      container = true;

      // header checking
      if (data.size() < 3) {
        diag << "invalid SPIR-V container\n";
        valid = false;
        return;
      }
      if (data[1] != SPIRV_CONTAINER_VERSION) {
        diag << "invalid SPIR-V container version: '" << data[1]
             << "', expected '" << SPIRV_CONTAINER_VERSION << "'\n";
        valid = false;
        return;
      }

      // get module entry count + size checking
      const auto entry_count = data[2];
      const auto expected_entry_words = entry_count * 2;
      uint32_t running_offset = 3;
      uint32_t spirv_data_offset = running_offset + expected_entry_words;
      if (data.size() < spirv_data_offset) {
        diag << "invalid SPIR-V container size (not enough header entries)\n";
        valid = false;
        return;
      }
      const auto data_size = data.size() * sizeof(uint32_t);
      const auto data_end_ptr = ((const char*)&data[0]) + data_size;

      // process entries
      modules.reserve(entry_count);
      for (uint32_t i = 0; i < entry_count; ++i, running_offset += 2) {
        const auto function_count = data[running_offset];
        const auto module_word_count = data[running_offset + 1];
        modules.emplace_back(&data[spirv_data_offset], module_word_count);
        modules.back().functions.resize(function_count);
        spirv_data_offset += module_word_count;
      }
      running_offset = spirv_data_offset;

      // process metadata
      for (uint32_t module_idx = 0; module_idx < entry_count; ++module_idx) {
        auto& mod = modules[module_idx];
        const auto func_count = mod.functions.size();

        for (size_t func_idx = 0; func_idx < func_count; ++func_idx) {
          mod.functions[func_idx].first = data[running_offset++];
        }

        for (size_t func_idx = 0; func_idx < func_count; ++func_idx) {
          const auto data_ptr = (const char*)&data[running_offset];
          if (std::find(data_ptr, data_end_ptr, '\0') == data_end_ptr) {
            diag << "invalid SPIR-V container: function name has no terminator\n";
            valid = false;
            return;
          }
          mod.functions[func_idx].second = data_ptr; // string is \0 terminated

          auto padded_len = (uint32_t)mod.functions[func_idx].second.size();
          padded_len += 4u - (padded_len % 4u);
          if ((running_offset * 4u) + padded_len > data_size) {
            diag << "invalid SPIR-V container: invalid function name size (not padded?)\n";
            valid = false;
            return;
          }
          running_offset += padded_len / 4u;
        }
      }

      // we're done here
      if (running_offset > data.size()) {
        diag << "invalid SPIR-V container size (SPIR-V data too large)\n";
        valid = false;
        return;
      }
    } else {
      // this is a simple SPIR-V file
      modules.emplace_back(data.data(), data.size());
    }
  }

  // TODO: construct from a file

  //! rebuilds this container by assembling all specified modules
  template <typename F = decltype(std::cerr)>
  bool rebuild(const std::vector<module_t>& new_modules, F &diag = std::cerr) {
    data.clear();
    modules.clear();
    container = true;
    
    // write new header
    data.emplace_back(0x43565053 /* little-endian SPVC */);
    data.emplace_back(SPIRV_CONTAINER_VERSION);
    const auto new_module_count = (uint32_t)new_modules.size();
    data.emplace_back(new_module_count);
    
    // write header entries
    uint32_t add_reserve_size = 0;
    for (const auto& mod : new_modules) {
      if (mod.functions.empty()) {
        diag << "no functions in module\n";
        return false;
      }
      data.emplace_back((uint32_t)mod.functions.size());
      data.emplace_back(mod.size);
      add_reserve_size += mod.size;
    }
    
    // write individual modules
    data.reserve(data.size() + add_reserve_size);
    std::vector<uint32_t> module_offsets;
    for (const auto& mod : new_modules) {
      module_offsets.emplace_back((uint32_t)data.size());
      data.insert(data.end(), mod.data, mod.data + mod.size);
    }
    
    // write additional metadata
    for (const auto& mod : new_modules) {
      // function types
      for (const auto& func : mod.functions) {
        data.emplace_back(func.first);
      }
      // function names
      for (const auto& func : mod.functions) {
        const auto name_len = (uint32_t)func.second.size();
        const auto name_padding = 4u - (name_len % 4u);
        assert((name_len + name_padding) % 4u == 0u);
        auto cur_offset = data.size();
        data.resize(cur_offset + ((name_len + name_padding) / 4u));
        memcpy(&data[cur_offset], func.second.c_str(), name_len);
        memset(((uint8_t*)&data[cur_offset]) + name_len, 0, name_padding);
      }
    }
    
    // update container modules
    for (uint32_t mod_idx = 0; mod_idx < new_module_count; ++mod_idx) {
      modules.emplace_back(module_t(&data[module_offsets[mod_idx]], new_modules[mod_idx].size));
      modules.back().functions = new_modules[mod_idx].functions;
    }
    
    return true;
  }

  //! writes the data of this container to the specified "out_file"
  bool write(const char* out_file) const {
    if (!WriteFile<uint32_t>(out_file, "wb", &data[0], data.size())) {
      return false;
    }
    return true;
  }

  //! returns true if this is a container, false if it's a simple SPIR-V file
  bool is_container() const { return container; }

  //! returns true if the container is in a valid state
  bool is_valid() const { return valid; }

  //! module begin iterator
  decltype(modules.begin()) begin() { return modules.begin(); }

  //! module end iterator
  decltype(modules.end()) end() { return modules.end(); }

  //! module const begin iterator
  decltype(modules.cbegin()) cbegin() const { return modules.cbegin(); }

  //! module const end iterator
  decltype(modules.cend()) cend() const { return modules.cend(); }

  //! returns the amount of modules in this container
  decltype(modules.size()) size() const { return modules.size(); }

  //! returns the underlying container or SPIR-V module data
  decltype(data.data()) underlying_data() { return data.data(); }
};

#endif
