#include "stdafx.h"
#include "jsctx.h"

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
void MainContext::error(const kr::JsException& err) noexcept
{
	try
	{
		JsValue stack = err.getValue().get(u"stack");
		if (stack == undefined) stack = err.toString();
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
void MainContext::_tickCallback() noexcept
{
	try
	{
		JsValue(m_tickCallback).call();
	}
	catch (JsException& err)
	{
		error(err);
	}
}

Manual<MainContext> g_ctx;

JsContextRef getBdsxJsContextRef() noexcept
{
	return g_ctx->getRaw();
}
