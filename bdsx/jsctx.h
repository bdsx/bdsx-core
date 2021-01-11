#pragma once

class MainContext: public kr::JsContext
{
public:
	MainContext() noexcept;
	~MainContext() noexcept;

	void log(kr::Text16 tx) noexcept;
	void error(kr::Text16 tx) noexcept;
	void error(const kr::JsException &err) noexcept;
	void _tickCallback() noexcept;

private:
	kr::JsPersistent m_tickCallback;
	kr::JsPersistent m_console_log;
	kr::JsPersistent m_console_error;

};

extern kr::Manual<MainContext> g_ctx;
