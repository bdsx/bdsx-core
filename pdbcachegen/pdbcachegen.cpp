#include <KR3/main.h>
#include <KR3/fs/file.h>
#include <KR3/io/bufferedstream.h>
#include <KR3/util/pdb.h>
#include <KR3/win/symopt.h>
#include <KR3/win/windows.h>
#include <KR3/data/crypt.h>
#include "../bdsx/gen/version.h"
#include "pdbcachegen.h"

using namespace kr;

uint32_t hashString(Text16 v) noexcept {
	uint32_t out = 0;
	uint32_t shift = 0;
	uint32_t n = intact<uint32_t>(v.size());
	for (uint32_t i = 0; i < n;i++) {
		uint32_t chr = v[i] + i;
		out += (chr << shift) | (chr >> (32 - shift));
		shift = (shift + 7) & 0x1f;
	}
	out += n;
	return out;
}

struct Symbol {
	AText16 name;
	void* address;
};

struct OffsetEntry {
	uint32_t hash;
	uint32_t nameOffset;
	uint32_t rva;
};

int wmain(int argn, const wchar_t** args) {
	byte md5[encoder::Md5Context::SIZE];
	filetime_t exeModifiedTime;

	bool force = false;
	int argi = 0;

	pcstr16 exePath = nullptr, cachePath = nullptr;

	args++;
	for (;;) {
		pcstr16 arg = unwide(*args++);
		if (arg == nullptr) break;
		Text16 argtx = (Text16)arg;
		if (argtx == u"-f") {
			force = true;
		}
		else {
			switch (argi) {
			case 0:
				exePath = arg;
				break;
			case 1:
				cachePath = arg;
				break;
			default:
				cerr << "Too many arguments" << endl;
				return EINVAL;
			}
			argi++;
		}
	}
	if (argi < 2) {
		cerr << "Need two arguments at least" << endl;
		cerr << "ex) pdbcachegen.exe (exe) (cachefile)" << endl;
		return EINVAL;
	}
	try {
		Must<File> exe = File::open(exePath);
		exeModifiedTime = exe->getLastModifiedTime();
		encoder::Md5Context ctx;
		ctx.reset();
		ctx.update((TBuffer)exe->readAll<void>());
		ctx.finish(md5);
	}
	catch (Error&) {
		cerr << "exe file not found." << endl;
		return ERROR_NOT_FOUND;
	}
	try {
		Must<File> cache = File::open(cachePath);
		if (!force) {
			if (exeModifiedTime < cache->getLastModifiedTime()) {
				PdbCacheHeader header;
				cache->read(&header, sizeof(header));
				if (header.version == PdbCacheHeader::VERSION) {
					if (memcmp(header.md5, md5, encoder::Md5Context::SIZE) == 0) {
						cout << "pdbcache.bin: latest" << endl;
						return S_OK;
					}
				}
			}
		}
	}
	catch (Error&) {
	}
	Array<Symbol> symbols;
	symbols.reserve(100000);

	cout << "pdbcache.bin: generating..." << endl;
	cout.flush();

	PdbReader pdb;
	uint64_t base = 0x1000000;
	pdb.load(base, exePath);
	pdb.setOptions(SYMOPT_PUBLICS_ONLY);
	pdb.getAll16([&symbols](Text16 name, autoptr64 address, uint32_t typeId) {
		symbols.push({ name, address });
		return true;
		});
	pdb.setOptions(SYMOPT_NO_PUBLICS);
	
	pdb.getAll16([&symbols](Text16 name, autoptr64 address, uint32_t typeId) {
		symbols.push({ name, address });
		return true;
		});

	size_t capacity = symbols.size() * 2;

	Array<OffsetEntry> keyTable;
	keyTable.resize(capacity);
	keyTable.zero();

	size_t offsetBegin = capacity * sizeof(OffsetEntry) + sizeof(PdbCacheHeader);
	Array<char> names;
	names.reserve(capacity*5);

	PdbCacheHeader header;
	header.version = PdbCacheHeader::VERSION;
	header.hashMapCapacity = intact<uint32_t>(capacity);
	memcpy(header.md5, md5, encoder::Md5Context::SIZE);

	Text16 mainText = u"main";
	uint32_t mainHash = hashString(mainText);

	size_t dupMax = 0;
	for (Symbol& sym : symbols) {
		uint32_t hash = hashString(sym.name);
		uint32_t rva = intact<uint32_t>((uint64_t)sym.address - base);
		if (hash == mainHash && sym.name == mainText) {
			header.mainRva = rva;
		}

		uint32_t index = hash;
		size_t dupCount = 0;
		for (;;) {
			index %= capacity;
			OffsetEntry& entry = keyTable[index];
			if (entry.nameOffset != 0) {
				dupCount++;
				index++;
				continue;
			}
			entry.hash = hash;
			entry.nameOffset = intact<uint32_t>(names.size() + offsetBegin);
			entry.rva = rva;
			break;
		}

		names << (Utf16ToUtf8)(sym.name);
		names.write('\0');
	}

	io::FileStream<void>* file = File::create(cachePath)->stream<void>();
	file->writeas(header);
	file->write((Buffer)keyTable);
	file->write((Buffer)names);
	delete file;

	cout << "pdbcache.bin: generated" << endl;
	return S_OK;
}
