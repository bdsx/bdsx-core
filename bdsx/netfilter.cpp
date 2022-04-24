#include "stdafx.h"
#include "netfilter.h"

#include "nodegate.h"
#include "jsctx.h"

#include <WinSock2.h>

#include <KRWin/hook.h>
#include <KRWin/handle.h>
#include <KR3/data/set.h>
#include <KR3/mt/criticalsection.h>
#include <KR3/win/windows.h>
#include <KR3/msg/pool.h>
#include <KR3/net/ipaddr.h>


using namespace kr;

namespace
{
	class TrafficLogger;

	Map<Ipv4Address, time_t> s_ipfilter;
	RWLock s_ipfilterLock;
	uint64_t s_trafficLimit = (uint64_t)-1;
	uint64_t s_trafficLimitPeriod = 0;
	std::atomic<uint32_t> s_lastSender;

	RWLock s_trafficLock;
	atomic<size_t> s_trafficCount = 0;
	Cond s_trafficDeleting;
	TrafficLogger* s_currentTrafficLogger = nullptr;

	struct DeferField
	{
		JsPersistent callbackOnExceeded;

		DeferField() noexcept;
		~DeferField() noexcept;
	};
	Deferred<DeferField> s_field(JsRuntime::initpack);

	class TrafficOverCallback :public AsyncTask
	{
	private:
		const Ipv4Address m_ip;

	public:
		TrafficOverCallback(Ipv4Address ip) noexcept
			:AsyncTask([](AsyncTask* task) {
			JsScope _scope;
			try
			{
				JsValue callback = (JsValue)s_field->callbackOnExceeded;
				callback(TSZ16() << static_cast<TrafficOverCallback*>(task)->m_ip);
			}
			catch (JsException& err)
			{
				g_ctx->fireError(err);
			}
		}), m_ip(ip)
		{
		}

	};

	class TrafficLogger
	{
		friend NetFilter;

	public:
		AText16 m_logPath;
		timepoint m_trafficTime = timepoint::now();
		Map<Ipv4Address, uint64_t> m_back;
		Map<Ipv4Address, uint64_t> m_current;
		atomic<size_t> m_ref;

		~TrafficLogger() noexcept
		{
		}
		TrafficLogger(AText16 path) noexcept
			:m_logPath(move(path)), m_ref(1)
		{
			m_logPath.c_str();
			s_trafficCount++;
		}

		void addRef() noexcept
		{
			m_ref++;
		}
		void release() noexcept
		{
			if (m_ref-- == 1)
			{
				delete this;
				s_trafficCount--;
				s_trafficDeleting.set();
			}
		}

		uint64_t addTraffic(Ipv4Address ip, uint64_t value) noexcept
		{
			timepoint now = timepoint::now();
			if (now - m_trafficTime >= 1_s)
			{
				m_trafficTime = now;
				Map<Ipv4Address, uint64_t> back = move(m_back);
				m_back = move(m_current);
				m_current = move(back);
				m_current.clear();
				m_ref++;
				threadingVoid([this] {
					for (;;)
					{
						try
						{
							io::FOStream<char> file = File::openWrite(m_logPath.data());
							file.base()->toEnd();

							{
								s_trafficLock.enterRead();
								finally {
									s_trafficLock.leaveRead();
								};

								for (auto [key, value] : m_back)
								{
									file << key << ": " << value << "\r\n";
								}
								file << "\r\n";
							}

							release();
							s_trafficDeleting.set();
							return;
						}
						catch (...)
						{
							(_new AsyncTask([](AsyncTask*){
								JsScope _scope;
								g_ctx->error(u"[BDSX] failed to log traffics");
								}))->post();
						}
					}
					});
			}
			auto res = m_current.insert({ ip, uint64_t() });
			if (res.second)
			{
				return res.first->second = value;
			}
			return res.first->second += value;
		}

