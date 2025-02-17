
/*
 * @COPYRIGHT@
 *
 * Author: Archie Cobbs <archie@freebsd.org>
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <pthread.h>

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#ifndef PD_BASE_INCLUDED
#include "pdel/pd_base.h"	/* picks up pd_port.h */
#endif
#include "pdel/pd_time.h"

#include "structs/structs.h"
#include "structs/type/array.h"

#include "util/mesg_port.h"
#include "util/pevent.h"
#include "util/typed_mem.h"

#include "internal.h"

#define MESG_PORT_MAGIC		0xe8a20c14

struct mesg {
	void			*data;
	TAILQ_ENTRY(mesg)	next;		/* next message in queue */
};

struct mesg_port {
	u_int32_t		magic;		/* magic number */
	pthread_mutex_t		mutex;		/* mutex */
	pthread_cond_t		readable;	/* condition variable */
	u_int			qlen;		/* length of queue */
	TAILQ_HEAD(,mesg)	queue;		/* message queue */
	const char		*mtype;		/* typed memory type */
	char			mtype_buf[TYPED_MEM_TYPELEN];
	struct pevent		*event;		/* associated event, if any */
};

/*
 * Create a new message port.
 */
struct mesg_port *
mesg_port_create(const char *mtype)
{
	struct mesg_port *port;

	if ((port = MALLOC(mtype, sizeof(*port))) == NULL)
		return (NULL);
	memset(port, 0, sizeof(*port));
	port->magic = MESG_PORT_MAGIC;
	if (mtype != NULL) {
		strlcpy(port->mtype_buf, mtype, sizeof(port->mtype_buf));
		port->mtype = port->mtype_buf;
	}
	TAILQ_INIT(&port->queue);
	if ((errno = pthread_mutex_init(&port->mutex, NULL)) != 0) {
		FREE(mtype, port);
		return (NULL);
	}
	if ((errno = pthread_cond_init(&port->readable, NULL)) != 0) {
		pthread_mutex_destroy(&port->mutex);
		FREE(mtype, port);
		return (NULL);
	}
	return (port);
}

/*
 * Destroy a message port.
 */
void
mesg_port_destroy(struct mesg_port **portp)
{
	struct mesg_port *const port = *portp;

	if (port == NULL)
		return;
	*portp = NULL;
	assert(port->magic == MESG_PORT_MAGIC);
	assert(port->qlen == 0);
	if (port->event != NULL)
		_pevent_unref(port->event);
	pthread_cond_destroy(&port->readable);
	pthread_mutex_destroy(&port->mutex);
	port->magic = 0;
	FREE(port->mtype, port);
}

/*
 * Send a message down a message port.
 */
int
mesg_port_put(struct mesg_port *port, void *data)
{
	struct pevent *ev = NULL;
	struct mesg *mesg;
	int r;

	/* Sanity check */
	if (data == NULL) {
		errno = EINVAL;
		return (-1);
	}

	/* Get new mesg */
	if ((mesg = MALLOC(port->mtype, sizeof(*mesg))) == NULL)
		return (-1);
	memset(mesg, 0, sizeof(*mesg));
	mesg->data = data;

	/* Add message to the queue */
	r = pthread_mutex_lock(&port->mutex);
	assert(r == 0);
	assert(port->magic == MESG_PORT_MAGIC);
	TAILQ_INSERT_TAIL(&port->queue, mesg, next);

	/* Notify waiters and grab associated event */
	if (port->qlen++ == 0) {
		pthread_cond_signal(&port->readable);
		ev = port->event;
		port->event = NULL;
	}

	/* Unlock port */
	r = pthread_mutex_unlock(&port->mutex);
	assert(r == 0);

	/* Trigger the event, if any */
	if (ev != NULL) {
		pevent_trigger(ev);
		_pevent_unref(ev);
	}

	/* Done */
	return (0);
}

/*
 * Read the next message from a message port.
 */
void *
mesg_port_get(struct mesg_port *port, int timeout)
{
	struct timespec waketime;
	struct mesg *mesg;
	void *data = NULL;
	int r;

	/* Get waketime, which is absolute */
	if (timeout > 0) {
		struct timeval waketimeval;
		struct timeval tv;

		tv.tv_sec = timeout / 1000;
		tv.tv_usec = timeout % 1000;
		(void)pd_gettimeofday(&waketimeval, NULL);
		pd_timeradd(&waketimeval, &tv, &waketimeval);
		PD_TIMEVAL_TO_TIMESPEC(&waketimeval, &waketime);
	}

	/* Grab mutex, but release if thread is canceled */
	r = pthread_mutex_lock(&port->mutex);
	assert(r == 0);
	pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock,
	    &port->mutex);
	assert(port->magic == MESG_PORT_MAGIC);

	/* Wait for the queue to be non-empty */
	while (port->qlen == 0) {

		/* Sleep on condition variable with optional timeout */
		if (timeout == 0)
			errno = ENOTDIR;
		else if (timeout > 0) {
			errno = pthread_cond_timedwait(&port->readable,
			    &port->mutex, &waketime);
		} else {
			errno = pthread_cond_wait(&port->readable,
			    &port->mutex);
		}

		/* Sanity check */
		assert(port->magic == MESG_PORT_MAGIC);

		/* Bail if there was an error */
		if (errno != 0)
			goto done;
	}

	/* Remove next message after grabbing its payload */
	mesg = TAILQ_FIRST(&port->queue);
	TAILQ_REMOVE(&port->queue, mesg, next);
	port->qlen--;
	data = mesg->data;
	FREE(port->mtype, mesg);

	/* Signal next reader in line, if any */
	if (port->qlen > 0)
		pthread_cond_signal(&port->readable);

done:;
	/* Done */
	pthread_cleanup_pop(1);
	return (data);
}

/*
 * Get the number of messages queued on a message port.
 */
u_int
mesg_port_qlen(struct mesg_port *port)
{
	u_int qlen;
	int r;

	r = pthread_mutex_lock(&port->mutex);
	assert(r == 0);
	assert(port->magic == MESG_PORT_MAGIC);
	qlen = port->qlen;
	r = pthread_mutex_unlock(&port->mutex);
	assert(r == 0);
	return (qlen);
}

/*
 * Set the associated pevent.
 */
int
_mesg_port_set_event(struct mesg_port *port, struct pevent *ev)
{
	int rtn;
	int r;

	/* Lock port */
	r = pthread_mutex_lock(&port->mutex);
	assert(r == 0);

	/* Sanity check */
	assert(port->magic == MESG_PORT_MAGIC);
	assert(ev != NULL);

	/* Check if event already there; if so, and it's obsolete, nuke it */
	if (port->event != NULL) {
		if (!_pevent_canceled(port->event)) {
			errno = EBUSY;
			rtn = -1;
			goto done;
		}
		_pevent_unref(port->event);
		port->event = NULL;
	}

	/* Remember event */
	port->event = ev;
	rtn = 0;

done:
	/* Unlock port */
	r = pthread_mutex_unlock(&port->mutex);
	assert(r == 0);
	return (rtn);
}

