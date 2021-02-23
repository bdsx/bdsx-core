#include "stdafx.h"
#include "nodegate.h"
#include "jshook.h"
#include "netfilter.h"
#include "voidpointer.h"
#include "staticpointer.h"
#include "nativepointer.h"
#include "structurepointer.h"
#include "cachedpdb.h"
#include "sehandler.h"
#include "jsctx.h"
#include "uvasync.h"
#include "mtqueue.h"
#include "gen/version.h"

#include <KR3/win/windows.h>
#include <KRWin/hook.h>
#include <KR3/initializer.h>
#include <KR3/util/unaligned.h>
#include <KR3/net/socket.h>
#include <KR3/util/StackWalker.h>
#include <KR3/data/crypt.h>

#include <shellapi.h>
#include <DbgHelp.h>

#define USE_EDGEMODE_JSRT
#include <jsrt.h>

#undef main

using namespace kr;
using namespace hook;

namespace
{
	kr::AText s_args_buffer;
	int s_argc;
	char** s_args;

	struct FunctionTarget
	{
		Text varName;
		void** dest;
		size_t skipCount;
	};

	using __scrt_common_main_seh_t = int(*)();
	using main_t = int(*)(int, char**, char**);
	main_t s_bedrockMain;
	__scrt_common_main_seh_t s_bedrockSehMain;

	TText16 readNativeStackFromExceptionPointer(EXCEPTION_POINTERS* ptr) noexcept
	{
		TText16 nativeStack;
		StackWriter writer(ptr->ContextRecord);
		nativeStack << writer;
		return nativeStack;
	}

	Manual<JsScope> s_globalScope;
}

Initializer<Socket> __init;


int nodeStart(int argc, char** argv) noexcept
{
	__try
	{
		return nodegate::start(argc, argv);
	}
	__except (GetExceptionCode() == EXCEPTION_BREAKPOINT ? EXCEPTION_CONTINUE_SEARCH : runtimeError::raise(GetExceptionInformation()))
	{
		return -1;
	}
}

