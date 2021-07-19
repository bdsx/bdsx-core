#include "stdafx.h"
#include "sehandler.h"
#include "voidpointer.h"
#include "nativepointer.h"
#include "nodegate.h"
#include "jsctx.h"
#include "unwind.h"

#include <KR3/win/windows.h>
#include <KR3/mt/criticalsection.h>
#include <KR3/util/StackWalker.h>
#include <KR3/util/pdb.h>
#include <KRWin/handle.h>

using namespace kr;


namespace
{
	atomic<unsigned int> s_nativeErrorCode = 0;
	unsigned int s_nativeExceptionThread = 0;
	Array<StackInfo> s_nativeExceptionStack;
	CriticalSection s_stackLock;

	struct DeferField
	{
		JsPersistent runtimeErrorClass;
		JsPersistent runtimeErrorAllocator;
		JsPersistent handler;
		DWORD threadId;
		ULONG_PTR exceptionInfos[EXCEPTION_MAXIMUM_PARAMETERS];

		DeferField() noexcept
		{
			JsValue array = JsRuntime::run(u"(()=>{class RuntimeError extends Error{} return [RuntimeError, (msg)=>{ return new RuntimeError(msg); }];})()");
			runtimeErrorClass = array.get(0);
			runtimeErrorAllocator = array.get(1);
			threadId = GetCurrentThreadId();
		}
	};
	Deferred<DeferField> s_field(JsRuntime::initpack);

	void fireInJsThread() noexcept
	{
		JsScope _scope;
		try
		{
			JsValue error = runtimeError::getError();
			JsValue handler = s_field->handler;
			if (!handler.isEmpty())
			{
				handler.call(error);
			}
		}
		catch (JsException& err)
		{
			g_ctx->fireError(err);
		}
		terminate(-1);
	}

	Array<StackInfo> getStack(EXCEPTION_POINTERS* exptr, HANDLE thread) noexcept
	{
		PdbReader::setOptions(0x00000002);
		for (int i = 0; i < 3; i++)
		{
			Array<StackInfo> infos = getStackInfos(exptr->ContextRecord, thread);
			if (infos.empty())
			{
				DWORD64 rsp = exptr->ContextRecord->Rsp;
				exptr->ContextRecord->Rsp = (rsp % ~0xf) + 0x10;
				continue;
			}
			return infos;
		}
		return nullptr;
	}
	JsValue lookUpFunctionEntry(VoidPointer* address) throws(JsException) {
		if (address == nullptr) return nullptr;
		uintptr_t base = 0;
		uintptr_t addr = (uintptr_t)address->getAddressRaw();
		RUNTIME_FUNCTION* table = RtlLookupFunctionEntry(addr, &base, nullptr);
		if (table != nullptr) {
			JsValue out = JsNewArray(4);
			out.set(0, VoidPointer::make((void*)base));
			out.set(1, (int)table->BeginAddress);
			out.set(2, (int)table->EndAddress);
			out.set(3, (int)table->UnwindInfoAddress);
			return out;
		}
		else if (base != 0) {
			JsValue out = JsNewArray(1);
			out.set(0, VoidPointer::make((void*)base));
			return out;
		}
		else {
			return nullptr;
		}
	}
	bool addFunctionTable(VoidPointer* runtimeFunctionTable, int fncount, VoidPointer* baseptr) noexcept
	{
		if (baseptr == nullptr) return false;
		if (runtimeFunctionTable == nullptr) return false;
		uintptr_t base = (uintptr_t)baseptr->getAddressRawSafe();
		RUNTIME_FUNCTION* functionTable = (RUNTIME_FUNCTION*)runtimeFunctionTable->getAddressRawSafe();
		return RtlAddFunctionTable(functionTable, fncount, base);
	}
}

