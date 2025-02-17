/*
 * MinGW syslog.h
 *
 * MinGW has no syslog.h.  So, short header 
 * to avoid modifying multiple source files
 * with minimal defines needed.
 */

#ifndef __PDEL_PD_SYSLOG_H__
#define __PDEL_PD_SYSLOG_H__ 1

#ifndef WIN32
#include <syslog.h>
#else

/* Borrowed from FreeBSD sys/syslog.h */

#define	LOG_EMERG	0
#define	LOG_ALERT	1
#define	LOG_CRIT	2
#define	LOG_ERR		3
#define	LOG_WARNING	4
#define	LOG_NOTICE	5
#define	LOG_INFO	6
#define	LOG_DEBUG	7

#define LOG_PRIMASK     0x07    /* mask to extract priority part (internal) */
                                /* extract priority */
#define LOG_PRI(p)      ((p) & LOG_PRIMASK)
#define LOG_MAKEPRI(fac, pri)   ((fac) | (pri))

#ifdef SYSLOG_NAMES
#define INTERNAL_NOPRI  0x10    /* the "no priority" priority */
                                /* mark "facility" */
#define INTERNAL_MARK   LOG_MAKEPRI((LOG_NFACILITIES<<3), 0)
typedef struct _code {
        const char      *c_name;
        int             c_val;
} CODE;

CODE prioritynames[] = {
        { "alert",      LOG_ALERT,      },
        { "crit",       LOG_CRIT,       },
        { "debug",      LOG_DEBUG,      },
        { "emerg",      LOG_EMERG,      },
        { "err",        LOG_ERR,        },
        { "error",      LOG_ERR,        },      /* DEPRECATED */
        { "info",       LOG_INFO,       },
        { "none",       INTERNAL_NOPRI, },      /* INTERNAL */
        { "notice",     LOG_NOTICE,     },
        { "panic",      LOG_EMERG,      },      /* DEPRECATED */
        { "warn",       LOG_WARNING,    },      /* DEPRECATED */
        { "warning",    LOG_WARNING,    },
        { NULL,         -1,             }
};
#endif

/* facility codes */
#define LOG_KERN        (0<<3)  /* kernel messages */
#define LOG_USER        (1<<3)  /* random user-level messages */
#define LOG_MAIL        (2<<3)  /* mail system */
#define LOG_DAEMON      (3<<3)  /* system daemons */
#define LOG_AUTH        (4<<3)  /* authorization messages */
#define LOG_SYSLOG      (5<<3)  /* messages generated internally by syslogd */
#define LOG_LPR         (6<<3)  /* line printer subsystem */
#define LOG_NEWS        (7<<3)  /* network news subsystem */
#define LOG_UUCP        (8<<3)  /* UUCP subsystem */
#define LOG_CRON        (9<<3)  /* clock daemon */
#define LOG_AUTHPRIV    (10<<3) /* authorization messages (private) */
                                /* Facility #10 clashes in DEC UNIX, where */
                                /* it's defined as LOG_MEGASAFE for AdvFS  */
                                /* event logging.                          */
#define LOG_FTP         (11<<3) /* ftp daemon */
#define LOG_NTP         (12<<3) /* NTP subsystem */
#define LOG_SECURITY    (13<<3) /* security subsystems (firewalling, etc.) */
#define LOG_CONSOLE     (14<<3) /* /dev/console output */

        /* other codes through 15 reserved for system use */
#define LOG_LOCAL0      (16<<3) /* reserved for local use */
#define LOG_LOCAL1      (17<<3) /* reserved for local use */
#define LOG_LOCAL2      (18<<3) /* reserved for local use */
#define LOG_LOCAL3      (19<<3) /* reserved for local use */
#define LOG_LOCAL4      (20<<3) /* reserved for local use */
#define LOG_LOCAL5      (21<<3) /* reserved for local use */
#define LOG_LOCAL6      (22<<3) /* reserved for local use */
#define LOG_LOCAL7      (23<<3) /* reserved for local use */

#define LOG_NFACILITIES 24      /* current number of facilities */
#define LOG_FACMASK     0x03f8  /* mask to extract facility part */
                                /* facility of pri */
#define LOG_FAC(p)      (((p) & LOG_FACMASK) >> 3)

#ifdef SYSLOG_NAMES
CODE facilitynames[] = {
        { "auth",       LOG_AUTH,       },
        { "authpriv",   LOG_AUTHPRIV,   },
        { "console",    LOG_CONSOLE,    },
        { "cron",       LOG_CRON,       },
        { "daemon",     LOG_DAEMON,     },
        { "ftp",        LOG_FTP,        },
        { "kern",       LOG_KERN,       },
        { "lpr",        LOG_LPR,        },
        { "mail",       LOG_MAIL,       },
        { "mark",       INTERNAL_MARK,  },      /* INTERNAL */
        { "news",       LOG_NEWS,       },
        { "ntp",        LOG_NTP,        },
        { "security",   LOG_SECURITY,   },
        { "syslog",     LOG_SYSLOG,     },
        { "user",       LOG_USER,       },
        { "uucp",       LOG_UUCP,       },
        { "local0",     LOG_LOCAL0,     },
        { "local1",     LOG_LOCAL1,     },
        { "local2",     LOG_LOCAL2,     },
        { "local3",     LOG_LOCAL3,     },
        { "local4",     LOG_LOCAL4,     },
        { "local5",     LOG_LOCAL5,     },
        { "local6",     LOG_LOCAL6,     },
        { "local7",     LOG_LOCAL7,     },
        { NULL,         -1,             }


};
#endif

#if 0

#ifdef _KERNEL
#define LOG_PRINTF      -1      /* pseudo-priority to indicate use of printf */
#endif

/*
 * arguments to setlogmask.
 */
#define LOG_MASK(pri)   (1 << (pri))            /* mask for one priority */
#define LOG_UPTO(pri)   ((1 << ((pri)+1)) - 1)  /* all priorities through pri */

/*
 * Option flags for openlog.
 *
 * LOG_ODELAY no longer does anything.
 * LOG_NDELAY is the inverse of what it used to be.
 */
#define LOG_PID         0x01    /* log the pid with each message */
#define LOG_CONS        0x02    /* log on the console if errors in sending */
#define LOG_ODELAY      0x04    /* delay open until first syslog() (default) */
#define LOG_NDELAY      0x08    /* don't delay open */
#define LOG_NOWAIT      0x10    /* don't wait for console forks: DEPRECATED */
#define LOG_PERROR      0x20    /* log to stderr as well */
#endif

#if 0
/*
 * Don't use va_list in the vsyslog() prototype.   Va_list is typedef'd in two
 * places (<machine/varargs.h> and <machine/stdarg.h>), so if we include one
 * of them here we may collide with the utility's includes.  It's unreasonable
 * for utilities to have to include one of them to include syslog.h, so we get
 * __va_list from <sys/_types.h> and use it.
 */
#include <sys/cdefs.h>
#include <sys/_types.h>

__BEGIN_DECLS
void    closelog(void);
void    openlog(const char *, int, int);
int     setlogmask(int);
void    syslog(int, const char *, ...) __printflike(2, 3);
void    vsyslog(int, const char *, __va_list) __printflike(2, 0);
__END_DECLS
#endif

#endif

#endif
