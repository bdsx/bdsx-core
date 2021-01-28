#include "stdafx.h"
#include "jshook.h"
#include "jsctx.h"

#include <KRWin/hook.h>
#include <KRWin/handle.h>
#include <KR3/util/wide.h>
#include <KR3/parser/jsonparser.h>

#define USE_EDGEMODE_JSRT
#include <jsrt.h>

using namespace kr;

namespace
{
	struct DeferField
	{
		JsPersistent onError;
		Map<Text, AText> uuidToPackPath;
	};
	Deferred<DeferField> s_field(JsRuntime::initpack);

	void fireError(JsRawData err) noexcept
	{
		try
		{
			JsValue onError = s_field->onError;
			onError(err);
		}
		catch (JsException& err)
		{
			g_ctx->error(err);
		}
	}
	void catchException() noexcept
	{
		JsValueRef exception;
		if (JsGetAndClearException(&exception) == JsNoError)
		{
			JsScope scope;
			fireError((JsRawData)exception);
		}
	}

	JsErrorCode CALLBACK JsCreateRuntimeHook(
		JsRuntimeAttributes attributes,
		JsThreadServiceCallback threadService,
		JsRuntimeHandle* runtime) noexcept
	{
		*runtime = JsRuntime::getRaw();
		if (*runtime == JS_INVALID_REFERENCE) return JsErrorOutOfMemory;

		JsonParser parser(File::open(u"valid_known_packs.json"));
		parser.array([&](size_t idx) {
			AText uuid;
			AText path;
			parser.fields([&](JsonField& field) {
				field("uuid", &uuid);
				field("path", &path);
				});
			if (path == nullptr) return;
			if (uuid == nullptr) return;

			s_field->uuidToPackPath[uuid] = move(path);
			});
		return JsNoError;
	}
	JsErrorCode CALLBACK JsDisposeRuntimeHook(JsRuntimeHandle runtime) noexcept
	{
		return JsNoError;
	}
	JsErrorCode CALLBACK JsCreateContextHook(JsRuntimeHandle runtime, JsContextRef* newContext) noexcept
	{
		*newContext = g_ctx->getRaw();
		if (*newContext == JS_INVALID_REFERENCE) return JsErrorOutOfMemory;
		return JsNoError;
	}
	JsErrorCode CALLBACK JsSetCurrentContextHook(JsContextRef context) noexcept
	{
		return JsNoError;
	}

	JsErrorCode CALLBACK JsRunScriptHook(
		const wchar_t* script,
		JsSourceContext sourceContext,
		const wchar_t* sourceUrl,
		JsValueRef* result) noexcept
	{
		constexpr size_t UUID_LEN = 36;
		auto path = (Text16)unwide(sourceUrl);
		if (path.size() >= UUID_LEN)
		{
			TText uuid = TText::concat(toUtf8(path.cut(UUID_LEN)));
			auto iter = s_field->uuidToPackPath.find(uuid);
			if (iter != s_field->uuidToPackPath.end())
			{
				path.subarr_self(UUID_LEN);
				Text16 rpath = path.subarr(path.find_e(u'/'));
				pcstr16 remove_end = rpath.find_r(u'_');
				if (remove_end != nullptr) rpath.cut_self(remove_end);

				TText16 newpath = TText16::concat(u"./", utf8ToUtf16(iter->second), u"/scripts", rpath, nullterm);

				{
					JsScope _scope;
					try
					{
						TText source = File::openAsArray<char>(newpath.data());
						TText16 source16 = (Utf8ToUtf16)source;
						JsRuntime::run((Text16)newpath, (Text16)source16);
					}
					catch (JsException& e)
					{
						fireError(e.getValue());
					}

					g_ctx->_tickCallback();
				}
				// JsErrorCode err = JsRunScript(script, sourceContext, wide(newpath.data()), result);
				// if (err != JsNoError) catchException();
				return JsNoError;
			}
		}
		JsErrorCode err = JsRunScript(script, sourceContext, sourceUrl, result);
		if (err != JsNoError) catchException();
		{
			JsScope _scope;

			g_ctx->_tickCallback();
		}
		return err;
	}
	JsErrorCode CALLBACK JsCallFunctionHook(
		JsValueRef function,
		JsValueRef* arguments,
		unsigned short argumentCount,
		JsValueRef* result) noexcept
	{
		JsErrorCode err = JsCallFunction(function, arguments, argumentCount, result);
		if (err != JsNoError) catchException();

		g_ctx->_tickCallback();
		return err;
	}
	JsErrorCode CALLBACK JsStartDebuggingHook() noexcept
	{
		return JsNoError;
	}

	JsErrorCode CALLBACK JsSetPropertyHook(
		JsValueRef object,
		JsPropertyIdRef propertyId,
		JsValueRef value,
		bool useStrictRules)
	{
		const wchar_t* name;
		JsGetPropertyNameFromId(propertyId, &name);
		_assert((Text16)unwide(name) != u"console");
		return JsSetProperty(object, propertyId, value, useStrictRules);
	}

	JsErrorCode CALLBACK JsSetPromiseContinuationCallbackHook(
		JsPromiseContinuationCallback promiseContinuationCallback,
		void* callbackState)
	{
		return JsNoError;
	}

	void init(kr::JsValue onError) noexcept
	{
		s_field->onError = onError;

		kr::hook::IATModule chakra(win::Module::current(), "chakra.dll");
		chakra.hooking("JsCreateContext", JsCreateContextHook);
		chakra.hooking("JsSetCurrentContext", JsSetCurrentContextHook);
		chakra.hooking("JsCreateRuntime", JsCreateRuntimeHook);
		chakra.hooking("JsDisposeRuntime", JsDisposeRuntimeHook);
		chakra.hooking("JsRunScript", JsRunScriptHook);
		chakra.hooking("JsCallFunction", JsCallFunctionHook);
		chakra.hooking("JsStartDebugging", JsStartDebuggingHook);
		chakra.hooking("JsSetPromiseContinuationCallback", JsSetPromiseContinuationCallbackHook);
	#ifndef NDEBUG
		chakra.hooking("JsSetProperty", JsSetPropertyHook);
	#endif
	}

}


JsValue getJsHookNamespace() noexcept
{
	JsValue jshook = JsNewObject;
	jshook.setMethod(u"init", init);
	jshook.setMethod(u"setOnError", [](JsValue onError) { 
		JsValue old = (JsValue)(s_field->onError);
		s_field->onError = onError;
		return old;
		});
	jshook.setMethod(u"getOnError", [] { return (JsValue)s_field->onError; });
	jshook.setMethod(u"fireError", fireError);
	return jshook;
}
