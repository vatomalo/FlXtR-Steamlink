/* Bounded compressed-packet FIFO. Network demux runs ahead of playback;
 * payloads and side data live in a rolling file, never a decoded-frame cache. */
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <errno.h>
#ifndef DISK_POLL
#define DISK_POLL() ((void)0)
#endif
#define DISK_SLOTS 4096
typedef struct {int size,stream,flags,sides;int64_t pts,dts,duration,pos;} DiskHeader;
typedef struct {uint32_t size,type;} DiskSide;
typedef struct {uint64_t offset;uint32_t bytes;int64_t time;} DiskRecord;
typedef struct {
    pthread_t thread;pthread_mutex_t mutex;pthread_cond_t changed;
    AVFormatContext *format;int fd,vi,ai,si,result,done,quit,primed;
    int head,count,seconds;uint64_t capacity,read_bytes,write_bytes;
    int64_t newest;
    DiskRecord records[DISK_SLOTS];
} DiskBuffer;
static int disk_io(DiskBuffer *q,void *data,size_t size,uint64_t offset,int writing){
    unsigned char *p=data;
    while(size){size_t part=(size_t)(q->capacity-offset%q->capacity);if(part>size)part=size;
        ssize_t n=writing?pwrite(q->fd,p,part,(off_t)(offset%q->capacity)):pread(q->fd,p,part,(off_t)(offset%q->capacity));
        if(n<0&&errno==EINTR)continue;
        if(n<=0)return -1;
        size-=(size_t)n;p+=n;offset+=(uint64_t)n;
    }return 0;
}
static void disk_wait(DiskBuffer *q){
    struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_nsec+=100000000;
    if(until.tv_nsec>=1000000000){until.tv_sec++;until.tv_nsec-=1000000000;}
    pthread_cond_timedwait(&q->changed,&q->mutex,&until);
}
static void *disk_producer(void *opaque){
    DiskBuffer *q=opaque;AVPacket *p=av_packet_alloc();int rc=AVERROR(ENOMEM);
    if(!p)goto finished;
    for(;;){
        pthread_mutex_lock(&q->mutex);int quit=q->quit;pthread_mutex_unlock(&q->mutex);
        if(quit||stopped){rc=AVERROR_EXIT;break;}
        deadline=av_gettime_relative()+30000000;rc=av_read_frame(q->format,p);deadline=0;
        if(rc<0)break;
        if(p->stream_index!=q->vi&&p->stream_index!=q->ai&&p->stream_index!=q->si){av_packet_unref(p);continue;}
        uint64_t bytes=sizeof(DiskHeader)+(uint64_t)p->size;
        if(p->size<0||p->size>8*1024*1024||p->side_data_elems>64){rc=AVERROR_INVALIDDATA;break;}
        for(int i=0;i<p->side_data_elems;i++)bytes+=sizeof(DiskSide)+p->side_data[i].size;
        if(bytes>16*1024*1024||bytes>q->capacity){rc=AVERROR_INVALIDDATA;break;}
        int64_t ts=p->dts!=AV_NOPTS_VALUE?p->dts:p->pts;
        if(ts!=AV_NOPTS_VALUE)ts=av_rescale_q(ts,q->format->streams[p->stream_index]->time_base,AV_TIME_BASE_Q);
        pthread_mutex_lock(&q->mutex);
        while(!q->quit&&!stopped&&(q->count==DISK_SLOTS||q->write_bytes-q->read_bytes+bytes>q->capacity)){
            q->primed=1;pthread_cond_broadcast(&q->changed);disk_wait(q);
        }
        if(q->quit||stopped){pthread_mutex_unlock(&q->mutex);rc=AVERROR_EXIT;break;}
        uint64_t at=q->write_bytes;
        DiskHeader h={p->size,p->stream_index,p->flags,p->side_data_elems,p->pts,p->dts,p->duration,p->pos};
        int error=disk_io(q,&h,sizeof(h),at,1);at+=sizeof(h);
        error|=disk_io(q,p->data,(size_t)p->size,at,1);at+=(size_t)p->size;
        for(int i=0;i<p->side_data_elems&&!error;i++){
            DiskSide side={(uint32_t)p->side_data[i].size,(uint32_t)p->side_data[i].type};
            error|=disk_io(q,&side,sizeof(side),at,1);at+=sizeof(side);
            error|=disk_io(q,p->side_data[i].data,side.size,at,1);at+=side.size;
        }
        if(error){pthread_mutex_unlock(&q->mutex);rc=AVERROR(EIO);break;}
        DiskRecord *record=&q->records[(q->head+q->count)%DISK_SLOTS];
        record->offset=q->write_bytes;record->bytes=(uint32_t)bytes;record->time=ts;
        q->write_bytes+=bytes;q->count++;
        if(ts!=AV_NOPTS_VALUE&&(q->newest==AV_NOPTS_VALUE||ts>q->newest))q->newest=ts;
        int64_t first=q->records[q->head].time;
        if((first==AV_NOPTS_VALUE&&q->write_bytes-q->read_bytes>=8*1024*1024)||(first!=AV_NOPTS_VALUE&&q->newest-first>=(int64_t)q->seconds*AV_TIME_BASE))q->primed=1;
        pthread_cond_broadcast(&q->changed);pthread_mutex_unlock(&q->mutex);av_packet_unref(p);
    }
    av_packet_free(&p);
finished:
    pthread_mutex_lock(&q->mutex);q->done=1;q->result=rc;pthread_cond_broadcast(&q->changed);pthread_mutex_unlock(&q->mutex);return NULL;
}
static DiskBuffer *disk_start(AVFormatContext *fmt,int vi,int ai,int si,int seconds,int megabytes){
    mkdir("playback-cache",0700);
    struct statvfs space;if(statvfs("playback-cache",&space))return NULL;
    uint64_t capacity=(uint64_t)megabytes*1024*1024;
    if((uint64_t)space.f_bavail*space.f_frsize<capacity+32*1024*1024)return NULL;
    DiskBuffer *q=calloc(1,sizeof(*q));if(!q)return NULL;
    char name[]="playback-cache/movie-XXXXXX";q->fd=mkstemp(name);if(q->fd<0){free(q);return NULL;}
    /* Unlinked immediately: even a crash/forced stop cannot leave movie data. */
    unlink(name);q->capacity=capacity;q->format=fmt;q->vi=vi;q->ai=ai;q->si=si;q->seconds=seconds;
    q->newest=AV_NOPTS_VALUE;
    pthread_mutex_init(&q->mutex,NULL);pthread_cond_init(&q->changed,NULL);
    if(pthread_create(&q->thread,NULL,disk_producer,q)){close(q->fd);pthread_cond_destroy(&q->changed);pthread_mutex_destroy(&q->mutex);free(q);return NULL;}
    return q;
}
static int disk_packet(DiskBuffer *q,AVPacket *p){
    pthread_mutex_lock(&q->mutex);
    if(!q->count)q->primed=0;
    while(!stopped&&!q->quit&&!q->done&&(!q->count||!q->primed)){
        disk_read_wait_count++;
        disk_wait(q);pthread_mutex_unlock(&q->mutex);DISK_POLL();pthread_mutex_lock(&q->mutex);
    }
    if(stopped||q->quit){pthread_mutex_unlock(&q->mutex);return AVERROR_EXIT;}
    if(!q->count){int rc=q->result;pthread_mutex_unlock(&q->mutex);return rc;}
    DiskRecord record=q->records[q->head];uint64_t at=record.offset;DiskHeader h;
    int error=disk_io(q,&h,sizeof(h),at,0);at+=sizeof(h);
    if(error||h.size<0||(uint64_t)h.size+sizeof(h)>record.bytes||h.sides<0||h.sides>64){pthread_mutex_unlock(&q->mutex);return AVERROR_INVALIDDATA;}
    if(av_new_packet(p,h.size)<0){pthread_mutex_unlock(&q->mutex);return AVERROR(ENOMEM);}
    error=disk_io(q,p->data,(size_t)h.size,at,0);at+=(size_t)h.size;
    p->stream_index=h.stream;p->flags=h.flags;p->pts=h.pts;p->dts=h.dts;p->duration=h.duration;p->pos=h.pos;
    for(int i=0;i<h.sides&&!error;i++){
        DiskSide side;if(at+sizeof(side)>record.offset+record.bytes){error=1;break;}
        error=disk_io(q,&side,sizeof(side),at,0);at+=sizeof(side);
        if(error||at+side.size>record.offset+record.bytes){error=1;break;}
        uint8_t *data=av_packet_new_side_data(p,(enum AVPacketSideDataType)side.type,side.size);
        if(!data){error=1;break;}error=disk_io(q,data,side.size,at,0);at+=side.size;
    }
    q->read_bytes+=record.bytes;q->head=(q->head+1)%DISK_SLOTS;q->count--;
    pthread_cond_broadcast(&q->changed);pthread_mutex_unlock(&q->mutex);
    if(error){av_packet_unref(p);return AVERROR(EIO);}return 0;
}
static void disk_close(DiskBuffer *q){
    if(!q)return;
    pthread_mutex_lock(&q->mutex);q->quit=1;pthread_cond_broadcast(&q->changed);pthread_mutex_unlock(&q->mutex);
    stopped=1;pthread_join(q->thread,NULL);close(q->fd);pthread_cond_destroy(&q->changed);pthread_mutex_destroy(&q->mutex);free(q);
}
