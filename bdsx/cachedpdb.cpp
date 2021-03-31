#include "stdafx.h"
#include "cachedpdb.h"
#include "nativepointer.h"
#include "jsctx.h"
#include "nodegate.h"

#include <KR3/util/pdb.h>
#include <KR3/data/set.h>
#include <KRWin/handle.h>
#include <KR3/data/crypt.h>

#include <KR3/win/dynamic_dbghelp.h>

using namespace kr;

CachedPdb g_pdb;
BText<32> g_md5;

kr::BText16<kr::Path::MAX_LEN> CachedPdb::predefinedForCore;

namespace
{
	uint32_t getSelIndex(Text16 *text) noexcept
	{
		const char16* namepos = text->find_r(u'#');
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

	CriticalSection s_lock;

	enum class CacheState
	{
		Found,
		New,
		NotMatched
	};
	struct SymbolMaskInfo
	{
		uint32_t counter;
		uint32_t mask;
	};
	struct SymbolMap
	{
		Map<Text16, SymbolMaskInfo, true> targets;
		Keep<io::FOStream<char, false, false>> fos;
		Keep<File> file;
		byte* base;
		bool endWithLine = true;

		SymbolMap(pcstr16 filepath) noexcept
			:fos(nullptr)
		{
			base = (byte*)GetModuleHandleW(nullptr);
			if (filepath != nullptr)
			{
				try
				{
					file = File::openRW(filepath);
				}
				catch (Error&)
				{
				}
			}
		}

		void put(Text16 tx) noexcept
		{
			const char16* namepos = tx.find_r(u'#');
			uint32_t selbit = 1U << getSelIndex(&tx);

			auto res = targets.insert(tx, { 0, selbit });
			if (!res.second)
			{
				res.first->second.mask |= selbit;
			}
		}

		bool del(Text16 name) noexcept
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

		bool test(Text16 name, void* address, TText16* line, Text16* nameWithIndex) noexcept
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
				*line << u'#' << index;
			}
			*nameWithIndex = *line;

			*line << u" = 0x" << hexf((byte*)address - base);
			if (fos != nullptr) *fos << (Utf16ToUtf8)*line << "\r\n";
			return true;
		}

		bool empty() noexcept
		{
			return targets.empty();
		}

		void startWriting() noexcept 
		{
			if (file != nullptr && fos == nullptr) fos = _new io::FOStream<char, false, false>(file);
		}

