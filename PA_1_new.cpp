#include <windows.h>
#include <iostream>
#include <unordered_map>
#include <cassert>



// Memory Structures
struct MemBlock {
    size_t size;
    bool free;
    bool first;
    bool last;
};

struct MemArena {
    size_t size;
    void* base;
    MemArena* next;
};


size_t default_arena_size = 4096;
MemArena* arena_list = nullptr;
std::unordered_map<void*, MemBlock*> block_map;

// Memory Allocator Functions and Structures
size_t align_mem_size(size_t size) {
    return (size + 3) & ~3;
}

MemArena* create_new_arena(size_t size, size_t default_size, MemArena*& arena_list, std::unordered_map<void*, MemBlock*>& block_map) {
    size = max(size, default_size);
    size = align_mem_size(size);

    void* base = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!base) {
        return nullptr;
    }

    MemArena* arena = new MemArena{ size, base, arena_list };
    arena_list = arena;

    MemBlock* initial_block = reinterpret_cast<MemBlock*>(base);
    initial_block->size = size - sizeof(MemBlock);
    initial_block->free = true;
    initial_block->first = true;
    initial_block->last = true;

    block_map[base] = initial_block;

    return arena;
}

void split_mem_block(MemBlock* block, size_t size, std::unordered_map<void*, MemBlock*>& block_map) {
    size = align_mem_size(size);
    if (block->size >= size + sizeof(MemBlock) + 4) {
        MemBlock* new_block = reinterpret_cast<MemBlock*>(reinterpret_cast<char*>(block) + sizeof(MemBlock) + size);
        new_block->size = block->size - size - sizeof(MemBlock);
        new_block->free = true;
        new_block->first = false;
        new_block->last = block->last;

        block->size = size;
        block->last = false;

        block_map[new_block] = new_block;
    }
}

void merge_free_blocks(std::unordered_map<void*, MemBlock*>& block_map) {
    for (auto it = block_map.begin(); it != block_map.end();) {
        MemBlock* block = it->second;
        if (block->free) {
            char* base = reinterpret_cast<char*>(block);
            MemBlock* next_block = reinterpret_cast<MemBlock*>(base + sizeof(MemBlock) + block->size);

            if (block_map.find(next_block) != block_map.end() && next_block->free) {
                block->size += sizeof(MemBlock) + next_block->size;
                block->last = next_block->last;
                block_map.erase(next_block);
                continue;
            }
        }
        ++it;
    }
}

void* allocate_memory_block(MemArena* arena, size_t size, std::unordered_map<void*, MemBlock*>& block_map) {
    size = align_mem_size(size);

    char* base = static_cast<char*>(arena->base);
    while (reinterpret_cast<size_t>(base) < reinterpret_cast<size_t>(arena->base) + arena->size) {
        MemBlock* block = reinterpret_cast<MemBlock*>(base);
        if (block->free && block->size >= size) {
            split_mem_block(block, size, block_map);
            block->free = false;
            return base + sizeof(MemBlock);
        }
        base += sizeof(MemBlock) + block->size;
    }
    return nullptr;
}

void* mem_alloc(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    size = align_mem_size(size);

    for (MemArena* arena = arena_list; arena; arena = arena->next) {
        if (void* ptr = allocate_memory_block(arena, size, block_map)) {
            return ptr;
        }
    }

    MemArena* new_arena = create_new_arena(size + sizeof(MemBlock), default_arena_size, arena_list, block_map);
    if (!new_arena) {
        return nullptr;
    }

    return allocate_memory_block(new_arena, size, block_map);
}

void mem_free(void* ptr) {
    if (!ptr) {
        return;
    }

    auto it = block_map.find(static_cast<char*>(ptr) - sizeof(MemBlock));
    if (it == block_map.end()) {
        return;
    }

    MemBlock* block = it->second;
    block->free = true;
    merge_free_blocks(block_map);
}

void* mem_realloc(void* ptr, size_t size) {
    if (!ptr) {
        return mem_alloc(size);
    }

    size = align_mem_size(size);

    auto it = block_map.find(static_cast<char*>(ptr) - sizeof(MemBlock));
    if (it == block_map.end()) {
        return nullptr;
    }

    MemBlock* block = it->second;
    if (block->size >= size) {
        split_mem_block(block, size, block_map);
        return ptr;
    }

    void* new_ptr = mem_alloc(size);
    if (!new_ptr) {
        return nullptr;
    }

    memcpy(new_ptr, ptr, block->size);
    mem_free(ptr);
    return new_ptr;
}

