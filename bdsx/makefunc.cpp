#include "stdafx.h"
#include "makefunc.h"
#include "nodegate.h"
#include "sehandler.h"

#include <KR3/util/stackgc.h>
#include <KR3/util/wide.h>
#include <KR3/win/windows.h>
#include <KRWin/hook.h>
#include <KR3/js/js.h>

#include "nativepointer.h"

#define USE_EDGEMODE_JSRT
#include <jsrt.h>

#pragma comment(lib, "chakrart.lib")

using namespace kr;
using namespace hook;

enum class RawTypeId :int
{
	Int32,
	FloatAsInt64,
	Float,
	StringAnsi,
	StringUtf8,
	StringUtf16,
	Buffer,
	Bin64,
	Boolean,
	JsValueRef,
	Void,
	Pointer,
	WrapperToNp,
	WrapperToJs,
	StructureReturn
};

namespace
{

	struct ThreadLocalAllocate
	{
		DWORD id;
		ThreadLocalAllocate() noexcept
		{
			id = TlsAlloc();
		}
		~ThreadLocalAllocate() noexcept
		{
			TlsFree(id);
		}
	};
	ThreadLocalAllocate returnPointIdAllocated;
}
extern "C" void* returnPoint = nullptr;
extern "C" ATTR_NORETURN void makefunc_getout();

namespace
{
	uintptr_t last_allocate = 1;

	struct DeferField
	{
		JsPropertyId pointer = u"pointer";
		JsValue js2np = JsNewSymbol((JsValue)u"makefunc.js2np");
		JsValue np2js = JsNewSymbol((JsValue)u"makefunc.np2js");
		JsPersistent onError;
		JsPropertyId js2np_prop = JsPropertyId::fromSymbol(js2np);
		JsPropertyId np2js_prop = JsPropertyId::fromSymbol(np2js);
		JsPropertyId pthis = u"this";
		JsPropertyId pstructureReturn = u"structureReturn";
		JsPropertyId pnullableReturn = u"nullableReturn";
		JsPropertyId pnullableThis = u"nullableThis";
		JsPropertyId pnullableParams = u"nullableParams";
	};
	Deferred<DeferField> s_field(JsRuntime::initpack);

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

	JsValueRef makeError(Text16 text) noexcept
	{
		JsValueRef message;
		JsValueRef error;
		JsErrorCode err;

		err = JsPointerToString(wide(text.data()), text.size(), &message);
		_assert(err == JsNoError);
		err = JsCreateError(message, &error);
		_assert(err == JsNoError);
		return error;
	}

	void getout_jserror(JsValueRef error) noexcept
	{
		stack_free_all();
		JsErrorCode err = JsSetException(error);
		_assert(err == JsNoError);

		if ((uintptr_t)returnPoint & 1) makefunc_getout();
		else runtimeError::fire((JsRawData)error);
	}

	ATTR_NORETURN void getout_invalid_parameter(uint32_t paramNum) noexcept
	{
		JsValueRef error;
		if (paramNum <= 0) error = makeError(u"Invalid parameter at this");
		else error = makeError(TSZ16() << u"Invalid parameter at " << (paramNum));
		getout_jserror(error);
	}

	ATTR_NORETURN void getout_invalid_parameter_count(uint32_t current, uint32_t needs) noexcept
	{
		JsValueRef error = makeError(TSZ16() << u"Invalid parameter count " << current << u", needs " << needs);
		getout_jserror(error);
	}

	void _throwJsrtError(JsErrorCode err) throws(JsValue, JsException)
	{
		JsValueRef exception;
		if (JsGetAndClearException(&exception) == JsNoError)
		{
			throw (JsValue)(JsRawData)exception;
		}
		else
		{
			throw JsException(TSZ16() << u"JsErrorCode: 0x" << kr::hexf((int)err));
		}
	}

	ATTR_NORETURN void getout(JsErrorCode err) noexcept
	{
		if ((uintptr_t)returnPoint & 1)
		{
			bool has;
			if ((JsHasException(&has) == JsNoError) && has)
			{
				stack_free_all();
				makefunc_getout();
			}
		}
		else
		{
			JsValueRef exception;
			if ((JsGetAndClearException(&exception) == JsNoError))
			{
				stack_free_all();
				runtimeError::fire((JsRawData)exception);
			}
		}
		JsValueRef error = makeError(TSZ16() << u"JsErrorCode: 0x" << kr::hexf((int)err));
		getout_jserror(error);
	}

	char* stack_ansi(JsValueRef value, uint32_t paramNum) noexcept
	{
		try
		{
			JsValue jsvalue = ((JsRawData)value);
			switch (jsvalue.getType())
			{
			case JsType::Null: return nullptr;
			case JsType::String: {
				Utf16ToUtf8 dec = jsvalue.as<Text16>();
				size_t size = dec.size();
				char* out = (char*)stack_alloc(size + 1);
				dec.copyTo(out);
				out[size] = '\0';
				return out;
			}
			}
		}
		catch (...)
		{
		}
		getout_invalid_parameter(paramNum);
	}

	char* stack_utf8(JsValueRef value, uint32_t paramNum) noexcept
	{
		try
		{
			JsValue jsvalue = ((JsRawData)value);
			switch (jsvalue.getType())
			{
			case JsType::Null: return nullptr;
			case JsType::String: {
				Utf16ToUtf8 dec = jsvalue.as<Text16>();
				size_t size = dec.size();
				char* out = (char*)stack_alloc(size + 1);
				dec.copyTo(out);
				out[size] = '\0';
				return out;
			}
			}
		}
		catch (...)
		{
		}
		getout_invalid_parameter(paramNum);
	}

	void* buffer_to_pointer(JsValueRef value, uint32_t paramNum) noexcept
	{
		try
		{
			JsValue jsvalue = ((JsRawData)value);
			switch (jsvalue.getType())
			{
			case JsType::Null: return nullptr;
			case JsType::ArrayBuffer:
				return jsvalue.getArrayBuffer().data();
			case JsType::TypedArray:
				return jsvalue.getTypedArrayBuffer().data();
			case JsType::DataView:
				return jsvalue.getDataViewBuffer().data();
			case JsType::Object:
				VoidPointer* ptr = jsvalue.getNativeObject<VoidPointer>();
				if (ptr != nullptr) return ptr->getAddressRaw();
				break;
			}
		}
		catch (...)
		{
		}
		getout_invalid_parameter(paramNum);
	}

	const char16_t* js2np_utf16(JsValueRef value, uint32_t paramNum) noexcept
	{
		try
		{
			JsValue jsvalue = ((JsRawData)value);
			switch (jsvalue.getType())
			{
			case JsType::Null: return nullptr;
			case JsType::String:
				return jsvalue.as<Text16>().data();
			}
		}
		catch (...)
		{
		}
		getout_invalid_parameter(paramNum);
	}

	JsValueRef np2js_ansi(pcstr str, uint32_t paramNum) noexcept
	{
		JsErrorCode err;
		JsValueRef out;
		{
			TSZ16 buf;
			buf << (AnsiToUtf16)(Text)str;

			err = JsPointerToString(wide(buf.data()), buf.size(), &out);
		}
		if (err != JsNoError) getout_invalid_parameter(paramNum);
		return out;
	}

