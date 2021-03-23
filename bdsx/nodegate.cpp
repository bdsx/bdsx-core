#include "stdafx.h"
#include "nodegate.h"

#define BUILDING_CHAKRASHIM
#pragma warning(push, 0)
#include "node.h"
#include "uv.h"
#pragma warning(pop)

#pragma comment(lib, "chakracore.lib")
#pragma comment(lib, "cares.lib")
#pragma comment(lib, "chakrashim.lib")
#pragma comment(lib, "http_parser.lib")
#pragma comment(lib, "icudata.lib")
#pragma comment(lib, "icui18n.lib")
#pragma comment(lib, "icustubdata.lib")
#pragma comment(lib, "icutools.lib")
#pragma comment(lib, "icuucx.lib")
#pragma comment(lib, "libuv.lib")
#pragma comment(lib, "nghttp2.lib")
#pragma comment(lib, "node.lib")
#pragma comment(lib, "openssl.lib")
#pragma comment(lib, "zlib.lib")

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "Userenv.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Version.lib")

using std::pair;

JsContextRef getBdsxJsContextRef() noexcept;

namespace
{
    kr::AtomicQueue s_atomicQueue;
    uv_async_t s_processTask;
    intptr_t s_asyncRef;
    bool s_inited;
    bool s_checkAsyncInAsync = false;

    constexpr size_t INIT_COUNT = 30;

    void clear() noexcept
    {
        if (!s_inited) return;
        s_inited = false;
        nodegate::clearNativeModule();
    }

    void init(
        v8::Local<v8::Object> exports,
        v8::Local<v8::Value> module,
        v8::Local<v8::Context> context) noexcept
    {
        s_inited = true;
        v8::Isolate* isolate = context->GetIsolate();
        node::AddEnvironmentCleanupHook(isolate, [](void*) { clear();  }, nullptr);
        nodegate::initNativeModule(*exports);
        atexit(clear);
    }

    void codeCheckPost() noexcept
    {
        JsValueRef exception;
        JsErrorCode err = JsGetAndClearException(&exception);
        if (err == JsNoError)
        {
            nodegate::error(exception);
        }
        nodegate::_tickCallback();
    }

}
NODE_MODULE_CONTEXT_AWARE(bdsx_core, init);

int nodegate::start(int argc, char** argv) noexcept
{
    return node::Start(argc, argv);
}
void nodegate::loopOnce() noexcept
{
    if (s_checkAsyncInAsync)
    {
        if (s_asyncRef == 0) return;
        uv_async_send(&s_processTask);
        s_checkAsyncInAsync = false;
    }

    uv_loop_t* loop = uv_default_loop();
    int counter = 0;
    for (;;)
    {
        counter++;
        if (uv_run(loop, UV_RUN_NOWAIT) == 0) break;
        if (counter > 30) break;
    }
}
void nodegate::loop(uint64_t hd_point) noexcept
{
    if (s_checkAsyncInAsync)
    {
        if (s_asyncRef == 0) return;
        uv_async_send(&s_processTask);
        s_checkAsyncInAsync = false;
    }

    constexpr int64_t milli2nano = high_resolution_clock::period::den / std::milli::den;

    uv_loop_t* loop = uv_default_loop();
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    node::MultiIsolatePlatform* platform = node::GetMainThreadMultiIsolatePlatform();
    
    uint64_t hd_now = high_resolution_clock::now().time_since_epoch().count();

    int64_t duramilli = (int64_t)(hd_point - hd_now) / milli2nano;
    if (duramilli <= 0)
    {
        int counter = 0;
        for (;;)
        {
            counter++;
            int count = uv_run(loop, UV_RUN_NOWAIT);
            platform->DrainTasks(isolate);
            if (count == 0) break;
            if (counter > 30) break;
        }
        return;
    }

    struct Timer:uv_timer_t
    {
        bool done = false;
    };
    Timer awakeTimer;
    uv_timer_init(loop, &awakeTimer);
    uv_timer_start(&awakeTimer, [](uv_timer_t* timer){
        static_cast<Timer*>(timer)->done = true;
        uv_stop(uv_default_loop());
        }, duramilli, 0);

    for (;;)
    {
        uv_run(loop, UV_RUN_DEFAULT);
        platform->DrainTasks(isolate);
        if (awakeTimer.done) break;
    }
}

#include <typeinfo>

AsyncTask::AsyncTask(void (*fn)(AsyncTask*)) noexcept
    :fn(fn)
{
}
AsyncTask::~AsyncTask() noexcept
{
}
void AsyncTask::post() noexcept
{
    _assert(s_asyncRef != 0);

    size_t count = s_atomicQueue.enqueue(this);
    if (count == 1)
    {
        uv_async_send(&s_processTask);
    }
}
void AsyncTask::open() noexcept
{
    if (s_asyncRef++ == 0)
    {
        uv_loop_t* uv = uv_default_loop();
        uv_async_init(uv, &s_processTask, (uv_async_cb)[](uv_async_t*) {
            for (;;)
            {
                auto pair = s_atomicQueue.dequeue();
                if (pair.first == nullptr)
                {
                    if (s_asyncRef == 0) return;
                    uv_async_send(&s_processTask);
                    break;
                }

                AsyncTask* task = static_cast<AsyncTask*>(pair.first);
                if (pair.second != 0)
                {
                    s_checkAsyncInAsync = true;
                    task->fn(task);
                    task->release();
                    s_checkAsyncInAsync = false;
                    codeCheckPost();
                }
                else
                {
                    task->fn(task);
                    task->release();
                    codeCheckPost();
                    break;
                }
            }
            });
    }
}
void AsyncTask::close() noexcept
{
    if (--s_asyncRef == 0)
    {
        uv_close((uv_handle_t*)&s_processTask, nullptr);
    }
}

AsyncTask* AsyncTask::alloc(void (*cb)(AsyncTask*), size_t size) noexcept
{
    AsyncTask* data = (AsyncTask*)malloc(size + sizeof(AsyncTask));
    new(data) AsyncTask(cb);
    reline_new(data);
    return data;
}
void AsyncTask::call(void (*cb)(AsyncTask*)) noexcept
{
    (_new AsyncTask(cb))->post();
}
