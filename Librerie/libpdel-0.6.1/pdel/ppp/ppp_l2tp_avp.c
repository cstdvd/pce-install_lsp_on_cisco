
/*
 * @COPYRIGHT@
 *
 * Author: Archie Cobbs <archie@freebsd.org>
 */

#include "ppp/ppp_defs.h"
#include "ppp/ppp_fsm_option.h"
#include "ppp/ppp_auth.h"
#include "ppp/ppp_lcp.h"
#include "ppp/ppp_l2tp_avp.h"
#include "ppp/ppp_util.h"
#include <openssl/md5.h>

/* Memory types */
#define AVP_MTYPE	"ppp_l2tp_avp"
#define AVP_LIST_MTYPE	"ppp_l2tp_avp_list"
#define AVP_PTRS_MTYPE	"ppp_l2tp_avp_ptrs"

/***********************************************************************
			AVP STRUCTURE METHODS
***********************************************************************/

/*
 * Create a new AVP structure.
 */
struct ppp_l2tp_avp *
ppp_l2tp_avp_create(int mandatory, u_int16_t vendor,
	u_int16_t type, const void *value, size_t vlen)
{
	struct ppp_l2tp_avp *avp;

	if ((avp = MALLOC(AVP_MTYPE, sizeof(*avp))) == NULL)
		return (NULL);
	memset(avp, 0, sizeof(*avp));
	avp->mandatory = !!mandatory;
	avp->vendor = vendor;
	avp->type = type;
	if (vlen > 0) {
		if ((avp->value = MALLOC(AVP_MTYPE, vlen)) == NULL) {
			FREE(AVP_MTYPE, avp);
			return (NULL);
		}
		memcpy(avp->value, value, vlen);
	}
	avp->vlen = vlen;
	return (avp);
}

/*
 * Copy an AVP struture.
 */
struct ppp_l2tp_avp *
ppp_l2tp_avp_copy(const struct ppp_l2tp_avp *avp)
{
	return (ppp_l2tp_avp_create(avp->mandatory, avp->vendor,
	    avp->type, avp->value, avp->vlen));
}

/*
 * Destroy an AVP structure.
 */
void
ppp_l2tp_avp_destroy(struct ppp_l2tp_avp **avpp)
{
	struct ppp_l2tp_avp *const avp = *avpp;

	if (avp == NULL)
		return;
	*avpp = NULL;
	FREE(AVP_MTYPE, avp->value);
	FREE(AVP_MTYPE, avp);
}

/***********************************************************************
			AVP LIST METHODS
***********************************************************************/

/*
 * Create a new AVP list.
 */
struct ppp_l2tp_avp_list *
ppp_l2tp_avp_list_create(void)
{
	struct ppp_l2tp_avp_list *list;

	if ((list = MALLOC(AVP_LIST_MTYPE, sizeof(*list))) == NULL)
		return (NULL);
	memset(list, 0, sizeof(*list));
	return (list);
}

/*
 * Insert an AVP into a list.
 */
int
ppp_l2tp_avp_list_insert(struct ppp_l2tp_avp_list *list,
	struct ppp_l2tp_avp **avpp, int index)
{
	struct ppp_l2tp_avp *const avp = *avpp;
	void *mem;

	if (avp == NULL || index < 0 || index > list->length) {
		errno = EINVAL;
		return (-1);
	}
	if ((mem = REALLOC(AVP_LIST_MTYPE, list->avps,
	    (list->length + 1) * sizeof(*list->avps))) == NULL)
		return (-1);
	list->avps = mem;
	memmove(list->avps + index + 1, list->avps + index,
	    (list->length++ - index) * sizeof(*list->avps));
	list->avps[index] = *avp;
	FREE(AVP_MTYPE, avp);
	*avpp = NULL;
	return (0);
}

/*
 * Create a new AVP and add it to the end of the given list.
 */
int
ppp_l2tp_avp_list_append(struct ppp_l2tp_avp_list *list, int mandatory,
	u_int16_t vendor, u_int16_t type, const void *value, size_t vlen)
{
	struct ppp_l2tp_avp *avp;

	if ((avp = ppp_l2tp_avp_create(mandatory,
	    vendor, type, value, vlen)) == NULL)
		return (-1);
	if (ppp_l2tp_avp_list_insert(list, &avp, list->length) == -1) {
		ppp_l2tp_avp_destroy(&avp);
		return (-1);
	}
	return (0);
}

/*
 * Extract an AVP from a list.
 */
