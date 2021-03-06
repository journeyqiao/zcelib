
#ifndef ZCE_LIB_THREAD_BUS_PIPE_H_
#define ZCE_LIB_THREAD_BUS_PIPE_H_

//2012年9月18日晚上，中国人民抗日81周年纪念日，81年来，我们哪一天不在抗“日”

#include "zce_boost_non_copyable.h"
#include "zce_lockfree_kfifo.h"
#include "zce_os_adapt_file.h"
#include "zce_os_adapt_error.h"
#include "zce_share_mem_mmap.h"
#include "zce_trace_debugging.h"
#include "zce_lock_null_lock.h"
#include "zce_lock_thread_mutex.h"

namespace ZCE_LIB
{
class dequechunk_node;
class shm_dequechunk;
};

//线程使用的双向BUS管道，
//不要直接使用这个类，使用下面的两个typedef
template <typename ZCE_LOCK>
class ZCE_Thread_Bus_Pipe : public ZCE_NON_Copyable
{

protected:

    //内部的枚举，PIPE的编号，外部不用了解
    enum ZCE_BUS_PIPE_ID
    {
        THR_RECV_PIPE_ID     = 0,
        THR_SEND_PIPE_ID     = 1,

        //长度标示,不要用于做函数参数,否则会有溢出
        THR_NUM_OF_PIPE      = 2,
    };

protected:

    //管道用的内存空间地址
    char                      *pipe_buffer_;

    //管道配置长度,2个管道的配置长度,
    size_t                     size_pipe_[THR_NUM_OF_PIPE];
    //发送管道的实际空间长度
    size_t                     size_room_[THR_NUM_OF_PIPE];

    //N个管道,比如接收管道,发送管道……,最大MAX_NUMBER_OF_PIPE个
    ZCE_LIB::shm_dequechunk  *bus_pipe_[THR_NUM_OF_PIPE];

    //锁
    ZCE_LOCK                   bus_lock_[THR_NUM_OF_PIPE];

protected:
    //instance函数使用的东西
    static ZCE_Thread_Bus_Pipe *instance_;

public:
    //构造函数,允许你有多个实例的可能，不做保护
    ZCE_Thread_Bus_Pipe():
        pipe_buffer_(NULL)
    {
        for (size_t i = 0; i < THR_NUM_OF_PIPE; ++i )
        {
            size_pipe_[i] = 0;
            size_room_[i] = 0;
            bus_pipe_[i] = NULL;
        }

    }
    //析购函数
    ~ZCE_Thread_Bus_Pipe()
    {

    }

public:

    //-----------------------------------------------------------------
    //初始化部分参数,允许发送和接收的长度不一致
    int initialize(size_t size_recv_pipe,
                   size_t size_send_pipe,
                   size_t max_frame_len)
    {
        ZCE_ASSERT(NULL == pipe_buffer_);


        size_pipe_[THR_RECV_PIPE_ID] = size_recv_pipe;
        size_pipe_[THR_SEND_PIPE_ID] = size_send_pipe;

        size_room_[THR_RECV_PIPE_ID] = ZCE_LIB::shm_dequechunk::getallocsize(size_pipe_[THR_RECV_PIPE_ID]);
        size_room_[THR_SEND_PIPE_ID] = ZCE_LIB::shm_dequechunk::getallocsize(size_pipe_[THR_SEND_PIPE_ID]);

        //
        const size_t FIXED_INTERVALS = 16;
        size_t sz_malloc = size_room_[THR_RECV_PIPE_ID] + size_room_[THR_SEND_PIPE_ID] + FIXED_INTERVALS * 2;

        pipe_buffer_ = new char [sz_malloc ];

        //初始化内存
        bus_pipe_[THR_RECV_PIPE_ID] = ZCE_LIB::shm_dequechunk::initialize(size_pipe_[THR_RECV_PIPE_ID],
                                                                          max_frame_len,
                                                                          pipe_buffer_,
                                                                          false);

        bus_pipe_[THR_SEND_PIPE_ID] = ZCE_LIB::shm_dequechunk::initialize(size_pipe_[THR_SEND_PIPE_ID],
                                                                          max_frame_len,
                                                                          pipe_buffer_ + size_room_[THR_RECV_PIPE_ID] + FIXED_INTERVALS,
                                                                          false);

        //管道创建自己也会检查是否能恢复
        if ( NULL == bus_pipe_[THR_RECV_PIPE_ID]  || NULL == bus_pipe_[THR_SEND_PIPE_ID])
        {
            ZCE_LOG(RS_ERROR, "[zcelib] ZCE_Thread_Bus_Pipe::initialize pipe fail recv[%p]size[%u],send[%p],size[%u].",
                    bus_pipe_[THR_RECV_PIPE_ID],
                    size_pipe_[THR_RECV_PIPE_ID],
                    bus_pipe_[THR_SEND_PIPE_ID],
                    size_pipe_[THR_SEND_PIPE_ID]
                   );
            return -1;
        }

        return 0;
    }



