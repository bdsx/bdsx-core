#include "stdafx.h"
#include "mtqueue.h"
#include "staticpointer.h"
#include <KR3/win/windows.h>

using namespace kr;


MultiThreadQueue::MultiThreadQueue(const kr::JsArguments& args) throws(JsException)
	:JsObjectT<MultiThreadQueue, VoidPointer>(args)
{
	setAddressRaw(this);

	int itemSize = args.at<int>(0);
	if (itemSize < 0) throw JsException(u"Invalid item size");
	m_itemSize = itemSize;
	m_event = EventHandle::create(false, false);
}
void MultiThreadQueue::enqueue(void* src) noexcept
{
	AtomicQueueNode* item = reline_new(newAlignedExtra<AtomicQueueNode>(m_itemSize));
	memcpy(item + 1, src, m_itemSize);
	size_t count = m_queue.enqueue(item);
	if (count == 1) m_event->set();
}
void MultiThreadQueue::dequeue(void* dest) noexcept
{
	m_event->wait();
	for (;;)
	{
		auto pair = m_queue.dequeue();
		if (pair.first == nullptr)
		{
			Sleep(0);
			continue;
		}
		memcpy(dest, pair.first + 1, m_itemSize);
		pair.first->release();
		return;
	}
}
void MultiThreadQueue::enqueueJs(VoidPointer* src) noexcept
{
	enqueue(src->getAddressRaw());
}
void MultiThreadQueue::dequeueJs(VoidPointer* dest) noexcept
{
	dequeue(dest->getAddressRaw());
}

void MultiThreadQueue::initMethods(JsClassT<MultiThreadQueue>* cls) noexcept
{
	cls->setMethod(u"enqueue", &MultiThreadQueue::enqueueJs);
	cls->setMethod(u"dequeue", &MultiThreadQueue::dequeueJs);

	void(* enqueue)(MultiThreadQueue * queue, void* src) = [](MultiThreadQueue* queue, void* src) { queue->enqueue(src); };
	cls->set(u"enqueue", VoidPointer::make(enqueue));
	void(* dequeue)(MultiThreadQueue * queue, void* dest) = [](MultiThreadQueue* queue, void* dest) { queue->dequeue(dest); };
	cls->set(u"dequeue", VoidPointer::make(dequeue));
}