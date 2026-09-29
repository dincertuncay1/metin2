#ifndef __INC_ETERLIB_THREAD_H__
#define __INC_ETERLIB_THREAD_H__

class CThread
{
	public:
		CThread();
		int Create(void * arg);

	protected:
		static UINT CALLBACK	EntryPoint(void * pThis);

		virtual UINT			Setup() = 0;
		virtual UINT			Execute(void * arg) = 0;

		UINT					Run(void * arg);

		void *					Arg() const		{ return m_pArg; }
		void					Arg(void * arg) { m_pArg = arg; }

		HANDLE					m_hThread;

	private:
		void *					m_pArg;
		unsigned				m_uThreadID;
};

#endif
//martysama0134's 2e58d0b8baeb072acdf3afc4a5d1999f
