// stdafx.cpp : source file that includes just the standard includes
//	scriptLib.pch will be the pre-compiled header
//	stdafx.obj will contain the pre-compiled type information

#include "stdafx.h"

void SetExceptionSender(IPythonExceptionSender * pkExceptionSender)
{
	g_pkExceptionSender = pkExceptionSender;
}
//martysama0134's 2e58d0b8baeb072acdf3afc4a5d1999f
