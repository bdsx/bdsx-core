#include "stdafx.h"
#include "cachedpdb.h"
#include "nativepointer.h"
#include "jsctx.h"

#include <KR3/util/pdb.h>
#include <KR3/data/set.h>
#include <KRWin/handle.h>
#include <KR3/data/crypt.h>

CachedPdb g_pdb;

using namespace kr;

namespace
{
	uint32_t getSelIndex(Text *text) noexcept
	{
		const char* namepos = text->find_r('#');
		if (namepos != nullptr)
		{
			uint32_t value = text->subarr(namepos + 1).to_uint();
			text->cut_self(namepos);
			return value;
		}
		return 0;
	}
	void* getPointer(const JsValue& value) noexcept
	{
		VoidPointer* ptr = value.getNativeObject<VoidPointer>();
		if (ptr == nullptr) return nullptr;
		return ptr->getAddressRaw();
	}
	ATTR_NORETURN void throwAsJsException(FunctionError& err) throws(JsException)
	{
		TSZ16 tsz;
		tsz << (AnsiToUtf16)(Text)err.getFunctionName() << u": failed, ";
		err.getMessageTo(&tsz);
		tsz << u"(0x" << hexf(err.getErrorCode(), 8) << u")\n";
		throw JsException(tsz);
	}
}

File* openPredefinedFile(Text hash) throws(Error)
{
	TText16 bdsxPath = win::Module::byName(u"bdsx.dll")->fileName();
	bdsxPath.cut_self(bdsxPath.find_r('\\') + 1);
	bdsxPath << u"predefined";
	File::createDirectory(bdsxPath.c_str());
	bdsxPath << u'\\' << (Utf8ToUtf16)hash << u".ini";
	return File::openRW(bdsxPath.c_str());
}

CachedPdb::CachedPdb() noexcept
{
	m_fail = false;
	m_opened = false;

	if (m_hash.empty())
	{
		try
		{
			TText16 moduleName = CurrentApplicationPath();
			m_hash = (encoder::Hex)(TBuffer)encoder::Md5::hash(File::open(moduleName.data()));
		}
		catch (Error&)
		{
			cerr << "[BDSX] Cannot read bedrock_server.exe" << endl;
			cerr << "[BDSX] Failed to get MD5" << endl;
			return;
		}
	}
}
CachedPdb::~CachedPdb() noexcept
{
}

Text CachedPdb::getMd5() noexcept
{
	return m_hash;
}
bool CachedPdb::open() noexcept
{
	m_fail = false;
	m_opened = false;

	if (!m_hash.empty() && m_predefinedFile == nullptr)
	{
		try
		{
			m_predefinedFile = openPredefinedFile(m_hash);
		}
		catch (Error&)
		{
			cout << "[BDSX] Cannot open the predefined.ini" << endl;
			m_fail = true;
			return false;
		}
	}
	m_opened = true;
	return true;
}
void CachedPdb::close() noexcept
{
	m_fail = false;
	m_opened = false;
	m_predefinedFile = nullptr;
	m_pdb.close();
}
int CachedPdb::setOptions(int options) noexcept
{
	return PdbReader::setOptions(options);
}
int CachedPdb::getOptions() noexcept
{
	return PdbReader::getOptions();
}
void CachedPdb::getProcAddresses(View<Text> text, void(*cb)(Text name, void* fnptr)) noexcept
{
	_assert(m_opened);

	Map<Text, uint32_t, true> targets;
	HINSTANCE instance = GetModuleHandleW(nullptr);

	for (Text tx : text)
	{
		const char * namepos = tx.find_r('#');
		uint32_t selbit = 1U << getSelIndex(&tx);

		auto res = targets.insert(tx, selbit);
		if (!res.second)
		{
			res.first->second |= selbit;
		}
	}

	if (m_predefinedFile != nullptr)
	{
		try
		{
			m_predefinedFile->setPointer(0);
			io::FIStream<char, false> fis = (File*)m_predefinedFile;
			for (;;)
			{
				Text line = fis.readLine();

				pcstr equal = line.find_r('=');
				if (equal == nullptr) continue;

				Text name = line.cut(equal).trim();
				Text value = line.subarr(equal + 1).trim();

				uint32_t selbit = 1U << getSelIndex(&name);

				uintptr_t offset;
				if (value.startsWith("0x"))
				{
					offset = value.subarr(2).to_uintp(16);
				}
				else
				{
					offset = value.to_uintp();
				}

				auto iter = targets.find(name);
				if (iter == targets.end()) continue;
				if ((iter->second & selbit) == 0) continue;
				iter->second &= ~selbit;
				if (iter->second == 0)
				{
					targets.erase(iter);
				}
				cb(name, (byte*)instance + offset);
			}
		}
		catch (EofException&)
		{
		}
		if (targets.empty()) return;
	}

	// load from pdb
	try
	{
		if (m_pdb.base() == nullptr)
		{
			m_pdb.load();
		}

		cout << "[BDSX] PdbReader: Search Symbols..." << endl;
		if (m_predefinedFile != nullptr) m_predefinedFile->movePointerToEnd(0);

		struct Local
		{
			Map<Text, uint32_t, true>& targets;
			io::FOStream<char, false, false> fos;
			HINSTANCE instance;
			void(*cb)(Text name, void* fnptr);
		} local = { targets, (File*)m_predefinedFile, instance, cb };

		m_pdb.search(nullptr, [&local](Text name, void* address, uint32_t typeId) {
			auto iter = local.targets.find(name);
			if (iter == local.targets.end())
			{
				return true;
			}
			uint32_t selects = iter->second;
			iter->second >>= 1;
			if ((selects & 1) == 0)
			{
				return true;
			}
			if (iter->second == 0)
			{
				local.targets.erase(iter);
			}

			TText line = TText::concat(name, " = 0x", hexf((byte*)address - (byte*)local.instance));
			if (local.fos.base() != nullptr) local.fos << "\r\n" << line;
			cerr << line << endl;
			local.cb(name, address);
			return !local.targets.empty();
			});
		if (local.fos.base() != nullptr) local.fos.flush();
	}
	catch (FunctionError& err)
	{
		cerr << err.getFunctionName() << ": failed, ";
		{
			TSZ tsz;
			err.getMessageTo(&tsz);
			cerr << tsz;
		}
		cerr << "(0x" << hexf(err.getErrorCode(), 8) << ')' << endl;
	}

	if (!targets.empty())
	{
		for (auto& item : targets)
		{
			Text name = item.first;
			cerr << name << " not found" << endl;
		}
	}
}

