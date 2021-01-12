#include "stdafx.h"
#include "voidpointer.h"

#include "nativepointer.h"

#include <KR3/util/unaligned.h>

using namespace kr;

VoidPointer::VoidPointer(const JsArguments& args) noexcept
	:JsObjectT(args)
{
	VoidPointer* other = args.at<VoidPointer*>(0);
	if (other) m_address = other->m_address;
	else m_address = nullptr;
}

void VoidPointer::setAddressRaw(const void* ptr) noexcept
{
	m_address = (uint8_t*)ptr;
}
void* VoidPointer::getAddressRaw() noexcept
{
	return m_address;
}
int32_t VoidPointer::getAddressLow() noexcept
{
	return (uint32_t)(uintptr_t)m_address;
}
int32_t VoidPointer::getAddressHigh() noexcept
{
	return (uint32_t)((uintptr_t)m_address >> 32);
}

Text16 VoidPointer::getAddressBin() noexcept
{
	return Text16((char16_t*)&m_address, 4);
}
kr::JsValue VoidPointer::addressOfThis() noexcept
{
	JsValue ptr = NativePointer::newInstance();
	ptr.getNativeObject<NativePointer>()->setAddressRaw(&m_address);
	return ptr;
}

bool VoidPointer::equals(VoidPointer* other) noexcept
{
	if (other == nullptr) return false;
	return m_address == other->m_address;
}
NativePointer* VoidPointer::pointer() noexcept
{
	return NativePointer::newInstance(this);
}
NativePointer* VoidPointer::add(int32_t lowBits, int32_t highBits) noexcept
{
	NativePointer* ptr = NativePointer::newInstance(this);
	ptr->m_address = m_address + (intptr_t)makeqword(lowBits, highBits);
	return ptr;
}
NativePointer* VoidPointer::sub(int32_t lowBits, int32_t highBits) noexcept
{
	NativePointer* ptr = NativePointer::newInstance(this);
	ptr->m_address = m_address - (intptr_t)makeqword(lowBits, highBits);
	return ptr;
}
NativePointer* VoidPointer::addBin(kr::Text16 bin64) throws(JsException)
{
	NativePointer* ptr = NativePointer::newInstance(this);
	ptr->m_address = m_address + getBin64(bin64);
	return ptr;
}
NativePointer* VoidPointer::subBin(kr::Text16 bin64) throws(JsException)
{
	NativePointer* ptr = NativePointer::newInstance(this);
	ptr->m_address = m_address - getBin64(bin64);
	return ptr;
}
int32_t VoidPointer::subptr(VoidPointer* ptr) throws(JsException)
{
	if (ptr == nullptr) throw JsException(u"argument must be *Pointer");
	return intact<int32_t>(m_address - ptr->m_address);
}

JsValue VoidPointer::as(JsValue cls) throws(JsException)
{
	JsValue ptr = ((JsClass)cls).newInstanceRaw({});
	VoidPointer* vptr = ptr.getNativeObject<VoidPointer>();
	if (vptr == nullptr) throw JsException(u"*Pointer class required");
	vptr->setAddressRaw(m_address);
	return ptr;
}
JsValue VoidPointer::addAs(JsValue cls, int32_t lowBits, int32_t highBits) throws(JsException)
{
	JsValue ptr = ((JsClass)cls).newInstanceRaw({});
	VoidPointer* vptr = ptr.getNativeObject<VoidPointer>();
	if (vptr == nullptr) throw JsException(u"*Pointer class required");
	vptr->setAddressRaw(m_address + (((uint64_t)highBits << 32) | (uint64_t)lowBits ));
	return vptr;
}
JsValue VoidPointer::subAs(JsValue cls, int32_t lowBits, int32_t highBits) throws(JsException)
{
	JsValue ptr = ((JsClass)cls).newInstanceRaw({});
	VoidPointer* vptr = ptr.getNativeObject<VoidPointer>();
	if (vptr == nullptr) throw JsException(u"*Pointer class required");
	vptr->setAddressRaw(m_address - (((uint64_t)highBits << 32) | (uint64_t)lowBits));
	return vptr;
}
JsValue VoidPointer::addBinAs(JsValue cls, Text16 bin64) throws(JsException)
{
	JsValue ptr = ((JsClass)cls).newInstanceRaw({});
	VoidPointer* vptr = ptr.getNativeObject<VoidPointer>();
	if (vptr == nullptr) throw JsException(u"*Pointer class required");
	vptr->setAddressRaw(m_address + getBin64(bin64));
	return vptr;
}
JsValue VoidPointer::subBinAs(JsValue cls, Text16 bin64) throws(JsException)
{
	JsValue ptr = ((JsClass)cls).newInstanceRaw({});
	VoidPointer* vptr = ptr.getNativeObject<VoidPointer>();
	if (vptr == nullptr) throw JsException(u"*Pointer class required");
	vptr->setAddressRaw(m_address - getBin64(bin64));
	return vptr;
}

