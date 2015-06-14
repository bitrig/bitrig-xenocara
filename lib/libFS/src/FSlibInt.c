/*
 * Copyright 1990 Network Computing Devices;
 * Portions Copyright 1987 by Digital Equipment Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation, and that the names of Network Computing
 * Devices or Digital not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission. Network Computing Devices or Digital make no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * NETWORK COMPUTING DEVICES AND  DIGITAL DISCLAIM ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL NETWORK COMPUTING DEVICES
 * OR DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

/*

Copyright 1987, 1994, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.

*/

/*
 *	FSlibInt.c - Internal support routines for the C subroutine
 *	interface library (FSlib).
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include "FSlibint.h"
#include <X11/Xtrans/Xtransint.h>
#include <X11/Xos.h>

static void _EatData32 ( FSServer *svr, unsigned long n );
static const char * _SysErrorMsg ( int n );

/* check for both EAGAIN and EWOULDBLOCK, because some supposedly POSIX
 * systems are broken and return EWOULDBLOCK when they should return EAGAIN
 *
 * Solaris defines EWOULDBLOCK to be EAGAIN, so don't need to check twice
 * for it.
 */
#ifdef WIN32
#define ETEST() (WSAGetLastError() == WSAEWOULDBLOCK)
#else
#if defined(EAGAIN) && defined(EWOULDBLOCK) && (EAGAIN != EWOULDBLOCK)
#define ETEST() (errno == EAGAIN || errno == EWOULDBLOCK)
#else
#ifdef EAGAIN
#define ETEST() (errno == EAGAIN)
#else
#define ETEST() (errno == EWOULDBLOCK)
#endif
#endif
#endif
#ifdef WIN32
#define ECHECK(err) (WSAGetLastError() == err)
#define ESET(val) WSASetLastError(val)
#else
#ifdef ISC
#define ECHECK(err) ((errno == err) || ETEST())
#else
#define ECHECK(err) (errno == err)
#endif
#define ESET(val) errno = val
#endif

/*
 * The following routines are internal routines used by FSlib for protocol
 * packet transmission and reception.
 *
 * FSIOError(FSServer *) will be called if any sort of system call error occurs.
 * This is assumed to be a fatal condition, i.e., FSIOError should not return.
 *
 * FSError(FSServer *, FSErrorEvent *) will be called whenever an FS_Error event is
 * received.  This is not assumed to be a fatal condition, i.e., it is
 * acceptable for this procedure to return.  However, FSError should NOT
 * perform any operations (directly or indirectly) on the DISPLAY.
 *
 * Routines declared with a return type of 'Status' return 0 on failure,
 * and non 0 on success.  Routines with no declared return type don't
 * return anything.  Whenever possible routines that create objects return
 * the object they have created.
 */

_FSQEvent  *_FSqfree = NULL;	/* NULL _FSQEvent. */

static int  padlength[4] = {0, 3, 2, 1};

 /*
  * lookup table for adding padding bytes to data that is read from or written
  * to the FS socket.
  */

static fsReq _dummy_request = {
    0, 0, 0
};

/*
 * _FSFlush - Flush the FS request buffer.  If the buffer is empty, no
 * action is taken.  This routine correctly handles incremental writes.
 * This routine may have to be reworked if int < long.
 */
void
_FSFlush(register FSServer *svr)
{
    register long size,
                todo;
    register int write_stat;
    register char *bufindex;

    size = todo = svr->bufptr - svr->buffer;
    bufindex = svr->bufptr = svr->buffer;
    /*
     * While write has not written the entire buffer, keep looping until the
     * entire buffer is written.  bufindex will be incremented and size
     * decremented as buffer is written out.
     */
    while (size) {
	ESET(0);
	write_stat = _FSTransWrite(svr->trans_conn, bufindex, (int) todo);
	if (write_stat >= 0) {
	    size -= write_stat;
	    todo = size;
	    bufindex += write_stat;
	} else if (ETEST()) {
	    _FSWaitForWritable(svr);
#ifdef SUNSYSV
	} else if (ECHECK(0)) {
	    _FSWaitForWritable(svr);
#endif

#ifdef EMSGSIZE
	} else if (ECHECK(EMSGSIZE)) {
	    if (todo > 1)
		todo >>= 1;
	    else
		_FSWaitForWritable(svr);
#endif
	} else {
	    /* Write failed! */
	    /* errno set by write system call. */
	    (*_FSIOErrorFunction) (svr);
	}
    }
    svr->last_req = (char *) &_dummy_request;
}