void mem_show(const char* message) {
    std::cout << message << std::endl;
    for (MemArena* arena = arena_list; arena; arena = arena->next) {
        std::cout << "Arena at " << arena->base << " with size " << arena->size << std::endl;
        char* base = static_cast<char*>(arena->base);
        while (reinterpret_cast<size_t>(base) < reinterpret_cast<size_t>(arena->base) + arena->size) {
            MemBlock* block = reinterpret_cast<MemBlock*>(base);
            std::cout << "  Block size: " << block->size
                << ", Free: " << (block->free ? "Yes" : "No")
                << ", First: " << (block->first ? "Yes" : "No")
                << ", Last: " << (block->last ? "Yes" : "No") << std::endl;
            base += sizeof(MemBlock) + block->size;
        }
    }
}

// Test Functions
struct MemoryTestBlock {
    void* ptr;
    size_t size;
    uint32_t checksum;
};

uint32_t compute_checksum(const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t checksum = 0;
    for (size_t i = 0; i < size; ++i) {
        checksum += bytes[i];
    }
    return checksum;
}

void fill_data_randomly(void* data, size_t size) {
    uint8_t* bytes = static_cast<uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = rand() % 256;
    }
}

void run_memory_test(size_t iterations, size_t max_block_size) {
    size_t default_arena_size = 4096;
    MemArena* arena_list = nullptr;
    std::unordered_map<void*, MemBlock*> block_map;

    std::vector<MemoryTestBlock> test_blocks;

    srand(static_cast<unsigned>(time(0)));

    for (size_t i = 0; i < iterations; ++i) {
        int operation = rand() % 3;
        switch (operation) {
        case 0: {
            size_t size = rand() % max_block_size + 1;
            std::cout << "mem_alloc(size=" << size << ")" << std::endl;
            void* ptr = mem_alloc(size);
            if (ptr) {
                fill_data_randomly(ptr, size);
                uint32_t checksum = compute_checksum(ptr, size);
                test_blocks.push_back({ ptr, size, checksum });
            }
            break;
        }
        case 1: {
            if (!test_blocks.empty()) {
                size_t index = rand() % test_blocks.size();
                MemoryTestBlock& block = test_blocks[index];
                uint32_t checksum = compute_checksum(block.ptr, block.size);
                assert(checksum == block.checksum && "Checksum mismatch before free");
                std::cout << "mem_free(ptr=" << block.ptr << ", size=" << block.size << ")" << std::endl;
                mem_free(block.ptr);
                test_blocks.erase(test_blocks.begin() + index);
            }
            break;
        }
        case 2: {
            if (!test_blocks.empty()) {
                size_t index = rand() % test_blocks.size();
                MemoryTestBlock& block = test_blocks[index];
                uint32_t checksum = compute_checksum(block.ptr, block.size);
                assert(checksum == block.checksum && "Checksum mismatch before realloc");
                size_t new_size = rand() % max_block_size + 1;
                std::cout << "mem_realloc(ptr=" << block.ptr << ", old_size=" << block.size << ", new_size=" << new_size << ")" << std::endl;
                void* new_ptr = mem_realloc(block.ptr, new_size);
                if (new_ptr) {
                    fill_data_randomly(new_ptr, new_size);
                    block.ptr = new_ptr;
                    block.size = new_size;
                    block.checksum = compute_checksum(new_ptr, new_size);
                }
            }
            break;
        }
        }
        mem_show(">>>");
    }

    for (const MemoryTestBlock& block : test_blocks) {
        uint32_t checksum = compute_checksum(block.ptr, block.size);
        assert(checksum == block.checksum && "Checksum mismatch in final verification");
        mem_free(block.ptr);
    }

    std::cout << "Automatic test completed" << std::endl;
}

// Main Function
int main() {
    /*
    run_memory_test((size_t) 10, (size_t) 1024);
    */

    void* p1 = mem_alloc(100);
    mem_show("mem_alloc(100)");
    void* p2 = mem_alloc(200);
    mem_show("mem_alloc(200)");
    void* p3 = mem_alloc(300);
    mem_show("mem_alloc(300)");
    mem_free(p1);
    mem_show("mem_free(p1)");
    mem_realloc(p2, 400);
    mem_show("mem_realloc(p2, 400)");

    
}