	JsValueRef np2js_utf8(pcstr str, uint32_t paramNum) noexcept
	{
		JsValueRef out;
		JsErrorCode err;

		{
			TSZ16 buf;
			buf << (Utf8ToUtf16)(Text)str;
			err = JsPointerToString(wide(buf.data()), buf.size(), &out);
		}
		if (err != JsNoError) getout_invalid_parameter(paramNum);
		return out;
	}

	JsValueRef np2js_utf16(pcstr16 str, uint32_t paramNum) noexcept
	{
		JsValueRef out;
		JsErrorCode err = JsPointerToString(wide(str), wcslen(wide(str)), &out);
		if (err != JsNoError) getout_invalid_parameter(paramNum);
		return out;
	}

	void* js2np_pointer(JsValueRef value) noexcept
	{
		JsValue ptrvalue = JsRawData(value);
		VoidPointer* ptr = ptrvalue.getNativeObject<VoidPointer>();
		if (ptr == nullptr) return nullptr;
		return ptr->getAddressRaw();
	}

	JsValueRef np2js_pointer(void* ptr, JsValueRef ctor) noexcept
	{
		{
			JsScope _scope;
			try
			{
				JsValue ctorv = JsClass((JsRawData)ctor).newInstanceRaw({});
				VoidPointer* nptr = ctorv.getNativeObject<VoidPointer>();
				_assert(nptr != nullptr);
				nptr->setAddressRaw(ptr);

				return ctorv.getRaw();
			}
			catch (JsException& err)
			{
				JsSetException(err.getValue().getRaw());
			}
		}
		getout(JsErrorScriptException);
	}

	JsValueRef np2js_pointer_nullable(void* ptr, JsValueRef ctor) noexcept
	{
		{
			JsScope _scope;
			try
			{
				if (ptr == nullptr) return JsValue(nullptr).getRaw();
				JsValue ctorv = JsClass((JsRawData)ctor).newInstanceRaw({});
				VoidPointer* nptr = ctorv.getNativeObject<VoidPointer>();
				_assert(nptr != nullptr);
				nptr->setAddressRaw(ptr);

				return ctorv.getRaw();
			}
			catch (JsException& err)
			{
				JsSetException(err.getValue().getRaw());
			}
		}
		getout(JsErrorScriptException);
	}

	JsValueRef js_pointer_new(JsValueRef ctor, JsValueRef* out) noexcept
	{
		JsValue ctorv = JsClass((JsRawData)ctor).newInstanceRaw({JsValue(true)});
		*out = ctorv.getRaw();

		VoidPointer* nptr = ctorv.getNativeObject<VoidPointer>();
		return nptr->getAddressRaw();
	}

	int64_t bin64(JsValueRef value, uint32_t paramNum) noexcept
	{
		pcstr16 str;
		size_t len;
		JsErrorCode err = JsStringToPointer(value, (const wchar_t **)&str, &len);
		if (err != JsNoError) getout_invalid_parameter(paramNum);
		return getBin64(Text16(str, str+len));
	}

	void* js2np_wrapper(JsValueRef ptr, JsValueRef func) noexcept
	{
		JsScope _scope;
		JsValue res = ((JsRawData)func).call(JsRuntime::global(), { (JsValue)(JsRawData)ptr });
		VoidPointer* nptr = res.getNativeObject<VoidPointer>();
		if (nptr == nullptr)
		{
			JsValueRef error = makeError(TSZ16() << u"Invalid [makefunc.js2np] return type, must return *Pointer type.");
			getout_jserror(error);
		}
		return nptr->getAddressRaw();
	}

	JsValueRef np2js_wrapper(void* ptr, JsValueRef func, JsValueRef ctor) noexcept
	{
		JsScope _scope;
		try
		{
			JsValue ctorv = JsClass((JsRawData)ctor).newInstanceRaw({});
			VoidPointer* nptr = ctorv.getNativeObject<VoidPointer>();
			_assert(nptr != nullptr);
			nptr->setAddressRaw(ptr);

			return JsValue((JsRawData)func).call(ctorv).getRaw();
		}
		catch (JsException& err)
		{
			JsSetException(err.getValue().getRaw());
			return nullptr;
		}
	}
	JsValueRef np2js_wrapper_nullable(void* ptr, JsValueRef func, JsValueRef ctor) noexcept
	{
		JsScope _scope;
		try
		{
			if (ptr == nullptr)
			{
				return JsValue((JsRawData)func).call(nullptr).getRaw();
			}

			JsValue ctorv = JsClass((JsRawData)ctor).newInstanceRaw({});
			VoidPointer* nptr = ctorv.getNativeObject<VoidPointer>();
			_assert(nptr != nullptr);
			nptr->setAddressRaw(ptr);

			return JsValue((JsRawData)func).call(ctorv).getRaw();
		}
		catch (JsException& err)
		{
			JsSetException(err.getValue().getRaw());
			return nullptr;
		}
	}

	struct FunctionTable
	{
		void* p_getout = getout;
		void* p_np2js_wrapper_nullable = np2js_wrapper_nullable;
		void* p_np2js_wrapper = np2js_wrapper;
		void* p_js2np_wrapper = js2np_wrapper;
		void* p_stack_alloc = stack_alloc;
		void* p_stack_free_all = stack_free_all;
		void* p_stack_ansi = stack_ansi;
		void* p_stack_utf8 = stack_utf8;
		void* p_js2np_utf16 = js2np_utf16;
		void* p_js2np_pointer = js2np_pointer;
		void* p_bin64 = bin64;
		void* p_JsNumberToInt = JsNumberToInt;
		void* p_JsBoolToBoolean = JsBoolToBoolean;
		void* p_JsBooleanToBool = JsBooleanToBool;

		void* p_getout_invalid_parameter = getout_invalid_parameter;

		void* p_JsIntToNumber = JsIntToNumber;
		void* p_JsNumberToDouble = JsNumberToDouble;
		void* p_buffer_to_pointer = buffer_to_pointer;
		void* p_JsDoubleToNumber = JsDoubleToNumber;
		void* p_JsPointerToString = JsPointerToString;
		void* p_np2js_ansi = np2js_ansi;
		void* p_np2js_utf8 = np2js_utf8;
		void* p_np2js_utf16 = np2js_utf16;
		void* p_np2js_pointer = np2js_pointer;
		void* p_np2js_pointer_nullable = np2js_pointer_nullable;
		void* p_getout_invalid_parameter_count = getout_invalid_parameter_count;
		void* p_JsCallFunction = JsCallFunction;
		void* p_js_pointer_new = js_pointer_new;
	};

	const FunctionTable functionTableDefine;

	constexpr size_t centerOfFunctionTable = offsetof(FunctionTable, p_getout_invalid_parameter);

	const qword functionTablePtr = (qword)((byte*)&functionTableDefine + centerOfFunctionTable);