JsValue CachedPdb::getProcAddresses(kr::JsValue out, JsValue array, bool quiet) throws(kr::JsException)
{
	if (!m_opened) throw JsException(u"PDB is closed");
	Map<Text, uint32_t> targets;
	HINSTANCE instance = GetModuleHandleW(nullptr);

	int length = array.getArrayLength();
	for (int i = 0; i < length; i++)
	{
		TText txbuf = array.get(i).cast<TText>();
		Text tx = txbuf;
		const char* namepos = tx.find_r('#');
		uint32_t selbit = 1U << getSelIndex(&tx);

		auto res = targets.insert(tx, selbit);
		if (!res.second)
		{
			res.first->second |= selbit;
		}
	}

	if (m_predefinedFile != nullptr)
	{
		try
		{
			m_predefinedFile->setPointer(0);
			io::FIStream<char, false> fis = (File*)m_predefinedFile;
			for (;;)
			{
				Text line = fis.readLine();

				pcstr equal = line.find_r('=');
				if (equal == nullptr) continue;

				Text name = line.cut(equal).trim();
				Text value = line.subarr(equal + 1).trim();

				uint32_t selbit = 1U << getSelIndex(&name);

				uintptr_t offset;
				if (value.startsWith("0x"))
				{
					offset = value.subarr(2).to_uintp(16);
				}
				else
				{
					offset = value.to_uintp();
				}

				auto iter = targets.find(name);
				if (iter == targets.end()) continue;
				if ((iter->second & selbit) == 0) continue;
				iter->second &= ~selbit;
				if (iter->second == 0)
				{
					targets.erase(iter);
				}
				NativePointer* ptr = NativePointer::newInstance();
				ptr->setAddressRaw((byte*)instance + offset);
				out.set(name, ptr);
			}
		}
		catch (EofException&)
		{
		}
		if (targets.empty()) return out;
	}

	// load from pdb
	try
	{
		if (m_pdb.base() == nullptr)
		{
			m_pdb.load();
		}

		if (!quiet) g_ctx->log(u"[BDSX] PdbReader: Search Symbols...");
		if (m_predefinedFile != nullptr) m_predefinedFile->movePointerToEnd(0);
		
		struct Local
		{
			Map<Text, uint32_t>& targets;
			bool quiet;
			io::FOStream<char, false, false> fos;
			JsValue& out;
			HINSTANCE instance;
		} local = {targets, quiet, (File*)m_predefinedFile, out, instance };

		m_pdb.search(nullptr, [&local](Text name, void* address, uint32_t typeId) {
			auto iter = local.targets.find(name);
			if (iter == local.targets.end())
			{
				return true;
			}
			uint32_t selects = iter->second;
			iter->second >>= 1;
			if ((selects & 1) == 0)
			{
				return true;
			}
			if (iter->second == 0)
			{
				local.targets.erase(iter);
			}

			TText16 name16 = (Utf8ToUtf16)name;
			if (!local.quiet)
			{
				TText line;
				line << name;
				line << " = 0x";
				line << hexf((byte*)address - (byte*)local.instance);
				if (local.fos.base() != nullptr) local.fos << "\r\n" << line;
				g_ctx->log(TText16() << (Utf8ToUtf16)line);
			}

			NativePointer* ptr = NativePointer::newInstance();
			ptr->setAddressRaw(address);
			local.out.set(JsValue(name16), ptr);
			return !local.targets.empty();
			});
		if (local.fos.base() != nullptr) local.fos.flush();
	}
	catch (FunctionError& err)
	{
		throwAsJsException(err);
	}

	if (!targets.empty())
	{
		for (auto& item : targets)
		{
			Text name = item.first;
			if (!quiet) g_ctx->log(TSZ16() << Utf8ToUtf16(name) << u" not found");
		}
	}

	return out;
}