struct ppp_l2tp_avp *
ppp_l2tp_avp_list_extract(struct ppp_l2tp_avp_list *list, u_int index)
{
	struct ppp_l2tp_avp *elem;
	struct ppp_l2tp_avp *avp;

	if (index < 0 || index >= list->length) {
		errno = EINVAL;
		return (NULL);
	}
	elem = &list->avps[index];
	if ((avp = ppp_l2tp_avp_create(elem->mandatory, elem->vendor,
	    elem->type, elem->value, elem->vlen)) == NULL)
		return (NULL);
	memmove(list->avps + index, list->avps + index + 1,
	    (--list->length - index) * sizeof(*list->avps));
	return (avp);
}

/*
 * Remove and destroy an AVP from a list.
 */
int
ppp_l2tp_avp_list_remove(struct ppp_l2tp_avp_list *list, u_int index)
{
	if (index < 0 || index >= list->length) {
		errno = EINVAL;
		return (-1);
	}
	FREE(AVP_MTYPE, list->avps[index].value);
	memmove(list->avps + index, list->avps + index + 1,
	    (--list->length - index) * sizeof(*list->avps));
	return (0);
}

/*
 * Find an AVP in a list.
 */
int
ppp_l2tp_avp_list_find(const struct ppp_l2tp_avp_list *list,
	u_int16_t vendor, u_int16_t type)
{
	int i;

	for (i = 0; i < list->length; i++) {
		const struct ppp_l2tp_avp *const avp = &list->avps[i];

		if (avp->vendor == vendor && avp->type == type)
			return (i);
	}
	return (-1);
}

/*
 * Copy an AVP list.
 */
struct ppp_l2tp_avp_list *
ppp_l2tp_avp_list_copy(const struct ppp_l2tp_avp_list *orig)
{
	struct ppp_l2tp_avp_list *list;
	int i;

	if ((list = ppp_l2tp_avp_list_create()) == NULL)
		return (NULL);
	for (i = 0; i < orig->length; i++) {
		const struct ppp_l2tp_avp *const avp = &orig->avps[i];

		if (ppp_l2tp_avp_list_append(list, avp->mandatory,
		    avp->vendor, avp->type, avp->value, avp->vlen) == -1) {
			ppp_l2tp_avp_list_destroy(&list);
			return (NULL);
		}
	}
	return (list);
}

/*
 * Destroy an AVP list.
 */
void
ppp_l2tp_avp_list_destroy(struct ppp_l2tp_avp_list **listp)
{
	struct ppp_l2tp_avp_list *const list = *listp;
	int i;

	if (list == NULL)
		return;
	*listp = NULL;
	for (i = 0; i < list->length; i++) {
		const struct ppp_l2tp_avp *const avp = &list->avps[i];

		FREE(AVP_MTYPE, avp->value);
	}
	FREE(AVP_LIST_MTYPE, list->avps);
	FREE(AVP_LIST_MTYPE, list);
}

/*
 * Encode a list of AVP's into a single buffer, preserving the order
 * of the AVP's.  If a shared secret is supplied, and any of the AVP's
 * are hidden, then any required random vector AVP's are created and
 * inserted automatically.
 */
int
ppp_l2tp_avp_pack(const struct ppp_l2tp_avp_info *info,
	const struct ppp_l2tp_avp_list *list, const u_char *secret,
	size_t slen, u_char *buf)
{
	int len;
	int i;

	/* Pack AVP's */
	for (len = i = 0; i < list->length; i++) {
		const struct ppp_l2tp_avp *const avp = &list->avps[i];
		const struct ppp_l2tp_avp_info *desc;
		u_int16_t hdr[3];
		int hide = 0;
		int j;

		/* Find descriptor */
		for (desc = info; desc->name != NULL
		    && (desc->vendor != avp->vendor || desc->type != avp->type);
		    desc++);
		if (desc->name == NULL) {
			errno = EILSEQ;
			return (-1);
		}

		/* Sanity check AVP */
		if (avp->vlen < desc->min_length
		    || avp->vlen > desc->max_length
		    || avp->vlen > AVP_MAX_VLEN) {
			errno = EILSEQ;
			return (-1);
		}

		/* Set header stuff for this AVP */
		memset(&hdr, 0, sizeof(hdr));
		if (avp->mandatory)
			hdr[0] |= AVP_MANDATORY;
		if (secret != NULL && desc->hidden_ok) {
			hdr[0] |= AVP_HIDDEN;
			hide = 1;
		}
		hdr[0] |= (avp->vlen + 6);
		hdr[1] = avp->vendor;
		hdr[2] = avp->type;
		for (j = 0; j < 3; j++)
			hdr[j] = htons(hdr[j]);
		if (buf != NULL)
			memcpy(buf + len, &hdr, 6);
		len += 6;

		/* Copy AVP value, optionally hiding it */
		if (hide) {
			errno = EOPNOTSUPP;	/* XXX implement hiding */
			return (-1);
		} else {
			if (buf != NULL)
				memcpy(buf + len, avp->value, avp->vlen);
			len += avp->vlen;
		}
	}

	/* Done */
	return (len);
}