		static void newInstance(AText16 path) noexcept
		{
			TrafficLogger* trafficLogger = _new TrafficLogger(move(path));
			s_trafficLock.enterWrite();
			TrafficLogger* old = s_currentTrafficLogger;
			s_currentTrafficLogger = trafficLogger;
			s_trafficLock.leaveWrite();

			if (old) old->release();
		}
		static void clear() noexcept
		{
			s_trafficLock.enterWrite();
			TrafficLogger* old = s_currentTrafficLogger;
			s_currentTrafficLogger = nullptr;
			s_trafficLock.leaveWrite();
			if (old) old->release();
		}
		static void clearWait() noexcept
		{
			clear();
			while (s_trafficCount != 0)
			{
				s_trafficDeleting.wait();
			}
		}
	};

	void addTraffic(Ipv4Address ip, uint64_t value) noexcept
	{
		s_trafficLock.enterWrite();
		TrafficLogger* logger = s_currentTrafficLogger;
		if (logger)
		{
			value = logger->addTraffic(ip, value);
		}
		s_trafficLock.leaveWrite();

		if (value >= s_trafficLimit)
		{
			time_t endTime;
			if (s_trafficLimitPeriod == 0) {
				endTime = 0;
			}
			else {
				time_t now = time(nullptr);
				endTime = now + s_trafficLimitPeriod;
				if (endTime <= now) { // overflowed
					endTime = (time_t)-1;
				}
			}
			if (NetFilter::addFilter(ip, endTime))
			{
				(_new TrafficOverCallback(ip))->post();
			}
		}
	}

	int CALLBACK sendtoHook(
		SOCKET s, const char FAR* buf, int len, int flags,
		const struct sockaddr FAR* to, int tolen)
	{
		Ipv4Address& ip = (Ipv4Address&)((sockaddr_in*)to)->sin_addr;
		if (NetFilter::isFilted(ip))
		{
			WSASetLastError(WSAECONNREFUSED);
			return SOCKET_ERROR;
		}
		int res = sendto(s, buf, len, flags, to, tolen);
		if (res == SOCKET_ERROR) return SOCKET_ERROR;
		addTraffic(ip, res);
		return res;
	}

	int CALLBACK WSACleanupHook() noexcept
	{
		return 0;
	}

	int CALLBACK recvfromHook(
		SOCKET s, char* buf, int len, int flags,
		sockaddr* from, int* fromlen
	) noexcept
	{
		int res = recvfrom(s, buf, len, flags, from, fromlen);
		if (res == SOCKET_ERROR) return SOCKET_ERROR;
		uint32_t ip32 = ((sockaddr_in*)from)->sin_addr.s_addr;
		s_lastSender = ip32;

		Ipv4Address& ip = (Ipv4Address&)ip32;
		addTraffic(ip, res);
		if (NetFilter::isFilted(ip))
		{
			*fromlen = 0;
			WSASetLastError(WSAECONNREFUSED);
			return SOCKET_ERROR;
		}
		return res;
	}

	DeferField::DeferField() noexcept
	{
	}
	DeferField::~DeferField() noexcept
	{
		callbackOnExceeded = JsPersistent();
		TrafficLogger::clearWait();
	}
}

void NetFilter::init(JsValue callbackOnExceeded) noexcept
{
	s_field->callbackOnExceeded = callbackOnExceeded;

	hook::IATModule ws2_32(win::Module::current(), "WS2_32.dll");
	ws2_32.hooking(17, recvfromHook);
	ws2_32.hooking(20, sendtoHook);
	ws2_32.hooking(116, WSACleanupHook);
}