kr::Text16 runtimeError::codeToString(unsigned int errorCode) noexcept
{
	switch (errorCode)
	{
	case STILL_ACTIVE: return u"STILL_ACTIVE";
	case EXCEPTION_ACCESS_VIOLATION: return u"EXCEPTION_ACCESS_VIOLATION";
	case EXCEPTION_DATATYPE_MISALIGNMENT: return u"EXCEPTION_DATATYPE_MISALIGNMENT";
	case EXCEPTION_BREAKPOINT: return u"EXCEPTION_BREAKPOINT";
	case EXCEPTION_SINGLE_STEP: return u"EXCEPTION_SINGLE_STEP";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return u"EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
	case EXCEPTION_FLT_DENORMAL_OPERAND: return u"EXCEPTION_FLT_DENORMAL_OPERAND";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO: return u"EXCEPTION_FLT_DIVIDE_BY_ZERO";
	case EXCEPTION_FLT_INEXACT_RESULT: return u"EXCEPTION_FLT_INEXACT_RESULT";
	case EXCEPTION_FLT_INVALID_OPERATION: return u"EXCEPTION_FLT_INVALID_OPERATION";
	case EXCEPTION_FLT_OVERFLOW: return u"EXCEPTION_FLT_OVERFLOW";
	case EXCEPTION_FLT_STACK_CHECK: return u"EXCEPTION_FLT_STACK_CHECK";
	case EXCEPTION_FLT_UNDERFLOW: return u"EXCEPTION_FLT_UNDERFLOW";
	case EXCEPTION_INT_DIVIDE_BY_ZERO: return u"EXCEPTION_INT_DIVIDE_BY_ZERO";
	case EXCEPTION_INT_OVERFLOW: return u"EXCEPTION_INT_OVERFLOW";
	case EXCEPTION_PRIV_INSTRUCTION: return u"EXCEPTION_PRIV_INSTRUCTION";
	case EXCEPTION_IN_PAGE_ERROR: return u"EXCEPTION_IN_PAGE_ERROR";
	case EXCEPTION_ILLEGAL_INSTRUCTION: return u"EXCEPTION_ILLEGAL_INSTRUCTION";
	case EXCEPTION_NONCONTINUABLE_EXCEPTION: return u"EXCEPTION_NONCONTINUABLE_EXCEPTION";
	case EXCEPTION_STACK_OVERFLOW: return u"EXCEPTION_STACK_OVERFLOW";
	case EXCEPTION_INVALID_DISPOSITION: return u"EXCEPTION_INVALID_DISPOSITION";
	case EXCEPTION_GUARD_PAGE: return u"EXCEPTION_GUARD_PAGE";
	case EXCEPTION_INVALID_HANDLE: return u"EXCEPTION_INVALID_HANDLE";
	// case EXCEPTION_POSSIBLE_DEADLOCK: return u"EXCEPTION_POSSIBLE_DEADLOCK";
	case CONTROL_C_EXIT: return u"CONTROL_C_EXIT";
	default: return u"[INVALID ERROR CODE]";
	}
}

