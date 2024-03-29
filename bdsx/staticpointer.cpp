#include "stdafx.h"
#include "staticpointer.h"

#include "nativepointer.h"
#include "encoding.h"
#include "vcstring.h"

#include <KR3/win/handle.h>

using namespace kr;

ATTR_NORETURN void accessViolation(const void* address) throws(JsException)
{
	throw JsException((Text16)(TSZ16() << u"Access Violation: " << address));
}

StaticPointer::StaticPointer(const JsArguments& args) noexcept
	:JsObjectT(args)
{
	StaticPointer* other = args.at<StaticPointer*>(0);
	if (other) m_address = other->m_address;
	else m_address = nullptr;
}

bool StaticPointer::getBoolean(int offset) throws(JsException)
{
	return _getas<bool>(offset);
}
uint8_t StaticPointer::getUint8(int offset) throws(JsException)
{
	return _getas<uint8_t>(offset);
}
uint16_t StaticPointer::getUint16(int offset) throws(JsException)
{
	return _getas<uint16_t>(offset);
}
uint32_t StaticPointer::getUint32(int offset) throws(JsException)
{
	return _getas<uint32_t>(offset);
}
double StaticPointer::getUint64AsFloat(int offset) throws(JsException)
{
	return (double)_getas<uint64_t>(offset);
}
int8_t StaticPointer::getInt8(int offset) throws(JsException)
{
	return _getas<int8_t>(offset);
}
int16_t StaticPointer::getInt16(int offset) throws(JsException)
{
	return _getas<int16_t>(offset);
}
int32_t StaticPointer::getInt32(int offset) throws(JsException)
{
	return _getas<int32_t>(offset);
}
double StaticPointer::getInt64AsFloat(int offset) throws(JsException)
{
	return (double)_getas<int64_t>(offset);
}
float StaticPointer::getFloat32(int offset) throws(JsException)
{
	return _getas<float>(offset);
}
double StaticPointer::getFloat64(int offset) throws(JsException)
{
	return _getas<double>(offset);
}
JsValue StaticPointer::getNullablePointerAs(JsValue ptrclass, int offset) throws(JsException)
{
	void* ptrv = _getas<void*>(offset);
	if (ptrv == nullptr) return nullptr;

	JsValue instance = ((JsClass)ptrclass).newInstanceRaw({});
	VoidPointer* ptr = instance.getNativeObject<VoidPointer>();
	if (ptr == nullptr) throw JsException(u"Need a pointer constructor at the first parameter");
	ptr->setAddressRaw(ptrv);
	return instance;
}
JsValue StaticPointer::getNullablePointer(int offset) throws(JsException)
{
	void* ptrv = _getas<void*>(offset);
	if (ptrv == nullptr) return nullptr;
	JsValue instance = NativePointer::newInstanceRaw({});
	VoidPointer* ptr = instance.getNativeObject<NativePointer>();
	ptr->setAddressRaw(ptrv);
	return instance;
}
JsValue StaticPointer::getPointerAs(JsValue ptrclass, int offset) throws(JsException)
{
	void* ptrv = _getas<void*>(offset);
	JsValue instance = ((JsClass)ptrclass).newInstanceRaw({});
	VoidPointer* ptr = instance.getNativeObject<VoidPointer>();
	if (ptr == nullptr) throw JsException(u"Need a pointer constructor at the first parameter");
	ptr->setAddressRaw(ptrv);
	return instance;
}
JsValue StaticPointer::getPointer(int offset) throws(JsException)
{
	void* ptrv = _getas<void*>(offset);
	JsValue instance = NativePointer::newInstanceRaw({});
	VoidPointer* ptr = instance.getNativeObject<NativePointer>();
	ptr->setAddressRaw(ptrv);
	return instance;
}
JsValue StaticPointer::getString(JsValue bytes, int offset, int encoding) throws(JsException)
{
	if (encoding == ExEncoding::UTF16)
	{
		pstr16 str = (pstr16)(m_address + offset);
		try
		{
			if (bytes.abstractEquals(nullptr))
			{
				return Text16(str, mem16::find(str, '\0'));
			}
			else
			{
				return Text16(str, bytes.cast<int>());
			}
		}
		catch (...)
		{
			accessViolation(str);
		}
	}
	else if (encoding == ExEncoding::BUFFER)
	{
		return getBuffer(bytes.cast<int>(), offset);
	}
	else
	{
		pstr str = (pstr)(m_address + offset);
		try
		{
			TText16 text;
			Text src;
			if (bytes.abstractEquals(nullptr))
			{
				src = Text(str, mem::find(str, '\0'));
			}
			else
			{
				src = Text(str, bytes.cast<int>());
			}
			Charset cs = (Charset)encoding;
			CHARSET_CONSTLIZE(cs,
				text << (MultiByteToUtf16<cs>)src;
			);
			return text;
		}
		catch (...)
		{
			accessViolation(str);
		}
	}
}
JsValue StaticPointer::getBuffer(int bytes, int offset) throws(JsException)
{
	byte* p = m_address + offset;
	JsValue value = JsNewTypedArray(JsTypedType::Uint8, (uint32_t)bytes);
	try
	{
		value.getBuffer().subcopy(Buffer(p, (uint32_t)bytes));
	}
	catch (...)
	{
		accessViolation(p);
	}
	return value;
}
TText16 StaticPointer::getCxxString(int offset, int encoding) throws(JsException)
{
	String<char>* str = (String<char>*)(m_address + offset);
	TText16 text;
	try
	{
		Charset cs = (Charset)encoding;
		CHARSET_CONSTLIZE(cs,
			text << (MultiByteToUtf16<cs>)Text(str->data(), str->size);
		);
		return text;
	}
	catch (...)
	{
		accessViolation(str);
	}
}

