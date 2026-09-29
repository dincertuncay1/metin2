#ifndef __INC_ETERLIB_MUTEX_H__
#define __INC_ETERLIB_MUTEX_H__

class Mutex
{
	public:
		Mutex();
		~Mutex();

		void Lock();
		void Unlock();
		bool Trylock();

	private:
		CRITICAL_SECTION lock;
};

#endif
//martysama0134's 2e58d0b8baeb072acdf3afc4a5d1999f