/*
 * Decode a packet into an array of unpacked AVP structures, preserving
 * the order of the AVP's. Random vector AVP's are automatically removed.
 */
struct ppp_l2tp_avp_list *
ppp_l2tp_avp_unpack(const struct ppp_l2tp_avp_info *info,
	const u_char *data, size_t dlen, const u_char *secret, size_t slen)
{
	struct ppp_l2tp_avp_list *list;
	const u_char *randvec = NULL;
	u_int16_t hdr[3];
	int randvec_len;
	int i;

	/* Create list */
	if ((list = ppp_l2tp_avp_list_create()) == NULL)
		return (NULL);

	/* Unpack AVP's */
	while (dlen > 0) {
		const struct ppp_l2tp_avp_info *desc;
		u_int16_t alen;

		/* Get header */
		if (dlen < 6)
			goto bogus;
		memcpy(&hdr, data, 6);
		for (i = 0; i < 3; i++)
			hdr[i] = ntohs(hdr[i]);
		alen = hdr[0] & AVP_LENGTH_MASK;
		if (alen < 6 || alen > dlen)
			goto bogus;

		/* Check reserved bits */
		if ((hdr[0] & AVP_RESERVED) != 0)
			goto unknown;

		/* Find descriptor for this AVP */
		for (desc = info; desc->name != NULL
		    && (desc->vendor != hdr[1] || desc->type != hdr[2]);
		    desc++);
		if (desc->name == NULL) {
unknown:		if ((hdr[0] & AVP_MANDATORY) != 0) {
				errno = ENOSYS;
				goto fail;
			}
			goto skip;
		}

		/* Remember random vector AVP's as we see them */
		if (hdr[1] == htons(0) && hdr[2] == htons(AVP_RANDOM_VECTOR)) {
			randvec = data + 6;
			randvec_len = alen - 6;
			continue;
		}

		/* Un-hide AVP if hidden */
		if ((hdr[0] & AVP_HIDDEN) != 0) {
			if (randvec == NULL)
				goto bogus;
			if (secret == NULL) {
				errno = EAUTH;
				goto fail;
			}
			errno = EOPNOTSUPP;	/* XXX implement unhiding */
			goto fail;
		} else if (ppp_l2tp_avp_list_append(list,
		    (hdr[0] & AVP_MANDATORY) != 0, hdr[1], hdr[2],
		    data + 6, alen - 6) == -1)
			goto fail;

skip:
		/* Continue with next AVP */
		data += alen;
		dlen -= alen;
	}

	/* Done */
	return (list);

bogus:
	/* Invalid data */
	errno = EILSEQ;
fail:
	ppp_l2tp_avp_list_destroy(&list);
	return (NULL);
}

/***********************************************************************
			AVP POINTERS METHODS
***********************************************************************/

/*
 * Create an AVP pointers structure from an AVP list.
 */
