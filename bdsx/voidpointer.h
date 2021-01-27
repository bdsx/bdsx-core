#pragma once

class NativePointer;

int64_t getBin64(kr::Text16 text) noexcept;

class VoidPointer :public kr::JsObjectT<VoidPointer>
{
public:
	static constexpr const char16_t className[] = u"VoidPointer";
	static constexpr bool global = false;

	VoidPointer(const kr::JsArguments& args) noexcept;

	void setAddressRaw(const void* ptr) noexcept;
	void* getAddressRaw() noexcept;
	int32_t getAddressLow() noexcept;
	int32_t getAddressHigh() noexcept;
	kr::Text16 getAddressBin() noexcept;
	double getAddressAsFloat() noexcept;
	kr::JsValue addressOfThis() noexcept;

	bool equals(VoidPointer* other) noexcept;
	NativePointer* pointer() noexcept;
	NativePointer* add(int32_t lowBits, int32_t highBits) noexcept;
	NativePointer* sub(int32_t lowBits, int32_t highBits) noexcept;
	NativePointer* addBin(kr::Text16 bin64) throws(JsException);
	NativePointer* subBin(kr::Text16 bin64) throws(JsException);
	int32_t subptr(VoidPointer* ptr) throws(JsException);
	
	kr::JsValue as(kr::JsValue cls) throws(JsException);
	kr::JsValue addAs(kr::JsValue cls, int32_t lowBits, int32_t highBits) throws(JsException);
	kr::JsValue subAs(kr::JsValue cls, int32_t lowBits, int32_t highBits) throws(JsException);
	kr::JsValue addBinAs(kr::JsValue cls, kr::Text16 bin64) throws(JsException);
	kr::JsValue subBinAs(kr::JsValue cls, kr::Text16 bin64) throws(JsException);

	bool isNull() noexcept;
	bool isNotNull() noexcept;

	kr::TText16 toString(kr::JsValue radix) throws(JsException);

	static void initMethods(kr::JsClassT<VoidPointer>* cls) noexcept;
	static VoidPointer* make(void* value) noexcept;

protected:
	uint8_t* m_address;

private:

	template <typename T>
	T _getas(int offset) throws(kr::JsException);
	template <typename T>
	void _setas(T value, int offset) throws(kr::JsException);

};