/* _FSReadEvents - Flush the output queue,
 * then read as many events as possible (but at least 1) and enqueue them
 */
void
_FSReadEvents(register FSServer *svr)
{
    char        buf[BUFSIZE];
    BytesReadable_t pend_not_register;	/* because can't "&" a register
					 * variable */
    register BytesReadable_t pend;
    register fsEvent *ev;
    Bool        not_yet_flushed = True;

    do {
	/* find out how much data can be read */
	if (_FSTransBytesReadable(svr->trans_conn, &pend_not_register) < 0)
	    (*_FSIOErrorFunction) (svr);
	pend = pend_not_register;

	/*
	 * must read at least one fsEvent; if none is pending, then we'll just
	 * flush and block waiting for it
	 */
	if (pend < SIZEOF(fsEvent)) {
	    pend = SIZEOF(fsEvent);
	    /* don't flush until we block the first time */
	    if (not_yet_flushed) {
		int         qlen = svr->qlen;

		_FSFlush(svr);
		if (qlen != svr->qlen)
		    return;
		not_yet_flushed = False;
	    }
	}
	/* but we won't read more than the max buffer size */
	if (pend > BUFSIZE)
	    pend = BUFSIZE;

	/* round down to an integral number of XReps */
	pend = (pend / SIZEOF(fsEvent)) * SIZEOF(fsEvent);

	_FSRead(svr, buf, (long)pend);

	/* no space between comma and type or else macro will die */
	STARTITERATE(ev, fsEvent, buf, (pend > 0),
		     pend -= SIZEOF(fsEvent)) {
	    if (ev->type == FS_Error)
		_FSError(svr, (fsError *) ev);
	    else		/* it's an event packet; enqueue it */
		_FSEnq(svr, ev);
	}
	ENDITERATE
    } while (svr->head == NULL);
}

/*
 * _FSRead - Read bytes from the socket taking into account incomplete
 * reads.  This routine may have to be reworked if int < long.
 */
void
_FSRead(
    register FSServer	*svr,
    register char	*data,
    register long	 size)
{
    register long bytes_read;
#if defined(SVR4) && defined(i386)
    int	num_failed_reads = 0;
#endif

    if (size == 0)
	return;
    ESET(0);
    /*
     * For SVR4 with a unix-domain connection, ETEST() after selecting
     * readable means the server has died.  To do this here, we look for
     * two consecutive reads returning ETEST().
     */
    while ((bytes_read = _FSTransRead(svr->trans_conn, data, (int) size))
	    != size) {

	if (bytes_read > 0) {
	    size -= bytes_read;
	    data += bytes_read;
#if defined(SVR4) && defined(i386)
	    num_failed_reads = 0;
#endif
	}
	else if (ETEST()) {
	    _FSWaitForReadable(svr);
#if defined(SVR4) && defined(i386)
	    num_failed_reads++;
	    if (num_failed_reads > 1) {
		ESET(EPIPE);
		(*_FSIOErrorFunction) (svr);
	    }
#endif
	    ESET(0);
	}
#ifdef SUNSYSV
	else if (ECHECK(0)) {
	    _FSWaitForReadable(svr);
	}
#endif

	else if (bytes_read == 0) {
	    /* Read failed because of end of file! */
	    ESET(EPIPE);
	    (*_FSIOErrorFunction) (svr);
	} else {		/* bytes_read is less than 0; presumably -1 */
	    /* If it's a system call interrupt, it's not an error. */
	    if (!ECHECK(EINTR))
		(*_FSIOErrorFunction) (svr);
#if defined(SVR4) && defined(i386)
	    else
		num_failed_reads = 0;
#endif
	}
    }
}


