/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)vfscanf.c	8.1 (Berkeley) 6/4/93";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>


#include "namespace.h"
#include <ctype.h>
#include <inttypes.h>
#include <pd_stdio_p.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include "un-namespace.h"

#include "libc_private.h"
#include "local.h"

#ifndef NO_FLOATING_POINT
#include <locale.h>
#endif

#define	BUF		513	/* Maximum length of numeric string. */

/*
 * Flags used during conversion.
 */
#define	LONG		0x01	/* l: long or double */
#define	LONGDBL		0x02	/* L: long double */
#define	SHORT		0x04	/* h: short */
#define	SUPPRESS	0x08	/* *: suppress assignment */
#define	POINTER		0x10	/* p: void * (as hex) */
#define	NOSKIP		0x20	/* [ or c: do not skip blanks */
#define	LONGLONG	0x400	/* ll: long long (+ deprecated q: quad) */
#define	INTMAXT		0x800	/* j: intmax_t */
#define	PTRDIFFT	0x1000	/* t: ptrdiff_t */
#define	SIZET		0x2000	/* z: size_t */
#define	SHORTSHORT	0x4000	/* hh: char */
#define	UNSIGNED	0x8000	/* %[oupxX] conversions */

/*
 * The following are used in integral conversions only:
 * SIGNOK, NDIGITS, PFXOK, and NZDIGITS
 */
#define	SIGNOK		0x40	/* +/- is (still) legal */
#define	NDIGITS		0x80	/* no digits detected */
#define	PFXOK		0x100	/* 0x prefix is (still) legal */
#define	NZDIGITS	0x200	/* no zero digits detected */
#define	HAVESIGN	0x10000	/* sign detected */

/*
 * Conversion types.
 */
#define	CT_CHAR		0	/* %c conversion */
#define	CT_CCL		1	/* %[...] conversion */
#define	CT_STRING	2	/* %s conversion */
#define	CT_INT		3	/* %[dioupxX] conversion */
#define	CT_FLOAT	4	/* %[efgEFG] conversion */

static const u_char *__sccl(char *, const u_char *);
#ifndef NO_FLOATING_POINT
static int parsefloat(FILE *, char *, char *);
#endif

int __scanfdebug = 0;

#undef vfscanf
#undef FILE
#undef __sFILE

/*
 * __vfscanf - MT-safe version
 */
int
pd_vfscanf_p(FILEpd *fp, char const *fmt0, va_list ap)
{
	int ret;

	FLOCKFILE(fp);
	ret = __svfscanf(fp, fmt0, ap);
	FUNLOCKFILE(fp);
	return (ret);
}

/*
 * __svfscanf - non-MT-safe version of __vfscanf
 */