	Text16 getNameFromType(JsType type) noexcept
	{
		switch (type)
		{
		case JsType::Undefined: return u"undefined";
		case JsType::Null: return u"null";
		case JsType::Boolean: return u"boolean";
		case JsType::Integer:
		case JsType::Float: return u"number";
		case JsType::String: return u"string";
		case JsType::Function: return u"function";
		case JsType::Object:
		case JsType::ArrayBuffer:
		case JsType::TypedArray:
		case JsType::DataView:
			return u"object";
		default:
			return u"[invalid]";
		}
	}

	Text16 getNameFromRawType(RawTypeId type) noexcept
	{
		switch (type)
		{
		case RawTypeId::Int32: return u"RawTypeId.Int32";
		case RawTypeId::FloatAsInt64: return u"RawTypeId.FloatAsInt64";
		case RawTypeId::Float: return u"RawTypeId.Float";
		case RawTypeId::StringAnsi: return u"RawTypeId.StringAnsi";
		case RawTypeId::StringUtf8: return u"RawTypeId.StringUtf8";
		case RawTypeId::StringUtf16: return u"RawTypeId.StringUtf16";
		case RawTypeId::Buffer: return u"RawTypeId.Buffer";
		case RawTypeId::Bin64: return u"RawTypeId.Bin64";
		case RawTypeId::Boolean: return u"RawTypeId.Boolean";
		case RawTypeId::JsValueRef: return u"RawTypeId.JsValueRef";
		case RawTypeId::Void: return u"RawTypeId.Void";
		default: return u"RawTypeId.[invalid]";
		}
	}

	VoidPointer* pointerInstanceOrThrow(JsValue value, int32_t paramNumber) throws(JsException)
	{
		VoidPointer* ptr = value.getNativeObject<VoidPointer>();
		if (ptr == nullptr)
		{
			TSZ16 tsz;
			if (paramNumber == -1) tsz << u"Invalid return type";
			else if (paramNumber == 0) tsz << u"Invalid this type";
			else tsz << u"Invalid parameter type at " << (uint32_t)paramNumber;

			tsz << u", *Pointer instance required";
			throw JsException(tsz);
		}
		return ptr;
	}

	constexpr int32_t PARAMNUM_INVALID = -2;
	constexpr int32_t PARAMNUM_RETURN = -1;
	constexpr int32_t PARAMNUM_THIS = 0;

	ATTR_NORETURN void throwTypeError(int32_t paramNum, Text16 name, Text16 value, Text16 detail) throws(JsException)
	{
		TSZ16 tsz;
		if (paramNum == PARAMNUM_RETURN) tsz << u"Invalid return ";
		else if (paramNum == PARAMNUM_THIS) tsz << u"Invalid this ";
		else tsz << u"Invalid parameter ";
		tsz << name << u'(' << value << u')';
		if (paramNum > 0) tsz << u" at " << paramNum;
		tsz << u", " << detail;
		throw JsException(tsz);
	}

	ATTR_NORETURN void invalidParamType(RawTypeId type, int32_t paramNum) throws(JsException)
	{
		throwTypeError(paramNum, u"type", getNameFromRawType(type), u"Out of RawTypeId value");
	}

	void checkTypeIsFunction(JsValue value, int32_t paramNum) throws(JsException)
	{
		JsType jstype = value.getType();
		if (jstype != JsType::Function)
		{
			throwTypeError(paramNum, u"type", getNameFromType(jstype), u"function required");
		}
	}

	void pointerClassOrThrow(int32_t paramNum, JsValue type) throws(JsException)
	{
		if (!type.prototypeOf(VoidPointer::classObject))
		{
			JsValue name = type.get(u"name").toString();
			throwTypeError(paramNum, u"class", name.as<Text16>(), u"*Pointer class required");
		}
	}

	RawTypeId getRawTypeId(uint32_t paramNum, const JsValue& typeValue) throws(JsException)
	{
		JsType jstype = typeValue.getType();
		switch (jstype)
		{
		case JsType::Function:
			if (typeValue.get(s_field->js2np_prop).getType() == JsType::Function)
			{
				return RawTypeId::WrapperToNp;
			}
			else if (typeValue.get(s_field->np2js_prop).getType() == JsType::Function)
			{
				return RawTypeId::WrapperToJs;
			}
			else
			{
				pointerClassOrThrow(paramNum, typeValue);
				return RawTypeId::Pointer;
			}
		case JsType::Integer:
			// fall through
		case JsType::Float:
			return (RawTypeId)typeValue.valueOf().as<int>();
		case JsType::Object:
			if (typeValue.get(s_field->js2np_prop).getType() == JsType::Function)
			{
				return RawTypeId::WrapperToNp;
			}
			else if (typeValue.get(s_field->np2js_prop).getType() == JsType::Function)
			{
				return RawTypeId::WrapperToJs;
			}
			// fall through
		default:
			throwTypeError(paramNum, u"type", getNameFromType(jstype), u"RawTypeId or *Pointer class required");
		}

	}

	static const Register regMap[] = { RCX, RDX, R8, R9 };
	static const FloatRegister fregMap[] = { XMM0, XMM1, XMM2, XMM3 };

#define CALL(funcname) call(QwordPtr, RDI, (int32_t)offsetof(FunctionTable, p_##funcname)-(int32_t)centerOfFunctionTable);


	struct ParamInfo
	{
		dword indexOnCpp;
		dword numberOnMaking;
		dword numberOnUsing;
		JsValue type;
		RawTypeId typeId;
		bool nullable;
	};

	constexpr dword PARAM_OFFSET = 3;

	class ParamInfoMaker
	{
	public:
		const JsArguments& args;
		bool structureReturn;
		bool nullableThis;
		bool nullableReturn;
		bool useThis;
		bool nullableParams;
		JsValue thisType;

		TmpArray<RawTypeId> typeIds;
		dword countOnCalling;
		dword countOnCpp;

		ParamInfoMaker(const JsArguments& args) throws(JsException)
			:args(args)
		{
			uint32_t countOnMaking = intact<uint32_t>(args.size());
			if (countOnMaking < PARAM_OFFSET - 1)
			{
				throw JsException(u"Too few parameters");
			}
			JsValue opts;
			if (countOnMaking != PARAM_OFFSET - 1 && !(opts = args[2]).abstractEquals((JsRawData)nullptr))
			{
				structureReturn = opts.get(s_field->pstructureReturn).cast<bool>();
				thisType = opts.get(s_field->pthis);
				useThis = thisType.cast<bool>();
				nullableReturn = opts.get(s_field->pnullableReturn).cast<bool>();
				nullableThis = opts.get(s_field->pnullableThis).cast<bool>();
				nullableParams = opts.get(s_field->pnullableParams).cast<bool>();
				if (useThis)
				{
					if (!thisType.prototypeOf(VoidPointer::classObject))
					{
						throw JsException(u"Non pointer at this");
					}
				}
				if (nullableThis)
				{
					if (!useThis) throw JsException(u"Invalid options. nullableThis without this type");
				}
				if (nullableReturn)
				{
					JsValue rettype = args[1];
					if (!rettype.prototypeOf(VoidPointer::classObject)) throw JsException(u"Invalid options. nullableReturn with non pointer type");
				}
				if (nullableReturn && structureReturn)
				{
					throw JsException(u"Invalid options. nullableReturn with structureReturn");
				}
			}
			else
			{
				structureReturn = 0;
				useThis = 0;
				nullableReturn = 0;
				nullableThis = 0;
			}

			countOnCalling = countOnMaking == PARAM_OFFSET - 1 ? 0 : countOnMaking - PARAM_OFFSET;
			countOnCpp = countOnCalling + useThis + structureReturn;

			typeIds.reserve(countOnCpp);
			if (useThis) typeIds.push(getRawTypeId(2, thisType));
			if (structureReturn) typeIds.push(RawTypeId::StructureReturn);
			for (dword i = PARAM_OFFSET; i < countOnMaking; i++)
			{
				RawTypeId type = getRawTypeId(i + 1, args[i]);
				typeIds.push(type);
			}
		}

