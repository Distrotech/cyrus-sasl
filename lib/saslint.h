/* saslint.h - internal SASL library definitions
 * Rob Siemborski
 * Tim Martin
 * $Id: saslint.h,v 1.33.2.38 2001/07/19 22:49:53 rjs3 Exp $
 */
/* 
 * Copyright (c) 2001 Carnegie Mellon University.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name "Carnegie Mellon University" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For permission or any other legal
 *    details, please contact  
 *      Office of Technology Transfer
 *      Carnegie Mellon University
 *      5000 Forbes Avenue
 *      Pittsburgh, PA  15213-3890
 *      (412) 268-4387, fax: (412) 268-7395
 *      tech-transfer@andrew.cmu.edu
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Computing Services
 *     at Carnegie Mellon University (http://www.cmu.edu/computing/)."
 *
 * CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE
 * FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef SASLINT_H
#define SASLINT_H

#include <config.h>
#include "sasl.h"
#include "saslplug.h"
#include "saslutil.h"
#include "prop.h"

/* #define'd constants */
#define DEFAULT_MAXOUTBUF 8192
#define CANON_BUF_SIZE 256

/* Error Handling Foo */
/* Helpful Hints:
 *  -Error strings are set as soon as possible (first function in stack trace
 *   with a pointer to the sasl_conn_t.
 *  -Error codes are set as late as possible (only in the sasl api functions),
 *   thoug "as often as possible" also comes to mind to ensure correctness
 *  -Errors from calls to _buf_alloc, _sasl_strdup, etc are assumed to be
 *   memory errors.
 *  -Only errors (error codes < SASL_OK) should be remembered
 */
#define RETURN(conn, val) { if(conn && (val) < SASL_OK) \
                               (conn)->error_code = (val); \
                            return (val); }
#define MEMERROR(conn) {\
    if(conn) sasl_seterror( (conn), 0, \
                   "Out of Memory in " __FILE__ " near line %d", __LINE__ ); \
    RETURN(conn, SASL_NOMEM) }
#define PARAMERROR(conn) {\
    if(conn) sasl_seterror( (conn), SASL_NOLOG, \
                  "Parameter error in " __FILE__ " near line %d", __LINE__ ); \
    RETURN(conn, SASL_BADPARAM) }
#define INTERROR(conn, val) {\
    if(conn) sasl_seterror( (conn), 0, \
                   "Internal Error %d in " __FILE__ " near line %d", (val),\
		   __LINE__ ); \
    RETURN(conn, (val)) }

#ifndef PATH_MAX
# ifdef _POSIX_PATH_MAX
#  define PATH_MAX _POSIX_PATH_MAX
# else
#  define PATH_MAX 1024         /* arbitrary; probably big enough will
                                 * probably only be 256+64 on
                                 * pre-posix machines */
# endif
#endif

/* Datatype Definitions */
typedef struct {
  const sasl_callback_t *callbacks;
  const char *appname;
} sasl_global_callbacks_t;

typedef struct _sasl_external_properties 
{
    sasl_ssf_t ssf;
    char *auth_id;
} _sasl_external_properties_t;

typedef struct buffer_info
{ 
    char *data;
    unsigned curlen;
    unsigned reallen;
} buffer_info_t;

typedef struct add_plugin_list 
{
    const char *entryname;
    int (*add_plugin)(const char *, void *);
} add_plugin_list_t;

enum Sasl_conn_type { SASL_CONN_UNKNOWN = 0,
		      SASL_CONN_SERVER = 1,
                      SASL_CONN_CLIENT = 2 };

struct sasl_conn {
  enum Sasl_conn_type type;

  void (*destroy_conn)(sasl_conn_t *); /* destroy function */

  char *service;

  int flags;  /* flags passed to sasl_*_new */

  /* IP information.  A buffer of size 52 is adequate for this in its
     longest format (see sasl.h) */
  int got_ip_local, got_ip_remote;
  char iplocalport[NI_MAXHOST + NI_MAXSERV];
  char ipremoteport[NI_MAXHOST + NI_MAXSERV];

  void *context;
  sasl_out_params_t oparams;

  sasl_security_properties_t props;
  _sasl_external_properties_t external;

  sasl_secret_t *secret;

  int (*idle_hook)(sasl_conn_t *conn);
  const sasl_callback_t *callbacks;
  const sasl_global_callbacks_t *global_callbacks; /* global callbacks
						    * connection */
  char *serverFQDN;

  /* Pointers to memory that we are responsible for */
  buffer_info_t *encode_buf;

  int error_code;
  char *error_buf, *errdetail_buf;
  unsigned error_buf_len, errdetail_buf_len;
  char *decode_buf;
  unsigned decode_buf_len;

