#include "StdAfx.h"
#include "SymTable.h"

using namespace std;

CSymTable::CSymTable(int aTok, string aStr) : dVal(0), token(aTok), strlex(aStr)
{
}

CSymTable::~CSymTable()
{
}
//martysama0134's 2e58d0b8baeb072acdf3afc4a5d1999f
