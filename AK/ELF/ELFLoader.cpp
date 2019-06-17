#include "ELFLoader.h"
#include <AK/QuickSort.h>
#include <AK/kstdio.h>

#if defined(KERNEL)
#    include <Kernel/VM/MemoryManager.h>
#    include <Kernel/UnixTypes.h>
#else
#    include <sys/mman.h>
#endif

//#define ELFLOADER_DEBUG

ELFLoader::~ELFLoader()
{
}

void ELFLoader::add_image(OwnPtr<ELFImage> image)
{
    m_images.append(move(image));
}

bool ELFLoader::load()
{
#ifdef ELFLOADER_DEBUG
    m_images[0].dump();
#endif
    if (!m_images[0]->is_valid())
        return false;

    if (!layout())
        return false;

    return true;
}

bool ELFLoader::layout()
{
    bool failed = false;
    ELFImage& image = *m_images[0];
    image.for_each_program_header([&](const ELFImage::ProgramHeader& program_header) {
        if (program_header.type() != PT_LOAD)
            return;
#ifdef ELFLOADER_DEBUG
        kprintf("PH: L%x %u r:%u w:%u\n", program_header.vaddr().get(), program_header.size_in_memory(), program_header.is_readable(), program_header.is_writable());
#endif
        int prot = 0;
        if (program_header.is_readable())
            prot |= PROT_READ;
        if (program_header.is_writable())
            prot |= PROT_WRITE;
        if (program_header.is_executable())
            prot |= PROT_EXEC;

        if (program_header.is_writable()) {
            void *ptr = image.alloc_section(
                program_header.vaddr(),
                program_header.size_in_memory(),
                program_header.alignment(),
                prot);
            memcpy(ptr, program_header.raw_data(), program_header.size_in_image());
        } else {
            image.map_section(
                program_header.vaddr(),
                program_header.size_in_memory(),
                program_header.alignment(),
                program_header.offset(),
                prot);
        }
    });
    return !failed;
}

char* ELFLoader::symbol_ptr(const char* name)
{
    char* found_ptr = nullptr;
    ELFImage& image = *m_images[0];
    image.for_each_symbol([&](const ELFImage::Symbol symbol) {
        if (symbol.type() != STT_FUNC)
            return IterationDecision::Continue;
        if (strcmp(symbol.name(), name))
            return IterationDecision::Continue;
        if (image.is_executable())
            found_ptr = (char*)symbol.value();
        else
            ASSERT_NOT_REACHED();
        return IterationDecision::Break;
    });
    return found_ptr;
}

String ELFLoader::symbolicate(dword address) const
{
    const ELFImage& image = *m_images[0];
    SortedSymbol* sorted_symbols = nullptr;
#ifdef KERNEL
    if (!m_sorted_symbols_region) {
        m_sorted_symbols_region = MM.allocate_kernel_region(PAGE_ROUND_UP(image.symbol_count() * sizeof(SortedSymbol)), "Sorted symbols");
        sorted_symbols = (SortedSymbol*)m_sorted_symbols_region->vaddr().as_ptr();
        dbgprintf("sorted_symbols: %p\n", sorted_symbols);
        size_t index = 0;
        image.for_each_symbol([&](auto& symbol) {
            sorted_symbols[index++] = { symbol.value(), symbol.name() };
            return IterationDecision::Continue;
        });
        quick_sort(sorted_symbols, sorted_symbols + image.symbol_count(), [](auto& a, auto& b) {
            return a.address < b.address;
        });
    } else {
        sorted_symbols = (SortedSymbol*)m_sorted_symbols_region->vaddr().as_ptr();
    }
#else
    if (m_sorted_symbols.is_empty()) {
        m_sorted_symbols.ensure_capacity(image.symbol_count());
        image.for_each_symbol([this](auto& symbol) {
            m_sorted_symbols.append({ symbol.value(), symbol.name() });
            return IterationDecision::Continue;
        });
        quick_sort(m_sorted_symbols.begin(), m_sorted_symbols.end(), [](auto& a, auto& b) {
            return a.address < b.address;
        });
    }
    sorted_symbols = m_sorted_symbols.data();
#endif

    for (size_t i = 0; i < image.symbol_count(); ++i) {
        if (sorted_symbols[i].address > address) {
            if (i == 0)
                return "!!";
            auto& symbol = sorted_symbols[i - 1];
            return String::format("%s +%u", symbol.name, address - symbol.address);
        }
    }
    return "??";
}
