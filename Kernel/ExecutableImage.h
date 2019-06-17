#pragma once

#include <AK/ELF/ELFImage.h>
#include <AK/ELF/ELFLoader.h>
#include <AK/NonnullRefPtr.h>

class Process;
class VMObject;
class VirtualAddress;

class ExecutableImage final : public ELFImage {
public:
    explicit ExecutableImage(const byte*, Process&, NonnullRefPtr<VMObject> vmo);

    virtual void* alloc_section(VirtualAddress vaddr, size_t size, size_t alignment, int prot) override;
    virtual void* map_section(VirtualAddress vaddr, size_t size, size_t alignment, size_t offset_in_image, int prot) override;

private:
    Process& m_process;
    NonnullRefPtr<VMObject> m_vmo;
};
