/*
 * din_phil_boost.cpp
 *
 *  Created on: Apr 15, 2013
 *      Author: arun
 */
#include <iostream>
#include <vector>
#include<sstream>
#include <boost/noncopyable.hpp>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>

#define ITERATIONS     1  // Number of times for which a philosopher will EAT
#define WAITFORNXTFORK 3  // Number of seconds for which philosopher will wait
                          // to get hold of second fork, before putting down the first fork.
#define MAX_WAIT_THNK    3
#define MAX_WAIT_HUNG    3
#define MAX_WAIT_EAT     3

#define NUM_PHIL         5

template<typename Stream>
class Logger: private boost::noncopyable
{
public:
	Logger(Stream& stream):m_mutex(), m_stream(stream){}

	void log(const std::string& msg)
	{
		boost::mutex::scoped_lock lock(m_mutex);
		m_stream << msg << std::endl;
	}

private:
	boost::mutex m_mutex;
	Stream& m_stream;

};

typedef Logger<std::ostream> T_Logger;

/*
 * @desc Fork class design is based on Monitor Object pattern
 *       The public methods of the class takes care of all the
 *       synchronization related tasks for the instance.
 */
class Fork: private boost::noncopyable
{
public: /* Structors */
	Fork():m_mutex(), m_cond(), m_state(UNUSED){}
	~Fork(){}

public:
	bool get_fork();
	bool put_fork();

public:
	enum STATE{UNUSED = 0, USED};
	STATE m_state;

private: /* Fork synchronizers */
	boost::mutex     m_mutex;
	boost::condition m_cond;
};

bool
Fork::get_fork()
{
	// Check if the Fork has been locked by any other thread
	if(m_mutex.try_lock())
	{
		return true;
	}
	return false;
}

bool
Fork::put_fork()
{
	m_mutex.unlock();
	return true;
}

//======================================================================================

class Philosopher: private boost::noncopyable
{
public:
	Philosopher(Fork* left, Fork* right, int ID, T_Logger& log):m_pleft(left),
	                                                            m_pright(right),
							    	    m_philID(ID),		
								    m_state(THINKING),
								    m_eatcount(0),
								    m_logger(log),
								    m_thread(boost::thread(boost::bind(&Philosopher::start_thread,
												       this)
	                                                                                  )
	                                                                    ){}
	~Philosopher(){}

public: // Thread function
	void start_thread();
	void finish_eating();
	void wait(int /*time*/);

public:
	void join(){m_thread.join();}

public:
	enum STATE {THINKING = 0, HUNGRY, EATING};
	STATE m_state;
private:
	int m_philID;

private:
	Fork* m_pleft;
	Fork* m_pright;
	int m_eatcount;
private:
	boost::thread m_thread;
	T_Logger& m_logger;
};

void
Philosopher::wait(int time)
{
	boost::this_thread::sleep(static_cast<boost::posix_time::seconds>(std::rand()%time));
}

void
Philosopher::finish_eating()
{
	std::stringstream ss;
	ss << "Philosopher " << m_philID << "puts down left chopstick.";
	m_logger.log(ss.str());
	ss.str("");	
	m_pleft->put_fork();

	ss << "Philosopher " << m_philID << "puts down right chopstick.";
        m_logger.log(ss.str());
	ss.str("");

	m_pright->put_fork();
	m_eatcount++;
	m_state = THINKING;
}

void
Philosopher::start_thread()
{
	while(m_eatcount < ITERATIONS)
	{
		wait(MAX_WAIT_THNK);
		if(m_pleft->get_fork())
		{
			std::stringstream ss;
			ss << "Philosopher " << m_philID << "picks up left chopstick.";		
			m_logger.log(ss.str());
			ss.str("");

			if(m_pright->get_fork())
			{
				ss << "Philosopher " << m_philID << "picks up right chopstick.";
				m_logger.log(ss.str());
				ss.str("");

				ss << "Philosopher " << m_philID << "eats.";
				m_logger.log(ss.str());
				ss.str("");

				this->wait(MAX_WAIT_EAT);
				this->finish_eating();
			}else{
				while(!m_pright->get_fork())	
					this->wait(MAX_WAIT_HUNG);

				ss << "Philosopher " << m_philID << "picks up right chopstick.";
				m_logger.log(ss.str());
				ss.str("");

                                ss << "Philosopher " << m_philID << "eats.";
                                m_logger.log(ss.str());
				ss.str("");

				this->wait(MAX_WAIT_EAT);
                                this->finish_eating();
			}
		}
	}// end while

	return;

}

//==========================================================================================

class DiningTable
{
public:
	DiningTable(int numPhils, int numForks, T_Logger& log):m_numPhils(numPhils),
	                                        m_numForks(numForks),
	                                        m_forks_created(false),
	                                        m_logger(log)
	                                        {}
	~DiningTable();

public:
	void create_forks();
	bool start_phil_threads();
	void wait_for_dinner_cmpltn();

private:
	std::vector<Philosopher*>   m_pPhils;
	std::vector<Fork*>          m_pForks;
private:
	int m_numPhils;
	int m_numForks;
	bool m_forks_created;
	T_Logger& m_logger;

};

DiningTable::~DiningTable()
{

	std::vector<Philosopher*>::iterator pbegin = m_pPhils.begin();
	std::vector<Philosopher*>::iterator pend   = m_pPhils.end();
	while(pbegin != pend)
	{
		delete *pbegin;
		pbegin++;
	}
	std::vector<Fork*>::iterator fbegin = m_pForks.begin();
	std::vector<Fork*>::iterator fend   = m_pForks.end();
	while(fbegin != fend)
	{
		delete *fbegin;
		fbegin++;
	}


}

void
DiningTable::create_forks()
{
	for(int i = 0; i < m_numForks; i++)
	{
		try{
			m_pForks.push_back(new Fork());
		}catch(...){
			std::cout << "Error while instantiating forks" << std::endl;
			exit(1);
		}
	}
	m_forks_created = true;
}

bool
DiningTable::start_phil_threads()
{
	if(!m_forks_created)
		return false;

	m_logger.log("Dinner is starting!\n");

	for(int i = 0; i < m_numPhils; i++)
	{
		try{
			m_pPhils.push_back(new Philosopher(m_pForks[i], m_pForks[(i+1)%m_numForks], i + 1, m_logger));
		}catch(...){
			std::cout << "Error while instantiating Philosophers" << std::endl;
			exit(1);
		}
	}
	return true;
}

void
DiningTable::wait_for_dinner_cmpltn()
{
	for(int i = 0; i < m_numPhils; i++)
	{
		m_pPhils[i]->join();
	}

	m_logger.log("Dinner is over!");
}

//=========================================================================

int main()
{
	T_Logger log(std::cout);
	DiningTable dining(NUM_PHIL, NUM_PHIL, log);

	dining.create_forks();
	dining.start_phil_threads();
	dining.wait_for_dinner_cmpltn();
	return 0;
}

