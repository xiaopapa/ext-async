/*
 +----------------------------------------------------------------------+
 | Swoole                                                               |
 +----------------------------------------------------------------------+
 | Copyright (c) 2012-2015 The Swoole Group                             |
 +----------------------------------------------------------------------+
 | This source file is subject to version 2.0 of the Apache license,    |
 | that is bundled with this package in the file LICENSE, and is        |
 | available through the world-wide-web at the following url:           |
 | http://www.apache.org/licenses/LICENSE-2.0.html                      |
 | If you did not receive a copy of the Apache2.0 license and are unable|
 | to obtain it through the world-wide-web, please send a note to       |
 | license@swoole.com so we can mail you a copy immediately.            |
 +----------------------------------------------------------------------+
 | Author: Xinyu Zhu  <xyzhu1120@gmail.com>                             |
 +----------------------------------------------------------------------+
 */

#include "php_swoole_async.h"
#include "swoole_memory.h"
#include "swoole_log.h"

struct swRingQueue {
    int head;
    int tail;
    /**
     * empty or full
     */
    int tag;
    int size;
    void **data;
};

int swRingQueue_init(swRingQueue *queue, int buffer_size);
int swRingQueue_push(swRingQueue *queue, void *);
int swRingQueue_pop(swRingQueue *queue, void **);
void swRingQueue_free(swRingQueue *queue);

static inline int swRingQueue_count(swRingQueue *queue) {
    if (queue->tail > queue->head) {
        return queue->tail - queue->head;
    } else if (queue->head == queue->tail) {
        return queue->tag == 1 ? queue->size : 0;
    } else {
        return queue->tail + queue->size - queue->head;
    }
}

#define swRingQueue_empty(q) ((q->head == q->tail) && (q->tag == 0))
#define swRingQueue_full(q) ((q->head == q->tail) && (q->tag == 1))

static PHP_METHOD(swoole_ringqueue, __construct);
static PHP_METHOD(swoole_ringqueue, __destruct);
static PHP_METHOD(swoole_ringqueue, push);
static PHP_METHOD(swoole_ringqueue, pop);
static PHP_METHOD(swoole_ringqueue, count);
static PHP_METHOD(swoole_ringqueue, isFull);
static PHP_METHOD(swoole_ringqueue, isEmpty);

zend_class_entry *swoole_ringqueue_ce;
static zend_object_handlers swoole_ringqueue_handlers;

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_ringqueue_construct, 0, 0, 1)
    ZEND_ARG_INFO(0, len)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_ringqueue_push, 0, 0, 1)
    ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_void, 0, 0, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry swoole_ringqueue_methods[] =
{
    PHP_ME(swoole_ringqueue, __construct, arginfo_swoole_ringqueue_construct, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_ringqueue, __destruct, arginfo_swoole_void, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_ringqueue, push, arginfo_swoole_ringqueue_push, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_ringqueue, pop, arginfo_swoole_void, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_ringqueue, count, arginfo_swoole_void, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_ringqueue, isFull, arginfo_swoole_void, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_ringqueue, isEmpty, arginfo_swoole_void, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

void swoole_ringqueue_init(int module_number)
{
    SW_INIT_CLASS_ENTRY(swoole_ringqueue, "Swoole\\RingQueue", "swoole_ringqueue", NULL, swoole_ringqueue_methods);
    SW_SET_CLASS_SERIALIZABLE(swoole_ringqueue, zend_class_serialize_deny, zend_class_unserialize_deny);
    SW_SET_CLASS_CLONEABLE(swoole_ringqueue, sw_zend_class_clone_deny);
    SW_SET_CLASS_UNSET_PROPERTY_HANDLER(swoole_ringqueue, sw_zend_class_unset_property_deny);
}

int swRingQueue_init(swRingQueue *queue, int buffer_size) {
    queue->data = (void **) sw_calloc(buffer_size, sizeof(void *));
    if (queue->data == nullptr) {
        swWarn("malloc failed");
        return -1;
    }
    queue->size = buffer_size;
    queue->head = 0;
    queue->tail = 0;
    queue->tag = 0;
    return 0;
}

void swRingQueue_free(swRingQueue *queue) {
    sw_free(queue->data);
}

int swRingQueue_push(swRingQueue *queue, void *push_data) {
    if (swRingQueue_full(queue)) {
        return SW_ERR;
    }

    queue->data[queue->tail] = push_data;
    queue->tail = (queue->tail + 1) % queue->size;

    if (queue->tail == queue->head) {
        queue->tag = 1;
    }
    return SW_OK;
}

int swRingQueue_pop(swRingQueue *queue, void **pop_data) {
    if (swRingQueue_empty(queue)) {
        return SW_ERR;
    }

    *pop_data = queue->data[queue->head];
    queue->head = (queue->head + 1) % queue->size;

    if (queue->tail == queue->head) {
        queue->tag = 0;
    }
    return SW_OK;
}

static PHP_METHOD(swoole_ringqueue, __construct)
{
    long len;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "l", &len) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (len < 0)
    {
        len = SW_RINGQUEUE_LEN;
    }

    swRingQueue *queue = (swRingQueue *) emalloc(sizeof(swRingQueue));
    if (queue == NULL)
    {
        zend_throw_exception(swoole_exception_ce, "failed to create ringqueue.", SW_ERROR_MALLOC_FAIL);
        RETURN_FALSE;
    }
    if (swRingQueue_init(queue, len))
    {
        zend_throw_exception(swoole_exception_ce, "failed to init ringqueue.", SW_ERROR_MALLOC_FAIL);
        RETURN_FALSE;
    }
    swoole_set_object(ZEND_THIS, queue);
}

static PHP_METHOD(swoole_ringqueue, __destruct)
{
    SW_PREVENT_USER_DESTRUCT();

    swRingQueue *queue = (swRingQueue *) swoole_get_object(ZEND_THIS);
    efree(queue);
    swoole_set_object(ZEND_THIS, NULL);
}

static PHP_METHOD(swoole_ringqueue, push)
{
    swRingQueue *queue = (swRingQueue *) swoole_get_object(ZEND_THIS);
    zval *zdata;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "z", &zdata) == FAILURE)
    {
        RETURN_FALSE;
    }

    zdata = sw_zval_dup(zdata);
    if (swRingQueue_push(queue, zdata) < 0)
    {
        efree(zdata);
        RETURN_FALSE;
    }
    else
    {
        Z_TRY_ADDREF_P(zdata);
        RETURN_TRUE;
    }
}

static PHP_METHOD(swoole_ringqueue, pop)
{
    swRingQueue *queue = (swRingQueue *) swoole_get_object(ZEND_THIS);
    zval *zdata;

    int n = swRingQueue_pop(queue, (void**)&zdata);
    if (n < 0)
    {
        RETURN_FALSE;
    }
    RETVAL_ZVAL(zdata, 0, 0);
    efree(zdata);
}

static PHP_METHOD(swoole_ringqueue, isFull)
{
    swRingQueue *queue = (swRingQueue *) swoole_get_object(ZEND_THIS);
    RETURN_BOOL(swRingQueue_full(queue));
}

static PHP_METHOD(swoole_ringqueue, isEmpty)
{
    swRingQueue *queue = (swRingQueue *) swoole_get_object(ZEND_THIS);
    RETURN_BOOL(swRingQueue_empty(queue));
}

static PHP_METHOD(swoole_ringqueue, count)
{
    swRingQueue *queue = (swRingQueue *) swoole_get_object(ZEND_THIS);
    RETURN_LONG(swRingQueue_count(queue));
}
