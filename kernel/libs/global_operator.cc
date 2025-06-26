
#include "printer.hh"
#include "global_operator.hh"
#include "heap_memory_manager.hh"

void *operator new(uint64 size)
{

	void *p = mem::k_hmm.allocate(size);
	// Info("new with size %d,at 0x%p\n", size, p);
	return p;
}
void *operator new[](uint64 size)
{

	void *p = mem::k_hmm.allocate(size);
	// Info("new[] with size %d,at 0x%p\n", size, p);
	return p;
}
void operator delete(void *p) noexcept
{
	mem::k_hmm.free(p);
	// Info("delete 0x%p\n", p);
}
void operator delete[](void *p) noexcept
{
	mem::k_hmm.free(p);
	// Info("delete[] 0x%p\n", p);
}
void operator delete(void *p, uint64 size) noexcept
{
	mem::k_hmm.free(p);
	// Info("delete 0x%p with size %d\n", p, size);
}
void operator delete[](void *p, uint64 size) noexcept
{
	mem::k_hmm.free(p);
	// Info("delete[] 0x%p with size %d\n", p, size);
}
void operator delete[](void *ptr, std::size_t size, std::align_val_t align) noexcept
{
	operator delete[](ptr, size);
}

void operator delete(void *ptr, std::size_t size, std::align_val_t align) noexcept
{
	// 如果你对齐分配用的是 aligned_alloc/free，就改成对应的 free_aligned(ptr)
	operator delete(ptr, size);
}

void *operator new(std::size_t size, void *p) noexcept
{
	// Info("placement new at %p\n", p);
	return p;
}
void operator delete(void *, void *) noexcept
{
	/* no-op */
}