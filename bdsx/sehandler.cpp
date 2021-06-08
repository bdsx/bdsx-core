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
	unsigned int s_threadId = 0;
	AText16 s_nativeExceptionStack;
	CriticalSection s_stackLock;

	struct DeferField
	{
		JsPersistent runtimeErrorClass;
		JsPersistent runtimeErrorAllocator;
		JsPersistent handler;
		JsPropertyId nativeStack = JsPropertyId(u"nativeStack");
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
			g_ctx->error(err);
		}
		terminate(-1);
	}

	AText16 getStack(EXCEPTION_POINTERS* exptr) noexcept
	{
		PdbReader::setOptions(0x00000002);
		AText16 stack;
		for (int i = 0; i < 3; i++)
		{
			StackWriter writer(exptr->ContextRecord);
			stack << writer;
			if (stack.empty())
			{
				exptr->ContextRecord->Rsp += 0x10;
				continue;
			}
			return stack;
		}
		return u"[Unknown]";
	}
	bool addFunctionTable(VoidPointer* runtimeFunctionTable, int fncount, VoidPointer* baseptr) throws(JsException)
	{
		DWORD64 base = (DWORD64)baseptr->getAddressRawSafe();
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

			AText16 stack = getStack(exptr);
			if (stack.endsWith('\n')) stack.pop();
			CsLock lock = s_stackLock;
			if (s_nativeErrorCode != 0) break;
			s_nativeErrorCode = code;
			s_threadId = threadId;
			s_nativeExceptionStack << move(stack);
			memcpy(s_field->exceptionInfos, exptr->ExceptionRecord->ExceptionInformation, sizeof(s_field->exceptionInfos));
		}
		catch (...)
		{
			CsLock lock = s_stackLock;
			s_nativeErrorCode = -1;
			s_nativeExceptionStack = u"[[setRuntimeException, Invalid EXCEPTION_POINTERS]]";
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
		g_ctx->error(err);
	}
	terminate(-1);
}
kr::JsValue runtimeError::getError() noexcept
{
	CsLock lock = s_stackLock;
	unsigned int code = s_nativeErrorCode;
	if (code == 0) return nullptr;

	JsClass runtimeErrorAllocator = (JsValue)s_field->runtimeErrorAllocator;
	TSZ16 message;
	message << u"RuntimeError: " << codeToString(code) << u"(0x" << hexf(code, 8) << u')';
	JsValue err = runtimeErrorAllocator.call((Text16)message);
	err.set(u"code", (int)code);
	
	err.set(s_field->nativeStack, s_nativeExceptionStack);
	if (s_threadId != s_field->threadId)
	{
		message << u"\n[thread: ";
		message << s_threadId;
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
	runtimeError.setMethod(u"addFunctionTable", addFunctionTable);
	return runtimeError;
}
