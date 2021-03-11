#pragma once

#include <KR3/mt/atomicqueue.h>
#include <KR3/win/eventhandle.h>

#include "voidpointer.h"

class MultiThreadQueue :public kr::JsObjectT<MultiThreadQueue, VoidPointer>
{
public:
	static constexpr const char16_t className[] = u"MultiThreadQueue";
	static constexpr bool global = false;
	MultiThreadQueue(const kr::JsArguments& args) throws(kr::JsException);
	void enqueue(void* src) noexcept;
	void dequeue(void* dest) noexcept;
	bool tryDequeue(void* dest) noexcept;
	void enqueueJs(VoidPointer* src) noexcept;
	void dequeueJs(VoidPointer* dest) noexcept;
	bool tryDequeueJs(VoidPointer* dest) noexcept;

	static void initMethods(kr::JsClassT<MultiThreadQueue>* cls) noexcept;

private:
	kr::EventHandle* m_event;
	kr::AtomicQueue m_queue;
	size_t m_itemSize;
};
