#include "stdafx.h"
#include "structurepointer.h"

using namespace kr;

namespace
{
	JsPropertyId s_size;
	JsPropertyId s_nativeCtor;
	JsPropertyId s_nativeDtor;

	using ptrfunc_t = void(*)(void*);
}

StructurePointer::StructurePointer(const JsArguments& args) noexcept
	:JsObjectT<StructurePointer, StaticPointer>(args)
{
}
void StructurePointer::finalize() noexcept
{
	if (m_finalize != nullptr)
	{
		m_finalize(m_address);
	}

	delete this;
}

JsObject* StructurePointer::_allocate(const JsArguments& args) throws(JsException)
{
	StructurePointer* data;
	if (args.size() != 0 && args[0] == true)
	{
		JsValue cls = args.getThis().getConstructor();
		JsValue sizevar = cls.get(s_size);
		JsType type = sizevar.getType();
		if (type == JsType::Integer || type == JsType::Float)
		{
			int extraSize = sizevar.as<int>();
			if (extraSize < 0) throw JsException(u"Invalid structureSize");

			data = (StructurePointer*)alloc<alignof(StructurePointer)>::allocate(extraSize + sizeof(StructurePointer));
			data->m_finalize = (ptrfunc_t)cls.get(s_nativeDtor).getNativeObject<VoidPointer>()->getAddressRawSafe();
			try
			{
				data = reline_new(new(data) StructurePointer(args));
				byte* dataptr = (byte*)data + sizeof(StructurePointer);
				data->m_address = dataptr;
				VoidPointer* ctorptr = cls.get(s_nativeCtor).getNativeObject<VoidPointer>();
				if (ctorptr != nullptr)
				{
					((ptrfunc_t)ctorptr->getAddressRaw())(dataptr);
				}
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
	data->m_finalize = nullptr;
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

	symbol = JsNewSymbol((JsValue)u"native-ctor");
	cls->set(u"nativeCtor", symbol);
	s_nativeCtor = JsPropertyId::fromSymbol(symbol);

	symbol = JsNewSymbol((JsValue)u"native-dtor");
	cls->set(u"nativeDtor", symbol);
	s_nativeDtor = JsPropertyId::fromSymbol(symbol);
}
void StructurePointer::clearMethods() noexcept
{
	s_size = JsPropertyId();
	s_nativeCtor = JsPropertyId();
	s_nativeDtor = JsPropertyId();
}