BOOL WINAPI DllMain(
	_In_ HINSTANCE hinstDLL,
	_In_ DWORD     fdwReason,
	_In_ LPVOID    lpvReserved
)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		// update utf8 args
		{
			Array<size_t> positions;
			wchar_t* commandLine = GetCommandLineW();
			int argc;
			char16_t** argv_16 = kr::unwide(CommandLineToArgvW(commandLine, &argc));
			positions.reserve(argc);
			s_argc = argc;

			while (*argv_16 != nullptr)
			{
				Text16 arg = (Text16)*argv_16++;
				positions.push(s_args_buffer.size());
				s_args_buffer << (kr::Utf16ToUtf8)arg << '\0';
			}

			size_t argv_pos = s_args_buffer.size();
			s_args_buffer.prepare(positions.bytes() + sizeof(char*));
			s_args_buffer.shrink();

			char** argptr = (char**)(s_args_buffer.begin() + argv_pos);
			s_args = argptr;

			char* bufptr = s_args_buffer.begin();
			for (size_t& pos : positions)
			{
				*argptr++ = bufptr + pos;
			}
			*argptr = nullptr;
		}

		// md5 self
		try
		{
			TText16 moduleName = CurrentApplicationPath();
			g_md5 = (encoder::Hex)(TBuffer)encoder::Md5::hash(File::open(moduleName.data()));

			CurrentApplicationPath();
			path16.joinEx(&CachedPdb::predefinedForCore, { (Text16)moduleName, (Text16)u"../../bdsx/bds/pdb.ini"}, false);
			CachedPdb::predefinedForCore << nullterm;
		}
		catch (Error&)
		{
			cerr << "[BDSX] Cannot read bedrock_server.exe" << endl;
			cerr << "[BDSX] Failed to get MD5" << endl;
			terminate(-1);
		}

		// load pdb
		if (!g_pdb.getProcAddresses(CachedPdb::predefinedForCore.data(), View<Text>{ "__scrt_common_main_seh"_tx }, [](Text name, void* fnptr, void*) {
			s_bedrockSehMain = (__scrt_common_main_seh_t)fnptr;
			}, nullptr, false))
		{
			terminate(-1);
		}
		if (s_bedrockSehMain == nullptr)
		{
			cerr << "[BDSX] Failed to find entry of bedrock_server.exe" << endl;
			terminate(-1);
		}
		PdbReader::setOptions(0);

		// hook main

		size_t offset_hook = 0xff;
		size_t offset_call = offset_hook + 0x8;

		int32_t offset_main_call = *(Unaligned<int32_t>*)((byte*)s_bedrockSehMain + offset_call + 1);
		s_bedrockMain = (main_t)((byte*)s_bedrockSehMain + offset_call + 5 + offset_main_call);

		/*
		mov r8,rax
		mov rdx,rdi
		mov ecx,dword ptr ds:[rbx]
		call <bedrock_server.main>
		*/
		static const byte ORIGINAL_CODE[] = {
			0x4C, 0x8B, 0xC0,
			0x48, 0x8B, 0xD7,
			0x8B, 0x0B,
			0xE8, 0xB0, 0x47, 0xFF, 0xFE
		};

		JitFunction junction(64);
		junction.mov(R8, RAX);
		junction.mov(RDX, RDI);
		junction.mov(RCX, QwordPtr, RBX);
		junction.jump64(nodeStart, RAX);
		CodeDiff diff = junction.patchTo((byte*)s_bedrockSehMain + offset_hook, ORIGINAL_CODE, R9, false, { {9, 13} });
		if (!diff.succeeded())
		{
			cerr << "[BDSX] entry point hooking failed, bytes did not matched at {";
			for (const pair<size_t, size_t>& v : diff)
			{
				cerr << " {" << v.first << ", " << v.second << "}, ";
			}
			cerr << '}' << endl;
		}
	}
	return true;
}

namespace
{
	float tester(double a, float b, int64_t c) noexcept
	{
		return (float)a;
	}

	void trycatch(void* param, void(*func)(void*), void(*_catch)(void*, pcstr)) noexcept
	{
		try
		{
			func(param);
		}
		catch (pcstr str)
		{
			_catch(param, str);
		}
		catch (ThrowAbort&)
		{
		}
		catch (std::exception& e)
		{
			_catch(param, e.what());
		}
		catch (ErrorCode& e)
		{
			TSZ tsz;
			tsz << "0x" << hexf(e.getErrorCode(), 8) << ", ";
			e.getMessageTo(&tsz);
			_catch(param, tsz);
		}
		catch (Exception&)
		{
			_catch(param, "kr::Exception");
		}
		catch (JsException& e)
		{
			TSZ tsz;
			tsz << (Utf16ToAnsi)e.toString();
			_catch(param, tsz);
		}
		catch (...)
		{
			_catch(param, "Unknown exception");
		}
	}

	void cxxthrow() throws(...)
	{
		throw ThrowAbort();
	}
	void cxxthrowString(pcstr str) throws(...)
	{
		throw str;
	}
	void throwJsErrorCode(JsErrorCode err) throws(kr::JsException)
	{
		throw kr::JsException(kr::TSZ16() << u"JsErrorCode: 0x" << kr::hexf((int)err));
	}

	namespace stackutil
	{
		uintptr_t last_allocate = 1;
		void* stack_alloc(size_t size) noexcept
		{
			StackAllocator* alloc = StackAllocator::getInstance();
			void* p = alloc->allocate(8 + size);
			*(uintptr_t*)p = last_allocate;
			last_allocate = (uintptr_t)p;
			return (byte*)p + 8;
		}
		void stack_free_all() noexcept
		{
			uintptr_t p = last_allocate;
			if (p & 1) return;

			StackAllocator* alloc = StackAllocator::getInstance();
			for (;;)
			{
				void* orip = (void*)(p & ~1);
				p = *(uintptr_t*)orip;
				alloc->free(orip);
				if (p & 1)
				{
					last_allocate = p;
					break;
				}
			}
		}