/*
 * _FSReadPad - Read bytes from the socket taking into account incomplete
 * reads.  If the number of bytes is not 0 mod 32, read additional pad
 * bytes. This routine may have to be reworked if int < long.
 */
void
_FSReadPad(
    register FSServer	*svr,
    register char	*data,
    register long	 size)
{
    register long bytes_read;
    struct iovec iov[2];
    char        pad[3];

    if (size == 0)
	return;
    iov[0].iov_len = size;
    iov[0].iov_base = data;
    /*
     * The following hack is used to provide 32 bit long-word aligned padding.
     * The [1] vector is of length 0, 1, 2, or 3, whatever is needed.
     */

    iov[1].iov_len = padlength[size & 3];
    iov[1].iov_base = pad;
    size += iov[1].iov_len;

    ESET(0);
    while ((bytes_read = readv(svr->trans_conn->fd, iov, 2)) != size) {

	if (bytes_read > 0) {
	    size -= bytes_read;
	    if (iov[0].iov_len < bytes_read) {
		int pad_bytes_read = bytes_read - iov[0].iov_len;
		iov[1].iov_len -=  pad_bytes_read;
		iov[1].iov_base =
		    (char *)iov[1].iov_base + pad_bytes_read;
		iov[0].iov_len = 0;
	    } else {
		iov[0].iov_len -= bytes_read;
		iov[0].iov_base = (char *)iov[0].iov_base + bytes_read;
	    }
	}
	else if (ETEST()) {
	    _FSWaitForReadable(svr);
	    ESET(0);
	}
#ifdef SUNSYSV
	else if (ECHECK(0)) {
	    _FSWaitForReadable(svr);
	}
#endif

	else if (bytes_read == 0) {
	    /* Read failed because of end of file! */
	    ESET(EPIPE);
	    (*_FSIOErrorFunction) (svr);
	} else {		/* bytes_read is less than 0; presumably -1 */
	    /* If it's a system call interrupt, it's not an error. */
	    if (!ECHECK(EINTR))
		(*_FSIOErrorFunction) (svr);
	}
    }
}

/*
 * _FSSend - Flush the buffer and send the client data. 32 bit word aligned
 * transmission is used, if size is not 0 mod 4, extra bytes are transmitted.
 * This routine may have to be reworked if int < long;
 */
void
_FSSend(
    register FSServer	*svr,
    const char		*data,
    register long	 size)
{
    struct iovec iov[3];
    static char pad[3] = {0, 0, 0};

    long        skip = 0;
    long        svrbufsize = (svr->bufptr - svr->buffer);
    long        padsize = padlength[size & 3];
    long        total = svrbufsize + size + padsize;
    long        todo = total;

    /*
     * There are 3 pieces that may need to be written out:
     *
     * o  whatever is in the display buffer o  the data passed in by the user o
     * any padding needed to 32bit align the whole mess
     *
     * This loop looks at all 3 pieces each time through.  It uses skip to figure
     * out whether or not a given piece is needed.
     */
    while (total) {
	long        before = skip;	/* amount of whole thing written */
	long        remain = todo;	/* amount to try this time, <= total */
	int         i = 0;
	long        len;

	/*
	 * You could be very general here and have "in" and "out" iovecs and
	 * write a loop without using a macro, but what the heck.  This
	 * translates to:
	 *
	 * how much of this piece is new? if more new then we are trying this
	 * time, clamp if nothing new then bump down amount already written,
	 * for next piece else put new stuff in iovec, will need all of next
	 * piece
	 *
	 * Note that todo had better be at least 1 or else we'll end up writing 0
	 * iovecs.
	 */
#define InsertIOV(pointer, length) \
	    len = (length) - before; \
	    if (len > remain) \
		len = remain; \
	    if (len <= 0) { \
		before = (-len); \
	    } else { \
		iov[i].iov_len = len; \
		iov[i].iov_base = (pointer) + before; \
		i++; \
		remain -= len; \
		before = 0; \
	    }

	InsertIOV(svr->buffer, svrbufsize)
	InsertIOV((char *)data, size)
	InsertIOV(pad, padsize)

	ESET(0);
	if ((len = _FSTransWritev(svr->trans_conn, iov, i)) >= 0) {
	    skip += len;
	    total -= len;
	    todo = total;
	} else if (ETEST()) {
		_FSWaitForWritable(svr);
#ifdef SUNSYSV
	} else if (ECHECK(0)) {
	    _FSWaitForWritable(svr);
#endif

#ifdef EMSGSIZE
	} else if (ECHECK(EMSGSIZE)) {
	    if (todo > 1)
		todo >>= 1;
	    else
		_FSWaitForWritable(svr);
#endif
	} else {
	    (*_FSIOErrorFunction) (svr);
	}
    }

    svr->bufptr = svr->buffer;
    svr->last_req = (char *) &_dummy_request;
}

