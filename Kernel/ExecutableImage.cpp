#include <Kernel/ExecutableImage.h>
#include <Kernel/Process.h>
#include <Kernel/VM/VMObject.h>
#include <AK/VirtualAddress.h>

ExecutableImage::ExecutableImage(const byte* data, Process& process, NonnullRefPtr<VMObject> vmo)
    : ELFImage(data)
    , m_process(process)
    , m_vmo(vmo)
{
}

void* ExecutableImage::alloc_section(VirtualAddress vaddr, size_t size, size_t alignment, int prot)
{
    ASSERT(size);
    ASSERT(alignment == PAGE_SIZE);
    (void) m_process.allocate_region(vaddr, size, "elf alloc", prot);
    return vaddr.as_ptr();
}

void* ExecutableImage::map_section(VirtualAddress vaddr, size_t size, size_t alignment, size_t offset_in_image, int prot)
{
    ASSERT(size);
    ASSERT(alignment == PAGE_SIZE);
    (void) m_process.allocate_region_with_vmo(vaddr, size, m_vmo.copy_ref(), offset_in_image, "elf map", prot);
    return vaddr.as_ptr();
}
