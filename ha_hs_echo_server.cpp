/*
 * Platform : Linux
 * g++ version: 4.1.2
 * ACE version: 5.6.1
 * Compilation : g++ -o ha_hs_echo_server ha_hs_echo_server.cpp -I /usr/local/local64/ACE_wrappers-5.6.1_64b -L /usr/local/local64/ACE_wrappers-5.6.1_64b/lib -lACE
 * 
 * Listening Port : 25001
 * Design highly influenced by Dr. Doug Schmidt PA 3 solution    
 */

#include <iostream> 
#include <cstdlib>
#include <cstring>

#include <ace/INET_Addr.h>
#include <ace/SOCK_Stream.h>
#include <ace/Reactor.h>
#include <ace/Acceptor.h>
#include <ace/Svc_Handler.h>
#include <ace/SOCK_Acceptor.h>
#include <ace/Task.h>

#include <ace/OS_NS_unistd.h>
#include <ace/OS_main.h>


// define max bytes that will be recieved at a time
#define MAXBUF 5000
#define PORT_TO_LISTEN 60000

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

#define HWM 20971520      // High water mark set to 20MB

#define BUF_EVENT_SIZE 5000

static const int TIMEOUT_IN_SECS = 60;


/* @desc: Echo_Task implements the Half sync portion of
 * the Half-Sync/Half-Async Pattern.
 * There are 4 threads configured to read the message from
 * the synchronized ACE_Message_Queue.
 * The message received has both the SOCK_Stream pointer
 * and the actual network message recieved .
 */


class Echo_Task: public ACE_Task<ACE_MT_SYNCH>
{
public:
    static const int HIGH_WATER_MARK = 1024 * 20000;
    static const int MAX_THREADS     = 4;
public:
    Echo_Task()
    {
        msg_queue()->high_water_mark(HIGH_WATER_MARK);
        this->activate(THR_NEW_LWP, MAX_THREADS);
    }

    virtual int svc();
    virtual int put(ACE_Message_Block* client_input, ACE_Time_Value* timeout = 0)
    {
        return putq(client_input,timeout);
    }
            

};

int
Echo_Task::svc()
{
    for(ACE_Message_Block* input; getq(input) != -1;)
    {
        ACE_SOCK_Stream* stream = reinterpret_cast<ACE_SOCK_Stream*>(input->rd_ptr()) ;
        //send the message in blocking manner 
        ssize_t bytes_sent = stream->send(input->cont()->rd_ptr(), input->cont()->length());
        std::cout << "bytes sent back to network: " << bytes_sent << std::endl;
        if(bytes_sent == -1)
        {
            std::cout << "Error while sending message" << std::endl;
        }
        input->release();
    }

    return 0;
}

// ------------------------ Echo Task Sync Ends here -----------------------------



/* Echo_Svc_Handler : Implements Half ASync Layer of the HS/HA pattern
 * This class puts the received messages from network to the ACE_Task 
 * Message Queue.
 * Reordering of the messages not handled using suspend and resume handler
 * because of cyclic dependency issue and cannot be resolved by using header file
 * in this course.
 */

 
class Echo_Svc_Handler: public ACE_Svc_Handler<ACE_SOCK_Stream, ACE_NULL_SYNCH>
{
public:
    typedef ACE_Svc_Handler<ACE_SOCK_Stream, ACE_NULL_SYNCH> PARENT;

public:
    Echo_Svc_Handler():PARENT(){}
    Echo_Svc_Handler(Echo_Task* task):m_task(task), 
                                      m_client_data(0),
                                      m_incomplete_data(0){ACE_OS::memset(peer_name, 0, sizeof(peer_name));}

public:
    virtual int open(void*); 

    virtual int handle_timeout(ACE_Time_Value const&, void const*)
    {
        std::cout << "Removing Handler for client : " << peer_name << " due to timeout" << std::endl;
        this->reactor()->remove_handler(this, ACE_Event_Handler::READ_MASK);
        return 0;
    }
    