#ifdef undef
/*
 * _FSAllocID - normal resource ID allocation routine.  A client
 * can roll their own and instantiate it if they want, but must
 * follow the rules.
 */
FSID
_FSAllocID(register FSServer *svr)
{
    return (svr->resource_base + (svr->resource_id++ << svr->resource_shift));
}

#endif

/*
 * The hard part about this is that we only get 16 bits from a reply.  Well,
 * then, we have three values that will march along, with the following
 * invariant:
 *	svr->last_request_read <= rep->sequenceNumber <= svr->request
 * The right choice for rep->sequenceNumber is the largest that
 * still meets these constraints.
 */

unsigned long
_FSSetLastRequestRead(
    register FSServer		*svr,
    register fsGenericReply	*rep)
{
    register unsigned long newseq,
                lastseq;

    newseq = (svr->last_request_read & ~((unsigned long) 0xffff)) |
	rep->sequenceNumber;
    lastseq = svr->last_request_read;
    while (newseq < lastseq) {
	newseq += 0x10000;
	if (newseq > svr->request) {
	    (void) fprintf(stderr,
	       "FSlib:  sequence lost (0x%lx > 0x%lx) in reply type 0x%x!\n",
			   newseq, svr->request,
			   (unsigned int) rep->type);
	    newseq -= 0x10000;
	    break;
	}
    }

    svr->last_request_read = newseq;
    return (newseq);
}

/*
 * _FSReply - Wait for a reply packet and copy its contents into the
 * specified rep.  Mean while we must handle error and event packets that
 * we may encounter.
 */
