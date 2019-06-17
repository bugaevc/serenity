#pragma once

#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/OwnPtr.h>
#include <AK/Vector.h>
#include <AK/VirtualAddress.h>
#include <AK/ELF/ELFImage.h>

#ifdef KERNEL
class Region;
#endif

class ELFLoader {
public:
    ~ELFLoader();

    void add_image(OwnPtr<ELFImage> image);

    bool load();
    char* symbol_ptr(const char* name);

    bool has_symbols() const { return m_images[0]->symbol_count(); }
    String symbolicate(dword address) const;

    VirtualAddress entry() const { return m_images[0]->entry(); }

private:
    bool layout();
    bool perform_relocations();
    void* lookup(const ELFImage::Symbol&);
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
