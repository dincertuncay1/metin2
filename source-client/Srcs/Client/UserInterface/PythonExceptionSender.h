#pragma once

class CPythonExceptionSender : public IPythonExceptionSender
{
	public:
		CPythonExceptionSender();
		virtual ~CPythonExceptionSender();

		void Send();

	protected:
		std::set<DWORD> m_kSet_dwSendedExceptionCRC;
};
//martysama0134's 2e58d0b8baeb072acdf3afc4a5d1999f