bool NetFilter::addFilter(kr::Ipv4Address ip, time_t endTime) noexcept
{
	s_ipfilterLock.enterWrite();
	auto res = s_ipfilter.insert({ ip, endTime });
	if (!res.second) {
		if (res.first->second != 0) {
			res.first->second = endTime;
		}
	}
	s_ipfilterLock.leaveWrite();
	return res.second;
}
bool NetFilter::isFilted(Ipv4Address ip) noexcept
{
	s_ipfilterLock.enterRead();
	auto iter = s_ipfilter.find(ip);
	if (iter == s_ipfilter.end()) {
		s_ipfilterLock.leaveRead();
		return false;
	}

	time_t endtime = iter->second;
	s_ipfilterLock.leaveRead();

	if (endtime == 0) {
		return true;
	}

	time_t now;
	if (endtime > (now = time(nullptr))) {
		return true;
	}

	// possible to change something, recheck all
	s_ipfilterLock.enterWrite();
	iter = s_ipfilter.find(ip);
	if (iter == s_ipfilter.end()) {
		s_ipfilterLock.leaveWrite();
		return false;
	}
	endtime = iter->second;
	if (endtime == 0) {
		s_ipfilterLock.leaveWrite();
		return true;
	}
	if (endtime > now) {
		s_ipfilter.erase(iter);
		s_ipfilterLock.leaveWrite();
		return false;
	}
	s_ipfilterLock.leaveWrite();
	return true;
}
double NetFilter::getTime(Ipv4Address ip) noexcept
{
	s_ipfilterLock.enterRead();
	auto iter = s_ipfilter.find(ip);
	if (iter == s_ipfilter.end()) {
		s_ipfilterLock.leaveRead();
		return -1.0;
	}

	time_t endtime = iter->second;
	s_ipfilterLock.leaveRead();

	if (endtime == 0) {
		return 0.0;
	}

	time_t now;
	if (endtime > (now = time(nullptr))) {
		return (double)endtime;
	}

	// possible to change something, recheck all
	s_ipfilterLock.enterWrite();
	iter = s_ipfilter.find(ip);
	if (iter == s_ipfilter.end()) {
		s_ipfilterLock.leaveWrite();
		return -1.0;
	}
	endtime = iter->second;
	if (endtime == 0) {
		s_ipfilterLock.leaveWrite();
		return 0.0;
	}
	if (endtime > now) {
		s_ipfilter.erase(iter);
		s_ipfilterLock.leaveWrite();
		return -1.0;
	}
	s_ipfilterLock.leaveWrite();
	return (double)endtime;
}
bool NetFilter::removeFilter(kr::Ipv4Address ip) noexcept
{
	s_ipfilterLock.enterWrite();
	bool erased = s_ipfilter.erase(ip) != 0;
	s_ipfilterLock.leaveWrite();
	return erased;
}
void NetFilter::clearFilter() noexcept
{
	s_ipfilterLock.enterWrite();
	s_ipfilter.clear();
	s_ipfilterLock.leaveWrite();
}
void NetFilter::setTrafficLimit(uint64_t bytes) noexcept
{
	s_trafficLimit = bytes;
}
void NetFilter::setTrafficLimitPeriod(uint64_t seconds) noexcept
{
	s_trafficLimitPeriod = seconds;
}
Ipv4Address NetFilter::getLastSender() noexcept
{
	uint32_t output = s_lastSender;
	return (Ipv4Address&)output;
}
JsValue NetFilter::entries() noexcept
{
	time_t now = time(nullptr);
	Array<Ipv4Address> removing;

	s_ipfilterLock.enterRead();
	JsValue arr = JsNewArray();

	int index = 0;
	auto end = s_ipfilter.end();
	for (auto iter = s_ipfilter.begin(); iter != end; iter++) {
		time_t endtime = iter->second;
		if (endtime != 0 && endtime < now) {
			if (removing.capacityBytes() == 0) {
				removing.reserve(16);
			}
			removing.push(iter->first);
			continue;
		}
		JsValue entry = JsNewArray();
		entry.set(0, TSZ16() << iter->first);
		entry.set(1, (double)iter->second);
		arr.set(index++, entry);
	}
	s_ipfilterLock.leaveRead();

	if (!removing.empty()) {
		s_ipfilterLock.enterWrite();
		for (Ipv4Address addr : removing) {
			auto iter = s_ipfilter.find(addr);
			if (iter == s_ipfilter.end()) continue;
			if (iter->second != 0 && iter->second < now) {
				s_ipfilter.erase(iter);
			}
			else {
				JsValue entry = JsNewArray();
				entry.set(0, TSZ16() << iter->first);
				entry.set(1, (double)iter->second);
				arr.set(index++, entry);
			}
		}
		s_ipfilterLock.leaveWrite();
	}

	return arr;
}

