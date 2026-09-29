#pragma once

#include "AbstractSingleton.h"

class IAbstractChat : public TAbstractSingleton<IAbstractChat>
{
	public:
		IAbstractChat() {}
		virtual ~IAbstractChat() {}

#if defined(__BL_CLIENT_LOCALE_STRING__)
		enum ESpecialColorType
		{
			CHAT_SPECIAL_COLOR_NORMAL,
			CHAT_SPECIAL_COLOR_DICE_0,
			CHAT_SPECIAL_COLOR_DICE_1,
		};

#if defined(__BL_MULTI_LANGUAGE_PREMIUM__)
		virtual void AppendChat(int iType, const char* c_szChat, BYTE bSpecialColorType = ESpecialColorType::CHAT_SPECIAL_COLOR_NORMAL, BYTE bEmpire = 0, const std::string& countryName = "") = 0;
#else
		virtual void AppendChat(int iType, const char * c_szChat, BYTE bSpecialColorType = ESpecialColorType::CHAT_SPECIAL_COLOR_NORMAL) = 0;
#endif
#else
		virtual void AppendChat(int iType, const char* c_szChat) = 0;
#endif
};
//martysama0134's 2e58d0b8baeb072acdf3afc4a5d1999f