    //-----------------------------------------------------------------
    //注意

    //从RECV管道读取数据，
    inline bool pop_front_recvpipe(ZCE_LIB::dequechunk_node * &node)
    {
        ZCE_Lock_Guard<ZCE_LOCK> lock_guard(bus_lock_[THR_RECV_PIPE_ID]);
        return bus_pipe_[THR_RECV_PIPE_ID]->pop_front(node);
    }

    //向RECV管道写入数据
    inline bool push_back_recvpipe(const ZCE_LIB::dequechunk_node *node)
    {
        ZCE_Lock_Guard<ZCE_LOCK> lock_guard(bus_lock_[THR_RECV_PIPE_ID]);
        return bus_pipe_[THR_RECV_PIPE_ID]->push_end(node);
    }


    //从SEND管道读取数据，
    inline bool pop_front_sendpipe(ZCE_LIB::dequechunk_node * &node)
    {
        ZCE_Lock_Guard<ZCE_LOCK> lock_guard(bus_lock_[THR_SEND_PIPE_ID]);
        return bus_pipe_[THR_SEND_PIPE_ID]->pop_front(node);
    }

    //向SEND管道写入数据
    inline bool push_back_sendpipe(const ZCE_LIB::dequechunk_node *node)
    {
        ZCE_Lock_Guard<ZCE_LOCK> lock_guard(bus_lock_[THR_SEND_PIPE_ID]);
        return bus_pipe_[THR_SEND_PIPE_ID]->push_end(node);
    }


    //取Recv管道头的帧长
    inline int get_frontsize_recvpipe(size_t &note_size)
    {
        ZCE_Lock_Guard<ZCE_LOCK> lock_guard(bus_lock_[THR_RECV_PIPE_ID]);

        if (bus_pipe_[THR_RECV_PIPE_ID] ->empty())
        {
            return -1;
        }

        note_size = bus_pipe_[THR_RECV_PIPE_ID]->get_front_len();
        return 0;
    }

    //取Send管道头的帧长
    inline int get_frontsize_sendpipe(size_t &note_size)
    {
        ZCE_Lock_Guard<ZCE_LOCK> lock_guard(bus_lock_[THR_SEND_PIPE_ID]);

        if (bus_pipe_[THR_SEND_PIPE_ID] ->empty())
        {
            return -1;
        }

        note_size =  bus_pipe_[THR_SEND_PIPE_ID]->get_front_len();
        return 0;
    }



public:

    //为了SingleTon类准备
    //得到唯一的单子实例
    ZCE_Thread_Bus_Pipe *instance()
    {
        if (instance_ == NULL)
        {
            instance_ = new ZCE_Thread_Bus_Pipe();
        }

        return instance_;
    }

    //赋值唯一的单子实例
    void instance(ZCE_Thread_Bus_Pipe *pinstatnce)
    {
        clean_instance();
        instance_ = pinstatnce;
        return;
    }

    //清除单子实例
    void clean_instance()
    {
        if (instance_)
        {
            delete instance_;
        }

        instance_ = NULL;
        return;
    }

};




//--------------------------------------------------------------------------------------
//用于一个线程读，一个线程写的BUS，虽然叫ST，但其实还是在两个线程间有效，
//用于一些对于速度有极致追求的地方
typedef ZCE_Thread_Bus_Pipe<ZCE_Null_Mutex>   ZCE_Thread_ST_Bus_Pipe;

//加锁的BUS,可以有多人竞争
typedef ZCE_Thread_Bus_Pipe<ZCE_Thread_Light_Mutex> ZCE_Thread_MT_Bus_Pipe;






#endif //ZCE_LIB_THREAD_BUS_PIPE_H_





