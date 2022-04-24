#include "stdafx.h"
#include "cachedpdb.h"
#include "nativepointer.h"
#include "jsctx.h"
#include "nodegate.h"

#include <KR3/util/pdb.h>
#include <KR3/data/set.h>
#include <KRWin/handle.h>
#include <KR3/data/crypt.h>

#include <KR3/win/dynamic_dbghelp.h>

using namespace kr;

JsValue getPdbNamespace() noexcept
{
	JsValue pdb = JsNewObject;
	pdb.setMethod(u"setOptions", [](int options) { return PdbReader::setOptions(options); });
	pdb.setMethod(u"getOptions", []() { return PdbReader::getOptions(); });
	pdb.setMethod(u"undecorate", [](Text16 text, int flags)->TText16 {
		TText16 undecorated;
		undecorated << PdbReader::undecorate(text.data(), flags);
		return move(undecorated); 
	});
	return pdb;
}