struct ppp_l2tp_avp_ptrs *
ppp_l2tp_avp_list2ptrs(const struct ppp_l2tp_avp_list *list)
{
	struct ppp_l2tp_avp_ptrs *ptrs;
	int i;

	/* Macro to allocate one pointer structure */
#define AVP_ALLOC(field)						\
do {									\
	size_t _size = sizeof(*ptrs->field);				\
									\
	if (_size < avp->vlen)						\
		_size = avp->vlen;					\
	_size += 16;							\
	FREE(AVP_PTRS_MTYPE, ptrs->field);				\
	if ((ptrs->field = MALLOC(AVP_PTRS_MTYPE, _size)) == NULL)	\
		goto fail;						\
	memset(ptrs->field, 0, _size);					\
} while (0)

	/* Create new pointers structure */
	if ((ptrs = MALLOC(AVP_PTRS_MTYPE, sizeof(*ptrs))) == NULL)
		return (NULL);
	memset(ptrs, 0, sizeof(*ptrs));

	/* Add recognized AVP's */
	for (i = 0; i < list->length; i++) {
		const struct ppp_l2tp_avp *const avp = &list->avps[i];
		const u_char *const ptr8 = (u_char *)avp->value;
		const u_int16_t *const ptr16 = (u_int16_t *)avp->value;
		const u_int32_t *const ptr32 = (u_int32_t *)avp->value;

		if (avp->vendor != 0)
			continue;
		switch (avp->type) {
		case AVP_MESSAGE_TYPE:
			AVP_ALLOC(message);
			ptrs->message->mesgtype = ntohs(ptr16[0]);
			break;
		case AVP_RESULT_CODE:
			AVP_ALLOC(errresultcode);
			ptrs->errresultcode->result = ntohs(ptr16[0]);
			if (avp->vlen > 2)
				ptrs->errresultcode->error = ntohs(ptr16[1]);
			if (avp->vlen > 4) {
				memcpy(ptrs->errresultcode->errmsg,
				    (char *)avp->value + 4, avp->vlen - 4);
			}
			break;
		case AVP_PROTOCOL_VERSION:
			AVP_ALLOC(protocol);
			ptrs->protocol->version = ptr8[0];
			ptrs->protocol->revision = ptr8[1];
			break;
		case AVP_FRAMING_CAPABILITIES:
			AVP_ALLOC(framingcap);
			ptrs->framingcap->sync =
			    (ntohl(ptr32[0]) & L2TP_FRAMING_SYNC) != 0;
			ptrs->framingcap->async =
			    (ntohl(ptr32[0]) & L2TP_FRAMING_ASYNC) != 0;
			break;
		case AVP_BEARER_CAPABILITIES:
			AVP_ALLOC(bearercap);
			ptrs->bearercap->digital =
			    (ntohl(ptr32[0]) & L2TP_BEARER_DIGITAL) != 0;
			ptrs->bearercap->analog =
			    (ntohl(ptr32[0]) & L2TP_BEARER_ANALOG) != 0;
			break;
		case AVP_TIE_BREAKER:
			AVP_ALLOC(tiebreaker);
			memcpy(ptrs->tiebreaker->value, avp->value, 8);
			break;
		case AVP_FIRMWARE_REVISION:
			AVP_ALLOC(firmware);
			ptrs->firmware->revision = ntohs(ptr16[0]);
			break;
		case AVP_HOST_NAME:
			AVP_ALLOC(hostname);
			memcpy(ptrs->hostname->hostname, avp->value, avp->vlen);
			break;
		case AVP_VENDOR_NAME:
			AVP_ALLOC(vendor);
			memcpy(ptrs->vendor->vendorname, avp->value, avp->vlen);
			break;
		case AVP_ASSIGNED_TUNNEL_ID:
			AVP_ALLOC(tunnelid);
			ptrs->tunnelid->id = ntohs(ptr16[0]);
			break;
		case AVP_RECEIVE_WINDOW_SIZE:
			AVP_ALLOC(winsize);
			ptrs->winsize->size = ntohs(ptr16[0]);
			break;
		case AVP_CHALLENGE:
			AVP_ALLOC(challenge);
			ptrs->challenge->length = avp->vlen;
			memcpy(ptrs->challenge->value, avp->value, avp->vlen);
			break;
		case AVP_CAUSE_CODE:
			AVP_ALLOC(causecode);
			ptrs->causecode->causecode = ntohs(ptr16[0]);
			ptrs->causecode->causemsg = ptr8[3];
			memcpy(ptrs->causecode->message,
			    (char *)avp->value + 3, avp->vlen - 3);
			break;
		case AVP_CHALLENGE_RESPONSE:
			AVP_ALLOC(challengresp);
			memcpy(ptrs->challengresp->value,
			    avp->value, avp->vlen);
			break;
		case AVP_ASSIGNED_SESSION_ID:
			AVP_ALLOC(sessionid);
			ptrs->sessionid->id = ntohs(ptr16[0]);
			break;
		case AVP_CALL_SERIAL_NUMBER:
			AVP_ALLOC(serialnum);
			ptrs->serialnum->serialnum = ntohl(ptr32[0]);
			break;
		case AVP_MINIMUM_BPS:
			AVP_ALLOC(minbps);
			ptrs->minbps->minbps = ntohl(ptr32[0]);
			break;
		case AVP_MAXIMUM_BPS:
			AVP_ALLOC(maxbps);
			ptrs->maxbps->maxbps = ntohl(ptr32[0]);
			break;
		case AVP_BEARER_TYPE:
			AVP_ALLOC(bearer);
			ptrs->bearer->digital =
			    (ntohl(ptr32[0]) & L2TP_BEARER_DIGITAL) != 0;
			ptrs->bearer->analog =
			    (ntohl(ptr32[0]) & L2TP_BEARER_ANALOG) != 0;
			break;
		case AVP_FRAMING_TYPE:
			AVP_ALLOC(framing);
			ptrs->framing->sync =
			    (ntohl(ptr32[0]) & L2TP_FRAMING_SYNC) != 0;
			ptrs->framing->async =
			    (ntohl(ptr32[0]) & L2TP_FRAMING_ASYNC) != 0;
			break;
		case AVP_CALLED_NUMBER:
			AVP_ALLOC(callednum);
			memcpy(ptrs->callednum->number, avp->value, avp->vlen);
			break;
		case AVP_CALLING_NUMBER:
			AVP_ALLOC(callingnum);
			memcpy(ptrs->callingnum->number, avp->value, avp->vlen);
			break;
		case AVP_SUB_ADDRESS:
			AVP_ALLOC(subaddress);
			memcpy(ptrs->subaddress->number, avp->value, avp->vlen);
			break;
		case AVP_TX_CONNECT_SPEED:
			AVP_ALLOC(txconnect);
			ptrs->txconnect->bps = ntohl(ptr32[0]);
			break;
		case AVP_PHYSICAL_CHANNEL_ID:
			AVP_ALLOC(channelid);
			ptrs->channelid->channel = ntohl(ptr32[0]);
			break;
		case AVP_INITIAL_RECV_CONFREQ:
			AVP_ALLOC(recvlcp);
			ptrs->recvlcp->length = avp->vlen;
			memcpy(ptrs->recvlcp->data, avp->value, avp->vlen);
			break;
		case AVP_LAST_SENT_CONFREQ:
			AVP_ALLOC(lastsendlcp);
			ptrs->lastsendlcp->length = avp->vlen;
			memcpy(ptrs->lastsendlcp->data, avp->value, avp->vlen);
			break;
		case AVP_LAST_RECV_CONFREQ:
			AVP_ALLOC(lastrecvlcp);
			ptrs->lastrecvlcp->length = avp->vlen;
			memcpy(ptrs->lastrecvlcp->data, avp->value, avp->vlen);
			break;
		case AVP_PROXY_AUTHEN_TYPE:
			AVP_ALLOC(proxyauth);
			ptrs->proxyauth->type = ntohs(ptr16[0]);
			break;
		case AVP_PROXY_AUTHEN_NAME:
			AVP_ALLOC(proxyname);
			ptrs->proxyname->length = avp->vlen;
			memcpy(ptrs->proxyname->data, avp->value, avp->vlen);
			break;
		case AVP_PROXY_AUTHEN_CHALLENGE:
			AVP_ALLOC(proxychallenge);
			ptrs->proxychallenge->length = avp->vlen;
			memcpy(ptrs->proxychallenge->data,
			    avp->value, avp->vlen);
			break;
		case AVP_PROXY_AUTHEN_ID:
			AVP_ALLOC(proxyid);
			ptrs->proxyid->id = ntohs(ptr16[0]);
			break;
		case AVP_PROXY_AUTHEN_RESPONSE:
			AVP_ALLOC(proxyres);
			ptrs->proxyres->length = avp->vlen;
			memcpy(ptrs->proxyres->data, avp->value, avp->vlen);
			break;
		case AVP_CALL_ERRORS:
		    {
			u_int32_t vals[6];

			memcpy(&vals, &ptr16[1], sizeof(vals));
			AVP_ALLOC(callerror);
			ptrs->callerror->crc = ntohl(vals[0]);
			ptrs->callerror->frame = ntohl(vals[1]);
			ptrs->callerror->overrun = ntohl(vals[2]);
			ptrs->callerror->buffer = ntohl(vals[3]);
			ptrs->callerror->timeout = ntohl(vals[4]);
			ptrs->callerror->alignment = ntohl(vals[5]);
			break;
		    }
		case AVP_ACCM:
		    {
			u_int32_t vals[2];

			memcpy(&vals, &ptr16[1], sizeof(vals));
			AVP_ALLOC(accm);
			ptrs->accm->xmit = ntohl(vals[0]);
			ptrs->accm->recv = ntohl(vals[1]);
			break;
		    }
		case AVP_PRIVATE_GROUP_ID:
			AVP_ALLOC(groupid);
			ptrs->groupid->length = avp->vlen;
			memcpy(ptrs->groupid->data, avp->value, avp->vlen);
			break;
		case AVP_RX_CONNECT_SPEED:
			AVP_ALLOC(rxconnect);
			ptrs->rxconnect->bps = ntohl(ptr32[0]);
			break;
		case AVP_SEQUENCING_REQUIRED:
			AVP_ALLOC(seqrequired);
			break;
		default:
			break;
		}
	}

	/* Done */
	return (ptrs);

fail:
	/* Clean up after failure */
	ppp_l2tp_avp_ptrs_destroy(&ptrs);
	return (NULL);
}

