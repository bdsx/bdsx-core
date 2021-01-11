#include "stdafx.h"
#include "uvasync.h"

#include "nodegate.h"
#include "voidpointer.h"

namespace
{
    void postAsync(AsyncTask* task) noexcept
    {
        task->post();
    }
}

kr::JsValue getUvAsyncNamespace() noexcept
{
    kr::JsValue uv_async = kr::JsNewObject;
    uv_async.setMethod(u"open", AsyncTask::open);
    uv_async.setMethod(u"close", AsyncTask::close);
    uv_async.set(u"call", VoidPointer::make(AsyncTask::call));
    uv_async.set(u"alloc", VoidPointer::make(AsyncTask::alloc));
    uv_async.set(u"post", VoidPointer::make(postAsync));
    uv_async.set(u"sizeOfTask", (int)sizeof(AsyncTask));
    return uv_async;
}