  char user_buf[CANON_BUF_SIZE+1], authid_buf[CANON_BUF_SIZE+1];
};

/* Server Conn Type Information */

typedef struct mechanism
{
    int version;
    int condition; /* set to SASL_NOUSER if no available users;
		      set to SASL_CONTINUE if delayed plugn loading */
    const sasl_server_plug_t *plug;
    struct mechanism *next;
    char *f;       /* where should i load the mechanism from? */
} mechanism_t;

typedef struct mech_list {
  const sasl_utils_t *utils;  /* gotten from plug_init */

  void *mutex;            /* mutex for this data */ 
  mechanism_t *mech_list; /* list of mechanisms */
  int mech_length;       /* number of mechanisms */
} mech_list_t;

typedef struct sasl_server_conn {
    sasl_conn_t base; /* parts common to server + client */

    char *mechlist_buf;
    unsigned mechlist_buf_len;

    char *user_realm; /* domain the user authenticating is in */
    int sent_last; /* Have we already done the last send? */
    int authenticated;
    mechanism_t *mech; /* mechanism trying to use */
    sasl_server_params_t *sparams;
} sasl_server_conn_t;

/* Client Conn Type Information */

typedef struct cmechanism
{
  int version;
  const sasl_client_plug_t *plug;

  struct cmechanism *next;  
} cmechanism_t;

typedef struct cmech_list {
  const sasl_utils_t *utils; 

  void *mutex;            /* mutex for this data */ 
  cmechanism_t *mech_list; /* list of mechanisms */
  int mech_length;       /* number of mechanisms */

} cmech_list_t;

typedef struct sasl_client_conn {
  sasl_conn_t base; /* parts common to server + client */

  cmechanism_t *mech;
  sasl_client_params_t *cparams;

  char *serverFQDN;

} sasl_client_conn_t;

typedef struct sasl_allocation_utils {
  sasl_malloc_t *malloc;
  sasl_calloc_t *calloc;
  sasl_realloc_t *realloc;
  sasl_free_t *free;
} sasl_allocation_utils_t;

typedef struct sasl_mutex_utils {
  sasl_mutex_alloc_t *alloc;
  sasl_mutex_lock_t *lock;
  sasl_mutex_unlock_t *unlock;
  sasl_mutex_free_t *free;
} sasl_mutex_utils_t;

typedef struct sasl_log_utils_s {
  sasl_log_t *log;
} sasl_log_utils_t;

typedef int sasl_plaintext_verifier(sasl_conn_t *conn,
				    const char *userid,
				    const char *passwd,
				    const char *service,
				    const char *user_realm);

struct sasl_verify_password_s {
    char *name;
    sasl_plaintext_verifier *verify;
};

/*
 * globals & constants
 */
/*
 * common.c
 */
extern const sasl_utils_t *sasl_global_utils;

extern void (*_sasl_client_cleanup_hook)(void);
extern void (*_sasl_server_cleanup_hook)(void);
extern int (*_sasl_client_idle_hook)(sasl_conn_t *conn);
extern int (*_sasl_server_idle_hook)(sasl_conn_t *conn);

extern sasl_allocation_utils_t _sasl_allocation_utils;
extern sasl_mutex_utils_t _sasl_mutex_utils;

/*
 * checkpw.c
 */
extern struct sasl_verify_password_s _sasl_verify_password[];

/*
 * dlopen.c and staticopen.c
 */
extern const int _is_sasl_server_static;

/*
 * server.c
 */
/* (this is a function call to ensure this is read-only to the outside) */
extern int _is_sasl_server_active(void);

/*
 * Allocation and Mutex utility macros
 */
#define sasl_ALLOC(__size__) (_sasl_allocation_utils.malloc((__size__)))
#define sasl_CALLOC(__nelem__, __size__) \
	(_sasl_allocation_utils.calloc((__nelem__), (__size__)))
#define sasl_REALLOC(__ptr__, __size__) \
	(_sasl_allocation_utils.realloc((__ptr__), (__size__)))
#define sasl_FREE(__ptr__) (_sasl_allocation_utils.free((__ptr__)))

#define sasl_MUTEX_ALLOC() (_sasl_mutex_utils.alloc())
#define sasl_MUTEX_LOCK(__mutex__) (_sasl_mutex_utils.lock((__mutex__)))
#define sasl_MUTEX_UNLOCK(__mutex__) (_sasl_mutex_utils.unlock((__mutex__)))
#define sasl_MUTEX_FREE(__mutex__) \
	(_sasl_mutex_utils.free((__mutex__)))

/* function prototypes */
/*
 * dlopen.c and staticopen.c
 */
