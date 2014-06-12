// Copyright (c) 2005, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// ---
// Author: Jacob Hoffman-Andrews
// Modified: Roman Gershman (romange@gmail.com)

#ifndef BASE_GOOGLEINIT_H
#define BASE_GOOGLEINIT_H

#include "base/commandlineflags.h"
#include "base/logging.h"

namespace __internal__ {

class ModuleInitializer {
 public:
  typedef void (*VoidFunction)(void);

  ModuleInitializer(VoidFunction ctor, VoidFunction dtor);

  ~ModuleInitializer();
private:
  const VoidFunction destructor_;
};

}  // __internal__

#define REGISTER_MODULE_INITIALIZER(name, body)             \
  namespace {                                               \
    static void google_init_module_##name () { body; }      \
    __internal__::ModuleInitializer google_initializer_module_##name(     \
            google_init_module_##name, nullptr);            \
  }

#define REGISTER_MODULE_DESTRUCTOR(name, body)                  \
  namespace {                                                   \
    static void google_destruct_module_##name () { body; }      \
    __internal__::ModuleInitializer google_destructor_module_##name(nullptr, \
        google_destruct_module_##name);               \
  }

class MainInitGuard {
  public:
    MainInitGuard(int* argc, char*** argv);

    ~MainInitGuard();
};

#define MainInitGuard(x, y) static_assert(false, "Forgot variable name")

#endif /* BASE_GOOGLEINIT_H */