JsValue getNetFilterNamespace() noexcept
{
	JsValue ipfilter = JsNewObject;
	ipfilter.setMethod(u"getLastSender", []()->TText16 {
		Ipv4Address ptr = NetFilter::getLastSender();
		TText16 out;
		out << ptr;
		return move(out);
		});
	ipfilter.setMethod(u"add", [](Text16 ipport, double period) {
		Text16 iptext = ipport.readwith_e('|');
		if (iptext.empty()) return;

		time_t timeTo;
		if (isnan(period)) {
			timeTo = 0;
		}
		else {
			if (period <= 0) return;
			time_t now = time(nullptr);
			timeTo = now + (int64_t)period;
			if (timeTo < now) { // overflowed
				timeTo = (time_t)-1;
			}
		}
		NetFilter::addFilter(Ipv4Address(TSZ() << toNone(iptext)), timeTo);
		});
	ipfilter.setMethod(u"addAt", [](Text16 ipport, double uts) {
		Text16 iptext = ipport.readwith_e('|');
		if (iptext.empty()) return;
		time_t timeTo;
		time_t now = time(nullptr);
		if (isnan(uts)) {
			timeTo = now;
		}
		else if (uts < 0) {
			return;
		}
		else {
			timeTo = (time_t)uts;
			if (timeTo != 0 && timeTo <= now) return;
		}
		NetFilter::addFilter(Ipv4Address(TSZ() << toNone(iptext)), timeTo);
		});
	ipfilter.setMethod(u"remove", [](Text16 ipport) {
		Text16 iptext = ipport.readwith_e('|');
		if (iptext.empty()) return false;
		return NetFilter::removeFilter(Ipv4Address(TSZ() << toNone(iptext)));
		});
	ipfilter.setMethod(u"entries", NetFilter::entries);
	ipfilter.setMethod(u"init", [](JsValue callbackOnExceeded) {
		NetFilter::init(callbackOnExceeded);
		});
	ipfilter.setMethod(u"clear", [](JsValue callbackOnExceeded) {
		NetFilter::clearFilter();
		});
	ipfilter.setMethod(u"has", [](Text16 ipport) {
		Text16 iptext = ipport.readwith_e('|');
		if (iptext.empty()) return false;
		return NetFilter::isFilted(Ipv4Address(TSZ() << toNone(iptext)));
		});
	ipfilter.setMethod(u"getTime", [](Text16 ipport) {
		Text16 iptext = ipport.readwith_e('|');
		if (iptext.empty()) return -1.0;
		return NetFilter::getTime(Ipv4Address(TSZ() << toNone(iptext)));
		});
	ipfilter.setMethod(u"setTrafficLimit", [](double bytes) {
		if (isnan(bytes)) return;
		return NetFilter::setTrafficLimit(bytes <= 0 ? 0 : (uint64_t)bytes);
		});
	ipfilter.setMethod(u"setTrafficLimitPeriod", [](double seconds) {
		if (isnan(seconds)) return;
		return NetFilter::setTrafficLimitPeriod(seconds <= 0 ? 0 :(uint64_t)seconds);
		});
	ipfilter.setMethod(u"logTraffic", [](JsValue path) {
		if (path.cast<bool>())
		{
			TrafficLogger::newInstance(path.cast<AText16>());
		}
		else
		{
			TrafficLogger::clear();
		}
		});
	return ipfilter;
}