		uintptr_t readOffset(io::FIStream<char, false>* fis, TText16* name) throws(EofException)
		{
			for (;;)
			{
				auto line = fis->readLine();
				if (!line.second) endWithLine = false;
				pcstr equal = line.first.find_r('=');
				if (equal == nullptr) continue;

				*name = (Utf8ToUtf16)line.first.cut(equal).trim();
				if (!del(*name)) {
					name->truncate();
					continue;
				}

				Text value = line.first.subarr(equal + 1).trim();

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

		CacheState checkMd5(io::FIStream<char, false>* fis, bool overwrite) noexcept
		{
			try
			{
				auto md5 = fis->readLine();
				if (md5.first.empty()) {
					file->toBegin();
					file->truncate();
					throw EofException();
				}
				if (md5.first != g_md5)
				{
					if (!overwrite) {
						cerr << "[BDSX] MD5 Hash does not Matched" << endl;
						cerr << "[BDSX] pdb.ini MD5 = " << md5.first << endl;
						cerr << "[BDSX] current MD5 = " << g_md5 << endl;
						cerr << "[BDSX] Please use 'npm i' to update it" << endl;
						return CacheState::NotMatched;
					}
					file->toBegin();
					file->truncate();
					throw EofException();
				}
				if (!md5.second)
				{
					endWithLine = false;
				}
			}
			catch (EofException&)
			{
				startWriting();
				*fos << g_md5 << "\r\n";
				return CacheState::New;
			}
			return CacheState::Found;
		}
	};

	struct __StreamWithPad
	{
		void* padForAntiCorruptingByVisualStudioBug; // https://developercommunity2.visualstudio.com/t/An-invalid-handle-was-specified-a-weird/1322604
		io::FIStream<char, false>* const fis;

		__StreamWithPad(io::FIStream<char, false>* fis) noexcept
			:fis(fis)
		{
		}
		~__StreamWithPad() noexcept
		{
			delete fis;
		}
		operator io::FIStream<char, false>*() noexcept
		{
			return fis;
		}
		io::FIStream<char, false>* operator ->() noexcept
		{
			return fis;
		}
	};

}

CachedPdb::CachedPdb() noexcept
{
}
CachedPdb::~CachedPdb() noexcept
{
}

void CachedPdb::close() noexcept
{
	if (s_lock.tryEnter())
	{
		m_pdb.close();
		s_lock.leave();
	}
}
int CachedPdb::setOptions(int options) throws(JsException)
{
	if (!s_lock.tryEnter()) throw JsException(u"BUSY. It's using by async task");
	int out = PdbReader::setOptions(options);
	s_lock.leave();
	return out;
}
int CachedPdb::getOptions() throws(JsException)
{
	if (!s_lock.tryEnter()) throw JsException(u"BUSY. It's using by async task");
	int out = PdbReader::getOptions();
	s_lock.leave();
	return out;
}

TText16 CachedPdb::undecorate(Text16 text, int flags) noexcept {

	TText16 undecorated;
	undecorated << PdbReader::undecorate(text.data(), flags);
	return move(undecorated);
}

bool CachedPdb::getProcAddresses(pcstr16 predefined, View<Text16> text, Callback cb, void* param, bool quiet, bool overwrite) noexcept
{
	if (text.empty()) return true;

	CsLock __lock = s_lock;
	SymbolMap targets(predefined);

	for (Text16 tx : text)
	{
		targets.put(tx);
	}

	if (predefined != nullptr)
	{
		if (targets.file == nullptr)
		{
			if (!quiet) cout << "[BDSX] Failed to open " << (Utf16ToAnsi)(Text16)predefined << endl;
			return false;
		}

		__StreamWithPad fis = _new io::FIStream<char, false>((File*)targets.file);
		try
		{
			CacheState state = targets.checkMd5(fis, overwrite);
			if (state == CacheState::New)
			{
				if (!quiet) cout << "[BDSX] Generating " << (Utf16ToAnsi)(Text16)predefined << endl;
			}
			else
			{
				for (;;)
				{
					TText16 name;
					uintptr_t offset = targets.readOffset(fis, &name);
					cb(name, targets.base + offset, param);
					if (targets.empty()) return true;
				}
			}
		}
		catch (EofException&)
		{
			if (!targets.endWithLine)
			{
				targets.startWriting();
				targets.fos->write("\r\n");
			}
		}
	}

	// load from pdb
	try
	{
		if (!quiet) cout << "[BDSX] PdbReader: Search Symbols..." << endl;
		if (m_pdb.base() == nullptr)
		{
			m_pdb.load();
		}

		struct Local
		{
			SymbolMap& targets;
			void(*cb)(Text16 name, void* fnptr, void* param);
			void* param;
			bool quiet;
		} local = { targets, cb, param, quiet };

		targets.startWriting();

		m_pdb.search16(nullptr, [&local](Text16 name, void* address, uint32_t typeId) {
			TText16 line;
			Text16 nameWithIndex;
			if (!local.targets.test(name, address, &line, &nameWithIndex)) return true;

			if (!local.quiet) cout << (Utf16ToAnsi)line << endl;
			local.cb(nameWithIndex, address, local.param);
			return !local.targets.empty();
			});
		if (targets.fos != nullptr) targets.fos->flush();
	}
	catch (FunctionError& err)
	{
		if (!quiet)
		{
			cerr << err.getFunctionName() << ": failed, ";
			{
				TSZ tsz;
				err.getMessageTo(&tsz);
				cerr << tsz;
			}
			cerr << "(0x" << hexf(err.getErrorCode(), 8) << ')' << endl;
		}
	}

	if (!targets.empty())
	{
		if (!quiet)
		{
			for (auto& item : targets.targets)
			{
				Text name = item.first;
				cerr << name << " not found" << endl;
			}
		}
		return false;
	}

	return true;
}
JsValue CachedPdb::getProcAddresses(pcstr16 predefined, JsValue out, JsValue array, bool quiet, uint32_t undecorateOpts) throws(kr::JsException)
{
	int length = array.getArrayLength();
	if (length == 0) return out;

	if (!s_lock.tryEnter()) throw JsException(u"BUSY. It's using by async task");
	finally { s_lock.leave(); };

	SymbolMap targets(predefined);

	for (int i = 0; i < length; i++)
	{
		targets.put(array.get(i).cast<Text16>());
	}

	if (predefined != nullptr)
	{
		if (targets.file == nullptr)
		{
			throw JsException(TSZ16() << u"Failed to open " << predefined);
		}
		try
		{
			Must<io::FIStream<char, false>> fis = _new io::FIStream<char, false>((File*)targets.file);

			if (targets.checkMd5(fis, true) == CacheState::New)
			{
				if (!quiet) g_ctx->log(TSZ16() << u"[BDSX] Generating " << predefined);
			}
			else
			{
				for (;;)
				{
					TText16 name;
					uintptr_t offset = targets.readOffset(fis, &name);
					NativePointer* ptr = NativePointer::newInstance();
					ptr->setAddressRaw(targets.base + offset);
					out.set(name, ptr);
					if (targets.empty()) return out;
				}
			}
		}
		catch (EofException&)
		{
			if (!targets.endWithLine)
			{
				targets.startWriting();
				targets.fos->write("\r\n");
			}
		}
	}

	// load from pdb
	try
	{
		if (!quiet) g_ctx->log(u"[BDSX] PdbReader: Search Symbols...");
		if (m_pdb.base() == nullptr)
		{
			m_pdb.load();
		}
				
		struct Local
		{
			SymbolMap& targets;
			bool quiet;
			JsValue& out;
			uint32_t undecorateOpts;
		} local = {targets, quiet, out, undecorateOpts };

		targets.startWriting();

		m_pdb.search16(nullptr, [&local](Text16 name, void* address, uint32_t typeId) {
			TText16 undecorated;
			if (local.undecorateOpts != -1)
			{
				undecorated << PdbReader::undecorate(name.data(), local.undecorateOpts);
				name = undecorated;
			}
			Text16 nameWithIndex;
			TText16 line;
			if (!local.targets.test(name, address, &line, &nameWithIndex))
			{
				return true;
			}
			if (!local.quiet)
			{
				g_ctx->log(line);
			}
			NativePointer* ptr = NativePointer::newInstance();
			ptr->setAddressRaw(address);
			local.out.set(JsValue(nameWithIndex), ptr);
			return !local.targets.empty();
			});
		if (targets.fos != nullptr) targets.fos->flush();
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

		if (m_pdb.base() == nullptr)
		{
			m_pdb.load();
		}

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
		m_pdb.getAll([&local](Text name, autoptr address, int typeId) {
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
	pdb.set(u"coreCachePath", (Text16)CachedPdb::predefinedForCore);
	pdb.setMethod(u"open", [] {});
	pdb.setMethod(u"close", [] { return g_pdb.close(); });
	pdb.setMethod(u"setOptions", [](int options) { return g_pdb.setOptions(options); });
	pdb.setMethod(u"getOptions", []() { return g_pdb.getOptions(); });
	pdb.setMethod(u"undecorate", [](Text16 text, int flags)->TText16 { return g_pdb.undecorate(text, flags); });
	pdb.setMethod(u"search", [](JsValue masks, JsValue cb) { return g_pdb.search(masks, cb); });
	pdb.setMethod(u"getProcAddresses", [](JsValue out, JsValue array, bool quiet, bool undecorated) { return g_pdb.getProcAddresses(CachedPdb::predefinedForCore.data(), out, array, quiet, undecorated); });

	JsValue getList = JsFunction::makeT([](Text16 predefined, JsValue out, JsValue array, bool quiet, JsValue undecorateOpts) {
		return g_pdb.getProcAddresses(predefined.data(), out, array, quiet, undecorateOpts == undefined ? -1 : undecorateOpts.as<int>());
		});
	pdb.set(u"getList", getList);
	pdb.setMethod(u"getAll", [](JsValue onprogress) { return g_pdb.getAll(onprogress); });
	return pdb;
}

// async pdb, not completed
//getList.setMethod(u"async", [](Text16 predefined, JsValue out, JsValue array, bool quiet) {
//
//	Array<Text> list;
//
//	AText buffer;
//	buffer.reserve(1024);
//
//	int32_t len = array.getArrayLength();
//	for (int i = 0; i < len; i++)
//	{
//		size_t front = buffer.size();
//		buffer << (Utf16ToUtf8)array.get(i).cast<Text16>();
//		size_t back = buffer.size();
//		buffer << '\0';
//
//		list.push(Text((pcstr)front, (pcstr)back));
//	}
//
//	ThreadHandle::createLambda([predefined = AText16::concat(predefined, nullterm), quiet, buffer = move(buffer), list = move(list)]() mutable{
//		intptr_t off = (intptr_t)buffer.data();
//		for (Text& v : list)
//		{
//			v.addBegin(off);
//			v.addEnd(off);
//		}
//		struct Local
//		{
//			AText out;
//		} local;
//		local.out.reserve(1024);
//		g_pdb.getProcAddressesT<Local>(predefined.data(), list, [](Text name, void* fnptr, Local* param) {
//
//			}, & local, quiet);
//		AsyncTask::post([predefined = move(predefined), quiet, buffer = move(buffer), list = move(list)]() mutable{
//
//		});
//	});
//	});