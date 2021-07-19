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

	using main_t = int(*)(int, char**, char**);
	main_t s_bedrockMain;
	constexpr size_t ORIGINAL_BYTES_COUNT = 12;
	byte s_bedrockMainOriginal12Bytes[ORIGINAL_BYTES_COUNT];

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
	__except (GetExceptionCode() == EXCEPTION_BREAKPOINT ? EXCEPTION_CONTINUE_SEARCH : (runtimeError::raise(GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER))
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
		PdbReader::setOptions(0);
		s_bedrockMain = g_pdb.getProcAddress(CachedPdb::predefinedForCore.data(), "main");
		if (s_bedrockMain == nullptr)
		{
			cerr << "[BDSX] Failed to find 'main' of bedrock_server.exe" << endl;
			terminate(-1);
		}
		// hook main
		{
			memcpy(s_bedrockMainOriginal12Bytes, s_bedrockMain, ORIGINAL_BYTES_COUNT);
			Unprotector unpro(s_bedrockMain, ORIGINAL_BYTES_COUNT);
			CodeWriter writer((void*)unpro, ORIGINAL_BYTES_COUNT);
			writer.jump(nodeStart, RAX);
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
			_catch(param, "kr::ThrowAbort");
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
		void* pointer_js2class(JsValueRef value) noexcept
		{
			return JsValue((JsRawData)value).getNativeObject<VoidPointer>();
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
			bedrock_server_exe.set(u"mainOriginal12Bytes", VoidPointer::make(s_bedrockMainOriginal12Bytes));
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