		char* to_ansi(const char16_t* str, size_t size) noexcept
		{
			Utf16ToAnsi dec = Text16(str, size);
			size_t outsize = dec.size();
			char* out = (char*)stack_alloc(outsize + 1);
			dec.copyTo(out);
			out[outsize] = '\0';
			return out;
		}

		char* to_utf8(const char16_t* str, size_t size) noexcept
		{
			Utf16ToUtf8 dec = Text16(str, size);
			size_t outsize = dec.size();
			char* out = (char*)stack_alloc(outsize + 1);
			dec.copyTo(out);
			out[outsize] = '\0';

			return out;
		}

		void* pointer_js2class(JsValueRef value) noexcept
		{
			return JsValue((JsRawData)value).getNativeObject<VoidPointer>();
		}

		JsErrorCode from_ansi(pcstr str, JsValueRef* out) noexcept
		{
			TSZ16 buf;
			buf << (AnsiToUtf16)(Text)str;
			return JsPointerToString(wide(buf.data()), buf.size(), out);
		}

		JsErrorCode from_utf8(pcstr str, JsValueRef* out) noexcept
		{
			TSZ16 buf;
			buf << (Utf8ToUtf16)(Text)str;
			return JsPointerToString(wide(buf.data()), buf.size(), out);
		}

	}
}