		ParamInfo getInfo(uint32_t indexOnCpp) noexcept
		{
			ParamInfo info;
			info.indexOnCpp = indexOnCpp;
			info.nullable = false;

			if (indexOnCpp == -1)
			{
				info.type = args[1];
				info.numberOnMaking = 2;
				info.typeId = getRawTypeId(2, info.type);
				info.numberOnUsing = PARAMNUM_RETURN;
				info.nullable = nullableReturn;
				return info;
			}
			else if (useThis && indexOnCpp == 0)
			{
				info.type = thisType;
				info.numberOnMaking = 3;
				info.typeId = typeIds[0];
				info.numberOnUsing = PARAMNUM_THIS;
				info.nullable = nullableThis;
				return info;
			}
			if (structureReturn && indexOnCpp == (uint32_t)useThis)
			{
				info.type = args[1];
				info.numberOnMaking = 2;
				info.typeId = RawTypeId::StructureReturn;
				info.numberOnUsing = PARAMNUM_RETURN;
				info.nullable = false;
				return info;
			}
			uint32_t indexOnUsing = indexOnCpp - structureReturn - useThis;
			uint32_t indexOnMaking = PARAM_OFFSET + indexOnUsing;
			info.type = args[indexOnMaking];
			info.numberOnMaking = indexOnMaking + 1;
			info.numberOnUsing = indexOnUsing + 1;
			info.typeId = typeIds[indexOnCpp];
			info.nullable = nullableParams;
			return info;
		};
	};


	struct TargetInfo
	{
		Register reg;
		FloatRegister freg;
		int32_t offset;
		bool memory;

		bool operator ==(const TargetInfo& other) noexcept
		{
			if (memory)
			{
				return other.memory && reg == other.reg && offset == other.offset;
			}
			return !other.memory && reg == other.reg;
		}
		bool operator !=(const TargetInfo& other) noexcept
		{
			return !(*this == other);
		}

		TargetInfo() noexcept
		{
		}
		TargetInfo(Register reg, FloatRegister freg) noexcept
			:reg(reg), freg(freg)
		{
			memory = false;
		}
		TargetInfo(Register reg, int32_t offset) noexcept
			:reg(reg), offset(offset)
		{
			memory = true;
		}

		TargetInfo tempPtr() noexcept
		{
			if (memory) return *this;
			return TargetInfo(RBP, 0);
		}
	};

	const TargetInfo TARGET_RETURN = TargetInfo(RAX, XMM0);
	const TargetInfo TARGET_1 = TargetInfo(RCX, XMM0);


	class Maker :public JitFunction
	{

	public:
		int32_t stackSize;
		int32_t offsetForStructureReturn;
		ParamInfoMaker& pi;

		Maker(ParamInfoMaker& pi, size_t size, int32_t stackSize, bool useGetOut) noexcept
			:JitFunction(size), pi(pi), stackSize(stackSize)
		{
			mov(RAX, (qword)&returnPoint);
			mov(R10, QwordPtr, RAX);

			push(RDI);
			push(RSI);
			push(RBP);
			push(R10);

			if (useGetOut)
			{
				lea(R10, RSP, 1);
				mov(QwordPtr, RAX, R10);
			}
			else
			{
				mov(QwordPtr, RAX, RSP);
			}
			mov(RDI, functionTablePtr);
		}

		~Maker() noexcept
		{
			add(RSP, stackSize);
			pop(RCX);
			pop(RBP);
			pop(RSI);
			pop(RDI);

			mov(RDX, (qword)&returnPoint);
			mov(QwordPtr, RDX, 0, RCX);
			ret();
		}

		bool useStackAllocator = false;

		void _mov(TargetInfo target, uint64_t value) noexcept {
			
			if (target.memory)
			{
				Register temp = target.reg != R10 ? R10 : R11;
				mov(temp, value);
				mov(QwordPtr, target.reg, target.offset, temp);
			}
			else
			{
				mov(target.reg, value);
			}
		}

		void _mov(TargetInfo target, TargetInfo source, RawTypeId type, bool reverse) noexcept {
			if (target.memory)
			{
				Register temp = target.reg != R10 ? R10 : R11;
				FloatRegister ftemp = target.freg != XMM5 ? XMM5 : XMM6;

				if (source.memory)
				{
					if (type == RawTypeId::FloatAsInt64)
					{
						if (reverse)
						{
							cvttsi2sd(ftemp, QwordPtr, source.reg, source.offset);
							movsd(QwordPtr, target.reg, target.offset, ftemp);
						}
						else
						{
							cvttsd2si(temp, QwordPtr, source.reg, source.offset);
							mov(QwordPtr, target.reg, target.offset, temp);
						}
					}
					else if (type == RawTypeId::Boolean)
					{
						if (target == source)
						{
							// same
						}
						else
						{
							movzx(temp, BytePtr, source.reg, source.offset);
							mov(BytePtr, target.reg, target.offset, temp);
						}
					}
					else if (type == RawTypeId::Int32)
					{
						if (target == source)
						{
							// same
						}
						else
						{
							movsxd(temp, DwordPtr, source.reg, source.offset);
							mov(QwordPtr, target.reg, target.offset, temp);
						}
					}
					else
					{
						if (target == source)
						{
							// same
						}
						else
						{
							mov(temp, QwordPtr, source.reg, source.offset);
							mov(QwordPtr, target.reg, target.offset, temp);
						}
					}
				}
				else
				{
					if (type == RawTypeId::FloatAsInt64)
					{
						if (reverse)
						{
							cvttsi2sd(XMM0, source.reg);
							movsd(QwordPtr, target.reg, target.offset, XMM0);
						}
						else
						{
							cvttsd2si(temp, source.freg);
							mov(QwordPtr, target.reg, target.offset, temp);
						}
					}
					else if (type == RawTypeId::Float)
					{
						movsd(QwordPtr, target.reg, target.offset, source.freg);
					}
					else if (type == RawTypeId::Boolean)
					{
						mov(BytePtr, target.reg, target.offset, (RegisterLow)source.reg);
					}
					else
					{
						mov(QwordPtr, target.reg, target.offset, source.reg);
					}
				}
			}
			else
			{
				if (source.memory)
				{
					if (type == RawTypeId::FloatAsInt64)
					{
						if (reverse)
						{
							cvttsi2sd(target.freg, QwordPtr, source.reg, source.offset);
						}
						else
						{
							cvttsd2si(target.reg, QwordPtr, source.reg, source.offset);
						}
					}
					else if (type == RawTypeId::Float)
					{
						movsd(target.freg, QwordPtr, source.reg, source.offset);
					}
					else if (type == RawTypeId::Boolean)
					{
						movzx(target.reg, BytePtr, source.reg, source.offset);
					}
					else if (type == RawTypeId::Int32)
					{
						movsxd(target.reg, DwordPtr, source.reg, source.offset);
					}
					else
					{
						mov(target.reg, QwordPtr, source.reg, source.offset);
					}
				}
				else
				{
					if (type == RawTypeId::FloatAsInt64)
					{
						if (reverse)
						{
							cvttsi2sd(target.freg, source.reg);
						}
						else
						{
							cvttsd2si(target.reg, source.freg);
						}
					}
					else
					{
						if (target != source)
						{
							if (type == RawTypeId::Float)
							{
								movsd(target.freg, source.freg);
							}
							else
							{
								mov(target.reg, source.reg);
							}
						}
					}
				}
			}
		}