int
__svfscanf(FILEpd *fp, const char *fmt0, va_list ap)
{
	const u_char *fmt = (const u_char *)fmt0;
	int c;			/* character from format, or conversion */
	size_t width;		/* field width, or 0 */
	char *p;		/* points into all kinds of strings */
	int n;			/* handy integer */
	int flags;		/* flags as defined above */
	char *p0;		/* saves original value of p when necessary */
	int nassigned;		/* number of fields assigned */
	int nconversions;	/* number of conversions */
	int nread;		/* number of characters consumed from fp */
	int base = 10;		/* base argument to conversion function */
	char ccltab[256];	/* character class table for %[...] */
	char buf[BUF];		/* buffer for numeric and mb conversions */
	wchar_t *wcp;		/* handy wide character pointer */
	size_t nconv;		/* length of multibyte sequence converted */
	static const mbstate_t initial;
	mbstate_t mbs;

	/* `basefix' is used to avoid `if' tests in the integer scanner */
	static short basefix[17] =
		{ 10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

	ORIENT(fp, -1);

	nassigned = 0;
	nconversions = 0;
	nread = 0;
	for (;;) {
		c = *fmt++;
		if (c == 0)
			return (nassigned);
		if (isspace(c)) {
			while ((fp->_r > 0 || __srefill(fp) == 0) && isspace(*fp->_p))
				nread++, fp->_r--, fp->_p++;
			continue;
		}
		if (c != '%')
			goto literal;
		width = 0;
		flags = 0;
		/*
		 * switch on the format.  continue if done;
		 * break once format type is derived.
		 */
again:		c = *fmt++;
		switch (c) {
		case '%':
literal:
			if (fp->_r <= 0 && __srefill(fp))
				goto input_failure;
			if (*fp->_p != c)
				goto match_failure;
			fp->_r--, fp->_p++;
			nread++;
			continue;

		case '*':
			flags |= SUPPRESS;
			goto again;
		case 'j':
			flags |= INTMAXT;
			goto again;
		case 'l':
			if (flags & LONG) {
				flags &= ~LONG;
				flags |= LONGLONG;
			} else
				flags |= LONG;
			goto again;
		case 'q':
			flags |= LONGLONG;	/* not quite */
			goto again;
		case 't':
			flags |= PTRDIFFT;
			goto again;
		case 'z':
			flags |= SIZET;
			goto again;
		case 'L':
			flags |= LONGDBL;
			goto again;
		case 'h':
			if (flags & SHORT) {
				flags &= ~SHORT;
				flags |= SHORTSHORT;
			} else
				flags |= SHORT;
			goto again;

		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			width = width * 10 + c - '0';
			goto again;

		/*
		 * Conversions.
		 */
		case 'd':
			c = CT_INT;
			base = 10;
			break;

		case 'i':
			c = CT_INT;
			base = 0;
			break;

		case 'o':
			c = CT_INT;
			flags |= UNSIGNED;
			base = 8;
			break;

		case 'u':
			c = CT_INT;
			flags |= UNSIGNED;
			base = 10;
			break;

		case 'X':
		case 'x':
			flags |= PFXOK;	/* enable 0x prefixing */
			c = CT_INT;
			flags |= UNSIGNED;
			base = 16;
			break;

#ifndef NO_FLOATING_POINT
		case 'A': case 'E': case 'F': case 'G':
		case 'a': case 'e': case 'f': case 'g':
			c = CT_FLOAT;
			break;
#endif

		case 'S':
			flags |= LONG;
			/* FALLTHROUGH */
		case 's':
			c = CT_STRING;
			break;

		case '[':
			fmt = __sccl(ccltab, fmt);
			flags |= NOSKIP;
			c = CT_CCL;
			break;

		case 'C':
			flags |= LONG;
			/* FALLTHROUGH */
		case 'c':
			flags |= NOSKIP;
			c = CT_CHAR;
			break;

		case 'p':	/* pointer format is like hex */
			flags |= POINTER | PFXOK;
			c = CT_INT;		/* assumes sizeof(uintmax_t) */
			flags |= UNSIGNED;	/*      >= sizeof(uintptr_t) */
			base = 16;
			break;

		case 'n':
			nconversions++;
			if (flags & SUPPRESS)	/* ??? */
				continue;
			if (flags & SHORTSHORT)
				*va_arg(ap, char *) = nread;
			else if (flags & SHORT)
				*va_arg(ap, short *) = nread;
			else if (flags & LONG)
				*va_arg(ap, long *) = nread;
			else if (flags & LONGLONG)
				*va_arg(ap, long long *) = nread;
			else if (flags & INTMAXT)
				*va_arg(ap, intmax_t *) = nread;
			else if (flags & SIZET)
				*va_arg(ap, size_t *) = nread;
			else if (flags & PTRDIFFT)
				*va_arg(ap, ptrdiff_t *) = nread;
			else
				*va_arg(ap, int *) = nread;
			continue;

		default:
			goto match_failure;

		/*
		 * Disgusting backwards compatibility hack.	XXX
		 */
		case '\0':	/* compat */
			return (EOF);
		}

		/*
		 * We have a conversion that requires input.
		 */
		if (fp->_r <= 0 && __srefill(fp))
			goto input_failure;

		/*
		 * Consume leading white space, except for formats
		 * that suppress this.
		 */
		if ((flags & NOSKIP) == 0) {
			while (isspace(*fp->_p)) {
				nread++;
				if (--fp->_r > 0)
					fp->_p++;
				else if (__srefill(fp))
					goto input_failure;
			}
			/*
			 * Note that there is at least one character in
			 * the buffer, so conversions that do not set NOSKIP
			 * ca no longer result in an input failure.
			 */
		}

		/*
		 * Do the conversion.
		 */
		switch (c) {

		case CT_CHAR:
			/* scan arbitrary characters (sets NOSKIP) */
			if (width == 0)
				width = 1;
			if (flags & LONG) {
				if ((flags & SUPPRESS) == 0)
					wcp = va_arg(ap, wchar_t *);
				else
					wcp = NULL;
				n = 0;
				while (width != 0) {
					if (n == MB_CUR_MAX) {
						fp->_flags |= __SERR;
						goto input_failure;
					}
					buf[n++] = *fp->_p;
					fp->_p++;
					fp->_r--;
					mbs = initial;
					nconv = mbrtowc(wcp, buf, n, &mbs);
					if (nconv == (size_t)-1) {
						fp->_flags |= __SERR;
						goto input_failure;
					}
					if (nconv == 0 && !(flags & SUPPRESS))
						*wcp = L'\0';
					if (nconv != (size_t)-2) {
						nread += n;
						width--;
						if (!(flags & SUPPRESS))
							wcp++;
						n = 0;
					}
					if (fp->_r <= 0 && __srefill(fp)) {
						if (n != 0) {
							fp->_flags |= __SERR;
							goto input_failure;
						}
						break;
					}
				}
				if (!(flags & SUPPRESS))
					nassigned++;
			} else if (flags & SUPPRESS) {
				size_t sum = 0;
				for (;;) {
					if ((n = fp->_r) < width) {
						sum += n;
						width -= n;
						fp->_p += n;
						if (__srefill(fp)) {
							if (sum == 0)
							    goto input_failure;
							break;
						}
					} else {
						sum += width;
						fp->_r -= width;
						fp->_p += width;
						break;
					}
				}
				nread += sum;
			} else {
				size_t r = __fread((void *)va_arg(ap, char *), 1,
				    width, fp);

				if (r == 0)
					goto input_failure;
				nread += r;
				nassigned++;
			}
			nconversions++;
			break;

		case CT_CCL:
			/* scan a (nonempty) character class (sets NOSKIP) */
			if (width == 0)
				width = (size_t)~0;	/* `infinity' */
			/* take only those things in the class */
			if (flags & LONG) {
				wchar_t twc;
				int nchars;

				if ((flags & SUPPRESS) == 0)
					wcp = va_arg(ap, wchar_t *);
				else
					wcp = &twc;
				n = 0;
				nchars = 0;
				while (width != 0) {
					if (n == MB_CUR_MAX) {
						fp->_flags |= __SERR;
						goto input_failure;
					}
					buf[n++] = *fp->_p;
					fp->_p++;
					fp->_r--;
					mbs = initial;
					nconv = mbrtowc(wcp, buf, n, &mbs);
					if (nconv == (size_t)-1) {
						fp->_flags |= __SERR;
						goto input_failure;
					}
					if (nconv == 0)
						*wcp = L'\0';
					if (nconv != (size_t)-2) {
						if (wctob(*wcp) != EOF &&
						    !ccltab[wctob(*wcp)]) {
							while (n != 0) {
								n--;
								__ungetc(buf[n],
								    fp);
							}
							break;
						}
						nread += n;
						width--;
						if (!(flags & SUPPRESS))
							wcp++;
						nchars++;
						n = 0;
					}
					if (fp->_r <= 0 && __srefill(fp)) {
						if (n != 0) {
							fp->_flags |= __SERR;
							goto input_failure;
						}
						break;
					}
				}
				if (n != 0) {
					fp->_flags |= __SERR;
					goto input_failure;
				}
				n = nchars;
				if (n == 0)
					goto match_failure;
				if (!(flags & SUPPRESS)) {
					*wcp = L'\0';
					nassigned++;
				}
			} else if (flags & SUPPRESS) {
				n = 0;
				while (ccltab[*fp->_p]) {
					n++, fp->_r--, fp->_p++;
					if (--width == 0)
						break;
					if (fp->_r <= 0 && __srefill(fp)) {
						if (n == 0)
							goto input_failure;
						break;
					}
				}
				if (n == 0)
					goto match_failure;
			} else {
				p0 = p = va_arg(ap, char *);
				while (ccltab[*fp->_p]) {
					fp->_r--;
					*p++ = *fp->_p++;
					if (--width == 0)
						break;
					if (fp->_r <= 0 && __srefill(fp)) {
						if (p == p0)
							goto input_failure;
						break;
					}
				}
				n = p - p0;
				if (n == 0)
					goto match_failure;
				*p = 0;
				nassigned++;
			}
			nread += n;
			nconversions++;
			break;

		case CT_STRING:
			/* like CCL, but zero-length string OK, & no NOSKIP */
			if (width == 0)
				width = (size_t)~0;
			if (flags & LONG) {
				wchar_t twc;

				if ((flags & SUPPRESS) == 0)
					wcp = va_arg(ap, wchar_t *);
				else
					wcp = &twc;
				n = 0;
				while (!isspace(*fp->_p) && width != 0) {
					if (n == MB_CUR_MAX) {
						fp->_flags |= __SERR;
						goto input_failure;
					}
					buf[n++] = *fp->_p;
					fp->_p++;
					fp->_r--;
					mbs = initial;
					nconv = mbrtowc(wcp, buf, n, &mbs);
					if (nconv == (size_t)-1) {
						fp->_flags |= __SERR;
						goto input_failure;
					}
					if (nconv == 0)
						*wcp = L'\0';
					if (nconv != (size_t)-2) {
						if (iswspace(*wcp)) {
							while (n != 0) {
								n--;
								__ungetc(buf[n],
								    fp);
							}
							break;
						}
						nread += n;
						width--;
						if (!(flags & SUPPRESS))
							wcp++;
						n = 0;
					}
					if (fp->_r <= 0 && __srefill(fp)) {
						if (n != 0) {
							fp->_flags |= __SERR;
							goto input_failure;
						}
						break;
					}
				}
				if (!(flags & SUPPRESS)) {
					*wcp = L'\0';
					nassigned++;
				}
			} else if (flags & SUPPRESS) {
				n = 0;
				while (!isspace(*fp->_p)) {
					n++, fp->_r--, fp->_p++;
					if (--width == 0)
						break;
					if (fp->_r <= 0 && __srefill(fp))
						break;
				}
				nread += n;
			} else {
				p0 = p = va_arg(ap, char *);
				while (!isspace(*fp->_p)) {
					fp->_r--;
					*p++ = *fp->_p++;
					if (--width == 0)
						break;
					if (fp->_r <= 0 && __srefill(fp))
						break;
				}
				*p = 0;
				nread += p - p0;
				nassigned++;
			}
			nconversions++;
			continue;

		case CT_INT:
			/* scan an integer as if by the conversion function */
#ifdef hardway
			if (width == 0 || width > sizeof(buf) - 1)
				width = sizeof(buf) - 1;
#else
			/* size_t is unsigned, hence this optimisation */
			if (--width > sizeof(buf) - 2)
				width = sizeof(buf) - 2;
			width++;
#endif
			flags |= SIGNOK | NDIGITS | NZDIGITS;
			for (p = buf; width; width--) {
				c = *fp->_p;
				/*
				 * Switch on the character; `goto ok'
				 * if we accept it as a part of number.
				 */
				switch (c) {

				/*
				 * The digit 0 is always legal, but is
				 * special.  For %i conversions, if no
				 * digits (zero or nonzero) have been
				 * scanned (only signs), we will have
				 * base==0.  In that case, we should set
				 * it to 8 and enable 0x prefixing.
				 * Also, if we have not scanned zero digits
				 * before this, do not turn off prefixing
				 * (someone else will turn it off if we
				 * have scanned any nonzero digits).
				 */
				case '0':
					if (base == 0) {
						base = 8;
						flags |= PFXOK;
					}
					if (flags & NZDIGITS)
					    flags &= ~(SIGNOK|NZDIGITS|NDIGITS);
					else
					    flags &= ~(SIGNOK|PFXOK|NDIGITS);
					goto ok;

				/* 1 through 7 always legal */
				case '1': case '2': case '3':
				case '4': case '5': case '6': case '7':
					base = basefix[base];
					flags &= ~(SIGNOK | PFXOK | NDIGITS);
					goto ok;

				/* digits 8 and 9 ok iff decimal or hex */
				case '8': case '9':
					base = basefix[base];
					if (base <= 8)
						break;	/* not legal here */
					flags &= ~(SIGNOK | PFXOK | NDIGITS);
					goto ok;

				/* letters ok iff hex */
				case 'A': case 'B': case 'C':
				case 'D': case 'E': case 'F':
				case 'a': case 'b': case 'c':
				case 'd': case 'e': case 'f':
					/* no need to fix base here */
					if (base <= 10)
						break;	/* not legal here */
					flags &= ~(SIGNOK | PFXOK | NDIGITS);
					goto ok;

				/* sign ok only as first character */
				case '+': case '-':
					if (flags & SIGNOK) {
						flags &= ~SIGNOK;
						flags |= HAVESIGN;
						goto ok;
					}
					break;
					
				/*
				 * x ok iff flag still set & 2nd char (or
				 * 3rd char if we have a sign).
				 */
				case 'x': case 'X':
					if (flags & PFXOK && p ==
					    buf + 1 + !!(flags & HAVESIGN)) {
						base = 16;	/* if %i */
						flags &= ~PFXOK;
						goto ok;
					}
					break;
				}

				/*
				 * If we got here, c is not a legal character
				 * for a number.  Stop accumulating digits.
				 */
				break;
		ok:
				/*
				 * c is legal: store it and look at the next.
				 */
				*p++ = c;
				if (--fp->_r > 0)
					fp->_p++;
				else if (__srefill(fp))
					break;		/* EOF */
			}
			/*
			 * If we had only a sign, it is no good; push
			 * back the sign.  If the number ends in `x',
			 * it was [sign] '0' 'x', so push back the x
			 * and treat it as [sign] '0'.
			 */
			if (flags & NDIGITS) {
				if (p > buf)
					(void) __ungetc(*(u_char *)--p, fp);
				goto match_failure;
			}
			c = ((u_char *)p)[-1];
			if (c == 'x' || c == 'X') {
				--p;
				(void) __ungetc(c, fp);
			}
			if ((flags & SUPPRESS) == 0) {
				uintmax_t res;

				*p = 0;
				if ((flags & UNSIGNED) == 0)
				    res = strtoimax(buf, (char **)NULL, base);
				else
				    res = strtoumax(buf, (char **)NULL, base);
				if (flags & POINTER)
					*va_arg(ap, void **) =
							(void *)(uintptr_t)res;
				else if (flags & SHORTSHORT)
					*va_arg(ap, char *) = res;
				else if (flags & SHORT)
					*va_arg(ap, short *) = res;
				else if (flags & LONG)
					*va_arg(ap, long *) = res;
				else if (flags & LONGLONG)
					*va_arg(ap, long long *) = res;
				else if (flags & INTMAXT)
					*va_arg(ap, intmax_t *) = res;
				else if (flags & PTRDIFFT)
					*va_arg(ap, ptrdiff_t *) = res;
				else if (flags & SIZET)
					*va_arg(ap, size_t *) = res;
				else
					*va_arg(ap, int *) = res;
				nassigned++;
			}
			nread += p - buf;
			nconversions++;
			break;

#ifndef NO_FLOATING_POINT
		case CT_FLOAT:
			/* scan a floating point number as if by strtod */
			if (width == 0 || width > sizeof(buf) - 1)
				width = sizeof(buf) - 1;
			if ((width = parsefloat(fp, buf, buf + width)) == 0)
				goto match_failure;
			if ((flags & SUPPRESS) == 0) {
				if (flags & LONGDBL) {
#ifdef WIN32
					/* 
					 * Win32 is missing strtold() in the
					 * C runtime library even though 
					 * the headers and docs say it's
					 * there.  Punt with a double.
					 */
					long double res = 
					  (long double) strtod(buf, &p);
#else
					long double res = strtold(buf, &p);
#endif
					*va_arg(ap, long double *) = res;
				} else if (flags & LONG) {
					double res = strtod(buf, &p);
					*va_arg(ap, double *) = res;
				} else {
					float res = strtof(buf, &p);
					*va_arg(ap, float *) = res;
				}
				if (__scanfdebug && p - buf != width)
					abort();
				nassigned++;
			}
			nread += width;
			nconversions++;
			break;
#endif /* !NO_FLOATING_POINT */
		}
	}
input_failure:
	return (nconversions != 0 ? nassigned : EOF);
match_failure:
	return (nassigned);
}

/*
 * Fill in the given table from the scanset at the given format
 * (just after `[').  Return a pointer to the character past the
 * closing `]'.  The table has a 1 wherever characters should be
 * considered part of the scanset.
 */
static const u_char *
__sccl(tab, fmt)
	char *tab;
	const u_char *fmt;
{
	int c, n, v;

	/* first `clear' the whole table */
	c = *fmt++;		/* first char hat => negated scanset */
	if (c == '^') {
		v = 1;		/* default => accept */
		c = *fmt++;	/* get new first char */
	} else
		v = 0;		/* default => reject */

	/* XXX: Will not work if sizeof(tab*) > sizeof(char) */
	(void) memset(tab, v, 256);

	if (c == 0)
		return (fmt - 1);/* format ended before closing ] */

	/*
	 * Now set the entries corresponding to the actual scanset
	 * to the opposite of the above.
	 *
	 * The first character may be ']' (or '-') without being special;
	 * the last character may be '-'.
	 */
	v = 1 - v;
	for (;;) {
		tab[c] = v;		/* take character c */
doswitch:
		n = *fmt++;		/* and examine the next */
		switch (n) {

		case 0:			/* format ended too soon */
			return (fmt - 1);

		case '-':
			/*
			 * A scanset of the form
			 *	[01+-]
			 * is defined as `the digit 0, the digit 1,
			 * the character +, the character -', but
			 * the effect of a scanset such as
			 *	[a-zA-Z0-9]
			 * is implementation defined.  The V7 Unix
			 * scanf treats `a-z' as `the letters a through
			 * z', but treats `a-a' as `the letter a, the
			 * character -, and the letter a'.
			 *
			 * For compatibility, the `-' is not considerd
			 * to define a range if the character following
			 * it is either a close bracket (required by ANSI)
			 * or is not numerically greater than the character
			 * we just stored in the table (c).
			 */
			n = *fmt;
			if (n == ']' || n < c)
			{
				c = '-';
				break;	/* resume the for(;;) */
			}
			fmt++;
			/* fill in the range */
			do {
				tab[++c] = v;
			} while (c < n);
#if 1	/* XXX another disgusting compatibility hack */
			c = n;
			/*
			 * Alas, the V7 Unix scanf also treats formats
			 * such as [a-c-e] as `the letters a through e'.
			 * This too is permitted by the standard....
			 */
			goto doswitch;
#else
			c = *fmt++;
			if (c == 0)
				return (fmt - 1);
			if (c == ']')
				return (fmt);
#endif
			break;

		case ']':		/* end of scanset */
			return (fmt);

		default:		/* just another character */
			c = n;
			break;
		}
	}
	/* NOTREACHED */
}

#ifndef NO_FLOATING_POINT
static int
parsefloat(FILEpd *fp, char *buf, char *end)
{
	char *commit, *p;
	int infnanpos = 0;
	enum {
		S_START, S_GOTSIGN, S_INF, S_NAN, S_MAYBEHEX,
		S_DIGITS, S_FRAC, S_EXP, S_EXPDIGITS
	} state = S_START;
	unsigned char c;
	char decpt = *localeconv()->decimal_point;
	_Bool gotmantdig = 0, ishex = 0;

	/*
	 * We set commit = p whenever the string we have read so far
	 * constitutes a valid representation of a floating point
	 * number by itself.  At some point, the parse will complete
	 * or fail, and we will ungetc() back to the last commit point.
	 * To ensure that the file offset gets updated properly, it is
	 * always necessary to read at least one character that doesn't
	 * match; thus, we can't short-circuit "infinity" or "nan(...)".
	 */
	commit = buf - 1;
	for (p = buf; p < end; ) {
		c = *fp->_p;
reswitch:
		switch (state) {
		case S_START:
			state = S_GOTSIGN;
			if (c == '-' || c == '+')
				break;
			else
				goto reswitch;
		case S_GOTSIGN:
			switch (c) {
			case '0':
				state = S_MAYBEHEX;
				commit = p;
				break;
			case 'I':
			case 'i':
				state = S_INF;
				break;
			case 'N':
			case 'n':
				state = S_NAN;
				break;
			default:
				state = S_DIGITS;
				goto reswitch;
			}
			break;
		case S_INF:
			if (infnanpos > 6 ||
			    (c != "nfinity"[infnanpos] &&
			     c != "NFINITY"[infnanpos]))
				goto parsedone;
			if (infnanpos == 1 || infnanpos == 6)
				commit = p;	/* inf or infinity */
			infnanpos++;
			break;
		case S_NAN:
			switch (infnanpos) {
			case -1:	/* XXX kludge to deal with nan(...) */
				goto parsedone;
			case 0:
				if (c != 'A' && c != 'a')
					goto parsedone;
				break;
			case 1:
				if (c != 'N' && c != 'n')
					goto parsedone;
				else
					commit = p;
				break;
			case 2:
				if (c != '(')
					goto parsedone;
				break;
			default:
				if (c == ')') {
					commit = p;
					infnanpos = -2;
				} else if (!isalnum(c) && c != '_')
					goto parsedone;
				break;
			}
			infnanpos++;
			break;
		case S_MAYBEHEX:
			state = S_DIGITS;
			if (c == 'X' || c == 'x') {
				ishex = 1;
				break;
			} else {	/* we saw a '0', but no 'x' */
				gotmantdig = 1;
				goto reswitch;
			}
		case S_DIGITS:
			if ((ishex && isxdigit(c)) || isdigit(c))
				gotmantdig = 1;
			else {
				state = S_FRAC;
				if (c != decpt)
					goto reswitch;
			}
			if (gotmantdig)
				commit = p;
			break;
		case S_FRAC:
			if (((c == 'E' || c == 'e') && !ishex) ||
			    ((c == 'P' || c == 'p') && ishex)) {
				if (!gotmantdig)
					goto parsedone;
				else
					state = S_EXP;
			} else if ((ishex && isxdigit(c)) || isdigit(c)) {
				commit = p;
				gotmantdig = 1;
			} else
				goto parsedone;
			break;
		case S_EXP:
			state = S_EXPDIGITS;
			if (c == '-' || c == '+')
				break;
			else
				goto reswitch;
		case S_EXPDIGITS:
			if (isdigit(c))
				commit = p;
			else
				goto parsedone;
			break;
		default:
			abort();
		}
		*p++ = c;
		if (--fp->_r > 0)
			fp->_p++;
		else if (__srefill(fp))
			break;	/* EOF */
	}

parsedone:
	while (commit < --p)
		__ungetc(*(u_char *)p, fp);
	*++commit = '\0';
	return (commit - buf);
}
#endif
