
/*
 * @COPYRIGHT@
 *
 * Author: Archie Cobbs <archie@freebsd.org>
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/queue.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "structs/structs.h"
#include "structs/type/array.h"

#include "io/ssl_fp.h"
#include "io/timeout_fp.h"
#include "util/typed_mem.h"

#include "http/http_server.h"
#include "http/http_internal.h"

/* Internal functions */
static ssl_logger_t	http_connection_ssl_logger;

/*
 * Create a new connection from a raw socket/stream.
 *
 * If fp != NULL, we assume the stream already exists and that
 * sock is valid and will be implicitly closed by fclose(fp).
 * Otherwise, we assume sock is valid but has no stream on top of it.
 * Parameter "server" tells whether we are the server or the client.
 */
struct http_connection *
_http_connection_create(FILE *fp, int sock, int server,
	struct in_addr ip, u_int16_t port, SSL_CTX *ssl,
	http_logger_t *logger, u_int timeout)
{
	struct http_connection *conn;
	struct sockaddr_in sin;
	socklen_t slen;

	/* Sanity */
	if (sock < 0 || ip.s_addr == 0 || port == 0) {
		errno = EINVAL;
		return (NULL);
	}

	/* Get local address */
	slen = sizeof(sin);
	if (getsockname(sock, (struct sockaddr *)&sin, &slen) == -1) {
		(*logger)(LOG_ERR, "%s: %s", "getsockname", strerror(errno));
		return (NULL);
	}

	/* Get new connection object */
	if ((conn = MALLOC("http_connection", sizeof(*conn))) == NULL) {
		(*logger)(LOG_ERR, "%s: %s", "malloc", strerror(errno));
		return (NULL);
	}
	memset(conn, 0, sizeof(*conn));
	conn->remote_ip = ip;
	conn->remote_port = port;
	conn->local_ip = sin.sin_addr;
	conn->local_port = ntohs(sin.sin_port);
	conn->logger = logger;
	conn->server = !!server;
	conn->sock = sock;
	conn->ssl = ssl;

	/* Create associated request and response */
	if (_http_request_new(conn) == -1)
		goto fail;
	if (_http_response_new(conn) == -1)
		goto fail;

	/* Wrap socket descriptor with a stream (if not already supplied) */
	if (fp == NULL) {
		if (ssl != NULL) {
			fp = ssl_fdopen(ssl, sock, server,
			    "http_connection.fp", http_connection_ssl_logger,
			    conn, timeout);
		} else
			fp = timeout_fdopen(sock, "r+", timeout);
		if (fp == NULL) {
			(*logger)(LOG_ERR, "%s: %s", "fdopen", strerror(errno));
			goto fail;
		}
	}
	conn->fp = fp;

	/* Make stream not buffered */
	setbuf(conn->fp, NULL);

	/* Done */
	return (conn);

fail:
	_http_connection_free(&conn);
	return (NULL);
}

/*
 * Free a connection.
 */
void
_http_connection_free(struct http_connection **connp)
{
	struct http_connection *const conn = *connp;

	/* Check for NULL */
	if (conn == NULL)
		return;
	DBG(HTTP, "freeing connection %p", conn);

	/* Tell the caller that the connection went away */
	*connp = NULL;

	/* Close stream and socket */
	if (conn->fp != NULL)
 		fclose(conn->fp);
	else if (conn->sock != -1)
		(void)pd_close(conn->sock);

	/* Free request and response */
 	_http_request_free(&conn->req);
 	_http_response_free(&conn->resp);

	/* Free structure */
 	FREE("http_connection", conn);
}

/*
 * Logging callback for SSL code.
 */
static void
http_connection_ssl_logger(void *arg, int sev, const char *fmt, ...)
{
#if 0	/* SSL errors are quite common, so don't log them */
	struct http_connection *const conn = arg;
	va_list args;
	char *s;

	/* Prepend remote IP address to error message */
	va_start(args, fmt);
	VASPRINTF(TYPED_MEM_TEMP, &s, fmt, args);
	va_end(args);
	if (s == NULL)
		return;
	(*conn->logger)(sev, "%s: %s", inet_ntoa(conn->remote_ip), s);
	FREE(TYPED_MEM_TEMP, s);
#endif
}