		void _mov_from_ptr(TargetInfo target, TargetInfo source, RawTypeId type, bool reverse) noexcept {
			Register temp = target.reg != R10 ? R10 : R11;
			FloatRegister ftemp = target.freg != XMM5 ? XMM5 : XMM6;
			if (target.memory)
			{
				if (source.memory)
				{
					mov(temp, QwordPtr, source.reg, source.offset);
					if (type == RawTypeId::FloatAsInt64)
					{
						if (reverse)
						{
							cvttsi2sd(XMM0, QwordPtr, temp, 0);
							movsd(QwordPtr, target.reg, target.offset, XMM0);
						}
						else
						{
							cvttsd2si(temp, QwordPtr, temp, 0);
							mov(QwordPtr, target.reg, target.offset, temp);
						}
					}
					else if (type == RawTypeId::Boolean)
					{
						movzx(temp, BytePtr, temp, 0);
						mov(BytePtr, target.reg, target.offset, temp);
					}
					else if (type == RawTypeId::Int32)
					{
						movsxd(temp, DwordPtr, temp, 0);
						mov(QwordPtr, target.reg, target.offset, temp);
					}
					else
					{
						mov(temp, QwordPtr, temp, 0);
						mov(QwordPtr, target.reg, target.offset, temp);
					}
				}
				else
				{
					if (type == RawTypeId::FloatAsInt64)
					{
						if (reverse)
						{
							mov(temp, QwordPtr, source.reg, 0);
							cvttsi2sd(ftemp, temp);
							movsd(QwordPtr, target.reg, target.offset, ftemp);
						}
						else
						{
							cvttsd2si(temp, QwordPtr, source.reg, 0);
							mov(QwordPtr, target.reg, target.offset, temp);
						}
					}
					else if (type == RawTypeId::Boolean)
					{
						movzx(temp, BytePtr, source.reg, 0);
						mov(BytePtr, target.reg, target.offset, temp);
					}
					else
					{
						mov(temp, QwordPtr, source.reg, 0);
						mov(QwordPtr, target.reg, target.offset, temp);
					}
				}
			}
			else
			{
				if (source.memory)
				{
					mov(temp, QwordPtr, source.reg, source.offset);
					if (type == RawTypeId::FloatAsInt64)
					{
						if (reverse)
						{
							cvttsi2sd(target.freg, QwordPtr, temp, 0);
						}
						else
						{
							cvttsd2si(target.reg, QwordPtr, temp, 0);
						}
					}
					else if (type == RawTypeId::Float)
					{
						movsd(target.freg, QwordPtr, temp, 0);
					}
					else if (type == RawTypeId::Boolean)
					{
						movzx(target.reg, BytePtr, temp, 0);
					}
					else if (type == RawTypeId::Int32)
					{
						movsxd(target.reg, DwordPtr, temp, 0);
					}
					else
					{
						mov(target.reg, QwordPtr, temp, 0);
					}
				}
				else
				{
					if (type == RawTypeId::FloatAsInt64)
					{
						if (reverse)
						{
							cvttsi2sd(target.freg, QwordPtr, source.reg, 0);
						}
						else
						{
							cvttsd2si(target.reg, QwordPtr, source.reg, 0);
						}
					}
					else
					{
						if (target != source)
						{
							if (type == RawTypeId::Float)
							{
								movsd(target.freg, QwordPtr, source.reg, 0);
							}
							else
							{
								mov(target.reg, QwordPtr, source.reg, 0);
							}
						}
					}
				}
			}
		}

