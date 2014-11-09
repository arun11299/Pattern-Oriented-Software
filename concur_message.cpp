/*
 * g++ version : 4.1.2
 * ACE version : 5.6.1_64b
 * Compilation options : g++ -o concur_message concur_message.cpp -I /usr/local/local64/ACE_wrappers-5.6.1_64b -L /usr/local/local64/ACE_wrappers-5.6.1_64b/lib -lACE -lm
 * Arun Muralidharan
 */

#include <iostream>
#include <vector>
#include <exception>
#include <cstdlib>
#include <ace/Thread.h>
#include <ace/Synch.h>

// Easier to read thread related typedefs
typedef ACE_Thread    Thread;
typedef ACE_thread_t  thread_t;
typedef ACE_hthread_t thread_handle_t;

// Number of threads in the process
#define NUM_THREADS  2
#define ITERATIONS   5 
#define SLEEP_PERIOD 1

// Messages to be passed to threads
static const char* Messages[] = {"Ping!", "Pong!"};



/*
 * @desc: This structure holds the Mutex, Condition Variable
 *        and the thread ID.
 * Also provides functionalities for the thread to notify
 * and wait on a condition variable.     
 */
struct SyncArgs
{
    public:
        SyncArgs():mutex_(), cond_(mutex_),thread_ID(0){}

    public: /* Returning Message  based on thread ID*/ 

        /* Return the thread ID Based on which
         * the thread will fetch the Message from pool
         */
        int initialize()
        {
            ACE_Guard<ACE_Thread_Mutex> guard(this->mutex_);
            if(thread_ID > 1)
            {
                std::cout << "ERROR: Array Out Of Bounds" << std::endl;
                return -1;
            }
            return thread_ID++;
        }

        void print(const char* msg)
        {
            ACE_Guard<ACE_Thread_Mutex> guard(this->mutex_);
            std::cout << msg << std::endl;
        }

        void print_and_wait(const char* msg)
        {
            ACE_Guard<ACE_Thread_Mutex> guard(this->mutex_);
            std::cout << msg << std::endl;
            this->notify_and_wait();
        }

        void notify_others()
        {
            ACE_Guard<ACE_Thread_Mutex> guard(this->mutex_);
            this->notify();
        }

    private:/* Notification And Waiting */
        void notify_and_wait()
        {
            cond_.signal();
            cond_.wait();
        }
        void notify(){cond_.signal();}
        void wait(){cond_.wait();}

    public:
        ACE_Thread_Mutex mutex_;
        ACE_Thread_Condition<ACE_Thread_Mutex> cond_;

    private:
        int thread_ID;
};

//-------------------------------------------------------------------------------

/*
 * @desc: Functions of this class are declared static
 *        and are used by the threads to do their job.    
 */

class Task
{
    public: /* Structors */
        Task(){}
        virtual ~Task(){}

    public:
        static void* work(void* /*Arg*/);
        static void do_extra();
};

void*
Task::work(void* args)
{
    SyncArgs* arg = reinterpret_cast<SyncArgs *>(args);
    int iter = 0;
    const char* msg = NULL;

    // One time thread initialization
    int ret = arg->initialize(); 
    if(ret == -1)
        return NULL;

    msg = Messages[ret];

    while (iter++ < ITERATIONS)
    {
        arg->print_and_wait(msg);
    }

    // Notify other waiting threads before exiting
    arg->notify_others();

    return NULL;
}

void
Task::do_extra()
{
    ACE_OS::sleep(SLEEP_PERIOD); 
    return;
}


//---------------------------------------------------------------------------------


/*
 * @desc: This class manages the threads (Currently provides limited functionality)
 *        It provides interface to outside, to spawn and start N number of threads.
    It does the following:
       a. Creates thread instance
       b. Assigna work for the threads.
       c. Waits for the threads to finish.
       d. prints Done!.
       e. provides function implementation for the above.
 */

class ThreadManager
{
    public: /* Structors */
        ThreadManager():m_num_threads(1),
                        mp_thread_id(NULL),
                        mp_thread_handle(NULL),
                        m_args(){}

        ThreadManager(int nthreads):m_num_threads(nthreads),
                                      mp_thread_id(NULL),
                                      mp_thread_handle(NULL),
                                      m_args(){}  

        virtual ~ThreadManager()
        {
            delete[] mp_thread_id;
            delete[] mp_thread_handle;  
        }

    private: /* This Class is non-copyable and non-assignable */

        ThreadManager(const ThreadManager& other){};
        void operator= (const ThreadManager other){};

    /* Public interfaces */
    public:
        bool create_thread_inst();
        bool spawn();

    protected:
        void wait_till_finish();

    private:
        int m_num_threads;
        thread_t         *mp_thread_id;
        thread_handle_t  *mp_thread_handle;

    private:
        struct SyncArgs m_args;
        
};

bool
ThreadManager::create_thread_inst()
{
    try
    {
        this->mp_thread_id     = new thread_t[m_num_threads + 1];
        this->mp_thread_handle = new thread_handle_t[m_num_threads + 1]; 
    }
    catch(const std::bad_alloc& e)
    {
        delete[] mp_thread_id;
        delete[] mp_thread_handle;
        std::cout << "Runtime error detected while allocating memory" << std::endl;
        std::cout << e.what() << std::endl;

        exit(1);
    }
    catch(...)
    {
        delete[] mp_thread_id;
        delete[] mp_thread_handle;
        std::cout << "Unknown Run Time error detected" << std::endl;
    
        exit(2);
    }

    if(this->mp_thread_id == NULL || this->mp_thread_handle == NULL)
        return false;

    return true;
}

bool
ThreadManager::spawn()
{
    std::cout << "Ready... Set... Go!" << std::endl;
    for(int i = 0; i < m_num_threads ; i++)
    {
        int ret = Thread::spawn((ACE_THR_FUNC)Task::work, (void*)&m_args, 
                                THR_JOINABLE|THR_NEW_LWP, &this->mp_thread_id[i], &this->mp_thread_handle[i], 
                                ACE_DEFAULT_THREAD_PRIORITY, 0, 0, 0);    
        if(ret == -1)
        {
            std::cerr << "Failed while spawning." << std::endl;
            return false;
        }
    
    }

    wait_till_finish();
    std::cout << "Done!" << std::endl;

    return true;
}

void
ThreadManager::wait_till_finish()
{
    /* Wait for All threads to finish */
    for(int i = 0; i < m_num_threads ; i++)
    {
        Thread::join(mp_thread_handle[i]);
    }

    return;
}

//------------------------------------------------------------------------------------------------------------

int main()
{
    ThreadManager thr_mgr(NUM_THREADS);
    if(thr_mgr.create_thread_inst())
    {
        thr_mgr.spawn();
    }
    return 0;
}
