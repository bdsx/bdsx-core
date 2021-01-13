#pragma once

struct SehHandler;
typedef struct _EXCEPTION_POINTERS EXCEPTION_POINTERS;

namespace runtimeError
{
	int raise(EXCEPTION_POINTERS* exptr) noexcept;
	void setHandler(kr::JsValue listener) noexcept;
	ATTR_NORETURN void fire(kr::JsValue error) noexcept;

	kr::Text16 codeToString(unsigned int errorCode) noexcept;
	SehHandler* beginHandler() noexcept;
	void endHandler(SehHandler* old) noexcept;
	kr::JsValue getError() noexcept;
	kr::JsValue getRuntimeErrorClass() noexcept;
	kr::JsValue getNamespace() noexcept;
}