		void nativeToJs(const ParamInfo &info, TargetInfo target, TargetInfo source) throws(JsException)
		{
			switch (info.typeId)
			{
			case RawTypeId::StructureReturn: {
				_mov(target, TARGET_RETURN, RawTypeId::Void, true);
				break;
			}
			case RawTypeId::WrapperToNp:
			case RawTypeId::Pointer: {
				pointerClassOrThrow(info.numberOnMaking, info.type);

				JsValueRef rawvalue = info.type.getRaw();
				JsAddRef(rawvalue, nullptr);

				_mov(TARGET_1, source, RawTypeId::Void, true);
				mov(RDX, (qword)rawvalue);
				if (info.nullable)
				{
					CALL(np2js_pointer_nullable);
				}
				else
				{
					CALL(np2js_pointer);
				}
				_mov(target, TARGET_RETURN, RawTypeId::Void, true);
				break;
			}
			case RawTypeId::WrapperToJs: {
				JsValue np2js = info.type.get(s_field->np2js_prop);
				JsValueRef np2jsraw = np2js.getRaw();
				JsAddRef(np2jsraw, nullptr);

				_mov(TARGET_1, source, RawTypeId::Void, true);
				mov(RDX, (qword)np2jsraw);
				if (info.type.prototypeOf(VoidPointer::classObject))
				{
					JsValueRef ctorraw = info.type.getRaw();
					JsAddRef(ctorraw, nullptr);
					mov(R8, (qword)ctorraw);
				}
				else
				{
					mov(R8, (qword)NativePointer::classObject.getRaw());
				}
				if (info.nullable)
				{
					CALL(np2js_wrapper_nullable);
				}
				else
				{
					CALL(np2js_wrapper);
				}
				_mov(target, TARGET_RETURN, RawTypeId::Void, true);
				break;
			}
			case RawTypeId::Boolean: {
				TargetInfo temp = target.tempPtr();
				_mov(TARGET_1, source, info.typeId, true);
				lea(RDX, temp.reg, temp.offset);
				CALL(JsBoolToBoolean);
				test(RAX, RAX);
				jz(9);
				mov(RCX, info.numberOnUsing);
				CALL(getout_invalid_parameter);
				_mov(target, temp, RawTypeId::Void, true);
				break;
			}
			case RawTypeId::Int32: {
				TargetInfo temp = target.tempPtr();
				_mov(TARGET_1, source, info.typeId, true);
				lea(RDX, temp.reg, temp.offset);
				CALL(JsIntToNumber);
				test(RAX, RAX);
				jz(9);
				mov(RCX, info.numberOnUsing);
				CALL(getout_invalid_parameter);
				_mov(target, temp, RawTypeId::Void, true);
				break;
			}
			case RawTypeId::FloatAsInt64: {
				TargetInfo temp = target.tempPtr();
				_mov(TARGET_1, source, info.typeId, true);
				lea(RDX, temp.reg, temp.offset);
				CALL(JsDoubleToNumber);
				test(RAX, RAX);
				jz(9);
				mov(RCX, info.numberOnUsing);
				CALL(getout_invalid_parameter);
				_mov(target, temp, RawTypeId::Void, true);
				break;
			}
			case RawTypeId::Float: {
				TargetInfo temp = target.tempPtr();
				_mov(TARGET_1, source, info.typeId, true);
				lea(RDX, temp.reg, temp.offset);
				CALL(JsDoubleToNumber);
				test(RAX, RAX);
				jz(9);
				mov(RCX, info.numberOnUsing);
				CALL(getout_invalid_parameter);
				_mov(target, temp, RawTypeId::Void, true);
				break;
			}
			case RawTypeId::StringAnsi:
				_mov(TARGET_1, source, info.typeId, true);
				mov(RDX, info.numberOnUsing);
				CALL(np2js_ansi);
				_mov(target, TARGET_RETURN, RawTypeId::Void, true);
				break;
			case RawTypeId::StringUtf8:
				_mov(TARGET_1, source, info.typeId, true);
				mov(RDX, info.numberOnUsing);
				CALL(np2js_utf8);
				_mov(target, TARGET_RETURN, RawTypeId::Void, true);
				break;
			case RawTypeId::StringUtf16:
				_mov(TARGET_1, source, info.typeId, true);
				mov(RDX, info.numberOnUsing);
				CALL(np2js_utf16);
				_mov(target, TARGET_RETURN, RawTypeId::Void, true);
				break;
			case RawTypeId::Bin64: {
				TargetInfo temp = target.tempPtr();
				if (source.memory)
				{
					lea(RCX, source.reg, source.offset);
				}
				else
				{
					lea(RCX, RBP, 8);
					mov(QwordPtr, RCX, 0, source.reg);
				}
				mov(RDX, (dword)4);
				lea(R8, temp.reg, temp.offset);
				CALL(JsPointerToString);
				test(RAX, RAX);
				jz(9);
				mov(RCX, info.numberOnUsing);
				CALL(getout_invalid_parameter);
				_mov(target, temp, RawTypeId::Void, true);
				break;
			}
			case RawTypeId::JsValueRef:
				_mov(target, source, info.typeId, true);
				break;
			case RawTypeId::Void:
				_mov(target, (uint64_t)(JsValue(undefined).getRaw()));
				break;
			default:
				invalidParamType(info.typeId, info.numberOnUsing);
			}
		}

		void jsToNative(const ParamInfo& info, TargetInfo target, TargetInfo source) throws(JsException)
		{
			switch (info.typeId)
			{
			case RawTypeId::StructureReturn: {
				lea(RDX, RBP, offsetForStructureReturn);
				mov(RCX, (qword)info.type.getRaw());
				CALL(js_pointer_new);
				_mov(target, TARGET_RETURN, RawTypeId::Void, true);
				break;
			}
			case RawTypeId::WrapperToNp: {
				JsValue js2np = info.type.get(s_field->js2np_prop);
				JsValueRef js2npraw = js2np.getRaw();
				JsAddRef(js2npraw, nullptr);

				_mov(TARGET_1, source, RawTypeId::Void, true);
				mov(RDX, (qword)js2npraw);
				CALL(js2np_wrapper);
				if (info.numberOnUsing == PARAMNUM_THIS)
				{
					if (!pi.nullableThis)
					{
						test(RAX, RAX);
						jnz(9);
						mov(RCX, info.numberOnUsing);
						CALL(getout_invalid_parameter);
					}
				}
				_mov(target, TARGET_RETURN, RawTypeId::Void, false);
				break;
			}
			case RawTypeId::WrapperToJs:
			case RawTypeId::Pointer: {
				pointerClassOrThrow(info.numberOnMaking, info.type);
				_mov(TARGET_1, source, RawTypeId::Void, false);
				CALL(js2np_pointer);
				if (info.numberOnUsing == PARAMNUM_THIS)
				{
					if (!pi.nullableThis)
					{
						test(RAX, RAX);
						jnz(9);
						mov(RCX, info.numberOnUsing);
						CALL(getout_invalid_parameter);
					}
				}
				_mov(target, TARGET_RETURN, RawTypeId::Void, false);
				break;
			}
			case RawTypeId::Boolean: {
				TargetInfo temp = target.tempPtr();
				_mov(TARGET_1, source, RawTypeId::Void, false);
				lea(RDX, temp.reg, temp.offset);
				CALL(JsBooleanToBool);
				test(RAX, RAX);
				jz(9);
				mov(RCX, info.numberOnUsing);
				CALL(getout_invalid_parameter);
				_mov(target, temp, info.typeId, false);
				break;
			}
			case RawTypeId::Int32: {
				TargetInfo temp = target.tempPtr();
				_mov(TARGET_1, source, RawTypeId::Void, false);
				lea(RDX, temp.reg, temp.offset);
				CALL(JsNumberToInt);
				test(RAX, RAX);
				jz(9);
				mov(RCX, info.numberOnUsing);
				CALL(getout_invalid_parameter);
				_mov(target, temp, info.typeId, false);
				break;
			}
			case RawTypeId::FloatAsInt64: {
				TargetInfo temp = target.tempPtr();
				_mov(TARGET_1, source, RawTypeId::Void, false);
				lea(RDX, temp.reg, temp.offset);
				CALL(JsNumberToDouble);
				test(RAX, RAX);
				jz(9);
				mov(RCX, info.numberOnUsing);
				CALL(getout_invalid_parameter);
				_mov(target, temp, info.typeId, false);
				break;
			}
			case RawTypeId::Float: {
				TargetInfo temp = target.tempPtr();
				_mov(TARGET_1, source, RawTypeId::Void, false);
				lea(RDX, temp.reg, temp.offset);
				CALL(JsNumberToDouble);
				test(RAX, RAX);
				jz(9);
				mov(RCX, info.numberOnUsing);
				CALL(getout_invalid_parameter);
				_mov(target, temp, info.typeId, false);
				break;
			}
			case RawTypeId::StringAnsi:
				_mov(TARGET_1, source, RawTypeId::Void, false);
				mov(RDX, info.numberOnUsing);
				CALL(stack_ansi);
				_mov(target, TARGET_RETURN, info.typeId, false);
				useStackAllocator = true;
				break;
			case RawTypeId::StringUtf8:
				_mov(TARGET_1, source, RawTypeId::Void, false);
				mov(RDX, info.numberOnUsing);
				CALL(stack_utf8);
				_mov(target, TARGET_RETURN, info.typeId, false);
				useStackAllocator = true;
				break;
			case RawTypeId::StringUtf16:
				_mov(TARGET_1, source, RawTypeId::Void, false);
				mov(RDX, info.numberOnUsing);
				CALL(js2np_utf16);
				_mov(target, TARGET_RETURN, info.typeId, false);
				break;
			case RawTypeId::Buffer:
				_mov(TARGET_1, source, RawTypeId::Void, false);
				mov(RDX, info.numberOnUsing);
				CALL(buffer_to_pointer);
				_mov(target, TARGET_RETURN, info.typeId, false);
				break;
			case RawTypeId::Bin64:
				_mov(TARGET_1, source, RawTypeId::Void, false);
				mov(RDX, info.numberOnUsing);
				CALL(bin64);
				_mov(target, TARGET_RETURN, info.typeId, false);
				break;
			case RawTypeId::JsValueRef:
				_mov(target, source, info.typeId, false);
				break;
			case RawTypeId::Void:
				if (target == TARGET_RETURN) break;
			default:
				invalidParamType(info.typeId, info.numberOnMaking);
			}
		}
	};

#undef CALL
#define CALL(funcname) func.call(QwordPtr, RDI, (int32_t)offsetof(FunctionTable, p_##funcname)-(int32_t)centerOfFunctionTable);
#define JUMP(funcname) func.jump(QwordPtr, RDI, (int32_t)offsetof(FunctionTable, p_##funcname)-(int32_t)centerOfFunctionTable);

}

