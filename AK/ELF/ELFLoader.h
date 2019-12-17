#pragma once

#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/OwnPtr.h>
#include <AK/RefPtr.h>
#include <AK/Vector.h>
#include <AK/VirtualAddress.h>
#include <AK/ELF/ELFImage.h>
#include <AK/ELF/ELFFinder.h>

#ifdef KERNEL
class Region;
#endif

class ELFLoader {
public:
    ELFLoader(OwnPtr<ELFFinder>);
    ~ELFLoader();

    void add_image(OwnPtr<ELFImage> image);

    bool load();
<<<<<<< Updated upstream
=======
    Function<void*(VirtualAddress, size_t size, size_t alignment, size_t offset, int prot)> map_section_hook;
    Function<void*(VirtualAddress, size_t size, size_t alignment, int prot)> alloc_section_hook;
    VirtualAddress entry() const { return m_image.entry(); }
>>>>>>> Stashed changes
    char* symbol_ptr(const char* name);

    bool has_symbols() const { return m_images[0]->symbol_count(); }
    String symbolicate(dword address) const;

    VirtualAddress entry() const { return m_images[0]->entry(); }

private:
    bool layout(ELFImage&);
    bool perform_relocations();
    char* area_for_section(const ELFImage::Section&);
    char* area_for_section_name(const char*);

    struct PtrAndSize {
        PtrAndSize() {}
        PtrAndSize(char* p, unsigned s)
            : ptr(p)
            , size(s)
        {
        }

        char* ptr { nullptr };
        unsigned size { 0 };
    };
    Vector<OwnPtr<ELFImage>> m_images;
    Vector<ELFImage*> m_images_to_layout;
    OwnPtr<ELFFinder> m_finder;

    struct SortedSymbol {
        dword address;
        const char* name;
    };
#ifdef KERNEL
    mutable RefPtr<Region> m_sorted_symbols_region;
#else
    mutable Vector<SortedSymbol> m_sorted_symbols;
#endif
};