/*
 * Destroy an AVP pointers structure.
 */
void
ppp_l2tp_avp_ptrs_destroy(struct ppp_l2tp_avp_ptrs **ptrsp)
{
	struct ppp_l2tp_avp_ptrs *const ptrs = *ptrsp;

	if (ptrs == NULL)
		return;
	FREE(AVP_PTRS_MTYPE, ptrs->message);
	FREE(AVP_PTRS_MTYPE, ptrs->errresultcode);
	FREE(AVP_PTRS_MTYPE, ptrs->protocol);
	FREE(AVP_PTRS_MTYPE, ptrs->framingcap);
	FREE(AVP_PTRS_MTYPE, ptrs->bearercap);
	FREE(AVP_PTRS_MTYPE, ptrs->tiebreaker);
	FREE(AVP_PTRS_MTYPE, ptrs->firmware);
	FREE(AVP_PTRS_MTYPE, ptrs->hostname);
	FREE(AVP_PTRS_MTYPE, ptrs->vendor);
	FREE(AVP_PTRS_MTYPE, ptrs->tunnelid);
	FREE(AVP_PTRS_MTYPE, ptrs->sessionid);
	FREE(AVP_PTRS_MTYPE, ptrs->winsize);
	FREE(AVP_PTRS_MTYPE, ptrs->challenge);
	FREE(AVP_PTRS_MTYPE, ptrs->challengresp);
	FREE(AVP_PTRS_MTYPE, ptrs->causecode);
	FREE(AVP_PTRS_MTYPE, ptrs->serialnum);
	FREE(AVP_PTRS_MTYPE, ptrs->maxbps);
	FREE(AVP_PTRS_MTYPE, ptrs->bearer);
	FREE(AVP_PTRS_MTYPE, ptrs->framing);
	FREE(AVP_PTRS_MTYPE, ptrs->callednum);
	FREE(AVP_PTRS_MTYPE, ptrs->callingnum);
	FREE(AVP_PTRS_MTYPE, ptrs->subaddress);
	FREE(AVP_PTRS_MTYPE, ptrs->txconnect);
	FREE(AVP_PTRS_MTYPE, ptrs->rxconnect);
	FREE(AVP_PTRS_MTYPE, ptrs->channelid);
	FREE(AVP_PTRS_MTYPE, ptrs->groupid);
	FREE(AVP_PTRS_MTYPE, ptrs->recvlcp);
	FREE(AVP_PTRS_MTYPE, ptrs->lastsendlcp);
	FREE(AVP_PTRS_MTYPE, ptrs->lastrecvlcp);
	FREE(AVP_PTRS_MTYPE, ptrs->proxyauth);
	FREE(AVP_PTRS_MTYPE, ptrs->proxyname);
	FREE(AVP_PTRS_MTYPE, ptrs->proxychallenge);
	FREE(AVP_PTRS_MTYPE, ptrs->proxyid);
	FREE(AVP_PTRS_MTYPE, ptrs->proxyres);
	FREE(AVP_PTRS_MTYPE, ptrs->callerror);
	FREE(AVP_PTRS_MTYPE, ptrs->accm);
	FREE(AVP_PTRS_MTYPE, ptrs->seqrequired);
	FREE(AVP_PTRS_MTYPE, ptrs);
	*ptrsp = NULL;
}