    virtual int handle_input(ACE_HANDLE /* handle */);

public:
    virtual int suspend_handler();
    virtual int resume_handler();

protected:
    virtual int recv_from_network(ACE_Message_Block*& /*mb*/);   
    virtual int try_delegate_to_task(ACE_Message_Block*& /*mb*/, int /*recv_bytes*/);
    virtual int check_if_new_line(ACE_Message_Block*& /*mb*/, int /*recv_bytes*/);
    virtual int reschedule_timer();
    
private:
    char peer_name[MAXHOSTNAMELEN];
    Echo_Task* m_task;
    ACE_Message_Block* m_client_data;
    ACE_Message_Block* m_incomplete_data;
     
};

int 
Echo_Svc_Handler::open(void* acceptor)
{
    msg_queue()->high_water_mark(HWM) ;
    
    if(reactor()->schedule_timer(this, 0, ACE_Time_Value(TIMEOUT_IN_SECS)) == -1)
    {
        std::cout << "Error while scheduling timer" << std::endl;
        return -1;
    }
    /*
    else if(this->peer().enable(ACE_NONBLOCK) == -1)
    {
        std::cout << "Error while setting the handle as non-blocking" << std::endl;
        return -1;
    }
    */
    else if(PARENT::open(acceptor) == -1)
    {
        std::cout << "Error while accepting connection" << std::endl;
        return -1;
    }
    else
    {
        ACE_INET_Addr peer_addr;

        if(this->peer().get_remote_addr(peer_addr) == 0
            &&
            peer_addr.addr_to_string(peer_name, MAXHOSTNAMELEN) == 0)
        {
            std::cout << "Connection received from : " << peer_name << std::endl;
        }
    }

}

int 
Echo_Svc_Handler::handle_input(ACE_HANDLE handle)
{
    ACE_Message_Block* recv_data = 0;
    // Reschedule the timer
    if(reschedule_timer() == -1)   
    {
        std::cout << "reschedule_timer FAILED!!" << std::endl;
        return -1;
    }
    else
    {
        int read_bytes = recv_from_network(recv_data); 
        switch(read_bytes)
        {
            case -1:
                return -1;
            case 0:
                std::cout << "Client " << peer_name << " disconnected." << std::endl;
                return 0; 
            default:
                if(try_delegate_to_task(recv_data, read_bytes) == -1)
                    return 0;
                else
                    this->suspend_handler();
                    this->resume_handler();
                    return 0; 
        }
    } // end else part
}


int 
Echo_Svc_Handler::recv_from_network(ACE_Message_Block*& recv_data)
{
    ACE_NEW_RETURN(recv_data, ACE_Message_Block(BUF_EVENT_SIZE), -1) ;

    ssize_t bytes_read = this->peer().recv(recv_data->wr_ptr(), BUF_EVENT_SIZE);
    std::cout << "Bytes received from network : " << bytes_read << std::endl;
    switch(bytes_read)
    {
        case -1:
            if (errno == EWOULDBLOCK)
                return 0;
            else
                return -1;
        case 0:
            return -1;
        default:
            recv_data->wr_ptr(bytes_read);
            return bytes_read;
    }
}

int
Echo_Svc_Handler::try_delegate_to_task(ACE_Message_Block*& recv_data, int read_bytes)
{
    /* 1. Check if new line is detected.
     * 2. a. if new line detected, send the event to the task queue (along with instance/pointer of svc_handler)
     *    b. store the remaining of the event in m_client_data
     *    c. suspend the event handler
     */   

    // Check if there is data remaining 
    if(msg_queue()->is_empty())
    {
        /* Check if we have new line, if new line present, put it
         * to the tasks's queue, put the remaining data to msg_queue. 
         */
        if(check_if_new_line(recv_data, read_bytes) == -1)
        {
            std::cout << "Incomplete message received" << std::endl;
            if(this->m_incomplete_data == 0)
            {
                std::cout << "First time incomplete" << std::endl;
                ACE_NEW_RETURN(m_incomplete_data, 
                               ACE_Message_Block(recv_data->rd_ptr(), recv_data->length()), -1);
                m_incomplete_data->wr_ptr(recv_data->length());
            }
            else
            {
                //Append the message to previously stored data
                m_incomplete_data->copy(recv_data->rd_ptr(), recv_data->length());
                m_incomplete_data->wr_ptr(recv_data->length());
            }
            
            return -1;
        }
        else
        {
            // TODO: append of incomplete data missed here
            ACE_Message_Block* sync_msg = 0;
            ACE_NEW_RETURN(sync_msg, ACE_Message_Block(reinterpret_cast<char *>(&this->peer_)), -1);
            sync_msg->cont(this->m_client_data);
            this->m_task->put(sync_msg);
            return 0;
        }    
    } // end if part
}

