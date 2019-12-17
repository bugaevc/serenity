#include "ELFLoader.h"
#include <AK/QuickSort.h>
#include <AK/kstdio.h>

#if defined(KERNEL)
<<<<<<< Updated upstream
#    include <Kernel/VM/MemoryManager.h>
#    include <Kernel/UnixTypes.h>
#else
#    include <sys/mman.h>
=======
#    include <Kernel/Arch/i386/CPU.h>
#else
#    include <limits.h>
>>>>>>> Stashed changes
#endif

//#define ELFLOADER_DEBUG

ELFLoader::ELFLoader(OwnPtr<ELFFinder> finder)
    : m_finder(move(finder))
{
}

ELFLoader::~ELFLoader()
{
}

void ELFLoader::add_image(OwnPtr<ELFImage> image)
{
    m_images_to_layout.append(image.ptr());
    m_images.append(move(image));
}

bool ELFLoader::load()
{
#ifdef ELFLOADER_DEBUG
    m_images[0].dump();
#endif
    while (!m_images_to_layout.is_empty()) {
        ELFImage& image = *m_images_to_layout[0];
        m_images_to_layout.shift_left(1);
        bool r = layout(image);
        if (!r)
            return false;
    }
    return true;
}

bool ELFLoader::layout(ELFImage& image)
{
    bool failed = false;
    image.for_each_program_header([&](const ELFImage::ProgramHeader& program_header) {
         if (program_header.type() == PT_LOAD)
             image.note_section(program_header.vaddr(), program_header.size_in_memory());
    });
    image.note_sections_done();
    image.for_each_program_header([&](const ELFImage::ProgramHeader& program_header) {

#ifdef ELFLOADER_DEBUG
        kprintf("PH: L%x %u r:%u w:%u\n", program_header.vaddr().get(), program_header.size_in_memory(), program_header.is_readable(), program_header.is_writable());
#endif
        if (failed)
            return;

        switch (program_header.type()) {
        case PT_INTERP:
        {
            const char* path = program_header.raw_data();
            // Make sure it's null-terminated.
            if (path[program_header.size_in_image() - 1] != 0) {
                failed = true;
                return;
            }
            if (m_finder)
                m_finder->add_interpreter({ path });
            return;
        }
        case PT_DYNAMIC:
        {
            auto for_each_dyn_tagged = [&](Elf32_Sword tag, auto f) {
                for (const Elf32_Dyn* dyn = (const Elf32_Dyn*) program_header.raw_data(); dyn->d_tag != DT_NULL; ++dyn)
                    if (dyn->d_tag == tag)
                        f(dyn);
            };
            const char* strtab { nullptr };
            for_each_dyn_tagged(DT_STRTAB, [&](const Elf32_Dyn* dyn) {
                image.for_each_section_of_type(SHT_STRTAB, [&](const ELFImage::Section& section) {
                    if (section.offset() == dyn->d_un.d_ptr)
                        strtab = section.raw_data();
                    return true;
                });
            });
            ASSERT(strtab != nullptr);
            for_each_dyn_tagged(DT_NEEDED, [&](const Elf32_Dyn* dyn) {
                 if (m_finder)
                     m_finder->add_needed({ strtab + dyn->d_un.d_val });
            });
            return;
        }
        default:
            return;
        case PT_LOAD:
            ;
            // fallthrough
        }

        int prot = 0;
        if (program_header.is_readable())
            prot |= PROT_READ;
        if (program_header.is_writable())
            prot |= PROT_WRITE;
        if (program_header.is_executable())
            prot |= PROT_EXEC;

<<<<<<< Updated upstream
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
=======
        map_section_hook(
            program_header.vaddr(),
            program_header.size_in_image(),
            program_header.alignment(),
            program_header.offset(),
            prot);

        dword extra_size = program_header.size_in_memory() - program_header.size_in_image();
        VirtualAddress end = program_header.vaddr().offset(program_header.size_in_image());
        VirtualAddress base = end;
        dword extra_mapped = 0;
        if (end.get() != end.page_base()) {
            base = VirtualAddress(end.page_base() + PAGE_SIZE);
            extra_mapped = base.get() - end.get();
        }
        memset((void*) end.get(), 0, extra_mapped);
        if (extra_mapped < extra_size) {
            alloc_section_hook(base, extra_size - extra_mapped, program_header.alignment(), prot);
>>>>>>> Stashed changes
        }
        dbgprintf("vaddr %p, size in mem %lu in image %lu extra %lu, end %p, base %p, extra_mapped %lu\n",
                  (void *) program_header.vaddr().get(),
                  program_header.size_in_memory(), program_header.size_in_image(), extra_size,
                  (void*) end.get(), (void *) base.get(), extra_mapped);
    });
    return !failed;
}

char* ELFLoader::symbol_ptr(const char* name)
{
    for (auto& image : m_images)
    {
        char* found_ptr = nullptr;
        image.for_each_symbol([&](const ELFImage::Symbol symbol) {
            // if (symbol.type() != STT_FUNC)
            //    return IterationDecision::Continue;
            if (strcmp(symbol.name(), name))
                return IterationDecision::Continue;
            found_ptr = (char*) symbol.value() + image.slide();
            return IterationDecision::Break;
        });
        if (found_ptr)
            return found_ptr;
    }
    return nullptr;
}

bool ELFLoader::perform_relocations()
{
    for (auto& image : m_images)
    {
        
    }
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
