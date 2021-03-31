#pragma once

#include "staticpointer.h"

class StructurePointer : public kr::JsObjectT<StructurePointer, StaticPointer>
{
public:
	static constexpr const char16_t className[] = u"StructurePointer";
	static constexpr bool global = false;

	StructurePointer(const kr::JsArguments& args) noexcept;
	void finalize() noexcept override;

	static kr::JsObject* _allocate(const kr::JsArguments& args) throws(kr::JsException);
	static void initMethods(kr::JsClassT<StructurePointer>* cls) noexcept;
	static void clearMethods() noexcept;

private:
	void(*m_finalize)(void*);
};