/*
 * The differences here are:
 * _sasl_load_plugins loads all plugins from all files
 * _sasl_get_plugin loads the LIBRARY for an individual file
 * _sasl_done_with_plugins frees the LIBRARIES loaded by the above 2
 * _sasl_locate_entry locates an entrypoint in a given library
 */
extern int _sasl_load_plugins(const add_plugin_list_t *entrypoints,
			       const sasl_callback_t *getpath_callback,
			       const sasl_callback_t *verifyfile_callback);
extern int _sasl_get_plugin(const char *file,
			    const sasl_callback_t *verifyfile_cb,
			    void **libraryptr);
extern int _sasl_locate_entry(void *library, const char *entryname,
                              void **entry_point);
extern int _sasl_done_with_plugins();


/*
 * common.c
 */
extern const sasl_callback_t *
_sasl_find_getpath_callback(const sasl_callback_t *callbacks);

extern const sasl_callback_t *
_sasl_find_verifyfile_callback(const sasl_callback_t *callbacks);

extern int _sasl_common_init(void);

extern int _sasl_conn_init(sasl_conn_t *conn,
			   const char *service,
			   int secflags,
			   int (*idle_hook)(sasl_conn_t *conn),
			   const char *serverFQDN,
			   const char *iplocalport,
			   const char *ipremoteport,
			   const sasl_callback_t *callbacks,
			   const sasl_global_callbacks_t *global_callbacks);
extern void _sasl_conn_dispose(sasl_conn_t *conn);

extern sasl_utils_t *
_sasl_alloc_utils(sasl_conn_t *conn,
		  sasl_global_callbacks_t *global_callbacks);
extern int _sasl_free_utils(const sasl_utils_t ** utils);

extern int
_sasl_getcallback(sasl_conn_t * conn,
		  unsigned long callbackid,
		  int (**pproc)(),
		  void **pcontext);

extern void
_sasl_log(sasl_conn_t *conn,
	  int level,
	  const char *fmt,
	  ...);

/* More Generic Utilities in common.c */
extern int _sasl_strdup(const char *in, char **out, int *outlen);

/* Basically a conditional call to realloc(), if we need more */
int _buf_alloc(char **rwbuf, unsigned *curlen, unsigned newlen);

/* convert an iovec to a single buffer */
int _iovec_to_buf(const struct iovec *vec,
		  unsigned numiov, buffer_info_t **output);

/* Convert between string formats and sockaddr formats */
int _sasl_iptostring(const struct sockaddr *addr, socklen_t addrlen,
		     char *out, unsigned outlen);
int _sasl_ipfromstring(const char *addr, struct sockaddr *out,
		       socklen_t outlen);

/*
 * external plugin (external.c)
 */
int external_client_init(const sasl_utils_t *utils,
			 int max_version,
			 int *out_version,
			 sasl_client_plug_t **pluglist,
			 int *plugcount);
extern sasl_client_plug_t external_client_mech;
int external_server_init(const sasl_utils_t *utils,
			 int max_version,
			 int *out_version,
			 sasl_server_plug_t **pluglist,
			 int *plugcount);
extern sasl_server_plug_t external_server_mech;

/*
 * config file declarations (config.c)
 */
extern int sasl_config_init(const char *filename);
extern const char *sasl_config_getstring(const char *key,const char *def);
extern int sasl_config_getint(const char *key,int def);
extern int sasl_config_getswitch(const char *key,int def);

/* checkpw.c */
#ifdef DO_SASL_CHECKAPOP
extern int _sasl_sasldb_verify_apop(sasl_conn_t *conn,
				    const char *userstr,
				    const char *challenge,
				    const char *response,
				    const char *user_realm);
#endif /* DO_SASL_CHECKAPOP */

/* Auxprop Plugin (checkpw.c) */
extern int sasldb_auxprop_plug_init(const sasl_utils_t *utils,
				    int max_version,
				    int *out_version,
				    sasl_auxprop_plug_t **plug,
				    const char *plugname);

/*
 * auxprop.c
 */
extern int _sasl_auxprop_add_plugin(void *p, void *library);
extern void _sasl_auxprop_free(void);
extern void _sasl_auxprop_lookup(sasl_server_params_t *sparams,
				 unsigned flags,
				 const char *user, unsigned ulen);

/*
 * canonusr.c
 */
void _sasl_canonuser_free();
extern int internal_canonuser_init(const sasl_utils_t *utils,
				   int max_version,
				   int *out_version,
				   sasl_canonuser_plug_t **plug,
				   const char *plugname);
extern int _sasl_canon_user(sasl_conn_t *conn,
			    const char *user, unsigned ulen,
			    const char *authid, unsigned alen,
			    unsigned flags,
			    sasl_out_params_t *oparams);

#endif /* SASLINT_H */