void StaticPointer::fill(int bytevalue, int bytes, int offset) throws(kr::JsException)
{
	try
	{
		memset(m_address, bytevalue, (uint32_t)bytes);
	}
	catch (...)
	{
		accessViolation(m_address);
	}
}
void StaticPointer::copyFrom(VoidPointer* from, int bytes, int this_offset, int from_offset) throws(kr::JsException)
{
	if (from == nullptr) throw JsException(u"1st argument must be a Pointer instance");
	try
	{
		memcpy(m_address + this_offset, (byte*)from->getAddressRaw() + from_offset, (uint32_t)bytes);
	}
	catch (...)
	{
		accessViolation(m_address);
	}
}
void StaticPointer::copyTo(kr::JsValue buffer, int bytes, int offset) throws(kr::JsException) {
	VoidPointer* ptr = buffer.getNativeObject<VoidPointer>();
	if (ptr != nullptr) {
		memcpy(ptr->getAddressRaw(), m_address + offset, (uint32_t)bytes);
		return;
	}
	WBuffer buf = buffer.getBuffer();
	if (buf == nullptr) throw JsException(u"argument must be buffer or pointer");
	size_t minv = maxt((size_t)buf.size(), (uint32_t)bytes);
	memcpy(buf.data(), m_address + offset, minv);
}
void StaticPointer::setBoolean(bool v, int offset) throws(JsException)
{
	return _setas(v, offset);
}
void StaticPointer::setUint8(uint8_t v, int offset) throws(JsException)
{
	return _setas(v, offset);
}
void StaticPointer::setUint16(uint16_t v, int offset) throws(JsException)
{
	return _setas(v, offset);
}
void StaticPointer::setUint32(uint32_t v, int offset) throws(JsException)
{
	return _setas(v, offset);
}
void StaticPointer::setUint64WithFloat(double v, int offset) throws(JsException)
{
	return _setas((uint64_t)v, offset);
}
void StaticPointer::setInt8(int8_t v, int offset) throws(JsException)
{
	return _setas(v, offset);
}
void StaticPointer::setInt16(int16_t v, int offset) throws(JsException)
{
	return _setas(v, offset);
}
void StaticPointer::setInt32(int32_t v, int offset) throws(JsException)
{
	return _setas(v, offset);
}
void StaticPointer::setInt64WithFloat(double v, int offset) throws(kr::JsException)
{
	return _setas((int64_t)v, offset);
}
void StaticPointer::setFloat32(float v, int offset) throws(JsException)
{
	return _setas(v, offset);
}
void StaticPointer::setFloat64(double v, int offset) throws(JsException)
{
	return _setas(v, offset);
}
void StaticPointer::setPointer(VoidPointer* v, int offset) throws(JsException)
{
	return _setas(v != nullptr ? v->getAddressRaw() : nullptr, offset);
}
int StaticPointer::setString(JsValue buffer, int offset, int encoding) throws(JsException)
{
	if (encoding == ExEncoding::UTF16)
	{
		Text16 text = buffer.cast<Text16>();
		pstr16 str = (pstr16)(m_address + offset);
		try
		{
			size_t bytes = text.bytes();
			memcpy(str, text.data(), bytes + sizeof(char16_t));
			return (int)bytes;
		}
		catch (...)
		{
			accessViolation(str);
		}
	}
	else if (encoding == ExEncoding::BUFFER)
	{
		void* p = m_address + offset;
		Buffer buf = buffer.getBuffer();
		if (buf == nullptr) throw JsException(u"argument must be buffer");
		try
		{
			size_t size = buf.size();
			memcpy(p, buf.data(), size);
			((char*)p)[size] = '\0';
			return (int)size;
		}
		catch (...)
		{
			accessViolation(p);
		}
	}
	else
	{
		Text16 text = buffer.cast<Text16>();
		pstr str = (pstr)(m_address + offset);
		try
		{
			TSZ mb;
			Charset cs = (Charset)encoding;
			CHARSET_CONSTLIZE(cs,
				mb << Utf16ToMultiByte<cs>(text);
			);

			size_t size = mb.size();
			memcpy(str, mb.data(), size);
			str[size] = '\0';
			return (int)size;
		}
		catch (...)
		{
			accessViolation(str);
		}
	}
}
void StaticPointer::setBuffer(JsValue buffer, int offset) throws(JsException)
{
	void* p = m_address + offset;
	try
	{
		Buffer buf = buffer.getBuffer();
		if (buf == nullptr) return;
		size_t size = buf.size();
		memcpy(p, buf.data(), size);
	}
	catch (...)
	{
		accessViolation(p);
	}
}
void StaticPointer::setCxxString(Text16 text, int offset, int encoding) throws(JsException)
{
	String<char>* str = (String<char>*)(m_address + offset);
	TSZ utf8;
	try
	{
		Charset cs = (Charset)encoding;
		CHARSET_CONSTLIZE(cs,
			utf8 << Utf16ToMultiByte<cs>(text);
		);
		str->assign(utf8);
	}
	catch (...)
	{
		accessViolation(str);
	}
}
void StaticPointer::setInt32To64WithZero(int32_t v, int offset) throws(kr::JsException)
{
	return _setas((uint64_t)(uint32_t)v, offset);
}
void StaticPointer::setFloat32To64WithZero(double v, int offset) throws(kr::JsException) {
	byte* p = m_address + offset;
	try
	{
		*(Unaligned<float>*)(p) = (float)v;
		p += 4;
		*(Unaligned<uint32_t>*)(p) = 0;
	}
	catch (...)
	{
		accessViolation(p);
	}
}

