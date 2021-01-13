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

	struct SymbolMaskInfo
	{
		uint32_t counter;
		uint32_t mask;
	};
	template <bool selfBuffered>
	struct SymbolMap
	{
		Map<Text, SymbolMaskInfo, selfBuffered> targets;
		io::FOStream<char, false, false> fos;
		byte* base;

		SymbolMap() noexcept
			:fos(nullptr)
		{
		}

		void put(Text tx) noexcept
		{
			const char* namepos = tx.find_r('#');
			uint32_t selbit = 1U << getSelIndex(&tx);

			auto res = targets.insert(tx, { 0, selbit });
			if (!res.second)
			{
				res.first->second.mask |= selbit;
			}
		}

		bool del(Text name) noexcept
		{
			uint32_t selbit = 1U << getSelIndex(&name);
			auto iter = targets.find(name);
			if (iter == targets.end()) return false;

			if ((iter->second.mask & selbit) == 0) return false;
			iter->second.mask &= ~selbit;
			if (iter->second.mask == 0)
			{
				targets.erase(iter);
			}
			return true;
		}

		bool test(Text name, void* address, TText* line, Text* nameWithIndex) noexcept
		{
			auto iter = targets.find(name);
			if (iter == targets.end()) return false;
			SymbolMaskInfo& selects = iter->second;
			uint32_t bit = (1 << selects.counter);
			uint32_t index = selects.counter++;

			if ((selects.mask & bit) == 0) return false;
			selects.mask &= ~bit;
			if (selects.mask == 0)
			{
				targets.erase(iter);
			}

			*line << name;
			if (index != 0)
			{
				*line << '#' << index;
			}
			*nameWithIndex = *line;

			*line << " = 0x" << hexf((byte*)address - base);
			if (fos.base() != nullptr) fos << "\r\n" << *line;
			return true;
		}

		bool empty() noexcept
		{
			return targets.empty();
		}

		uintptr_t readOffset(io::FIStream<char, false>& fis, Text* name) throws(EofException)
		{
			for (;;)
			{
				Text line = fis.readLine();

				pcstr equal = line.find_r('=');
				if (equal == nullptr) continue;

				*name = line.cut(equal).trim();
				if (!del(*name)) continue;

				Text value = line.subarr(equal + 1).trim();

				if (value.startsWith("0x"))
				{
					return value.subarr(2).to_uintp(16);
				}
				else
				{
					return value.to_uintp();
				}
			}
		}
	};

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

	SymbolMap<true> targets;
	targets.base = (byte*)GetModuleHandleW(nullptr);

	for (Text tx : text)
	{
		targets.put(tx);
	}

	if (m_predefinedFile != nullptr)
	{
		try
		{
			m_predefinedFile->setPointer(0);
			io::FIStream<char, false> fis = (File*)m_predefinedFile;

			for (;;)
			{
				Text name;
				uintptr_t offset = targets.readOffset(fis, &name);
				cb(name, targets.base + offset);
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
		cout << "[BDSX] PdbReader: Search Symbols..." << endl;
		if (m_pdb.base() == nullptr)
		{
			m_pdb.load();
		}

		if (m_predefinedFile != nullptr) m_predefinedFile->movePointerToEnd(0);

		struct Local
		{
			SymbolMap<true>& targets;
			void(*cb)(Text name, void* fnptr);
		} local = { targets, cb };
		targets.fos.resetStream(m_predefinedFile);

		m_pdb.search(nullptr, [&local](Text name, void* address, uint32_t typeId) {
			TText line;
			Text nameWithIndex;
			if (!local.targets.test(name, address, &line, &nameWithIndex)) return true;

			cout << line << endl;
			local.cb(nameWithIndex, address);
			return !local.targets.empty();
			});
		if (targets.fos.base() != nullptr) targets.fos.flush();
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
		for (auto& item : targets.targets)
		{
			Text name = item.first;
			cerr << name << " not found" << endl;
		}
	}
}

JsValue CachedPdb::getProcAddresses(JsValue out, JsValue array, bool quiet) throws(kr::JsException)
{
	if (!m_opened) throw JsException(u"PDB is closed");

	SymbolMap<false> targets;
	targets.base = (byte*)GetModuleHandleW(nullptr);

	int length = array.getArrayLength();
	for (int i = 0; i < length; i++)
	{
		targets.put(array.get(i).cast<TText>());
	}

	if (m_predefinedFile != nullptr)
	{
		try
		{
			m_predefinedFile->setPointer(0);
			io::FIStream<char, false> fis = (File*)m_predefinedFile;
			for (;;)
			{
				Text name;
				uintptr_t offset = targets.readOffset(fis, &name);
				NativePointer* ptr = NativePointer::newInstance();
				ptr->setAddressRaw(targets.base + offset);
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
		if (!quiet) g_ctx->log(u"[BDSX] PdbReader: Search Symbols...");
		if (m_pdb.base() == nullptr)
		{
			m_pdb.load();
		}

		if (m_predefinedFile != nullptr) m_predefinedFile->movePointerToEnd(0);
		
		struct Local
		{
			SymbolMap<false>& targets;
			bool quiet;
			JsValue& out;
		} local = {targets, quiet, out };

		targets.fos.resetStream(m_predefinedFile);

		m_pdb.search(nullptr, [&local](Text name, void* address, uint32_t typeId) {
			TText line;
			Text nameWithIndex;
			if (!local.targets.test(name, address, &line, &nameWithIndex))
			{
				return true;
			}
			if (!local.quiet)
			{
				line << hexf((byte*)address - local.targets.base);
				g_ctx->log(TText16() << (Utf8ToUtf16)line);
			}
			NativePointer* ptr = NativePointer::newInstance();
			ptr->setAddressRaw(address);
			local.out.set(JsValue(nameWithIndex), ptr);
			return !local.targets.empty();
			});
		if (targets.fos.base() != nullptr) targets.fos.flush();
	}
	catch (FunctionError& err)
	{
		throwAsJsException(err);
	}

	if (!targets.empty())
	{
		for (auto& item : targets.targets)
		{
			Text name = item.first;
			if (!quiet) g_ctx->log(TSZ16() << Utf8ToUtf16(name) << u" not found");
		}
	}

	return out;
}

void CachedPdb::search(JsValue masks, JsValue cb) throws(kr::JsException)
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
JsValue CachedPdb::getAll(JsValue onprogress) throws(kr::JsException)
{
	try
	{
		bool callback = onprogress.getType() == JsType::Function;
		if (!callback) g_ctx->log(u"[BDSX] PdbReader: Search Symbols...");
		PdbReader reader;
		reader.load();

		struct Local
		{
			JsValue out = JsNewObject;
			timepoint now;
			JsValue onprogress;
			uintptr_t totalcount;
			bool callback;

			void report() noexcept
			{
				if (callback)
				{
					onprogress.call(totalcount);
				}
				else
				{
					TSZ16 tsz;
					tsz << u"[BDSX] PdbReader: Get symbols (" << totalcount << u')';
					g_ctx->log(tsz);
				}
			}
		} local;
		local.now = timepoint::now();
		local.callback = callback;
		local.totalcount = 0;
		if (callback)
		{
			local.onprogress = onprogress;
			local.report();
		}
		reader.getAll([&local](Text name, autoptr address) {
			++local.totalcount;
			timepoint newnow = timepoint::now();
			if (newnow - local.now > 500_ms)
			{
				local.now = newnow;
				local.report();
			}

			NativePointer* ptr = NativePointer::newInstance();
			ptr->setAddressRaw(address);
			local.out.set(name, ptr);
			return true;
			});

		local.report();
		if (!callback)
		{
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
	pdb.setMethod(u"search", [](JsValue masks, JsValue cb) { return g_pdb.search(masks, cb); });
	pdb.setMethod(u"getProcAddresses", [](JsValue out, JsValue array, bool quiet) { return g_pdb.getProcAddresses(out, array, quiet); });
	pdb.setMethod(u"getAll", [](JsValue onprogress) { return g_pdb.getAll(onprogress); });
	return pdb;
}