Status
_FSReply(
    register FSServer	*svr,
    register fsReply	*rep,
    int			 extra,	 /* number of 32-bit words expected after the
				  * reply */
    Bool		 discard)/* should I discard data following "extra"
				  * words? */
{
    /*
     * Pull out the serial number now, so that (currently illegal) requests
     * generated by an error handler don't confuse us.
     */
    unsigned long cur_request = svr->request;
    long rem_length;

    _FSFlush(svr);
    while (1) {
	_FSRead(svr, (char *) rep, (long) SIZEOF(fsReply));
	switch ((int) rep->generic.type) {

	case FS_Reply:
	    /*
	     * Reply received.  Fast update for synchronous replies, but deal
	     * with multiple outstanding replies.
	     */
	    if (rep->generic.sequenceNumber == (cur_request & 0xffff))
		svr->last_request_read = cur_request;
	    else
		(void) _FSSetLastRequestRead(svr, &rep->generic);
	    rem_length = rep->generic.length - (SIZEOF(fsReply) >> 2);
	    if (rem_length < 0) rem_length = 0;
	    if (extra == 0) {
		if (discard && rem_length)
		    /* unexpectedly long reply! */
		    _EatData32(svr, rem_length);
		return (1);
	    }
	    if (extra == rem_length) {
		/*
		 * Read the extra data into storage immediately following the
		 * GenericReply structure.
		 */
		_FSRead(svr, (char *) NEXTPTR(rep, fsReply), ((long) extra) << 2);
		return (1);
	    }
	    if (extra < rem_length) {
		/* Actual reply is longer than "extra" */
		_FSRead(svr, (char *) NEXTPTR(rep, fsReply), ((long) extra) << 2);
		if (discard)
		    _EatData32(svr, rem_length - extra);
		return (1);
	    }
	    /*
	     * if we get here, then extra > rem_length -- meaning we
	     * read a reply that's shorter than we expected.  This is an
	     * error,  but we still need to figure out how to handle it...
	     */
	    _FSRead(svr, (char *) NEXTPTR(rep, fsReply), rem_length << 2);
	    (*_FSIOErrorFunction) (svr);
	    return (0);

	case FS_Error:
	    {
		register _FSExtension *ext;
		register Bool ret = False;
		int         ret_code;
		fsError     err;
		unsigned long serial;
		long        err_data;

		/* copy in the part we already read off the wire */
		memcpy(&err, rep, SIZEOF(fsReply));
		/* read the rest of the error */
		_FSRead(svr, (char *) &err + SIZEOF(fsReply),
			(long) (SIZEOF(fsError) - SIZEOF(fsReply)));
		serial = _FSSetLastRequestRead(svr, (fsGenericReply *) rep);
		if (serial == cur_request)
		    /* do not die on certain failures */
		    switch ((int) err.request) {
			/* suck in any extra error info */
		    case FSBadResolution:
		    case FSBadLength:
		    case FSBadIDChoice:
		    case FSBadRange:
		    case FSBadFont:
		    case FSBadFormat:
			_FSRead(svr, (char *) &err_data, 4);
			break;
		    case FSBadAccessContext:
			_FSRead(svr, (char *) &err_data, 4);
			return 0;
		    case FSBadAlloc:
			return (0);
			/*
			 * we better see if there is an extension who may want
			 * to suppress the error.
			 */
		    default:
			ext = svr->ext_procs;
			while (ext) {
			    if (ext->error != NULL)
				ret = (*ext->error)
				    (svr, &err, &ext->codes, &ret_code);
			    ext = ext->next;
			}
			if (ret)
			    return (ret_code);
			break;
		    }
		_FSError(svr, &err);
		if (serial == cur_request)
		    return (0);
	    }
	    break;
	default:
	    _FSEnq(svr, (fsEvent *) rep);
	    break;
	}
    }
}


/* Read and discard "n" 8-bit bytes of data */

void
_FSEatData(
    FSServer			*svr,
    register unsigned long	 n)
{
#define SCRATCHSIZE 2048
    char        buf[SCRATCHSIZE];

    while (n > 0) {
	register long bytes_read = (n > SCRATCHSIZE) ? SCRATCHSIZE : n;

	_FSRead(svr, buf, bytes_read);
	n -= bytes_read;
    }
#undef SCRATCHSIZE
}


/* Read and discard "n" 32-bit words. */

static void
_EatData32(
    FSServer		*svr,
    unsigned long	 n)
{
    _FSEatData(svr, n << 2);
}


/*
 * _FSEnq - Place event packets on the display's queue.
 * note that no squishing of move events in V11, since there
 * is pointer motion hints....
 */
void
_FSEnq(
    register FSServer	*svr,
    register fsEvent	*event)
{
    register _FSQEvent *qelt;

/*NOSTRICT*/
    if ((qelt = _FSqfree) != NULL) {
	/* If _FSqfree is non-NULL do this, else malloc a new one. */
	_FSqfree = qelt->next;
    } else if ((qelt = FSmalloc(sizeof(_FSQEvent))) == NULL) {
	/* Malloc call failed! */
	ESET(ENOMEM);
	(*_FSIOErrorFunction) (svr);
    }
    qelt->next = NULL;
    /* go call through display to find proper event reformatter */
    if ((*svr->event_vec[event->type & 0177]) (svr, &qelt->event, event)) {
	if (svr->tail)
	    svr->tail->next = qelt;
	else
	    svr->head = qelt;

	svr->tail = qelt;
	svr->qlen++;
    } else {
	/* ignored, or stashed away for many-to-one compression */
	qelt->next = _FSqfree;
	_FSqfree = qelt;
    }
}