/***********************************************************************
			AVP DECODERS
***********************************************************************/

#define DECODE_INITIAL(t)						\
void									\
ppp_l2tp_avp_decode_ ## t(const struct ppp_l2tp_avp_info *info,		\
	const struct ppp_l2tp_avp *avp, char *buf, size_t bmax)		\
{									\
	const struct ppp_l2tp_avp_list list				\
	    = { 1, (struct ppp_l2tp_avp *)avp };			\
	struct ppp_l2tp_avp_ptrs *ptrs;					\
									\
	if ((ptrs = ppp_l2tp_avp_list2ptrs(&list)) == NULL) {		\
		snprintf(buf, bmax,					\
		    "decode failed: %s", strerror(errno));		\
		goto done;						\
	}								\
	strlcpy(buf, "", bmax);

#define DECODE_FINAL							\
done:									\
	ppp_l2tp_avp_ptrs_destroy(&ptrs);				\
}

#define DECODE_BYTES(p, len)						\
	do {								\
		int _i;							\
									\
		for (_i = 0; _i < len; _i++) {				\
			snprintf(buf + strlen(buf),			\
			    bmax - strlen(buf), "%02x",			\
			    ((u_char *)p)[_i]);				\
		}							\
	} while (0);

DECODE_INITIAL(MESSAGE_TYPE)
	{
		static const char *names[] = {
		    "?0?", "SCCRQ", "SCCRP", "SCCCN", "StopCCN", "?5?",
		    "HELLO", "OCRQ", "OCRP", "OCCN", "ICRQ", "ICRP",
		    "ICCN", "?13?", "CDN", "WEN", "SLI",
		};

		if (ptrs->message->mesgtype > sizeof(names) / sizeof(*names)) {
			snprintf(buf, bmax, "?%u?", ptrs->message->mesgtype);
			goto done;
		}
		strlcpy(buf, names[ptrs->message->mesgtype], bmax);
	}
