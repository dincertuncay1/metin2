#include "StdAfx.h"
#include "Base.h"

CBase::CBase()
{
    id = 0;
}

CBase::~CBase()
{
}

bool CBase::isNumber()
{
	return (id & MID_NUMBER) != 0 ? true : false;
}

bool CBase::isVar()
{
    return (id & MID_VARIABLE) != 0 ? true : false;
}

bool CBase::isSymbol()
{
    return (id & MID_SYMBOL) != 0 ? true : false;
}
//martysama0134's 2e58d0b8baeb072acdf3afc4a5d1999f
