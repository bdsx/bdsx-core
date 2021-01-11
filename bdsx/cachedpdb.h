#pragma once

#include <KR3/util/pdb.h>

class CachedPdb
{
private:
	kr::BText<32> m_hash;
	kr::Keep<kr::File> m_predefinedFile;
	kr::PdbReader m_pdb;
	bool m_fail;
	bool m_opened;

public:
	CachedPdb() noexcept;
	~CachedPdb() noexcept;
	kr::Text getMd5() noexcept;
	bool open() noexcept;
	void close() noexcept;
	int setOptions(int options) noexcept;
	int getOptions() noexcept;
	void getProcAddresses(kr::View<kr::Text> text, void(*cb)(kr::Text name, void* fnptr)) noexcept;
	kr::JsValue getProcAddresses(kr::JsValue out, kr::JsValue array, bool quiet) throws(kr::JsException);
	void search(kr::JsValue masks, kr::JsValue cb, bool quiet) throws(kr::JsException);
	kr::JsValue getAll(bool quiet) throws(kr::JsException);
};

extern CachedPdb g_pdb;

kr::JsValue getPdbNamespace() noexcept;

