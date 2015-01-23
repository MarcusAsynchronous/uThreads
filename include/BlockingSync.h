/*
 * BlockingSync.h
 *
 *  Created on: Nov 10, 2014
 *      Author: Saman Barghi
 */

#ifndef BLOCKINGSYNC_H_
#define BLOCKINGSYNC_H_

#include "EmbeddedList.h"
#include "kThread.h"
#include <cassert>
#include <mutex>
#include <iostream>

class Mutex;
template<typename Buffer> class MessageQueue;

class BlockingQueue {
    friend class uThread;
    friend class Mutex;
    friend class ConditionVariable;
    friend class Semaphore;
    template<typename Buffer> friend class MessageQueue;
    EmbeddedList<uThread> queue;

    bool suspend(std::mutex& lock);
    bool suspend(Mutex&);
//  bool suspend(mutex& lock, mword timeout);
//
    BlockingQueue(const BlockingQueue&) = delete;                  // no copy
    const BlockingQueue& operator=(const BlockingQueue&) = delete; // no assignment
public:
    BlockingQueue() = default;
    ~BlockingQueue() {
        assert(empty());
    }
    bool empty() const {
        return queue.empty();
    }

    // Thread*& passed to support atomic setting of Mutex::owner
    bool signal(std::mutex& lock,uThread*& owner);			//Used along with Mutex
    bool signal(std::mutex& lock){uThread* dummy = nullptr; signal(lock, dummy);}			//Used along with Mutex
    bool signal(Mutex&);						//Used with ConditionVariable
    void signalAll(Mutex&);						//Used with ConditionVariable
};

class Mutex {
    friend class ConditionVariable;
    friend class Semaphore;
    friend class BlockingQueue;
protected:
    std::mutex lock;
//	std::unique_lock<std::mutex> lock; //Create a unique_lock but do not lock the mutex yet
    BlockingQueue bq;
    uThread* owner;

    template<bool OL> // OL: owner lock
    bool internalAcquire(mword timeout) {

        lock.lock();
        assert(OL || owner != kThread::currentKT->currentUT);//Mutex cannot have recursive locking
        if fastpath(owner != nullptr && (!OL || owner != kThread::currentKT->currentUT)) {
            return bq.suspend(lock); // release lock, false: timeout
        } else {
            /*
             * End up here if:
             * 	- No one is holding the lock
             * 	- Someone is holding the lock and:
             * 		+ The type is OwnlerLock and the calling thread is holding the lock
             * Otherwise, block and wait for the lock
             */
            owner = kThread::currentKT->currentUT;
            lock.unlock();
            return true;
        }
    }

    void internalRelease() {
        // try baton-passing, if yes: 'signal' releases lock
        if fastpath(bq.signal(lock, owner)) return;
        owner = nullptr;
        lock.unlock();
    }

public:
    Mutex() : owner(nullptr) {}

    template<bool OL=false>
    bool acquire() {return internalAcquire<OL>(0);}

//  template<bool OL=false>
//  bool tryAcquire(mword t = 0) { return internalAcquire<true,OL>(t); }

    void release() {
        lock.lock();
        assert(owner == kThread::currentKT->currentUT);//Attempt to release lock by non-owner
        internalRelease();
    }
};

class OwnerLock: protected Mutex {
    mword counter;
public:
    OwnerLock() :
            counter(0) {
    }
    mword acquire() {
        if (internalAcquire<true>(0))
            return ++counter;
        else
            return 0;
    }
//  mword tryAcquire(mword t = 0) {
//    if (internalAcquire<true,true>(t)) return ++counter;
//    else return 0;
//  }
    mword release() {
        lock.lock();
        assert(owner == kThread::currentKT->currentUT); //, "attempt to release lock by non-owner");
        if (--counter == 0)
            internalRelease();
        else
            lock.unlock();
        return counter;
    }
};

class ConditionVariable {
    BlockingQueue bq;

public:
    bool empty() const {
        return bq.empty();
    }
    void wait(Mutex& mutex) {
        bq.suspend(mutex);
        mutex.acquire();
    }
    void signal(Mutex& mutex) {
        if slowpath(!bq.signal(mutex)) mutex.release();}
    void signalAll(Mutex& mutex) {bq.signalAll(mutex);}
};

class Semaphore {
    Mutex mutex;
    BlockingQueue bq;
    mword counter;

    template<bool TO>
    bool internalP(mword timeout) {
        if fastpath(counter < 1) {
            return bq.suspend(mutex); // release lock, false: timeout
        } else {
            counter -= 1;
            mutex.release();
            return true;
        }
    }

public:
    explicit Semaphore(mword c = 0) : counter(c) {}
    bool empty() const {return bq.empty();}

    bool P() {
        mutex.acquire();
        return internalP<false>(0);
    }

//	bool tryP(mword t = 0, SpinLock* l = nullptr) {
//		if (l) lock.acquire(*l); else lock.acquire();
//		return internalP<true>(t);
//	}

    void V() {
        mutex.acquire();
        // try baton-passing, if yes: 'signal' releases lock
        if fastpath(bq.signal(mutex)) return;
        counter += 1;
        mutex.release();
    }
};

#endif /* BLOCKINGSYNC_H_ */