JsValue functionFromNative(const JsArguments& args) throws(JsException, JsValue)
{
	VoidPointer* targetfuncptr;
	JsValue targetfuncjs = args[0];
	JsValue vfoff = targetfuncjs.get(0);

	ParamInfoMaker pimaker(args);
	
	// JsValueRef( * JsNativeFunction)(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);

	int paramsSize = pimaker.countOnCpp * 8;
	int stackSize = paramsSize;
	if (pimaker.structureReturn) stackSize += 0x8; // structureReturn space
	stackSize += 0x8; // temp space space
	if (stackSize < 0x20) stackSize = 0x20; // minimal

	// alignment
	constexpr int alignmentOffset = 8;
	stackSize -= alignmentOffset;
	stackSize = ((stackSize + 0xf) & ~0xf);
	stackSize += alignmentOffset;

	Maker func(pimaker, 512, stackSize, true);
	if (vfoff == undefined)
	{
		targetfuncptr = pointerInstanceOrThrow(targetfuncjs, 1);
	}
	else
	{
		targetfuncptr = nullptr;
		if (pimaker.nullableThis) throw JsException(u"Cannot use nullableThis at the virtual function");
	}
	if (pimaker.countOnCalling != 0)
	{
		func.cmp(R9, (int)(pimaker.countOnCalling + 1));
		func.jz(14);
		func.mov(RDX, pimaker.countOnCalling); // 7 bytes
		func.lea(RCX, R9, -1); // 4 bytes
		JUMP(getout_invalid_parameter_count); // 3 bytes
	}
	func.mov(RSI, R8);
	func.lea(RBP, RSP, - func.stackSize + paramsSize);

	if (pimaker.structureReturn)
	{
		func.offsetForStructureReturn = func.stackSize - paramsSize - 8;
	}

	if (pimaker.countOnCpp > 1)
	{
		constexpr int32_t stackSizeForConvert = 0x20;
		func.sub(RSP, func.stackSize + stackSizeForConvert);

		uint32_t last = pimaker.countOnCpp - 1;
		int32_t offset = -paramsSize;
		for (dword i = 0; i < pimaker.countOnCpp; i++)
		{
			ParamInfo info = pimaker.getInfo(i);
			func.jsToNative(info,
				i != last ? TargetInfo(RBP, offset) : i < 4 ? TargetInfo(regMap[i], fregMap[i]) : TargetInfo(RBP, offset),
				TargetInfo(RSI, info.numberOnUsing * 8));
			offset += 8;
		}

		if (func.useStackAllocator)
		{
			func.mov(RAX, (qword)&last_allocate);
			func.or_(QwordPtr, RAX, 0, 1);
		}

		func.add(RSP, stackSizeForConvert);

		// paramCountOnCpp >= 2
		if (pimaker.typeIds[0] == RawTypeId::Float)
		{
			func.movsd(XMM0, QwordPtr, RSP, 0);
		}
		else
		{
			func.mov(RCX, QwordPtr, RSP, 0);
		}
		if (pimaker.countOnCpp >= 3)
		{
			if (pimaker.typeIds[1] == RawTypeId::Float)
			{
				func.movsd(XMM1, QwordPtr, RSP, 8);
			}
			else
			{
				func.mov(RDX, QwordPtr, RSP, 8);
			}
			if (pimaker.countOnCpp >= 4)
			{
				if (pimaker.typeIds[2] == RawTypeId::Float)
				{
					func.movsd(XMM2, QwordPtr, RSP, 16);
				}
				else
				{
					func.mov(R8, QwordPtr, RSP, 16);
				}
				if (pimaker.countOnCpp >= 5)
				{
					if (pimaker.typeIds[3] == RawTypeId::Float)
					{
						func.movsd(XMM3, QwordPtr, RSP, 24);
					}
					else
					{
						func.mov(R9, QwordPtr, RSP, 24);
					}
				}
			}
		}
	}
	else
	{
		func.sub(RSP, func.stackSize);

		if (pimaker.countOnCpp != 0)
		{
			ParamInfo pi = pimaker.getInfo(0);
			func.jsToNative(pi, TARGET_1, TargetInfo(RSI, pi.numberOnUsing * 8));
		}

		if (func.useStackAllocator)
		{
			func.mov(RAX, (qword)&last_allocate);
			func.or_(QwordPtr, RAX, 0, 1);
		}
	}

	if (targetfuncptr != nullptr)
	{
		func.call(targetfuncptr->getAddressRaw(), RAX);
	}
	else
	{
		int32_t funcoff = vfoff.cast<int>();
		int32_t thisoff = targetfuncjs.get(1).cast<int>();
		func.mov(RAX, QwordPtr, RCX, thisoff);
		func.call(QwordPtr, RAX, funcoff);
	}

	JsValue retType = args[1];
	RawTypeId retTypeCode = getRawTypeId(PARAMNUM_RETURN, retType);
	if (retTypeCode != RawTypeId::Float)
	{
		func.mov(QwordPtr, RBP, RAX);
	}
	else
	{
		func.movsd(QwordPtr, RBP, 0, XMM0);
	}
	if (func.useStackAllocator)
	{
		func.mov(RDX, (qword)&last_allocate);
		func.xor_(QwordPtr, RDX, 0, 1);
		CALL(stack_free_all);
	}

	func.lea(RCX, RBP, 8);
	byte* jumplen = func.end();
	if (pimaker.structureReturn)
	{
		func.mov(RAX, QwordPtr, RBP, func.stackSize - paramsSize - 8);
	}
	else
	{
		func.nativeToJs(pimaker.getInfo(-1), TARGET_RETURN, TargetInfo(RBP, 0));
	}

	void* funcptr = func.pointer();
	JsValueRef funcref;
	JsErrorCode err = JsCreateFunction((JsNativeFunction)funcptr, nullptr, &funcref);
	if (err != JsNoError) _throwJsrtError(err);
	JsValue funcout = (JsRawData)funcref;
	if (targetfuncptr != nullptr)
	{
		funcout.set(s_field->pointer, targetfuncjs);
	}
	return funcout;
}
JsValue functionToNative(const JsArguments& args) throws(JsException, JsValue)
{
	ParamInfoMaker pimaker(args);

	JsValue jsfunc = args[0];
	JsAddRef(jsfunc.getRaw(), nullptr);
	checkTypeIsFunction(jsfunc, 1);

	int paramsSize = pimaker.countOnCalling * 8 + 8; // params + this
	int stackSize = paramsSize;
	stackSize += 0x8; // temp space
	stackSize += 0x20; // calling space (use stack through ending)
	if (stackSize < 0x20) stackSize = 0x20; // minimal

	// alignment
	constexpr int alignmentOffset = 8;
	stackSize -= alignmentOffset;
	stackSize = ((stackSize + 0xf) & ~0xf);
	stackSize += alignmentOffset;

	Maker func(pimaker, 512, stackSize, false);
	// 0x20~0x28 - return address
	// 0x00~0x20 - pushed data
	func.lea(RBP, RSP, 0x28);

	uint32_t activeRegisters = mint(pimaker.countOnCpp, 4U);
	if (activeRegisters > 1)
	{
		for (int i = 1; i < (int)activeRegisters; i++)
		{
			int offset = i * 8;
			if (pimaker.typeIds[i] == RawTypeId::Float)
			{
				FloatRegister freg = fregMap[i];
				func.movsd(QwordPtr, RBP, offset, freg);
			}
			else
			{
				Register reg = regMap[i];
				func.mov(QwordPtr, RBP, offset, reg);
			}
		}
	}

	func.lea(RSI, RSP, -func.stackSize+0x20); // without calling stack
	func.sub(RSP, func.stackSize);

	int offset = 0;
	if (!pimaker.useThis)
	{
		JsValueRef global = JsRuntime::global().getRaw();
		func._mov(TargetInfo(RSI, 0), (uint64_t)global);
	}

	uint32_t last = pimaker.countOnCpp - 1;
	for (uint32_t i = 0; i < pimaker.countOnCpp; i++)
	{
		ParamInfo info = pimaker.getInfo(i);
		if (i == 0)
		{
			func.nativeToJs(info, TargetInfo(RSI, info.numberOnUsing * 8), TARGET_1);
		}
		else
		{
			func.nativeToJs(info, TargetInfo(RSI, info.numberOnUsing * 8), TargetInfo(RBP, offset));
		}
		offset += 8;
	}

	func.mov(RCX, (qword)jsfunc.getRaw());
	func.lea(RDX, RSP, 0x20);
	func.mov(R8, pimaker.countOnCalling+1);
	func.mov(R9, RBP);
	CALL(JsCallFunction);

	func.test(RAX, RAX);
	func.jz(6);
	func.mov(RCX, RAX);
	CALL(getout);

	func.jsToNative(pimaker.getInfo(-1), TARGET_RETURN, TargetInfo(RBP, 0));

	JsValue resultptr = VoidPointer::newInstanceRaw({});
	resultptr.getNativeObject<VoidPointer>()->setAddressRaw(func.pointer());
	return resultptr;
}

