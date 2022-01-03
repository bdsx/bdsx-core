#pragma once

#include "voidpointer.h"

class NativePointer;
template <typename CHR> struct String;

class StaticPointer :public kr::JsObjectT<StaticPointer, VoidPointer>
{
public:
	static constexpr const char16_t className[] = u"StaticPointer";
	static constexpr bool global = false;

	StaticPointer(const kr::JsArguments& args) noexcept;

	bool getBoolean(int offset) throws(kr::JsException);
	uint8_t getUint8(int offset) throws(kr::JsException);
	uint16_t getUint16(int offset) throws(kr::JsException);
	uint32_t getUint32(int offset) throws(kr::JsException);
	double getUint64AsFloat(int offset) throws(kr::JsException);
	int8_t getInt8(int offset) throws(kr::JsException);
	int16_t getInt16(int offset) throws(kr::JsException);
	int32_t getInt32(int offset) throws(kr::JsException);
	double getInt64AsFloat(int offset) throws(kr::JsException);
	float getFloat32(int offset) throws(kr::JsException);
	double getFloat64(int offset) throws(kr::JsException);

	kr::JsValue getNullablePointerAs(kr::JsValue ptrclass, int offset) throws(kr::JsException);
	kr::JsValue getNullablePointer(int offset) throws(kr::JsException);
	kr::JsValue getPointerAs(kr::JsValue ptrclass, int offset) throws(kr::JsException);
	kr::JsValue getPointer(int offset) throws(kr::JsException);
	kr::JsValue getString(kr::JsValue bytes, int offset, int encoding) throws(kr::JsException);
	kr::JsValue getBuffer(int bytes, int offset) throws(kr::JsException);
	kr::TText16 getCxxString(int offset, int encoding) throws(kr::JsException);

	void fill(int bytevalue, int bytes, int offset) throws(kr::JsException);
	void copyFrom(VoidPointer* from, int bytes, int this_offset, int from_offset) throws(kr::JsException);
	void setBoolean(bool v, int offset) throws(kr::JsException);
	void setUint8(uint8_t v, int offset) throws(kr::JsException);
	void setUint16(uint16_t v, int offset) throws(kr::JsException);
	void setUint32(uint32_t v, int offset) throws(kr::JsException);
	void setUint64WithFloat(double v, int offset) throws(kr::JsException);
	void setInt8(int8_t v, int offset) throws(kr::JsException);
	void setInt16(int16_t v, int offset) throws(kr::JsException);
	void setInt32(int32_t v, int offset) throws(kr::JsException);
	void setInt64WithFloat(double v, int offset) throws(kr::JsException);
	void setFloat32(float v, int offset) throws(kr::JsException);
	void setFloat64(double v, int offset) throws(kr::JsException);
	void setPointer(VoidPointer* v, int offset) throws(kr::JsException);
	int setString(kr::JsValue buffer, int offset, int encoding) throws(kr::JsException);
	void setBuffer(kr::JsValue buffer, int offset) throws(kr::JsException);
	void setCxxString(kr::Text16 text, int offset, int encoding) throws(kr::JsException);
	void setInt32To64WithZero(int32_t v, int offset) throws(kr::JsException);
	void setFloat32To64WithZero(double v, int offset) throws(kr::JsException);

	int interlockedIncrement16(int offset) throws(kr::JsException);
	int interlockedIncrement32(int offset) throws(kr::JsException);
	kr::JsValue interlockedIncrement64(int offset) throws(kr::JsException);

	int interlockedDecrement16(int offset) throws(kr::JsException);
	int interlockedDecrement32(int offset) throws(kr::JsException);
	kr::JsValue interlockedDecrement64(int offset) throws(kr::JsException);

	int interlockedCompareExchange8(int exchange, int compared, int offset) throws(kr::JsException);
	int interlockedCompareExchange16(int exchange, int compared, int offset) throws(kr::JsException);
	int interlockedCompareExchange32(int exchange, int compared, int offset) throws(kr::JsException);
	kr::JsValue interlockedCompareExchange64(kr::Text16 exchange, kr::Text16 compared, int offset) throws(kr::JsException);

	kr::JsValue getBin(int words, int offset) throws(kr::JsException);
	void setBin(kr::Text16 buffer, int offset) throws(kr::JsException);
	kr::JsValue getBin64(int offset) throws(kr::JsException);

	kr::JsValue getJsValueRef(int offset) throws(kr::JsException);
	void setJsValueRef(kr::JsValue v, int offset) throws(kr::JsException);

	static void initMethods(kr::JsClassT<StaticPointer>* cls) noexcept;

private:

	template <typename T>
	T _getas(int offset) throws(kr::JsException);
	template <typename T>
	void _setas(T value, int offset) throws(kr::JsException);

};

class AllocatedPointer :public kr::JsObjectT<AllocatedPointer, StaticPointer>
{
public:
	static constexpr const char16_t className[] = u"AllocatedPointer";
	static constexpr bool global = false;

	AllocatedPointer(const kr::JsArguments& args) noexcept;

	static kr::JsObject* _allocate(const kr::JsArguments& args) throws(JsException);
	static void initMethods(kr::JsClassT<AllocatedPointer>* cls) noexcept;

};

ATTR_NORETURN void accessViolation(const void* address) throws(kr::JsException);
