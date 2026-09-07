#ifndef _MSGQUEUE_H
#define _MSGQUEUE_H

#include "_global.h"
#include "concurrentqueue/blockingconcurrentqueue.h"

#define MAX_MESSAGES 256

// Message structure
struct MESSAGE
{
    int msg = -1;
    duint param1 = 0;
    duint param2 = 0;
};

// Message stack structure
class MESSAGE_QUEUE
{
public:
    moodycamel::BlockingConcurrentQueue<MESSAGE> msgs;

    int WaitingCalls = 0; // Number of threads waiting
    bool Destroy = false; // Destroy stack as soon as possible
};

// Function definitions
MESSAGE_QUEUE* MsgAllocQueue();
void MsgFreeQueue(MESSAGE_QUEUE* Stack);
bool MsgSend(MESSAGE_QUEUE* Stack, int Msg, duint Param1, duint Param2);
bool MsgGet(MESSAGE_QUEUE* Stack, MESSAGE* Msg);
void MsgWait(MESSAGE_QUEUE* Stack, MESSAGE* Msg);

#endif // _MSGQUEUE_H
