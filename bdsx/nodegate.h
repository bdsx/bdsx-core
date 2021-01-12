#pragma once

#include <chrono>
#include <KR3/mt/atomicqueue.h>

using std::chrono::high_resolution_clock;
using hd_point_t = high_resolution_clock::time_point;
using hd_dura_t = high_resolution_clock::duration;

namespace nodegate
{
	using dtor_t = void(*)(void*);
	using ctor_t = dtor_t(*)(void*, void*);

	void initNativeModule(void* exports) noexcept;
	void clearNativeModule() noexcept;
	void _tickCallback() noexcept;
	void error(JsValueRef err) noexcept;
	int start(int argc, char** argv) noexcept;
	void loopOnce() noexcept;
	void loop(uint64_t hd_point) noexcept;
}

class AsyncTask :public kr::AtomicQueueNode
{
public:
	void (* const fn)(AsyncTask*);

	AsyncTask(void (* fn)(AsyncTask*)) noexcept;
	~AsyncTask() noexcept override;
	void post() noexcept;

	static void open() noexcept;
	static void close() noexcept;
	static AsyncTask* alloc(void (*cb)(AsyncTask*), size_t size) noexcept;
	static void call(void (*cb)(AsyncTask*)) noexcept;

	template <typename LAMBDA>
	static void post(LAMBDA && lambda) noexcept
	{
		class LambdaAsyncTask:public AsyncTask
		{
		private:
			LAMBDA m_lambda;

		public:
			LambdaAsyncTask(LAMBDA&& lambda) noexcept
				:AsyncTask([](AsyncTask* task){
				static_cast<LambdaAsyncTask*>(task)->m_lambda();
					}), 
				m_lambda(std::forward<LAMBDA>(lambda))
			{
			}
		};

		(_new LambdaAsyncTask(std::forward<LAMBDA>(lambda)))->post();
	}
};