void CachedPdb::search(JsValue masks, JsValue cb, bool quiet) throws(kr::JsException)
{
	if (!m_opened) throw JsException(u"PDB is closed");

	try
	{
		if (m_pdb.base() == nullptr)
		{
			m_pdb.load();
		}

		TText filter;
		const char* filterstr = nullptr;
		switch (masks.getType())
		{
		case JsType::String:
			filter = masks.cast<TText>();
			filter << '\0';
			filterstr = filter.data();
			break;
		case JsType::Null:
			break;
		case JsType::Function:
			cb = move(masks);
			break;
		case JsType::Object: {
			// array
			Map<Text, int> finder;
			int length = masks.getArrayLength();
			finder.reserve((size_t)length * 2);

			for (int i = 0; i < length; i++)
			{
				TText text = masks.get(i).cast<TText>();
				finder.insert(text, i);
			}

			m_pdb.search(nullptr, [&](Text name, void* address, uint32_t typeId) {
				auto iter = finder.find(name);
				if (name.endsWith("_fptr"))
				{
					debug();
				}
				if (finder.end() == iter) return true;

				NativePointer* ptr = NativePointer::newInstance();
				ptr->setAddressRaw(address);

				int index = iter->second;
				return cb(masks.get(index), ptr, index).cast<bool>();
				});
			return;
		}
		}
		m_pdb.search(filterstr, [&](Text name, void* address, uint32_t typeId) {
			NativePointer* ptr = NativePointer::newInstance();
			ptr->setAddressRaw(address);
			return cb(TText16() << (Utf8ToUtf16)name, ptr).cast<bool>();
			});
	}
	catch (FunctionError& err)
	{
		throwAsJsException(err);
	}
}
JsValue CachedPdb::getAll(bool quiet, int total) throws(kr::JsException)
{
	try
	{
		if (!quiet) g_ctx->log(u"[BDSX] PdbReader: Search Symbols...");
		PdbReader reader;
		reader.load();

		struct Local
		{
			JsValue out = JsNewObject;
			timepoint now;
			size_t totalcount;
			bool quiet;
			int total;

			void report() noexcept
			{
				TSZ16 tsz;
				tsz << u"[BDSX] PdbReader: Get symbols (" << totalcount;
				if (total != 0) tsz << u'/' << total;
				tsz << u')';
				g_ctx->log(tsz);
			}
		} local;
		if (!quiet)
		{
			local.totalcount = 0;
			local.now = timepoint::now();
			local.quiet = quiet;
			local.total = total;
			local.report();
		}
		reader.getAll([&local](Text name, autoptr address) {
			if (!local.quiet)
			{
				++local.totalcount;
				timepoint newnow = timepoint::now();
				if (newnow - local.now > 1000_ms)
				{
					local.now = newnow;
					local.report();
				}
			}

			NativePointer* ptr = NativePointer::newInstance();
			ptr->setAddressRaw(address);
			local.out.set(name, ptr);
			return true;
			});

		if (!quiet)
		{
			local.report();
			g_ctx->log(TSZ16() << u"[BDSX] PdbReader: done (" << local.totalcount << u")");
		}
		return local.out;
	}
	catch (FunctionError& err)
	{
		throwAsJsException(err);
	}
}
JsValue getPdbNamespace() noexcept
{
	JsValue pdb = JsNewObject;
	pdb.setMethod(u"open", [] { return g_pdb.open(); });
	pdb.setMethod(u"close", [] { return g_pdb.close(); });
	pdb.setMethod(u"setOptions", [](int options) { return g_pdb.setOptions(options); });
	pdb.setMethod(u"getOptions", []() { return g_pdb.getOptions(); });
	pdb.setMethod(u"search", [](JsValue masks, JsValue cb, bool quiet) { return g_pdb.search(masks, cb, quiet); });
	pdb.setMethod(u"getProcAddresses", [](JsValue out, JsValue array, bool quiet) { return g_pdb.getProcAddresses(out, array, quiet); });
	pdb.setMethod(u"getAll", [](bool quiet, int total) { return g_pdb.getAll(quiet, total); });
	return pdb;
}
