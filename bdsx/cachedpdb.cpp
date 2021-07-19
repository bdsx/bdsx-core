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

	struct SymbolCache
	{
		byte* base;
		Keep<io::FOStream<char, false, false>> fos;
		Keep<File> file;
		bool endWithLine = true;

		SymbolCache(pcstr16 filepath) noexcept
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

		void startWriting() noexcept
		{
			if (file != nullptr && fos == nullptr) fos = _new io::FOStream<char, false, false>(file);
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
	struct SymbolMap:SymbolCache
	{
		Map<Text16, SymbolMaskInfo, true> targets;

		SymbolMap(pcstr16 filepath) noexcept
			:SymbolCache(filepath)
		{
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

	struct DeferField
	{
		bool inited = false;
		JsPropertyId typeIndex;
		JsPropertyId index;
		JsPropertyId size;
		JsPropertyId flags;
		JsPropertyId value;
		JsPropertyId address;
		JsPropertyId _register;
		JsPropertyId scope;
		JsPropertyId tag;
		JsPropertyId name;
	};
	Deferred<DeferField> s_field(JsRuntime::initpack);
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

autoptr CachedPdb::getProcAddress(pcstr16 predefined, pcstr name) noexcept
{
	Text txname = (Text)name;
	CsLock __lock = s_lock;

	SymbolCache targets(predefined);
	void* foundptr = nullptr;

	if (predefined != nullptr)
	{
		try
		{
			Must<io::FIStream<char, false>> fis = _new io::FIStream<char, false>((File*)targets.file);
			CacheState state = targets.checkMd5(fis, false);
			if (state == CacheState::New)
			{
				cout << "[BDSX] Generating " << (Utf16ToAnsi)(Text16)predefined << endl;
			}
			else if (state == CacheState::NotMatched)
			{
				return nullptr;
			}
			else
			{
				TText16 name;
				bool endWithLine = true;
				for (;;)
				{
					auto line = fis->readLine();
					if (!line.second) endWithLine = false;
					pcstr equal = line.first.find_r('=');
					if (equal == nullptr) continue;
					if (line.first.cut(equal).trim() != txname) continue;
					Text value = line.first.subarr(equal + 1).trim();

					if (value.startsWith("0x"))
					{
						return targets.base + value.subarr(2).to_uintp(16);
					}
					else
					{
						return targets.base + value.to_uintp();
					}
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
		cout << "[BDSX] PdbReader: Search Symbols..." << endl;
		if (m_pdb.base() == nullptr)
		{
			m_pdb.load();
		}

		TText line;
		line << txname;
		foundptr = m_pdb.getFunctionAddress(line.c_str());
		line << " = 0x" << hexf((byte*)foundptr - (byte*)m_pdb.base());
		cout << line << endl;

		targets.startWriting();
		*targets.fos << line << "\r\n";
		targets.fos->flush();
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

	if (foundptr == nullptr)
	{
		cerr << txname << " not found" << endl;
	}

	return foundptr;
}
JsValue CachedPdb::getProcAddresses(pcstr16 predefined, JsValue out, JsValue array, bool quiet, uint32_t undecorateOpts) throws(kr::JsException)
{
	int length = array.getArrayLength();
	if (length == 0) return out;

	if (!s_lock.tryEnter()) throw JsException(u"BUSY. It's using by the async task");
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

			CacheState state = targets.checkMd5(fis, true);
			if (state == CacheState::New)
			{
				if (!quiet) g_ctx->log(TSZ16() << u"[BDSX] Generating " << predefined);
			}
			else if (state == CacheState::NotMatched)
			{
				// does nothing
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
		if (!quiet)
		{
			size_t size = targets.targets.size();
			if (size > 5)
			{
				g_ctx->log(TSZ16() << u"[BDSX] PdbReader: Cache not found, " << size << u" symbols");
			}
			else
			{
				for (auto& pair : targets.targets)
				{
					g_ctx->log(TSZ16() << u"[BDSX] PdbReader: Cache not found, " << (Text16)pair.first);
				}
			}
			g_ctx->log(u"[BDSX] PdbReader: Search Symbols...");
		}
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
			JsException exception;
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
			try
			{
				local.out.set(JsValue(nameWithIndex), ptr);
				return !local.targets.empty();
			}
			catch (JsException& ex)
			{
				local.exception = move(ex);
				return false;
			}
			});
		if (!local.exception.isEmpty())
		{
			throw move(local.exception);
		}
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
		JsException exception;
		m_pdb.search(filterstr, [&](Text name, void* address, uint32_t typeId) {
			NativePointer* ptr = NativePointer::newInstance();
			ptr->setAddressRaw(address);
			try
			{
				return cb(TText16() << (Utf8ToUtf16)name, ptr).cast<bool>();
			}
			catch (JsException& ex)
			{
				exception = move(ex);
				return false;
			}
			});
		if (!exception.isEmpty())
		{
			throw move(exception);
		}
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
			JsException exception;

			bool report() noexcept
			{
				if (callback)
				{
					try
					{
						JsValue res = onprogress.call(totalcount);
						return res.getType() != JsType::Boolean || res.as<bool>();
					}
					catch (JsException& ex)
					{
						exception = move(ex);
						return false;
					}
				}
				else
				{
					TSZ16 tsz;
					tsz << u"[BDSX] PdbReader: Get symbols (" << totalcount << u')';
					g_ctx->log(tsz);
				}
				return true;
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
		m_pdb.getAll16([&local](Text16 name, autoptr address, int typeId) {
			++local.totalcount;
			timepoint newnow = timepoint::now();

			NativePointer* ptr = NativePointer::newInstance();
			ptr->setAddressRaw(address);
			local.out.set(name, ptr);
			if (newnow - local.now > 500_ms)
			{
				local.now = newnow;
				return local.report();
			}
			return true;
			});

		local.report();
		if (!callback)
		{
			g_ctx->log(TSZ16() << u"[BDSX] PdbReader: done (" << local.totalcount << u")");
		}
		else if (!local.exception.isEmpty())
		{
			throw move(local.exception);
		}
		return local.out;
	}
	catch (FunctionError& err)
	{
		throwAsJsException(err);
	}
}
void CachedPdb::getAllEx(JsValue cb) throws(kr::JsException)
{
	try
	{
		if (cb.getType() != JsType::Function) throw kr::JsException(u"function required");
		if (m_pdb.base() == nullptr)
		{
			m_pdb.load();
		}

		if (!s_field->inited)
		{
			s_field->inited = true;
			s_field->typeIndex = JsPropertyId(u"typeIndex");
			s_field->index = JsPropertyId(u"index");
			s_field->size = JsPropertyId(u"size");
			s_field->flags = JsPropertyId(u"flags");
			s_field->value = JsPropertyId(u"value");
			s_field->address = JsPropertyId(u"address");
			s_field->_register = JsPropertyId(u"register");
			s_field->scope = JsPropertyId(u"scope");
			s_field->tag = JsPropertyId(u"tag");
			s_field->name = JsPropertyId(u"name");
		}

		struct Local
		{
			JsValue out = JsNewArray();
			timepoint now;
			JsValue cb;
			JsException exception;

			bool callback;
			int counter = 0;

			bool flush() throws(JsException)
			{
				if (counter == 0) return true;
				bool res = cb(out) != false;
				out.setArrayLength(0);
				counter = 0;
				return res;
			}
		} local;
		local.now = timepoint::now();
		local.cb = cb;

		m_pdb.getAllEx16([&local](Text16 name, SYMBOL_INFOW* info) {

			JsValue tuple = JsNewObject;
			tuple.set(s_field->typeIndex, (int)info->TypeIndex);
			tuple.set(s_field->index, (int)info->Index);
			tuple.set(s_field->size, (int)info->Size);
			tuple.set(s_field->flags, (int)info->Flags);

			NativePointer* value = NativePointer::newInstance();
			value->setAddressRaw((void*)info->Value);
			tuple.set(s_field->value, value);

			NativePointer* addr = NativePointer::newInstance();
			addr->setAddressRaw((void*)info->Address);
			tuple.set(s_field->address, addr);

			tuple.set(s_field->_register, (int)info->Register);
			tuple.set(s_field->scope, (int)info->Scope);
			tuple.set(s_field->tag, (int)info->Tag);
			tuple.set(s_field->name, name);
			local.out.set(local.counter++, tuple);

			timepoint newnow = timepoint::now();
			if (newnow - local.now > 100_ms)
			{
				local.now = newnow;
				try
				{
					return local.flush();
				}
				catch (JsException& ex)
				{
					local.exception = move(ex);
					return false;
				}
			}
			return true;
			});

		if (!local.exception.isEmpty())
		{
			throw move(local.exception);
		}
		local.flush();
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
	pdb.setMethod(u"close", [] { return g_pdb.close(); });
	pdb.setMethod(u"setOptions", [](int options) { return g_pdb.setOptions(options); });
	pdb.setMethod(u"getOptions", []() { return g_pdb.getOptions(); });
	pdb.setMethod(u"undecorate", [](Text16 text, int flags)->TText16 { return g_pdb.undecorate(text, flags); });
	pdb.setMethod(u"search", [](JsValue masks, JsValue cb) { return g_pdb.search(masks, cb); });
	pdb.setMethod(u"getProcAddresses", [](JsValue out, JsValue array, bool quiet, bool undecorated) { return g_pdb.getProcAddresses(CachedPdb::predefinedForCore.data(), out, array, quiet, undecorated); });

	JsValue getList = JsFunction::makeT([](Text16 predefined, JsValue out, JsValue array, bool quiet, JsValue undecorateOpts) {
		return g_pdb.getProcAddresses(predefined.data(), out, array, quiet, undecorateOpts.abstractEquals(nullptr) ? -1 : undecorateOpts.as<int>());
		});
	pdb.set(u"getList", getList);
	pdb.setMethod(u"getAll", [](JsValue onprogress) { return g_pdb.getAll(onprogress); });
	pdb.setMethod(u"getAllEx", [](JsValue onprogress) { return g_pdb.getAllEx(onprogress); });
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