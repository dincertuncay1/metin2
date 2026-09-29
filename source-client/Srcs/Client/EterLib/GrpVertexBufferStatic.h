#pragma once

#include "GrpVertexBuffer.h"

class CStaticVertexBuffer : public CGraphicVertexBuffer
{
	public:
		CStaticVertexBuffer();
		virtual ~CStaticVertexBuffer();

		bool Create(int vtxCount, DWORD fvf, bool isManaged=true);
};
//martysama0134's 2e58d0b8baeb072acdf3afc4a5d1999f
