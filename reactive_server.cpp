/*
 * Platform : Linux
 * g++ version: 4.1.2
 * ACE version: 5.6.1
 * Compilation : g++ -o reactive_server reactive_server.cpp -I /usr/local/local64/ACE_wrappers-5.6.1_64b -L /usr/local/local64/ACE_wrappers-5.6.1_64b/lib -lACE
 */

#include <iostream>
#include <vector>
#include <iterator>
#include <cstdlib>

#include <ace/INET_Addr.h>
#include <ace/SOCK_Stream.h>
#include <ace/Reactor.h>
#include <ace/Acceptor.h>
#include <ace/Svc_Handler.h>
#include <ace/SOCK_Acceptor.h>

// define max bytes that will be recieved at a time
#define MAXBUF 5000

#define PORT_TO_LISTEN 60000

//Fwd decl
class Echo_Svc_handler;


typedef ACE_Acceptor<Echo_Svc_handler, ACE_SOCK_Acceptor> ServAcceptor;

class Echo_Svc_handler : public ACE_Svc_Handler<ACE_SOCK_Stream, ACE_NULL_SYNCH>
{
private:
    typedef ACE_Svc_Handler<ACE_SOCK_Stream, ACE_NULL_SYNCH> PARENT;
public:
    // Called by acceptor, once a connection has been accepted
    virtual int open(void*);

    // Called when input is available from client
    virtual int handle_input(ACE_HANDLE fd = ACE_INVALID_HANDLE);

    //Called when handler is removed from the ACE Reactor
    virtual int handle_close(ACE_HANDLE fd, ACE_Reactor_Mask close_mask);

public: // typedef's
    typedef std::vector<unsigned char>::iterator buff_iter;

private:
    //Data storage buffer
    std::vector<unsigned char> buff_;   // message receive buffer
    std::vector<unsigned char> backlog_;// message flow control/
    std::vector<unsigned char> snd_buff;// message send buffer
};

int
Echo_Svc_handler::open(void* par) 
{
    if(PARENT::open(par) == -1)
        return -1;

    char peer_name[MAXHOSTNAMELEN];
    ACE_INET_Addr peer_addr;

    if(this->peer().get_remote_addr(peer_addr) == 0 
       &&
       peer_addr.addr_to_string(peer_name, MAXHOSTNAMELEN) == 0)
    {
        std::cout << "Connection received from : " << peer_name << std::endl;
    }

    return 0;
}


int 
Echo_Svc_handler::handle_input(ACE_HANDLE fd)
{
    buff_.resize(MAXBUF);
    ssize_t recv_bytes, sent_bytes;
    snd_buff.clear();

    recv_bytes = this->peer().recv(&buff_[0], MAXBUF);
    if(recv_bytes == -1)
        return -1;

    if(recv_bytes == 0) // Socket close condition
        return 0;

    buff_.resize(recv_bytes);

    /* __ Parsing Strategy __
     *
     * 1. The input is considered as a stream (binary, not ascii or string)   
     * 2. As we dont have any definite protocol structure, just EOL is considered
     *    as end of an "event". Pls NOTE that this makes the parsing look bit difficult.
     * 3. There can be scenarios, where we dont receive any EOL, in that case
     *    the server, takes a backup of the data, so that it can be echoed when it receives EOL.
     * 4. NOTE that case no. 3 is a special scenario, which wont be noticed when using TELNET.
     * 5. Use of vector<char> instead of ACE_Message_Block is for experimental purpose.
     */ 

    buff_iter begin(buff_.begin());
    buff_iter end(buff_.end());

    std::reverse_iterator<buff_iter>  rev_end(begin);
    std::reverse_iterator<buff_iter>  rev_iter(end);

    if(backlog_.size() != 0)
    {
        snd_buff.insert(snd_buff.end(), backlog_.begin(), backlog_.end());
    }
    int byte_count = 0;
    

    while(rev_iter < rev_end)
    {
        if(*rev_iter == '\n' || *rev_iter == '\r') // all the 3 cases can be handled with this check, '\r', '\n', '\r\n'
        {
            break;
        }
        byte_count++;
        rev_iter++;
    } // end while

    int copy_bytes = buff_.size() - byte_count;

    snd_buff.insert(snd_buff.end(), buff_.begin(), buff_.begin() + copy_bytes);

    if(byte_count > 0)
        backlog_.insert(backlog_.end(), buff_.begin()+copy_bytes - 1, buff_.end());

    buff_.clear();

    this->peer().send(&snd_buff[0], static_cast<size_t>(snd_buff.size()));
    return 0;
}

int 
Echo_Svc_handler::handle_close(ACE_HANDLE fd, ACE_Reactor_Mask close_mask)
{
    PARENT::handle_close(fd, close_mask);
    delete this;
    return 0;
}

//===========================================================================

int main()
{
    ACE_INET_Addr server_addr;
    if(server_addr.set(PORT_TO_LISTEN, INADDR_ANY) == -1)
        return 1;

    ServAcceptor acceptor;

    if(acceptor.open(server_addr) == -1)
        return 1;

    ACE_Reactor::instance()->run_reactor_event_loop();
    return 0;
}