bool VoidPointer::isNull() noexcept
{
	return m_address == nullptr;
}
bool VoidPointer::isNotNull() noexcept
{
	return m_address != nullptr;
}
TText16 VoidPointer::toString(JsValue radix) throws(JsException)
{
	if (radix == undefined)
	{
		TText16 out;
		out << u"0x";
		out << hexf((uintptr_t)m_address, 16);
		return out;
	}
	int radixn = radix.cast<int32_t>();
	if (radixn <= 0) throw JsException(TSZ16() << u"Invalid radix: " << radixn);
	
	TText16 out;
	out << radixf((uintptr_t)m_address, radixn);
	return out;
}

void VoidPointer::initMethods(JsClassT<VoidPointer>* cls) noexcept
{
	cls->setMethod(u"getAddressHigh", &VoidPointer::getAddressHigh);
	cls->setMethod(u"getAddressLow", &VoidPointer::getAddressLow);
	cls->setMethod(u"getAddressBin", &VoidPointer::getAddressBin);
	cls->setMethod(u"addressOfThis", &VoidPointer::addressOfThis);

	cls->setMethod(u"equals", &VoidPointer::equals);
	cls->setMethod(u"add", &VoidPointer::add);
	cls->setMethod(u"sub", &VoidPointer::sub);
	cls->setMethod(u"addBin", &VoidPointer::addBin);
	cls->setMethod(u"subBin", &VoidPointer::subBin);
	cls->setMethod(u"subptr", &VoidPointer::subptr);
	cls->setMethod(u"clone", &VoidPointer::pointer);
	
	cls->setMethod(u"as", &VoidPointer::as);
	cls->setMethod(u"addAs", &VoidPointer::addAs);
	cls->setMethod(u"subAs", &VoidPointer::subAs);
	cls->setMethod(u"addBinAs", &VoidPointer::addBinAs);
	cls->setMethod(u"subBinAs", &VoidPointer::subBinAs);

	cls->setMethod(u"isNull", &VoidPointer::isNull);
	cls->setMethod(u"isNotNull", &VoidPointer::isNotNull);
	cls->setMethod(u"toString", &VoidPointer::toString);
}

template <typename T>
T VoidPointer::_getas(int offset) throws(JsException)
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
void VoidPointer::_setas(T value, int offset) throws(JsException)
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

int64_t getBin64(Text16 text) noexcept
{
	size_t n = text.bytes();
	if (n <= 4)
	{
		if (n <= 2)
		{
			if (n == 0) return 0;
			return (uint16_t) * (Unaligned<uint16_t>*)text.data();
		}
		else
		{
			return (uint32_t) * (Unaligned<uint32_t>*)text.data();
		}
	}
	else
	{
		if (n <= 6)
		{
			uint64_t v = (uint32_t) * (Unaligned<uint32_t>*)text.data();
			v |= (uint64_t)((uint16_t) * (Unaligned<uint16_t>*)(text.data() + 4)) << 32;
			return v;
		}
		else
		{
			return (uint64_t) * (Unaligned<uint64_t>*)text.data();
		}
	}
}

VoidPointer* VoidPointer::make(void* value) noexcept
{
	VoidPointer* ptr = VoidPointer::newInstance();
	ptr->setAddressRaw(value);
	return ptr;
}