JsValue StaticPointer::getBin(int words, int offset) throws(kr::JsException)
{
	try
	{
		return Text16((char16_t*)(m_address + offset), (size_t)words);
	}
	catch (...)
	{
		accessViolation(m_address);
	}
}
void StaticPointer::setBin(Text16 buffer, int offset) throws(kr::JsException)
{
	try
	{
		memcpy(m_address + offset, buffer.data(), buffer.bytes());
	}
	catch (...)
	{
		accessViolation(m_address);
	}
}
JsValue StaticPointer::getBin64(int offset) throws(kr::JsException)
{
	return getBin(4, offset);
}
int StaticPointer::isoVolatileLoad8(int offset) throws(kr::JsException) {
	byte* p = m_address + offset;
	try
	{
		return __iso_volatile_load8((char*)p);
	}
	catch (...)
	{
		accessViolation(p);
	}
}
int StaticPointer::isoVolatileLoad16(int offset) throws(kr::JsException) {
	byte* p = m_address + offset;
	try
	{
		return __iso_volatile_load16((short*)p);
	}
	catch (...)
	{
		accessViolation(p);
	}
}
int StaticPointer::isoVolatileLoad32(int offset) throws(kr::JsException) {
	byte* p = m_address + offset;
	try
	{
		return __iso_volatile_load32((int*)p);
	}
	catch (...)
	{
		accessViolation(p);
	}
}
kr::JsValue StaticPointer::isoVolatileLoad64(int offset) throws(kr::JsException) {
	byte* p = m_address + offset;
	try
	{
		long long out = __iso_volatile_load64((long long*)p);
		return JsValue(Text16((char16_t*)&out, (char16_t*)&out + 4));
	}
	catch (...)
	{
		accessViolation(p);
	}
}

