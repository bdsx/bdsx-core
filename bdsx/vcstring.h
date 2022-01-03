#pragma once

void* String_allocate16(size_t size) noexcept;
void String_free16(void* p, size_t size) noexcept;

template<typename CHR>
struct String {
	static constexpr size_t BUF_SIZE = 16 / sizeof(CHR);
	static const CHR ERRMSG[BUF_SIZE];

	union {
		CHR buffer[BUF_SIZE];
		CHR* pointer;
	};
	size_t size;
	size_t capacity;

	CHR* data() noexcept;
	CHR* resize(size_t nsize) noexcept;
	void assign(kr::Text text) noexcept;
	
	template <typename Derived, typename Component, typename Parent>
	void init(const kr::HasCopyTo<Derived, Component, Parent> &text) noexcept {
		size_t sz = text.size();

		if (sz < BUF_SIZE) {
			size = sz;
			capacity = BUF_SIZE-1;
			text.copyTo(buffer);
			buffer[sz] = (CHR)'\0';
		}
		else {
			pointer = (CHR*)String_allocate16(sz*sizeof(CHR) + sizeof(CHR));
			text.copyTo(pointer);
			pointer[sz] = (CHR)'\0';
			capacity = size = sz;
		}
	}
};

template <typename CHR>
struct StringSpan {
	size_t size;
	const CHR* data;
};

String<char16_t>* String_toWide(String<char16_t>* out_str, StringSpan<char>* in_str) noexcept;
String<char>* String_toUtf8(String<char>* out_str, StringSpan<char16_t>* in_str) noexcept;

extern template String<char>;
extern template String<char16_t>;
