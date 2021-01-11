#pragma once

#include <KR3/net/ipaddr.h>

class NetFilter
{
public:
	static void init(kr::JsValue callbackOnExceeded) noexcept;

	static bool addFilter(kr::Ipv4Address ip, time_t endTime) noexcept;
	static bool isFilted(kr::Ipv4Address ip) noexcept;
	static double getTime(kr::Ipv4Address ip) noexcept;
	static bool removeFilter(kr::Ipv4Address ip) noexcept;
	static void clearFilter() noexcept;
	static void setTrafficLimit(uint64_t bytes) noexcept;
	static void setTrafficLimitPeriod(int seconds) noexcept;
	static kr::Ipv4Address getLastSender() noexcept;
	static kr::JsValue entries() noexcept;
};

kr::JsValue getNetFilterNamespace() noexcept;