void StaticPointer::isoVolatileStore8(int value, int offset) throws(kr::JsException) {
	byte* p = m_address + offset;
	try
	{
		return __iso_volatile_store8((char*)p, (char)value);
	}
	catch (...)
	{
		accessViolation(p);
	}
}
void StaticPointer::isoVolatileStore16(int value, int offset) throws(kr::JsException) {
	byte* p = m_address + offset;
	try
	{
		return __iso_volatile_store16((short*)p, (short)value);
	}
	catch (...)
	{
		accessViolation(p);
	}
}
void StaticPointer::isoVolatileStore32(int value, int offset) throws(kr::JsException) {
	byte* p = m_address + offset;
	try
	{
		return __iso_volatile_store32((int*)p, value);
	}
	catch (...)
	{
		accessViolation(p);
	}
}
void StaticPointer::isoVolatileStore64(kr::Text16 value, int offset) throws(kr::JsException) {
	if (value.size() < 4) throw JsException(u"the first arg is not 64bits");
	int64_t value_int = *(Unaligned<int64_t>*)value.data();

	byte* p = m_address + offset;
	try
	{
		return __iso_volatile_store64((long long*)p, value_int);
	}
	catch (...)
	{
		accessViolation(p);
	}
}

int StaticPointer::interlockedIncrement16(int offset) throws(kr::JsException)
{
	byte* p = m_address + offset;
	try
	{
		return _InterlockedIncrement16((short*)p);
	}
	catch (...)
	{
		accessViolation(p);
	}
}
int StaticPointer::interlockedIncrement32(int offset) throws(kr::JsException)
{
	byte* p = m_address + offset;
	try
	{
		return _InterlockedIncrement((long*)p);
	}
	catch (...)
	{
		accessViolation(p);
	}
}
JsValue StaticPointer::interlockedIncrement64(int offset) throws(kr::JsException)
{
	byte* p = m_address + offset;
	try
	{
		long long out = _InterlockedIncrement64((long long*)p);
		return JsValue(Text16((char16_t*)&out, (char16_t*)&out+4));
	}
	catch (...)
	{
		accessViolation(p);
	}
}

int StaticPointer::interlockedDecrement16(int offset) throws(kr::JsException)
{
	byte* p = m_address + offset;
	try
	{
		return _InterlockedDecrement16((short*)p);
	}
	catch (...)
	{
		accessViolation(p);
	}
}
int StaticPointer::interlockedDecrement32(int offset) throws(kr::JsException)
{
	byte* p = m_address + offset;
	try
	{
		return _InterlockedDecrement((long*)p);
	}
	catch (...)
	{
		accessViolation(p);
	}
}
JsValue StaticPointer::interlockedDecrement64(int offset) throws(kr::JsException)
{
	byte* p = m_address + offset;
	try
	{
		long long out = _InterlockedDecrement64((long long*)p);
		return JsValue(Text16((char16_t*)&out, (char16_t*)&out + 4));
	}
	catch (...)
	{
		accessViolation(p);
	}
}

int StaticPointer::interlockedCompareExchange8(int exchange, int compared, int offset) throws(kr::JsException)
{
	byte* p = m_address + offset;
	try
	{
		return _InterlockedCompareExchange16((short*)p, exchange, compared);
	}
	catch (...)
	{
		accessViolation(p);
	}
}
int StaticPointer::interlockedCompareExchange16(int exchange, int compared, int offset) throws(kr::JsException)
{
	byte* p = m_address + offset;
	try
	{
		return _InterlockedCompareExchange16((short*)p, exchange, compared);
	}
	catch (...)
	{
		accessViolation(p);
	}
}
int StaticPointer::interlockedCompareExchange32(int exchange, int compared, int offset) throws(kr::JsException)
{
	byte* p = m_address + offset;
	try
	{
		return _InterlockedCompareExchange((long*)p, exchange, compared);
	}
	catch (...)
	{
		accessViolation(p);
	}
}
JsValue StaticPointer::interlockedCompareExchange64(kr::Text16 exchange, kr::Text16 compared, int offset) throws(kr::JsException)
{
	if (exchange.size() < 4) throw JsException(u"the first arg is not 64bits");
	if (compared.size() < 4) throw JsException(u"the second arg is not 64bits");
	int64_t exchange_int = *(Unaligned<int64_t>*)exchange.data();
	int64_t compared_int = *(Unaligned<int64_t>*)compared.data();

	byte* p = m_address + offset;
	try
	{
		long long out = _InterlockedCompareExchange64((long long*)p, exchange_int, compared_int);
		return JsValue(Text16((char16_t*)&out, (char16_t*)&out + 4));
	}
	catch (...)
	{
		accessViolation(p);
	}
}