void nodegate::initNativeModule(void* exports_raw) noexcept
{
	g_ctx.create();
	{
		JsScope _scope;
		JsValue exports = (JsRawData)(JsValueRef)exports_raw;

		exports.set(u"VoidPointer", VoidPointer::classObject);
		exports.set(u"StaticPointer", StaticPointer::classObject);
		exports.set(u"AllocatedPointer", AllocatedPointer::classObject);
		exports.set(u"NativePointer", NativePointer::classObject);
		exports.set(u"StructurePointer", StructurePointer::classObject);
		exports.set(u"RuntimeError", runtimeError::getRuntimeErrorClass());
		exports.set(u"MultiThreadQueue", MultiThreadQueue::classObject);

		{
			JsValue bedrock_server_exe = JsNewObject;
			exports.set(u"bedrock_server_exe", bedrock_server_exe);
			bedrock_server_exe.set(u"md5", (Text)g_md5);
			bedrock_server_exe.set(u"argc", s_argc);
			bedrock_server_exe.set(u"args", VoidPointer::make(s_args));
			bedrock_server_exe.set(u"argsLine", (Text16)unwide(GetCommandLineW()));
			bedrock_server_exe.set(u"main", VoidPointer::make(s_bedrockMain));
			bedrock_server_exe.setMethod(u"forceKill", kr::terminate);
		}

		{
			JsValue cgate = JsNewObject;
			exports.set(u"cgate", cgate);

			cgate.set(u"bdsxCoreVersion", CONCAT(u, BDSX_CORE_VERSION));
			cgate.set(u"GetProcAddressPtr", VoidPointer::make(GetProcAddress));
			cgate.setMethod(u"GetProcAddress", [](VoidPointer* module, Text16 name) {
				return VoidPointer::make(GetProcAddress((HMODULE)module->getAddressRaw(), TSZ() << (Utf16ToUtf8)name));
				});
			cgate.setMethod(u"GetProcAddressByOrdinal", [](VoidPointer* module, int ordinal) {
				return VoidPointer::make(GetProcAddress((HMODULE)module->getAddressRaw(), (LPCSTR)(intptr_t)ordinal));
				});
			cgate.set(u"GetModuleHandleWPtr", VoidPointer::make(GetModuleHandleW));
			cgate.setMethod(u"GetModuleHandleW", [](JsValue name) {
				return VoidPointer::make(GetModuleHandleW(name == nullptr ? nullptr : wide(name.cast<Text16>().data())));
				});
			cgate.setMethod(u"nodeLoopOnce", nodegate::loopOnce);
			cgate.set(u"nodeLoop", VoidPointer::make(nodegate::loop));
			cgate.set(u"tester", VoidPointer::make(tester));

			cgate.setMethod(u"allocExecutableMemory", [](uint64_t size, uint64_t align) {
				hook::ExecutableAllocator* alloc = hook::ExecutableAllocator::getInstance();
				StaticPointer* ptr = StaticPointer::newInstance();
				ptr->setAddressRaw(alloc->alloc(size, max(align, 1)));
				return ptr;
				});
		}

		{
			JsValue chakraUtil = JsNewObject;
			exports.set(u"chakraUtil", chakraUtil);

			chakraUtil.set(u"stack_alloc", VoidPointer::make(stackutil::stack_alloc));
			chakraUtil.set(u"stack_free_all", VoidPointer::make(stackutil::stack_free_all));
			chakraUtil.set(u"stack_ptr", VoidPointer::make(&stackutil::last_allocate));
			chakraUtil.set(u"stack_utf8", VoidPointer::make(stackutil::to_utf8));
			chakraUtil.set(u"stack_ansi", VoidPointer::make(stackutil::to_ansi));
			chakraUtil.set(u"from_utf8", VoidPointer::make(stackutil::from_utf8));
			chakraUtil.set(u"from_ansi", VoidPointer::make(stackutil::from_ansi));
			chakraUtil.set(u"pointer_js2class", VoidPointer::make(stackutil::pointer_js2class));
			chakraUtil.setMethodRaw(u"JsCreateFunction", [](const JsArguments& args)->JsValue {
				VoidPointer* fnptr = args.at<VoidPointer*>(0);
				if (fnptr == nullptr) throw JsException(u"Need *Pointer for the first parameter");
				VoidPointer* paramptr = args.at<VoidPointer*>(1);

				JsValueRef func;
				JsErrorCode err = JsCreateFunction(
					(JsNativeFunction)fnptr->getAddressRaw(),
					paramptr->getAddressRawSafe(),
					&func);

				return (JsValue)(JsRawData)func;
				});
			chakraUtil.setMethodRaw(u"JsAddRef", [](const JsArguments& args)->JsValue {
				unsigned int old;
				JsErrorCode err = JsAddRef(args.at<JsValue>(0).getRaw(), &old);
				if (err != JsNoError) throwJsErrorCode(err);
				return old;
				});
			chakraUtil.setMethodRaw(u"JsRelease", [](const JsArguments& args)->JsValue {
				unsigned int old;
				JsErrorCode err = JsRelease(args.at<JsValue>(0).getRaw(), &old);
				if (err != JsNoError) throwJsErrorCode(err);
				return old;
				});
			chakraUtil.setMethodRaw(u"asJsValueRef", [](const JsArguments& args)->JsValue {
				return VoidPointer::make(args.at<JsValue>(0).getRaw());
				});
		}

		exports.set(u"ipfilter", getNetFilterNamespace());
		exports.set(u"jshook", getJsHookNamespace());
		exports.set(u"runtimeError", runtimeError::getNamespace());
		exports.set(u"pdb", getPdbNamespace());
		exports.set(u"uv_async", getUvAsyncNamespace());

		{
			JsValue obj = JsNewObject;
			obj.set(u"trycatch", VoidPointer::make(trycatch));
			obj.set(u"cxxthrow", VoidPointer::make(cxxthrow));
			obj.set(u"cxxthrowString", VoidPointer::make(cxxthrowString));
			exports.set(u"cxxException", obj);
		}
	}
	s_globalScope.create();
}
void nodegate::clearNativeModule() noexcept
{
	s_globalScope.remove();

	g_ctx.remove();
	JsRuntime::setRuntime(nullptr);
}