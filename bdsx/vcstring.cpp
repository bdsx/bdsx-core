#include "stdafx.h"
#include "vcstring.h"

#include <KR3/win/handle.h>

using namespace kr;

struct Ucrtbase
{
	void* (*malloc)(size_t size);
	void(*free)(void* ptr);

	Ucrtbase() noexcept
	{
		HMODULE dll = GetModuleHandleW(L"ucrtbase.dll");
		_assert(dll != nullptr);
		malloc = (autoptr)GetProcAddress(dll, "malloc");
		free = (autoptr)GetProcAddress(dll, "free");
	}
};
namespace {
	Ucrtbase ucrtbase;
}

const char String<char>::ERRMSG[BUF_SIZE] = "[out of memory]";
const char16_t String<char16_t>::ERRMSG[BUF_SIZE] = u"[OOMem]";

template <typename CHR>
CHR* String<CHR>::data() noexcept {
	if (capacity >= BUF_SIZE) return pointer;
	else return buffer;
}
template <typename CHR>
CHR* String<CHR>::resize(size_t nsize) noexcept {
	if (nsize > capacity)
	{
		if (capacity >= BUF_SIZE) String_free16(pointer, capacity);
		capacity = nsize;
		pointer = (CHR*)String_allocate16(nsize + 1);
		if (pointer == nullptr)
		{
			memcpy(buffer, "[out of memory]", 16);
			capacity = BUF_SIZE-1;
			size = BUF_SIZE-1;
		}
		return pointer;
	}
	else
	{
		return data();
	}
}
template <typename CHR>
void String<CHR>::assign(kr::Text text) noexcept {
	size_t nsize = text.size();
	CHR* dest = resize(nsize);
	memcpy(dest, text.data(), nsize);
	dest[nsize] = (CHR)'\0';
	size = nsize;
}

template String<char>;
template String<char16_t>;

void* String_allocate16(size_t size) noexcept {
	if (size >= 0x1000) {
		uintptr_t res = (uintptr_t)ucrtbase.malloc(size + 0x27);
		if (res == 0) return nullptr;
		void** out = (void**)((res + 0x27) & (~0x1f));
		out[-1] = (void*)res;
		return out;
	}
	else {
		return ucrtbase.malloc(size);
	}
}

void String_free16(void* p, size_t size) noexcept {
	if (size >= 0x1000) {
		p = ((void**)p)[-1];
	}
	ucrtbase.free(p);
}