int
Echo_Svc_Handler::check_if_new_line(ACE_Message_Block*& recv_data, int read_bytes)
{
    char *term = NULL;
    bool send = false;
    if( (term = strstr(recv_data->rd_ptr(), "\r\n")) != NULL) 
    {
        term += 2;
        send = true;
    }
    else if( (term = strchr(recv_data->rd_ptr(), '\r')) != NULL)
    {
        term += 1;
        send = true;
    }
    else if( (term = strchr(recv_data->rd_ptr(), '\n')) != NULL)
    {
        term += 1;
        send = true;
    }
    else {}

    if(send)
    {
        // Check if there is any incomplete data to be added
        if(m_incomplete_data != 0) 
        {
            ACE_NEW_RETURN(m_client_data, ACE_Message_Block(m_incomplete_data->rd_ptr(), m_incomplete_data->length()), -1);
            m_client_data->wr_ptr(m_incomplete_data->length());
            m_client_data->copy(recv_data->rd_ptr(), recv_data->length());
            m_client_data->wr_ptr(recv_data->length());
            m_incomplete_data->release();
            m_incomplete_data = 0;
        }
        else
        {
            ACE_NEW_RETURN(m_client_data, ACE_Message_Block(recv_data->rd_ptr(), recv_data->length()) , -1);
            m_client_data->wr_ptr(recv_data->length());
            if(read_bytes != recv_data->length())
            {
                recv_data->rd_ptr(recv_data->length());
                ACE_NEW_RETURN(m_incomplete_data, ACE_Message_Block(recv_data->rd_ptr(), recv_data->length()),-1);
                m_incomplete_data->wr_ptr(recv_data->length());
            }
        }
        return 0;
    }

    return -1;
}

int
Echo_Svc_Handler::suspend_handler()
{
    return this->reactor()->suspend_handler(this); 
}



int 
Echo_Svc_Handler::resume_handler()
{
    return this->reactor()->resume_handler(this);
}


int
Echo_Svc_Handler::reschedule_timer()
{
    // cancel the current running timer
    if(this->reactor()->cancel_timer(this) == -1)
    {
        std::cout << "Error while cancelling timer" << std::endl;
        return -1;
    }
    else if(this->reactor()->schedule_timer(this, 0, ACE_Time_Value(TIMEOUT_IN_SECS)) == -1)
    {
        std::cout << "Error while scheduling timer" << std::endl;
        return -1;
    }
    else
        return 0;
}



// --------------------- Half ASync Layer Ends Here ---------------------------------


/*
 * Acceptor part of the Acceptor-Connector framework 
 *
 */

class Echo_Acceptor: public ACE_Acceptor<Echo_Svc_Handler, ACE_SOCK_Acceptor>
{
public:
    typedef ACE_Acceptor<Echo_Svc_Handler, ACE_SOCK_Acceptor> PARENT;

public:
    virtual int open(ACE_INET_Addr /*addr*/, ACE_Reactor* /*reactor*/);
    virtual int make_svc_handler(Echo_Svc_Handler *& /*sh*/);

private:
    Echo_Task m_task;
};


int
Echo_Acceptor::open(ACE_INET_Addr addr, ACE_Reactor* reactor)
{
    std::cout << "Acceptor has started..." << std::endl;
    return PARENT::open(addr, reactor);
}

int 
Echo_Acceptor::make_svc_handler(Echo_Svc_Handler*& svc_handler)
{
    if(svc_handler == 0)
    {
        ACE_NEW_RETURN(svc_handler, Echo_Svc_Handler(&m_task), -1);
        svc_handler->reactor(this->reactor());
    }
    return 0;
}

// ------------------- Acceptor part ends here ------------------------------


int main()
{
    ACE_Reactor reactor;
    Echo_Acceptor echo_acceptor; 

    if (echo_acceptor.open (ACE_INET_Addr(25001),
                          &reactor) == -1)
        return 1;

    reactor.run_reactor_event_loop ();
    return 0;
}
