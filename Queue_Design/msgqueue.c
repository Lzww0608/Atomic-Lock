#include "msgqueue.h"
#include <stdlib.h>
#include <pthread.h>

struct __msgqueue {

    size_t msg_max;
    size_t msg_cnt;
    int linkOff;
    int nonblock;
    void *head1, *head2;
    void **get_head;    //put queue
    void **put_head;    //get queue
    void **put_tail;

    pthread_mutex_t get_mutex;
    pthread_mutex_t put_mutex;
    pthread_cond_t get_cond;
    pthread_cond_t put_cond;
};

/*
msgqueue_t *msgqueue_create(size_t maxLen, int linkOff);
void msgqueue_put(void *msg, msgqueue_t *queue);
void *msgqueue_get(msgqueue_t *queue);
void msgqueue_set_nonblock(msgqueue_t *queue);
void msgqueue_set_block(msgqueue_t *queue);
void msgqueue_destroy(msgqueue_t *queue);
*/

msgqueue_t *msgqueue_create(size_t maxLen, int linkOff) {
    msgqueue_t *queue = (msgqueue_t*)malloc(sizeof(msgqueue_t));
    int ret;

    if (!queue) {
        return NULL;
    }

    ret = pthread_mutex_init(&queue->get_mutex, NULL);
    if (ret == 0) {
        ret = pthread_mutex_init(&queue->put_mutex, NULL);
        if (ret == 0) {

            ret = pthread_cond_init(&queue->get_cond, NULL);
            if (ret == 0) {
                ret = pthread_cond_init(&queue->put_cond, NULL);

                if (ret == 0) {
                    queue->msg_max = maxLen;
                    queue->msg_cnt = 0;
                    queue->linkOff = linkOff;
                    queue->nonblock = 0;
                    queue->head1 = NULL;
                    queue->head2 = NULL;
                    queue->get_head = &queue->head1;
                    queue->put_head = &queue->head2;
                    queue->put_tail = &queue->head2;
                    return queue;
                }
                pthread_cond_destroy(&queue->get_cond);
            }
            
            pthread_mutex_destroy(&queue->put_mutex);
        }

        pthread_mutex_destroy(&queue->get_mutex);
    }

    
}

void msgqueue_set_nonblock(msgqueue_t *queue) {
    queue->nonblock = 1;
    pthread_mutex_lock(&queue->put_mutex);
    pthread_cond_signal(&queue->get_cond);
    pthread_cond_broadcast(&queue->put_cond);
    pthread_mutex_unlock(&queue->put_mutex);

}

void msgqueue_set_block(msgqueue_t *queue) {
    queue->nonblock = 0;
}

//交换put queue 与 get queue：减少生产者与消费者发生碰撞次数
static size_t __msgqueue_swap(msgqueue_t *queue) {

    void **get_head = queue->get_head;
    size_t cnt;
    //生产者与消费者线程碰撞
    queue->get_head = queue->put_head;
    pthread_mutex_lock(&queue->put_mutex);
    while (queue->msg_cnt == 0 && !queue->nonblock) {
        pthread_cond_wait(&queue->get_cond, &queue->put_mutex);
    }

    cnt = queue->msg_cnt;
    if (cnt > queue->msg_max - 1) {
        pthread_cond_broadcast(&queue->put_cond);
    }

    queue->put_head = get_head;
    queue->put_tail = get_head;
    queue->msg_cnt = 0;
    pthread_mutex_unlock(&queue->put_mutex);
    return cnt;
}


void msgqueue_put(void *msg, msgqueue_t* queue) {
    //计算每个队列节点存储地址
    void **link = (void **)((char *)msg + queue->linkOff);

    *link = NULL;
    pthread_mutex_lock(&queue->put_mutex);
    //队列已满，阻塞
    while (queue->msg_cnt > queue->msg_max - 1 && !queue->nonblock) {
       pthread_cond_wait(&queue->put_cond, &queue->put_mutex);
    }

    *queue->put_tail = link;
    queue->put_tail = link;
    queue->msg_cnt++;

    pthread_mutex_unlock(&queue->put_mutex);
    pthread_cond_signal(&queue->get_cond);
    
}

void *msgqueue_get(msgqueue_t* queue) {
    void *msg;

    pthread_mutex_lock(&queue->get_mutex);

    //get 队列为空时才进行交换
    if (*queue->get_head || __msgqueue_swap(queue) > 0) {
        msg = (char *)*queue->get_head - queue->linkOff;
        *queue->get_head =  *(void **)*queue->get_head;
    } else {
        msg = NULL;
    }

    pthread_mutex_unlock(&queue->get_mutex);
    return msg;
}

void msgqueue_destroy(msgqueue_t *queue) {
    
    pthread_mutex_destroy(&queue->put_mutex);
    pthread_mutex_destroy(&queue->get_mutex);
    pthread_cond_destroy(&queue->put_cond);
    pthread_cond_destroy(&queue->get_cond);
    free(queue);
}

