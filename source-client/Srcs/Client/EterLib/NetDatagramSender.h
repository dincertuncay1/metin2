#pragma once

#ifndef VC_EXTRALEAN

class CNetDatagramSender
{
	public:
		CNetDatagramSender();
		virtual ~CNetDatagramSender();

		BOOL isSocket();

		BOOL SetSocket(const char * c_szIP, WORD wPortIndex);
		BOOL SetSocket(DWORD dwAddress, WORD wPortIndex);
		BOOL Send(const void * pBuffer, int iSize);

	protected:
		BOOL m_isSocket;

		WORD m_dwAddress;
		WORD m_wPortIndex;

		SOCKET m_Socket;
		SOCKADDR_IN m_SockAddr;
};

#endif
//martysama0134's 2e58d0b8baeb072acdf3afc4a5d1999f