JsValue StaticPointer::getJsValueRef(int offset) throws(kr::JsException)
{
	return (JsRawData)_getas<JsValueRef>(offset);
}
void StaticPointer::setJsValueRef(JsValue v, int offset) throws(kr::JsException)
{
	_setas(v.getRaw(), offset);
}

void StaticPointer::initMethods(JsClassT<StaticPointer>* cls) noexcept
{
	cls->setMethod(u"getBoolean", &StaticPointer::getBoolean);
	cls->setMethod(u"getUint8", &StaticPointer::getUint8);
	cls->setMethod(u"getUint16", &StaticPointer::getUint16);
	cls->setMethod(u"getUint32", &StaticPointer::getUint32);
	cls->setMethod(u"getUint64AsFloat", &StaticPointer::getUint64AsFloat);
	cls->setMethod(u"getInt8", &StaticPointer::getInt8);
	cls->setMethod(u"getInt16", &StaticPointer::getInt16);
	cls->setMethod(u"getInt32", &StaticPointer::getInt32);
	cls->setMethod(u"getInt64AsFloat", &StaticPointer::getInt64AsFloat);
	cls->setMethod(u"getFloat32", &StaticPointer::getFloat32);
	cls->setMethod(u"getFloat64", &StaticPointer::getFloat64);
	cls->setMethod(u"getNullablePointerAs", &StaticPointer::getNullablePointerAs);
	cls->setMethod(u"getNullablePointer", &StaticPointer::getNullablePointer);
	cls->setMethod(u"getPointerAs", &StaticPointer::getPointerAs);
	cls->setMethod(u"getPointer", &StaticPointer::getPointer);
	cls->setMethod(u"getString", &StaticPointer::getString);
	cls->setMethod(u"getBuffer", &StaticPointer::getBuffer);
	cls->setMethod(u"getCxxString", &StaticPointer::getCxxString);

	cls->setMethod(u"fill", &StaticPointer::fill);
	cls->setMethod(u"copyFrom", &StaticPointer::copyFrom);
	cls->setMethod(u"copyTo", &StaticPointer::copyTo);
	cls->setMethod(u"setBoolean", &StaticPointer::setBoolean);
	cls->setMethod(u"setUint8", &StaticPointer::setUint8);
	cls->setMethod(u"setUint16", &StaticPointer::setUint16);
	cls->setMethod(u"setUint32", &StaticPointer::setUint32);
	cls->setMethod(u"setUint64WithFloat", &StaticPointer::setUint64WithFloat);
	cls->setMethod(u"setInt8", &StaticPointer::setInt8);
	cls->setMethod(u"setInt16", &StaticPointer::setInt16);
	cls->setMethod(u"setInt32", &StaticPointer::setInt32);
	cls->setMethod(u"setInt64WithFloat", &StaticPointer::setInt64WithFloat);
	cls->setMethod(u"setFloat32", &StaticPointer::setFloat32);
	cls->setMethod(u"setFloat64", &StaticPointer::setFloat64);
	cls->setMethod(u"setPointer", &StaticPointer::setPointer);
	cls->setMethod(u"setString", &StaticPointer::setString);
	cls->setMethod(u"setBuffer", &StaticPointer::setBuffer);
	cls->setMethod(u"setCxxString", &StaticPointer::setCxxString);
	cls->setMethod(u"setInt32To64WithZero", &StaticPointer::setInt32To64WithZero);
	cls->setMethod(u"setFloat32To64WithZero", &StaticPointer::setFloat32To64WithZero);
	cls->setMethod(u"setFloat32", &StaticPointer::setFloat32);

	cls->setMethod(u"isoVolatileLoad8", &StaticPointer::isoVolatileLoad8);
	cls->setMethod(u"isoVolatileLoad16", &StaticPointer::isoVolatileLoad16);
	cls->setMethod(u"isoVolatileLoad32", &StaticPointer::isoVolatileLoad32);
	cls->setMethod(u"isoVolatileLoad64", &StaticPointer::isoVolatileLoad64);
	cls->setMethod(u"isoVolatileStore8", &StaticPointer::isoVolatileStore8);
	cls->setMethod(u"isoVolatileStore16", &StaticPointer::isoVolatileStore16);
	cls->setMethod(u"isoVolatileStore32", &StaticPointer::isoVolatileStore32);
	cls->setMethod(u"isoVolatileStore64", &StaticPointer::isoVolatileStore64);

	cls->setMethod(u"interlockedIncrement16", &StaticPointer::interlockedIncrement16);
	cls->setMethod(u"interlockedIncrement32", &StaticPointer::interlockedIncrement32);
	cls->setMethod(u"interlockedIncrement64", &StaticPointer::interlockedIncrement64);
	cls->setMethod(u"interlockedDecrement16", &StaticPointer::interlockedDecrement16);
	cls->setMethod(u"interlockedDecrement32", &StaticPointer::interlockedDecrement32);
	cls->setMethod(u"interlockedDecrement64", &StaticPointer::interlockedDecrement64);
	cls->setMethod(u"interlockedCompareExchange8", &StaticPointer::interlockedCompareExchange8);
	cls->setMethod(u"interlockedCompareExchange16", &StaticPointer::interlockedCompareExchange16);
	cls->setMethod(u"interlockedCompareExchange32", &StaticPointer::interlockedCompareExchange32);
	cls->setMethod(u"interlockedCompareExchange64", &StaticPointer::interlockedCompareExchange64);

	cls->setMethod(u"getJsValueRef", &StaticPointer::getJsValueRef);
	cls->setMethod(u"setJsValueRef", &StaticPointer::setJsValueRef);

	cls->setMethod(u"setBin", &StaticPointer::setBin);
	cls->setMethod(u"getBin", &StaticPointer::getBin);
	cls->setMethod(u"getBin64", &StaticPointer::getBin64);
}

