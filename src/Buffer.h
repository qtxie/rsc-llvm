/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#ifndef RED_SYSTEM_BUFFER_H
#define RED_SYSTEM_BUFFER_H

#include <vector>

template <typename T>
class Buffer
{
private:
    const uint32_t ElementsPerChunk;
    uint32_t Count;
    std::vector<T*> Chunks;
    T *Chunk;

    void addChunk() {
        Count = 0;
        Chunk = (T*)::calloc(ElementsPerChunk, sizeof(T));
        if (Chunk == nullptr) {
            llvm::errs() << "Buffer: allocation failed\n";
            ::exit(EXIT_FAILURE);
        }
        Chunks.push_back(Chunk);
    }

public:
    Buffer() : ElementsPerChunk(200000), Count(0) {
        addChunk();
    };

    ~Buffer() {
        for (auto P : Chunks) {::free(P);}
    };

    T* alloc() {
        if (Count < ElementsPerChunk)
            return &Chunk[Count++];
        else
            addChunk();
    }
};

#endif /* RED_SYSTEM_BUFFER_H */
