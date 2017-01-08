/*******************************************************************************
 *     Copyright © 2015, 2016 Saman Barghi
 *
 *     This program is free software: you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation, either version 3 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *******************************************************************************/

#include "IOHandler.h"
#include "Network.h"
#include <unistd.h>
#include <sys/types.h>
#include <iostream>
#include <sstream>
#include <time.h>

IOHandler IOHandler::iohandler;
IOHandler::IOHandler(): unblockCounter(0),
                        ioKT(new std::thread(&IOHandler::pollerFunc, (ptr_t)this)),
                        poller(*this), isPolling(ATOMIC_FLAG_INIT){}

void IOHandler::open(PollData &pd){
    assert(pd.fd > 0);
    bool expected = false;

    //If another uThread called opened already, return
    //TODO: use a mutex instead?
    if(!__atomic_compare_exchange_n(&pd.opened, &expected, true, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return;

    //Add the file descriptor to the poller struct
    int res = poller._Open(pd.fd, pd);
    if(res != 0){
        __atomic_exchange_n(&pd.opened, false, __ATOMIC_RELAXED);
        std::cerr << "EPOLL_ERROR: " << errno << std::endl;
        //TODO: this should be changed to an exception
        exit(EXIT_FAILURE);
    }
    //TODO: handle epoll errors
}
void IOHandler::wait(PollData& pd, int flag){
    assert(pd.fd > 0);
    if(flag & Flag::UT_IOREAD) block(pd, true);
    if(flag & Flag::UT_IOWRITE) block(pd, false);
}
void IOHandler::block(PollData &pd, bool isRead){

    if(!pd.opened) open(pd);
    Arachne::ThreadId** utp = isRead ? &pd.rut : &pd.wut;

    Arachne::ThreadId* ut = *utp;

    if(ut == POLL_READY)
    {
        //No need for atomic, as for now only a single uThread can block on
        //read/write for each fd
        *utp = nullptr;  //consume the notification and return;
        return;
    }else if(ut > POLL_WAIT)
        std::cerr << "Exception on open rut" << std::endl;

    //This does not need synchronization, since only a single thread
    //will access it before and after blocking
    pd.isBlockingOnRead = isRead;

    // We do not require immediate suspension because Arachne's block() works
    // correctly even if notifications occur while we are running.
    Arachne::ThreadId *old, *expected;

    // It is fine to store this on the stack because we expect that this ID
    // will no longer be necessary once this function returns, since that means
    // we will have unblocked.
    Arachne::ThreadId utold = Arachne::getThreadId();

    while(true){
        old = *utp;
        expected = nullptr;
        if(old == POLL_READY){
            *utp = nullptr;         //consume the notification do not block
            return;
        }
        if(old != nullptr) // Perhaps someone else is using our pd?
            std::cerr << "Exception on rut"<< std::endl;

        if(__atomic_compare_exchange_n(utp, &expected, &utold, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED ))
            break;
    }

    Arachne::block();

//    kThread::currentKT->currentUT->suspend((funcvoid2_t)IOHandler::postSwitchFunc, (void*)&pd);
    //ask for immediate suspension so the possible closing/notifications do not get lost
    //when epoll returns this ut will be back on readyQueue and pick up from here
}
int IOHandler::close(PollData &pd){

    //unblock pd if blocked
    if(pd.rut > POLL_WAIT)
        unblock(pd, true);
    if(pd.wut > POLL_WAIT);
        unblock(pd, true);

    bool expected = false;
    //another thread is already closing this fd
    if(!__atomic_compare_exchange_n(&pd.closing, &expected, true, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return -1;
    //remove from underlying poll structure
    int res = poller._Close(pd.fd);

    //pd.reset();
    //TODO: handle epoll errors
    pd.reset();
    pollCache.pushPollData(&pd);
    return res;
}

ssize_t IOHandler::poll(int timeout, int flag){
    if( poller._Poll(timeout) < 0) return -1;
    return 0;
}

void IOHandler::reset(PollData& pd){
    pd.reset();
}

bool IOHandler::unblock(PollData &pd, bool isRead){

    //if it's closing no need to process
    if(pd.closing) return false;

    Arachne::ThreadId** utp = isRead ? &pd.rut : &pd.wut;
    Arachne::ThreadId* old;

    while(true){
        old = *utp;
        if(old == POLL_READY) return false;
        //For now only if io is ready we call the unblock
        Arachne::ThreadId* utnew = nullptr;
        if(old == nullptr || old == POLL_WAIT) utnew = POLL_READY;
        if(__atomic_compare_exchange_n(utp, &old, utnew, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)){
            if(old > POLL_WAIT){
                Arachne::signal(*old);
                return true;
            }
            break;
        }
    }
    return false;
}

void IOHandler::PollReady(PollData &pd, int flag){
    if( (flag & Flag::UT_IOREAD) && unblock(pd, true)) unblockCounter++;
    if( (flag & Flag::UT_IOWRITE) && unblock(pd, false)) unblockCounter++;
}

ssize_t IOHandler::nonblockingPoll(){
    ssize_t counter = -1;

#ifndef NPOLLNONBLOCKING
    if(!isPolling.test_and_set(std::memory_order_acquire)){
        //do a nonblocking poll
        counter = poll(0,0);
        isPolling.clear(std::memory_order_release);
    }
#endif //NPOLLNONBLOCKING
    return counter;
}

void IOHandler::pollerFunc(void* ioh){
    IOHandler* cioh = (IOHandler*)ioh;
    while(true){
        //do a blocking poll
        cioh->poll(-1, 0);
    }
}
