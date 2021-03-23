#pragma once

#include <KR3/util/pdb.h>
#include <KR3/util/path.h>

class CachedPdb
{
private:
	kr::PdbReader m_pdb;

public:
	static kr::BText16<kr::Path::MAX_LEN> predefinedForCore;
	using Callback = void(*)(kr::Text16 name, void* fnptr, void* param);

	CachedPdb() noexcept;
	~CachedPdb() noexcept;
	void close() noexcept;
	int setOptions(int options) throws(kr::JsException);
	int getOptions() throws(kr::JsException);
	kr::TText16 undecorate(kr::Text16 text, int flags) noexcept;
	bool getProcAddresses(kr::pcstr16 predefined, kr::View<kr::Text16> text, Callback cb, void* param, bool quiet) noexcept;
	template <typename T>
	bool getProcAddressesT(kr::pcstr16 predefined, kr::View<kr::Text16> text, void(*cb)(kr::Text16 name, void* fnptr, T* param), T* param, bool quiet) noexcept
	{
		static_assert(sizeof(T*) == sizeof(void*), "pointer size unmatched");
		return getProcAddresses(predefined, text, (Callback)cb, param, quiet);
	}
	kr::JsValue getProcAddresses(kr::pcstr16 predefined, kr::JsValue out, kr::JsValue array, bool quiet, uint32_t undecorateOpts) throws(kr::JsException);
	void search(kr::JsValue masks, kr::JsValue cb) throws(kr::JsException);
	kr::JsValue getAll(kr::JsValue onprogress) throws(kr::JsException);
};

extern CachedPdb g_pdb;
extern kr::BText<32> g_md5;

kr::JsValue getPdbNamespace() noexcept;