DECODE_FINAL

DECODE_INITIAL(RESULT_CODE)
	snprintf(buf, bmax, "result=%u error=%u errmsg=\"",
	    ptrs->errresultcode->result, ptrs->errresultcode->error);
	ppp_util_ascify(buf + strlen(buf), bmax - strlen(buf),
	    ptrs->errresultcode->errmsg, strlen(ptrs->errresultcode->errmsg));
	strlcat(buf, "\"", bmax);
DECODE_FINAL

DECODE_INITIAL(PROTOCOL_VERSION)
	snprintf(buf, bmax, "%u.%u",
	    ptrs->protocol->version, ptrs->protocol->revision);
DECODE_FINAL

DECODE_INITIAL(FRAMING_CAPABILITIES)
	snprintf(buf, bmax, "sync=%u async=%u",
	    ptrs->framingcap->sync, ptrs->framingcap->async);
DECODE_FINAL

DECODE_INITIAL(BEARER_CAPABILITIES)
	snprintf(buf, bmax, "digital=%u analog=%u",
	    ptrs->bearercap->digital, ptrs->bearercap->analog);
DECODE_FINAL

DECODE_INITIAL(TIE_BREAKER)
	snprintf(buf, bmax, "%02x%02x%02x%02x%02x%02x%02x%02x",
	    ((u_char *)ptrs->tiebreaker->value)[0],
	    ((u_char *)ptrs->tiebreaker->value)[1],
	    ((u_char *)ptrs->tiebreaker->value)[2],
	    ((u_char *)ptrs->tiebreaker->value)[3],
	    ((u_char *)ptrs->tiebreaker->value)[4],
	    ((u_char *)ptrs->tiebreaker->value)[5],
	    ((u_char *)ptrs->tiebreaker->value)[6],
	    ((u_char *)ptrs->tiebreaker->value)[7]);
DECODE_FINAL

DECODE_INITIAL(FIRMWARE_REVISION)
	snprintf(buf, bmax, "0x%04x", ptrs->firmware->revision);
DECODE_FINAL

DECODE_INITIAL(HOST_NAME)
	strlcpy(buf, "\"", bmax);
	ppp_util_ascify(buf + strlen(buf), bmax - strlen(buf),
	    ptrs->hostname->hostname, strlen(ptrs->hostname->hostname));
	strlcat(buf, "\"", bmax);
DECODE_FINAL

DECODE_INITIAL(VENDOR_NAME)
	strlcpy(buf, "\"", bmax);
	ppp_util_ascify(buf + strlen(buf), bmax - strlen(buf),
	    ptrs->vendor->vendorname, strlen(ptrs->vendor->vendorname));
	strlcat(buf, "\"", bmax);
DECODE_FINAL

DECODE_INITIAL(ASSIGNED_TUNNEL_ID)
	snprintf(buf, bmax, "0x%04x", ptrs->tunnelid->id);
DECODE_FINAL

DECODE_INITIAL(RECEIVE_WINDOW_SIZE)
	snprintf(buf, bmax, "%u", ptrs->winsize->size);
DECODE_FINAL

DECODE_INITIAL(CHALLENGE)
DECODE_BYTES(ptrs->challenge->value, ptrs->challenge->length)
DECODE_FINAL

DECODE_INITIAL(CAUSE_CODE)
	snprintf(buf, bmax, "causecode=0x%04x causemsg=0x%02x msg=\"",
	    ptrs->causecode->causecode, ptrs->causecode->causemsg);
	ppp_util_ascify(buf + strlen(buf), bmax - strlen(buf),
	    ptrs->causecode->message, strlen(ptrs->causecode->message));
	strlcat(buf, "\"", bmax);
DECODE_FINAL

DECODE_INITIAL(CHALLENGE_RESPONSE)
DECODE_BYTES(ptrs->challengresp->value, 16)
DECODE_FINAL

DECODE_INITIAL(ASSIGNED_SESSION_ID)
	snprintf(buf, bmax, "0x%04x", ptrs->sessionid->id);
DECODE_FINAL

DECODE_INITIAL(CALL_SERIAL_NUMBER)
	snprintf(buf, bmax, "%u", ptrs->serialnum->serialnum);
DECODE_FINAL

DECODE_INITIAL(MINIMUM_BPS)
	snprintf(buf, bmax, "%u", ptrs->minbps->minbps);
DECODE_FINAL

DECODE_INITIAL(MAXIMUM_BPS)
	snprintf(buf, bmax, "%u", ptrs->maxbps->maxbps);
DECODE_FINAL

DECODE_INITIAL(BEARER_TYPE)
	snprintf(buf, bmax, "digital=%u analog=%u",
	    ptrs->bearer->digital, ptrs->bearer->analog);
