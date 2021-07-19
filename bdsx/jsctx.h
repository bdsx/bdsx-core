#pragma once

class MainContext: public kr::JsContext
{
public:
	MainContext() noexcept;
	~MainContext() noexcept;

	void log(kr::Text16 tx) noexcept;
	void error(kr::Text16 err) noexcept;
	void fireError(const kr::JsRawData &err) noexcept;
	void fireError(const kr::JsException &err) noexcept;
	kr::JsValue setOnError(const kr:: JsValue& onError) noexcept;
	kr::JsValue getOnError() noexcept;
	void catchException() noexcept;
	void _tickCallback() noexcept;

private:
	kr::JsPersistent m_tickCallback;
	kr::JsPersistent m_console_log;
	kr::JsPersistent m_console_error;
	kr::JsPersistent m_onError;

};

extern kr::Manual<MainContext> g_ctx;
