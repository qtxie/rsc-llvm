/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
 * 
 * Summary: 
 *   Forward-declares and imports various common LLVM datatypes that
 *   redsc wants to use unqualified.
*/

#ifndef RED_SYSTEM_LLVM_H
#define RED_SYSTEM_LLVM_H

namespace llvm {
    class raw_ostream;

    // ADT's.
    class StringRef;
    class Twine;
    class VersionTuple;
    template<typename T> class ArrayRef;
    template<typename T> class MutableArrayRef;
    template<typename T> class OwningArrayRef;
    template<unsigned InternalLen> class SmallString;
    template<typename T> class Optional;
    template <class T> class Expected;

    template<typename T>
    struct SaveAndRestore;

    // Reference counting.
    template <typename T> class IntrusiveRefCntPtr;
    template <typename T> struct IntrusiveRefCntPtrInfo;
    template <class Derived> class RefCountedBase;

    class raw_ostream;
    class raw_pwrite_stream;
}

// ADT's.
using llvm::ArrayRef;
using llvm::MutableArrayRef;
using llvm::Optional;
using llvm::OwningArrayRef;
using llvm::SaveAndRestore;
using llvm::SmallString;
using llvm::StringRef;
using llvm::Twine;
using llvm::VersionTuple;

// Error handling.
using llvm::Expected;

// Reference counting.
using llvm::IntrusiveRefCntPtr;
using llvm::IntrusiveRefCntPtrInfo;
using llvm::RefCountedBase;

using llvm::raw_ostream;
using llvm::raw_pwrite_stream;

#endif /* RED_SYSTEM_LLVM_H */
