#pragma once

#include "../eterLib/GrpScreen.h"

#include "EffectElementBaseInstance.h"
#include "SimpleLightData.h"

class CLightInstance : public CEffectElementBaseInstance
{
	public:
		friend class CLightData;

		CLightInstance();
		virtual ~CLightInstance();

	protected:

		void OnSetDataPointer(CEffectElementBase * pElement);

		void OnInitialize();
		void OnDestroy();

		bool OnUpdate(float fElapsedTime);
		void OnRender();

		DWORD			m_LightID;
		CLightData *	m_pData;
		DWORD			m_dwRangeIndex;

		DWORD			m_iLoopCount;

	public:
		static void DestroySystem();

		static CLightInstance* New();
		static void Delete(CLightInstance* pkData);

		static CDynamicPool<CLightInstance>		ms_kPool;
};
//martysama0134's 2e58d0b8baeb072acdf3afc4a5d1999f