DECODE_FINAL

DECODE_INITIAL(FRAMING_TYPE)
	snprintf(buf, bmax, "sync=%u async=%u",
	    ptrs->framing->sync, ptrs->framing->async);
DECODE_FINAL

DECODE_INITIAL(CALLED_NUMBER)
	strlcpy(buf, "\"", bmax);
	ppp_util_ascify(buf + strlen(buf), bmax - strlen(buf),
	    ptrs->callednum->number, strlen(ptrs->callednum->number));
	strlcat(buf, "\"", bmax);
DECODE_FINAL

DECODE_INITIAL(CALLING_NUMBER)
	strlcpy(buf, "\"", bmax);
	ppp_util_ascify(buf + strlen(buf), bmax - strlen(buf),
	    ptrs->callingnum->number, strlen(ptrs->callingnum->number));
	strlcat(buf, "\"", bmax);
DECODE_FINAL

DECODE_INITIAL(SUB_ADDRESS)
	strlcpy(buf, "\"", bmax);
	ppp_util_ascify(buf + strlen(buf), bmax - strlen(buf),
	    ptrs->subaddress->number, strlen(ptrs->subaddress->number));
	strlcat(buf, "\"", bmax);
DECODE_FINAL

DECODE_INITIAL(TX_CONNECT_SPEED)
	snprintf(buf, bmax, "%u", ptrs->txconnect->bps);
DECODE_FINAL

DECODE_INITIAL(PHYSICAL_CHANNEL_ID)
	snprintf(buf, bmax, "0x%08x", ptrs->channelid->channel);
DECODE_FINAL

DECODE_INITIAL(INITIAL_RECV_CONFREQ)
	ppp_fsm_options_decode(lcp_opt_desc,
	    ptrs->recvlcp->data, ptrs->recvlcp->length, buf, bmax);
DECODE_FINAL

DECODE_INITIAL(LAST_SENT_CONFREQ)
	ppp_fsm_options_decode(lcp_opt_desc,
	    ptrs->lastsendlcp->data, ptrs->lastsendlcp->length, buf, bmax);
DECODE_FINAL

DECODE_INITIAL(LAST_RECV_CONFREQ)
	ppp_fsm_options_decode(lcp_opt_desc,
	    ptrs->lastrecvlcp->data, ptrs->lastrecvlcp->length, buf, bmax);
DECODE_FINAL

DECODE_INITIAL(PROXY_AUTHEN_TYPE)
	snprintf(buf, bmax, "%u", ptrs->proxyauth->type);
DECODE_FINAL

DECODE_INITIAL(PROXY_AUTHEN_NAME)
	strlcpy(buf, "\"", bmax);
	ppp_util_ascify(buf + strlen(buf), bmax - strlen(buf),
	    ptrs->proxyname->data, strlen(ptrs->proxyname->data));
	strlcat(buf, "\"", bmax);
DECODE_FINAL

DECODE_INITIAL(PROXY_AUTHEN_CHALLENGE)
DECODE_BYTES(ptrs->proxychallenge->data, ptrs->proxychallenge->length)
DECODE_FINAL

DECODE_INITIAL(PROXY_AUTHEN_ID)
	snprintf(buf, bmax, "%u", ptrs->proxyid->id);
DECODE_FINAL

DECODE_INITIAL(PROXY_AUTHEN_RESPONSE)
DECODE_BYTES(ptrs->proxyres->data, ptrs->proxyres->length)
DECODE_FINAL

DECODE_INITIAL(CALL_ERRORS)
	snprintf(buf, bmax, "crc=%u frame=%u overrun=%u"
	    "buffer=%u timeout=%u alignment=%u",
	    ptrs->callerror->crc, ptrs->callerror->frame,
	    ptrs->callerror->overrun, ptrs->callerror->buffer,
	    ptrs->callerror->timeout, ptrs->callerror->alignment);
DECODE_FINAL

DECODE_INITIAL(ACCM)
	snprintf(buf, bmax, "xmit=0x%08x recv=0x%08x",
	    ptrs->accm->xmit, ptrs->accm->recv);
DECODE_FINAL

DECODE_INITIAL(RANDOM_VECTOR)
DECODE_BYTES(avp->value, avp->vlen)
DECODE_FINAL

DECODE_INITIAL(PRIVATE_GROUP_ID)
DECODE_BYTES(ptrs->groupid->data, ptrs->groupid->length)
DECODE_FINAL

DECODE_INITIAL(RX_CONNECT_SPEED)
	snprintf(buf, bmax, "%u", ptrs->rxconnect->bps);
DECODE_FINAL

DECODE_INITIAL(SEQUENCING_REQUIRED)
DECODE_FINAL


