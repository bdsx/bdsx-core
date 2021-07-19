#pragma once

struct SehHandler;
typedef struct _EXCEPTION_POINTERS EXCEPTION_POINTERS;

namespace runtimeError
{
	ATTR_NORETURN void raise(EXCEPTION_POINTERS* exptr) noexcept;
	void setHandler(kr::JsValue listener) noexcept;
	ATTR_NORETURN void fire(JsValueRef error) noexcept;

	kr::Text16 codeToString(unsigned int errorCode) noexcept;
	kr::JsValue getError() throws(kr::JsException);
	kr::JsValue getRuntimeErrorClass() noexcept;
	kr::JsValue getNamespace() noexcept;
}