/*
 * EventToWire in separate file that is often not needed.
 */

/*ARGSUSED*/
Bool
_FSUnknownWireEvent(
    register FSServer	*svr,	/* pointer to display structure */
    register FSEvent	*re,	/* pointer to where event should be
				 * reformatted */
    register fsEvent	*event)	/* wire protocol event */
{

#ifdef notdef
    (void) fprintf(stderr,
	   "FSlib: unhandled wire event! event number = %d, display = %x\n.",
		   event->type, svr);
#endif

    return (False);
}

/*ARGSUSED*/
Status
_FSUnknownNativeEvent(
    register FSServer	*svr,	/* pointer to display structure */
    register FSEvent	*re,	/* pointer to where event should be
				 * reformatted */
    register fsEvent	*event)	/* wire protocol event */
{

#ifdef notdef
    (void) fprintf(stderr,
	 "FSlib: unhandled native event! event number = %d, display = %x\n.",
		   re->type, svr);
#endif

    return (0);
}

static const char *
_SysErrorMsg(int n)
{
    char       *s = strerror(n);

    return (s ? s : "no such error");
}

#ifdef __SUNPRO_C
/* prevent "Function has no return statement" error for _FSDefaultIOError */
#pragma does_not_return(exit)
#endif

/*
 * _FSDefaultIOError - Default fatal system error reporting routine.  Called
 * when an X internal system error is encountered.
 */
int
_FSDefaultIOError(FSServer *svr)
{
    (void) fprintf(stderr,
		   "FSIO:  fatal IO error %d (%s) on font server \"%s\"\r\n",
#ifdef WIN32
			WSAGetLastError(), strerror(WSAGetLastError()),
#else

		   errno, _SysErrorMsg(errno),
#endif
		   FSServerString(svr) ? FSServerString(svr) : "");
    (void) fprintf(stderr,
		   "      after %lu requests (%lu known processed) with %d events remaining.\r\n",
		   FSNextRequest(svr) - 1, FSLastKnownRequestProcessed(svr),
		   FSQLength(svr));

    if (ECHECK(EPIPE)) {
	(void) fprintf(stderr,
	"      The connection was probably broken by a server shutdown.\r\n");
    }
    exit(1);
    /* NOTREACHED */
}

/*
 * _FSError - Default non-fatal error reporting routine.  Called when an
 * FS_Error packet is encountered in the input stream.
 */