namespace
{
	namespace NativeCaller
	{
		static intptr_t getParameterValue(const JsValue& value, StackGC* gc) throws(JsException)
		{
			JsType type = value.getType();
			switch (type)
			{
			case JsType::Undefined: return 0;
			case JsType::Null: return 0;
			case JsType::Boolean: return value.as<bool>() ? 1 : 0;
			case JsType::Integer: return value.as<int>();
			case JsType::Float:
			{
				int res = value.as<int>();
				if ((double)value.as<int>() != value.as<double>())
				{
					throw JsException(u"non-supported type: float number");
				}
				return res;
			}
			case JsType::String:
			{
				return (intptr_t)value.as<Text16>().data();
			}
			case JsType::Function:
				throw JsException(u"non-supported type: function");
			case JsType::Object:
			{
				VoidPointer* ptr = value.getNativeObject<VoidPointer>();
				if (ptr) return (intptr_t)ptr->getAddressRaw();
				throw JsException(TSZ16() << u"non-supported type: object, " << JsValue(value.toString()).cast<Text16>());
			}
			case JsType::ArrayBuffer:
			case JsType::TypedArray:
			case JsType::DataView:
				return (intptr_t)value.getBuffer().data();
			default:
				throw JsException(TSZ16() << u"unknown javascript type: " << (int)type);
			}
		}

		template <size_t ... idx>
		struct Expander
		{
			template <size_t idx>
			using param = intptr_t;

			static intptr_t call(void* fn, const JsArguments& args) throws(JsException)
			{
				StackGC gc;
				return ((intptr_t(*)(param<idx>...))fn)(getParameterValue(args[idx], &gc) ...);
			}
		};

		template <size_t size>
		struct Call
		{
			static NativePointer* call(void* fn, const JsArguments& args) throws(...)
			{
				if (args.size() != size) return Call<size - 1>::call(fn, args);
				using caller = typename meta::make_numlist_counter<size>::template expand<Expander>;
				NativePointer* ptr = NativePointer::newInstance();
				intptr_t ret = caller::call(fn, args);
				ptr->setAddressRaw((void*)ret);

				return ptr;
			}
		};

		template <>
		struct Call<(size_t)-1>
		{
			static NativePointer* call(void* fn, const JsArguments& args) throws(...)
			{
				unreachable();
			}
		};

		template <size_t size>
		static NativePointer* call(void* fn, const JsArguments& args) throws(JsException)
		{
			return Call<size>::call(fn, args);
		}

		JsValue makefunc(void* fn) noexcept
		{
			JsValue func = JsFunction::make([fn](const kr::JsArguments& arguments) {
				JsAssert(arguments.size(), arguments.size() <= 16);
				return NativeCaller::call<16>(fn, arguments);
				});
			NativePointer* addr = NativePointer::newInstance();
			addr->setAddressRaw(fn);
			func.set(s_field->pointer, addr);
			return func;
		}
	}
};

JsValue functionFromNativeOld(StaticPointer* pointer) throws(JsException)
{
	if (pointer == nullptr) throw JsException(u"argument must be *Pointer");
	return NativeCaller::makefunc(pointer->getAddressRaw());
}

kr::JsValue getMakeFuncNamespace() noexcept
{
	JsValue makefunc = JsNewObject;
	makefunc.setMethodRaw(u"np", functionToNative);
	makefunc.setMethodRaw(u"js", functionFromNative);
	makefunc.setMethod(u"js_old", functionFromNativeOld);
	makefunc.set(u"js2np", s_field->js2np);
	makefunc.set(u"np2js", s_field->np2js);
	return makefunc;
}