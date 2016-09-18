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
#include <iostream>
#include <cstdint>
#include <cstring>

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
  struct module {
    //! pointer to the beginning of a SPIR-V module
    const uint32_t *const data;
    //! size of the SPIR-V module in 32-bit uint words
    const size_t size;
    module(const uint32_t *const data_, const size_t &size_) noexcept
        : data(data_), size(size_) {}
  };

protected:
  std::vector<uint32_t> data;
  std::vector<module> modules;
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

      // process entries
      modules.reserve(entry_count);
      for (uint32_t i = 0; i < entry_count; ++i, running_offset += 2) {
        // contents[running_offset] ignored (contains function count)
        const auto module_word_count = data[running_offset + 1];
        modules.emplace_back(&data[spirv_data_offset], module_word_count);
        spirv_data_offset += module_word_count;
      }

      // we're done here - metadata is ignored at this point

      if (spirv_data_offset > data.size()) {
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
  // TODO: assemble/construct from multiple modules

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
