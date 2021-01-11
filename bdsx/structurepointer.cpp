#include "stdafx.h"
#include "structurepointer.h"

using namespace kr;

namespace
{
	JsPropertyId s_size;
}

StructurePointer::StructurePointer(const JsArguments& args) noexcept
	:JsObjectT<StructurePointer, StaticPointer>(args)
{
}
void StructurePointer::finalize() noexcept
{
	delete this;
}

JsObject* StructurePointer::_allocate(const JsArguments& args) throws(JsException)
{
	StructurePointer* data;
	if (args[0] == true)
	{
		JsValue sizevar = args.getThis().getConstructor().get(s_size);
		JsType type = sizevar.getType();
		if (type == JsType::Integer || type == JsType::Float)
		{
			int extraSize = sizevar.as<int>();
			if (extraSize < 0) throw JsException(u"Invalid structureSize");
			data = (StructurePointer*)alloc<alignof(StructurePointer)>::allocate(extraSize + sizeof(StructurePointer));
			data->m_allocatedItSelf = true;
			try
			{
				data = reline_new(new(data) StructurePointer(args));
				data->m_address = (byte*)data + sizeof(StructurePointer);
				return data;
			}
			catch (...)
			{
				alloc<alignof(StructurePointer)>::free(data);
				throw;
			}
		}
		else
		{
			throw JsException(u"[StructurePointer.contentSize] is undefined");
		}
	}

	data = (StructurePointer*)alloc<alignof(StructurePointer)>::allocate(sizeof(StructurePointer));
	data->m_allocatedItSelf = false;
	try
	{
		return reline_new(new(data) StructurePointer(args));
	}
	catch (...)
	{
		alloc<alignof(StructurePointer)>::free(data);
		throw;
	}
}

void StructurePointer::initMethods(kr::JsClassT<StructurePointer>* cls) noexcept
{
	JsValue symbol = JsNewSymbol((JsValue)u"structure-size");
	cls->set(u"contentSize", symbol);
	s_size = JsPropertyId::fromSymbol(symbol);
}
void StructurePointer::clearMethods() noexcept
{
	s_size = JsPropertyId();
}