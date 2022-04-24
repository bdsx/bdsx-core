#pragma once

#include <KR3/main.h>
#include <KR3/data/crypt.h>

struct PdbCacheHeader {
	static constexpr uint32_t VERSION = 1;

	uint32_t version;
	kr::byte md5[kr::encoder::Md5Context::SIZE];
	uint32_t mainRva;
	uint32_t hashMapCapacity;
};
