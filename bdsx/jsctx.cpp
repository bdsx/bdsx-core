#include "stdafx.h"
#include "jsctx.h"
#include "nodegate.h"

#include <KR3/win/windows.h>
#include <conio.h>

#define USE_EDGEMODE_JSRT
#include <jsrt.h>

using namespace kr;

namespace
{
	JsContextRef getJsrtContext() noexcept
	{
		JsContextRef ctx;
		JsRuntimeRef runtime;
		JsGetCurrentContext(&ctx);
		if (ctx == nullptr)
		{
			cerr << "[BDSX] The chakra context is not found" << endl;
			cerr << "[BDSX] Failed to run BDSX" << endl;
			terminate(-1);
		}
		JsGetRuntime(ctx, &runtime);
		JsRuntime::setRuntime(runtime);
		return ctx;
	}
}

MainContext::MainContext() noexcept
	:JsContext(getJsrtContext())
{
	enter();

	try
	{
		JsScope _scope;
		JsValue array = JsRuntime::run(u"[process._tickCallback, function(){console.log.apply(console, arguments)}, function(){console.error.apply(console, arguments)}]");
		m_tickCallback = array.get(0);
		m_console_log = array.get(1);
		m_console_error = array.get(2);
	}
	catch (JsException& err)
	{
		Text16 tx = err.toString();
		cerr << (Utf16ToAnsi)tx << endl;
		cerr << "[BDSX] basic node functions are not found" << endl;
		cerr << "[BDSX] Failed to run BDSX" << endl;
		terminate(-1);
	}
	exit();
}
MainContext::~MainContext() noexcept
{
	{
		JsScope _scope;
		m_tickCallback = JsValue();
		m_console_log = JsValue();
		m_console_error = JsValue();
	}
	JsContext::_cleanStackCounter();
}

void MainContext::log(kr::Text16 tx) noexcept
{
	try
	{
		JsValue(m_console_log).call(tx);
	}
	catch (JsException& err)
	{
		cerr << (Utf16ToAnsi)err.toString() << endl;
	}
}
void MainContext::error(kr::Text16 tx) noexcept
{
	try
	{
		JsValue(m_console_error).call(tx);
	}
	catch (JsException& err)
	{
		cerr << (Utf16ToAnsi)err.toString() << endl;
	}
}
void MainContext::fireError(const kr::JsRawData& err) noexcept
{
	try
	{
		JsValue onError = m_onError;
		if (!onError.isEmpty()) {
			onError(err);
		}
		return;
	}
	catch (JsException&)
	{
	}

	try
	{
		JsValue stack = err.getByProperty(u"stack");
		if (stack.abstractEquals(nullptr)) stack = err.toString();
		g_ctx->error(stack.cast<Text16>());
	}
	catch (JsException& err)
	{
		cerr << (Utf16ToAnsi)err.toString() << endl;
	}
	catch (...)
	{
		cerr << "[Error in error]" << endl;
	}
}
void MainContext::fireError(const kr::JsException& err) noexcept
{
	fireError(err.getValue());
}
JsValue MainContext::setOnError(const JsValue& onError) noexcept {
	JsValue old = (JsValue)(m_onError);
	m_onError = onError;
	return old;
}
JsValue MainContext::getOnError() noexcept {
	return m_onError;
}
void MainContext::catchException() noexcept {
	nodegate::catchException();
}
void MainContext::_tickCallback() noexcept
{
	try
	{
		JsValue(m_tickCallback).call();
	}
	catch (JsException& err)
	{
		fireError(err);
	}
}

Manual<MainContext> g_ctx;

JsContextRef getBdsxJsContextRef() noexcept
{
	return g_ctx->getRaw();
}
void nodegate::_tickCallback() noexcept
{
	g_ctx->_tickCallback();
}
void nodegate::fireError(JsValueRef err) noexcept
{
	JsValue jserr((JsRawData)err);
	g_ctx->fireError(jserr);
}