template <typename T>
T StaticPointer::_getas(int offset) throws(JsException)
{
	byte* p = m_address + offset;
	try
	{
		T value = *(Unaligned<T>*)(p);
		return value;
	}
	catch (...)
	{
		accessViolation(p);
	}
}
template <typename T>
void StaticPointer::_setas(T value, int offset) throws(JsException)
{
	byte* p = m_address + offset;
	try
	{
		*(Unaligned<T>*)(p) = value;
	}
	catch (...)
	{
		accessViolation(p);
	}
}

AllocatedPointer::AllocatedPointer(const kr::JsArguments& args) noexcept
	:JsObjectT<AllocatedPointer, StaticPointer>(args)
{
}

JsObject* AllocatedPointer::_allocate(const JsArguments& args) throws(JsException)
{
	int extraSize = args.at<int>(0);
	if (extraSize < 0) throw JsException(u"Invalid size");
	AllocatedPointer* data = newAlignedExtra<AllocatedPointer>(extraSize, args);
	data->setAddressRaw(data+1);
	return data;
}
void AllocatedPointer::initMethods(kr::JsClassT<AllocatedPointer>* cls) noexcept
{
	cls->setStaticMethod(u"fromString", [](JsValue buffer, int encoding) {
		if (encoding == ExEncoding::UTF16)
		{
			Text16 text = buffer.cast<Text16>();
			size_t bytes = text.bytes();
			JsValue allocated = AllocatedPointer::newInstanceRaw({ JsValue(bytes + 2) });
			AllocatedPointer* ptr = allocated.getNativeObject<AllocatedPointer>();
			pstr16 str = (pstr16)ptr->getAddressRaw();
			memcpy(str, text.data(), bytes + sizeof(char16_t));
			return allocated;
		}
		else if (encoding == ExEncoding::BUFFER)
		{
			Buffer buf = buffer.getBuffer();
			if (buf == nullptr) throw JsException(u"argument must be buffer");
			size_t size = buf.size();
			JsValue allocated = AllocatedPointer::newInstanceRaw({ JsValue(size + 1) });
			AllocatedPointer* ptr = allocated.getNativeObject<AllocatedPointer>();
			void* p = ptr->getAddressRaw();
			memcpy(p, buf.data(), size);
			((char*)p)[size] = '\0';
			return allocated;
		}
		else
		{
			Text16 text = buffer.cast<Text16>();
			TSZ mb;
			Charset cs = (Charset)encoding;
			CHARSET_CONSTLIZE(cs,
				mb << Utf16ToMultiByte<cs>(text);
			);

			size_t size = mb.size();
			JsValue allocated = AllocatedPointer::newInstanceRaw({ JsValue(size + 1) });
			AllocatedPointer* ptr = allocated.getNativeObject<AllocatedPointer>();
			pstr str = (pstr)ptr->getAddressRaw();
			memcpy(str, mb.data(), size);
			str[size] = '\0';
			return allocated;
		}
	});
}