ATTR_NORETURN void runtimeError::raise(EXCEPTION_POINTERS* exptr) noexcept
{
	static DWORD raising = 0;
	if (raising != 0) Sleep(INFINITE);
	DWORD threadId = raising = GetCurrentThreadId();

#ifndef NDEBUG
	{
		cout << "   [[DEBUG INFO]]" << endl;
		DWORD64 rip = exptr->ContextRecord->Rip;
		unsigned int code = exptr->ExceptionRecord->ExceptionCode;
		cout << "ExceptionCode: 0x" << hexf(code, 8) << endl;
		cout << "Thread Id: " << threadId << endl;
		cout << "rip: 0x" << hexf(rip, 16) << endl;

		MEMORY_BASIC_INFORMATION mbi;
		if (VirtualQuery((void*)(uintptr_t)rip, &mbi, sizeof(mbi)))
		{
			win::Module* mod = (win::Module*)mbi.AllocationBase;
			
			TSZ16 filename;
			filename << mod->fileName();
			Text16 basename = path16.basename(filename);
			if (!basename.empty())
			{
				cout << "rip RVA: " << (Utf16ToUtf8)basename << "+0x" << hexf(rip - (uintptr_t)mod) << endl;
			}
			else
			{

				cout << "rip RVA: [0x" << hexf((uintptr_t)mod) << "]+0x" << hexf(rip - (uintptr_t)mod) << endl;
			}
		}
		cout << "   [[DEBUG INFO END]]" << endl;
	}

	if (requestDebugger())
	{
		debug();
	}
#endif

	for (;;)
	{
		try
		{
			if (s_nativeErrorCode != 0) break;
			unsigned int code = exptr->ExceptionRecord->ExceptionCode;

			Array<StackInfo> stack = getStack(exptr, GetCurrentThread());
			CsLock lock = s_stackLock;
			unsigned int expected = 0;
			if (!s_nativeErrorCode.compare_exchange_strong(expected, code)) break;
			s_nativeExceptionThread = threadId;
			s_nativeExceptionStack = move(stack);
			memcpy(s_field->exceptionInfos, exptr->ExceptionRecord->ExceptionInformation, sizeof(s_field->exceptionInfos));
		}
		catch (...)
		{
			CsLock lock = s_stackLock;
			s_nativeErrorCode = -1;
			s_nativeExceptionStack = nullptr;
		}
		if (threadId == s_field->threadId)
		{
			fireInJsThread();
		}
		else
		{
			AsyncTask::post(fireInJsThread);
			break;
		}
	}
	Sleep(INFINITE);
}
void runtimeError::setHandler(kr::JsValue handler) noexcept
{
	s_field->handler = handler;
}
void runtimeError::fire(JsValueRef error) noexcept
{
	JsScope _scope;
	try
	{
		JsValue handler = s_field->handler;
		if (!handler.isEmpty())
		{
			handler.call((JsValue)(JsRawData)error);
		}
	}
	catch (JsException& err)
	{
		g_ctx->fireError(err);
	}
	terminate(-1);
}
kr::JsValue runtimeError::getError() throws(JsException)
{
	CsLock lock = s_stackLock;
	unsigned int code = s_nativeErrorCode;
	if (code == 0) return nullptr;

	JsValue catched;
	try
	{
		JsExceptionCatcher catcher;
	}
	catch (JsException& cause)
	{
		catched = cause.getValue();
	}

	JsClass runtimeErrorAllocator = (JsValue)s_field->runtimeErrorAllocator;
	TSZ16 message;
	message << u"RuntimeError: " << codeToString(code) << u"(0x" << hexf(code, 8) << u')';
	JsValue err = runtimeErrorAllocator.call((Text16)message);
	if (!catched.isEmpty()) {
		err.set(u"message", catched.get(u"message"));
		err.set(u"stack", catched.get(u"stack"));
	}

	err.set(u"code", (int)code);

	JsPropertyId base = u"base";
	JsPropertyId address = u"address";
	JsPropertyId moduleName = u"moduleName";
	JsPropertyId fileName = u"fileName";
	JsPropertyId functionName = u"functionName";
	JsPropertyId lineNumber = u"lineNumber";

	size_t stackSize = s_nativeExceptionStack.size();
	JsValue stack = JsNewArray(stackSize);
	int i = 0;
	for (StackInfo& info : s_nativeExceptionStack) {
		JsValue obj = JsNewObject;
		obj.set(base, info.base == nullptr ? (JsValue)nullptr : NativePointer::make(info.base));
		obj.set(address, NativePointer::make(info.address));
		obj.set(moduleName, info.moduleName == nullptr ? (JsValue)nullptr : (JsValue)(Text16)info.moduleName);
		obj.set(fileName, info.filename == nullptr ? (JsValue)nullptr : (JsValue)(Text16)info.filename);
		obj.set(functionName, info.function == nullptr ? (JsValue)nullptr : (JsValue)(Text)info.function);
		obj.set(lineNumber, (int)info.line);
		stack.set(i++, obj);
	}

	err.set(u"nativeStack", stack);

	if (s_nativeExceptionThread != s_field->threadId)
	{
		message << u"\n[thread: ";
		message << s_nativeExceptionThread;
		message << u']';
		err.set(u"stack", (Text16)message);
	}
	JsValue exceptionInfos = JsNewArray(EXCEPTION_MAXIMUM_PARAMETERS);
	err.set(u"exceptionInfos", exceptionInfos);
	for (size_t i = 0; i < EXCEPTION_MAXIMUM_PARAMETERS; i++) {
		JsValue addr = NativePointer::newInstanceRaw({});
		addr.getNativeObject<VoidPointer>()->setAddressRaw((void*)s_field->exceptionInfos[i]);
		exceptionInfos.set((int)i, addr );
	}
	return err;
}
kr::JsValue runtimeError::getRuntimeErrorClass() noexcept
{
	return (JsValue)s_field->runtimeErrorClass;
}

kr::JsValue runtimeError::getNamespace() noexcept
{
	JsValue runtimeError = JsNewObject;
	runtimeError.setMethod(u"codeToString", codeToString);
	runtimeError.set(u"raise", VoidPointer::make(raise));
	runtimeError.setMethod(u"setHandler", setHandler);
	runtimeError.set(u"fire", VoidPointer::make(fire));
	runtimeError.setMethod(u"lookUpFunctionEntry", lookUpFunctionEntry);
	runtimeError.setMethod(u"addFunctionTable", addFunctionTable);
	return runtimeError;
}