int
_FSError(
    FSServer	*svr,
    fsError	*rep)
{
    FSErrorEvent event;

    /*
     * FS_Error packet encountered!  We need to unpack the error before giving
     * it to the user.
     */

    event.server = svr;
    event.type = FS_Error;
    event.serial = _FSSetLastRequestRead(svr, (fsGenericReply *) rep);
    event.error_code = rep->request;
    event.request_code = rep->major_opcode;
    event.minor_code = rep->minor_opcode;
    if (_FSErrorFunction != NULL) {
	return ((*_FSErrorFunction) (svr, &event));
    }
    exit(1);
    /* NOTREACHED */
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral" // We know better
#endif

int
_FSPrintDefaultError(
    FSServer		*svr,
    FSErrorEvent	*event,
    FILE		*fp)
{
    char        buffer[BUFSIZ];
    char        mesg[BUFSIZ];
    char        number[32];
    const char *mtype = "FSlibMessage";
    register _FSExtension *ext = (_FSExtension *) NULL;

    (void) FSGetErrorText(svr, event->error_code, buffer, BUFSIZ);
    (void) FSGetErrorDatabaseText(svr, mtype, "FSError", "FS Error", mesg,
				  BUFSIZ);
    (void) fprintf(fp, "%s:  %s\n  ", mesg, buffer);
    (void) FSGetErrorDatabaseText(svr, mtype, "MajorCode",
				  "Request Major code %d", mesg, BUFSIZ);
    (void) fprintf(fp, mesg, event->request_code);
    if (event->request_code < 128) {
	snprintf(number, sizeof(number), "%d", event->request_code);
	(void) FSGetErrorDatabaseText(svr, "FSRequest", number, "", buffer,
				      BUFSIZ);
    } else {
	for (ext = svr->ext_procs;
		ext && (ext->codes.major_opcode != event->request_code);
		ext = ext->next);
	if (ext)
#ifdef HAVE_STRLCPY
	    strlcpy(buffer, ext->name, sizeof(buffer));
#else
	    strcpy(buffer, ext->name);
#endif
	else
	    buffer[0] = '\0';
    }
    (void) fprintf(fp, " (%s)\n  ", buffer);
    (void) FSGetErrorDatabaseText(svr, mtype, "MinorCode",
				  "Request Minor code %d", mesg, BUFSIZ);
    (void) fprintf(fp, mesg, event->minor_code);
    if (ext) {
	snprintf(mesg, sizeof(mesg), "%s.%d", ext->name, event->minor_code);
	(void) FSGetErrorDatabaseText(svr, "FSRequest", mesg, "", buffer,
				      BUFSIZ);
	(void) fprintf(fp, " (%s)", buffer);
    }
    fputs("\n  ", fp);
    (void) FSGetErrorDatabaseText(svr, mtype, "ResourceID", "ResourceID 0x%x",
				  mesg, BUFSIZ);
    (void) fprintf(fp, mesg, event->resourceid);
    fputs("\n  ", fp);
    (void) FSGetErrorDatabaseText(svr, mtype, "ErrorSerial", "Error Serial #%d",
				  mesg, BUFSIZ);
    (void) fprintf(fp, mesg, event->serial);
    fputs("\n  ", fp);
    (void) FSGetErrorDatabaseText(svr, mtype, "CurrentSerial",
				  "Current Serial #%d", mesg, BUFSIZ);
    (void) fprintf(fp, mesg, svr->request);
    fputs("\n", fp);
    return 1;
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

int
_FSDefaultError(
    FSServer		*svr,
    FSErrorEvent	*event)
{
    if (_FSPrintDefaultError(svr, event, stderr) == 0)
	return 0;
    exit(1);
    /* NOTREACHED */
}


FSIOErrorHandler _FSIOErrorFunction = _FSDefaultIOError;
FSErrorHandler _FSErrorFunction = _FSDefaultError;

int
FSFree(char *data)
{
    FSfree(data);
    return 1;
}

unsigned char *
FSMalloc(unsigned size)
{
    return (unsigned char *) FSmalloc(size);
}

#ifdef DataRoutineIsProcedure
void
Data(
    FSServer	*svr,
    char	*data,
    long	 len)
{
    if (svr->bufptr + (len) <= svr->bufmax) {
	memmove(svr->bufptr, data, len);
	svr->bufptr += ((len) + 3) & ~3;
    } else {
	_FSSend(svr, data, len);
    }
}

#endif				/* DataRoutineIsProcedure */


/*
 * _FSFreeQ - free the queue of events, called by XCloseServer when there are
 * no more displays left on the display list
 */

void
_FSFreeQ(void)
{
    register _FSQEvent *qelt = _FSqfree;

    while (qelt) {
	register _FSQEvent *qnext = qelt->next;

	FSfree(qelt);
	qelt = qnext;
    }
    _FSqfree = NULL;
    return;
}

#ifndef _FSANYSET
/*
 * This is not always a macro.
 */
_FSANYSET(long *src)
{
    int i;

    for (i=0; i<MSKCNT; i++)
	if (src[ i ])
	    return (1);
    return (0);
}
#endif
