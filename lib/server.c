/* SASL server API implementation
 * Tim Martin
 * $Id: server.c,v 1.1.1.1 1998/11/16 20:06:37 rob Exp $
 */
/***********************************************************
        Copyright 1998 by Carnegie Mellon University

                      All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of CMU not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

CMU DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
CMU BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.
******************************************************************/

/* local functions/structs don't start with sasl
 */
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef SASL_DB_TYPE
# if SASL_DB_TYPE == gdbm
#  include <gdbm.h>
# elif SASL_DB_TYPE == ndbm
#  include <ndbm.h>
# else
#  error Invalid DB implementation specified
# endif
#endif /* defined(SASL_DB_TYPE) */
#include "sasl.h"
#include "saslint.h"
#include "saslutil.h"
#include <sys/param.h>
#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif
#include <string.h>

typedef struct mechanism
{
  int version;
  const sasl_server_plug_t *plug;
  struct mechanism *next;
  void *library;
} mechanism_t;


typedef struct mech_list {
  sasl_utils_t *utils;  /* gotten from plug_init */

  void *mutex;            /* mutex for this data */ 
  mechanism_t *mech_list; /* list of mechanisms */
  int mech_length;       /* number of mechanisms */

} mech_list_t;

typedef struct sasl_server_conn {
  sasl_conn_t base; /* parts common to server + client */

  char *local_domain;
  char *user_domain;

  int authenticated;
  mechanism_t *mech; /* mechanism trying to use */
  /* data for mechanism to use so can "remember" challenge thing
   * for example kerberos sends a random integer
   */ 
  union mech_data  
  {
    int Idata;  
    double Fdata;  
    char *Sdata;  
  } mech_data_t;

  sasl_server_params_t *sparams;

} sasl_server_conn_t;

static mech_list_t *mechlist; /* global var which holds the list */

static sasl_global_callbacks_t global_callbacks;

/* Contains functions:
 * 
 * sasl_server_init
 * sasl_server_new
 * sasl_listmech
 * sasl_server_start
 * sasl_server_step
 * sasl_checkpass NTI
 * sasl_userexists NTI
 * sasl_setpass NTI
 */


/* local mechanism which disposes of server */
static void server_dispose(sasl_conn_t *pconn)
{
  sasl_server_conn_t *s_conn=  (sasl_server_conn_t *) pconn;

  if (s_conn->mech)
    s_conn->mech->plug->mech_dispose(s_conn->base.context,
				     s_conn->sparams->utils);

  if (s_conn->local_domain)
    sasl_FREE(s_conn->local_domain);

  if (s_conn->user_domain)
    sasl_FREE(s_conn->user_domain);

  _sasl_free_utils(&s_conn->sparams->utils);

  if (s_conn->sparams)
    sasl_FREE(s_conn->sparams);

  _sasl_conn_dispose(pconn);
}

static int init_mechlist(void)
{
  /* set util functions - need to do rest*/
  mechlist->utils=_sasl_alloc_utils(NULL, &global_callbacks);

  if (mechlist->utils==NULL)
    return SASL_NOMEM;

  return SASL_OK;
}

static int add_plugin(void *p, void *library) {
  int plugcount;
  const sasl_server_plug_t *pluglist;
  mechanism_t *mech;
  sasl_server_plug_init_t *entry_point;
  int result;
  int version;
  int lupe;

  entry_point = (sasl_server_plug_init_t *)p;

  result = entry_point(mechlist->utils, SASL_SERVER_PLUG_VERSION, &version,
		       &pluglist, &plugcount);
  if (version != SASL_SERVER_PLUG_VERSION)
    result = SASL_FAIL;
  if (result != SASL_OK) return result;

  for (lupe=0;lupe< plugcount ;lupe++)
    {
      mech = sasl_ALLOC(sizeof(mechanism_t));
      if (! mech) return SASL_NOMEM;

      mech->plug=pluglist++;
      mech->version = version;
      if (lupe==0)
	mech->library=library;
      else
	mech->library=NULL;
      mech->next = mechlist->mech_list;
      mechlist->mech_list = mech;

      mechlist->mech_length++;
    }

  return SASL_OK;
}

static void server_done(void) {
  mechanism_t *m;
  mechanism_t *prevm;
  m=mechlist->mech_list; /* m point to begging of the list */

  while (m!=NULL)
  {
    prevm=m;
    m=m->next;
    
    if (prevm->plug->glob_context!=NULL)
      sasl_FREE(prevm->plug->glob_context);
    if (prevm->library!=NULL)
      _sasl_done_with_plugin(prevm->library);
    sasl_FREE(prevm);    
  }
  _sasl_free_utils(&mechlist->utils);
  sasl_FREE(mechlist);
}

static int
server_idle(sasl_conn_t *conn)
{
  mechanism_t *m;
  if (! mechlist)
    return 0;

  for (m = mechlist->mech_list;
       m;
       m = m->next)
    if (m->plug->idle
	&&  m->plug->idle(m->plug->glob_context,
			  conn,
			  conn ? ((sasl_server_conn_t *)conn)->sparams : NULL))
      return 1;
  return 0;
}

#ifdef SASL_DB_TYPE

static int
server_getsecret(void *context __attribute__((unused)),
		 const char *mechanism,
		 const char *auth_identity,
		 sasl_secret_t ** secret)
{
  int result = SASL_OK;
  char *key;
  size_t auth_id_len, mech_len, key_len;

  if (! mechanism || ! auth_identity || ! secret)
    return SASL_FAIL;

  auth_id_len = strlen(auth_identity);
  mech_len = strlen(mechanism);
  key_len = auth_id_len + mech_len + 1;
  key = sasl_ALLOC(key_len);
  if (! key)
    return SASL_NOMEM;
  memcpy(key, auth_identity, auth_id_len);
  key[auth_id_len] = '\0';
  memcpy(key + auth_id_len + 1, mechanism, mech_len);

#if SASL_DB_LIB == gdbm
  {
    GDBM_FILE db;
    datum gkey, gvalue;

    db = gdbm_open(SASL_DB_PATH, 0, GDBM_READER, S_IRUSR | S_IWUSR, NULL);
    if (! db) {
      result = SASL_FAIL;
      goto cleanup;
    }
    gkey.dptr = key;
    gkey.dsize = key_len;
    gvalue = gdbm_fetch(db, gkey);
    gdbm_close(db);
    if (! gvalue.dptr) {
      result = SASL_NOUSER;
      goto cleanup;
    }
    *secret = sasl_ALLOC(sizeof(sasl_secret_t)
			 + gvalue.dsize
			 + 1);
    if (! *secret) {
      result = SASL_NOMEM;
      free(gvalue.dptr);
      goto cleanup;
    }
    (*secret)->len = gvalue.dsize;
    memcpy(&(*secret)->data, gvalue.dptr, gvalue.dsize);
    (*secret)->data[(*secret)->len] = '\0'; /* sanity */
    /* Note: not sasl_FREE!  This is memory allocated by gdbm,
     * which is using libc malloc/free. */
    free(gvalue.dptr);
  }
#elif SASL_DB_LIB == ndbm
  {
    DBM *db;
    datum dkey, dvalue;

    db = dbm_open(SASL_DB_PATH, DBM_RDONLY, S_IRUSR | S_IWUSR);
    if (! db) {
      result = SASL_FAIL;
      goto cleanup;
    }
    dkey.dptr = key;
    dkey.dsize = key_len;
    dvalue = dbm_fetch(db, dkey);
    dbm_close(db);
    if (! dvalue.dptr) {
      result = SASL_NOUSER;
      goto cleanup;
    }
    *secret = sasl_ALLOC(sizeof(sasl_secret_t)
			 + dvalue.dsize
			 + 1);
    if (! *secret) {
      result = SASL_NOMEM;
      free(dvalue.dptr);
      goto cleanup;
    }
    (*secret)->len = dvalue.dsize;
    memcpy(&(*secret)->data, dvalue.dptr, dvalue.dsize);
    (*secret)->data[(*secret)->len] = '\0'; /* sanity */
    /* Note: not sasl_FREE!  This is memory allocated by ndbm,
     * which is using libc malloc/free. */
    free(dvalue.dptr);
  }
#else
# error Invalid DB implementation specified in server_getsecret
#endif /* SASL_DB_LIB */
 cleanup:
  sasl_FREE(key);

  return result;
}

static int
server_putsecret(void *context __attribute__((unused)),
		 const char *mechanism,
		 const char *auth_identity,
		 const sasl_secret_t * secret)
{
  int result = SASL_OK;
  char *key;
  size_t auth_id_len, mech_len, key_len;

  if (! mechanism || ! auth_identity)
    return SASL_FAIL;

  auth_id_len = strlen(auth_identity);
  mech_len = strlen(mechanism);
  key_len = auth_id_len + mech_len + 1;
  key = sasl_ALLOC(key_len);
  if (! key)
    return SASL_NOMEM;
  memcpy(key, auth_identity, auth_id_len);
  key[auth_id_len] = '\0';
  memcpy(key + auth_id_len + 1, mechanism, mech_len);

#if SASL_DB_LIB == gdbm
  {
    GDBM_FILE db;
    datum gkey;

    db = gdbm_open(SASL_DB_PATH, 0, GDBM_WRCREAT, S_IRUSR | S_IWUSR, NULL);
    if (! db) {
      result = SASL_FAIL;
      goto cleanup;
    }
    gkey.dptr = key;
    gkey.dsize = key_len;
    if (secret) {
      datum gvalue;
      gvalue.dptr = (char *)&secret->data;
      gvalue.dsize = secret->len;
      if (gdbm_store(db, gkey, gvalue, GDBM_REPLACE))
	result = SASL_FAIL;
    } else
      if (gdbm_delete(db, gkey))
	result = SASL_FAIL;
    gdbm_close(db);
  }
#elif SASL_DB_LIB == ndbm
  {
    DBM *db;
    datum dkey;

    db = dbm_open(SASL_DB_PATH,
		  O_RDWR | O_CREAT /* TODO: what should this be? */,
		  S_IRUSR | S_IWUSR);
    if (! db) {
      result = SASL_FAIL;
      goto cleanup;
    }
    dkey.dptr = key;
    dkey.dsize = key_len;
    if (secret) {
      datum dvalue;
      dvalue.dptr = &secret->data;
      dvalue.dsize = secret->len;
      if (dbm_store(db, dkey, dvalue, DBM_REPLACE))
	result = SASL_FAIL;
    } else
      if (dbm_delete(db, dkey))
	result = SASL_FAIL;
    dbm_close(db);
  }
#else
# error Invalid DB implementation specified in server_putsecret
#endif /* SASL_DB_LIB */
 cleanup:
  sasl_FREE(key);

  return result;
}

#endif /* SASL_DB_TYPE */

int sasl_server_init(const sasl_callback_t *callbacks,
		     const char *appname)
{
  int ret;

  _sasl_server_cleanup_hook = &server_done;
  _sasl_server_idle_hook = &server_idle;

#ifdef SASL_DB_TYPE
  _sasl_server_getsecret_hook = &server_getsecret;
  _sasl_server_putsecret_hook = &server_putsecret;
#endif /* SASL_DB_TYPE */

  global_callbacks.callbacks = callbacks;
  global_callbacks.appname = appname;

  mechlist=sasl_ALLOC(sizeof(mech_list_t));
  if (mechlist==NULL) return SASL_NOMEM;

  /* load plugins */
  ret=init_mechlist();
  if (ret!=SASL_OK)
    return ret;
  mechlist->mech_list=NULL;
  mechlist->mech_length=0;

  ret=_sasl_get_mech_list("sasl_server_plug_init",
			  &add_plugin);

  return ret;
}

static int
_sasl_transition(sasl_conn_t * conn,
		 const char * pass,
		 int passlen)
{
  int result = 0;
  mechanism_t *m;

  /* Zowie -- we have the user's plaintext password.
   * Let's tell all our mechanisms about it...
   */

  if (! conn || ! pass)
    return SASL_FAIL;

  if (! mechlist)		/* *shouldn't* ever happen... */
    return SASL_FAIL;

  if (! conn->oparams->authid)
    return SASL_NOTDONE;

  for (m = mechlist->mech_list;
       m;
       m = m->next)
    if (m->plug->setpass)
      /* TODO: Log something if this fails */
      if (m->plug->setpass(m->plug->glob_context,
			   ((sasl_server_conn_t *)conn)->sparams,
			   conn->oparams->authid,
			   pass,
			   passlen,
			   0,
			   NULL) == SASL_OK)
	result = 1;
  return result;
}

int sasl_server_new(const char *service,
		    const char *local_domain,
		    const char *user_domain,
		    const sasl_callback_t *callbacks,
		    int secflags,
		    sasl_conn_t **pconn)
{
  int result;
  sasl_server_conn_t *serverconn;

  if (! pconn) return SASL_FAIL;
  if (! service) return SASL_FAIL;

  *pconn=sasl_ALLOC(sizeof(sasl_server_conn_t));
  if (*pconn==NULL) return SASL_NOMEM;

  (*pconn)->destroy_conn = &server_dispose;
  result = _sasl_conn_init(*pconn, service, secflags,
			   &server_idle, callbacks, &global_callbacks);
  if (result != SASL_OK) return result;

  serverconn = (sasl_server_conn_t *)*pconn;

  serverconn->mech = NULL;

  /* make sparams */
  serverconn->sparams=sasl_ALLOC(sizeof(sasl_server_params_t));
  if (serverconn->sparams==NULL) return SASL_NOMEM;

  /* set util functions - need to do rest*/
  serverconn->sparams->utils=_sasl_alloc_utils(*pconn, &global_callbacks);
  if (serverconn->sparams->utils==NULL)
    return SASL_NOMEM;

  serverconn->sparams->transition = &_sasl_transition;

  if (local_domain==NULL) {
    char name[MAXHOSTNAMELEN];
    memset(name, 0, sizeof(name));
    gethostname(name, MAXHOSTNAMELEN);
#ifdef HAVE_GETDOMAINNAME
    {
      char *dot = strchr(name, '.');
      if (! dot) {
	size_t namelen = strlen(name);
	name[namelen] = '.';
	getdomainname(name + namelen + 1, MAXHOSTNAMELEN - namelen - 1);
      }
    }
#endif /* HAVE_GETDOMAINNAME */
    result = _sasl_strdup(name, &serverconn->local_domain, NULL);
    if (result != SASL_OK) goto cleanup_conn;
  } else {
    result = _sasl_strdup(local_domain, &serverconn->local_domain, NULL);
    if (result != SASL_OK) goto cleanup_conn;
  }


  /* set some variables */

  if (user_domain==NULL)
    serverconn->user_domain=NULL;
  else {
    result = _sasl_strdup(user_domain, &serverconn->user_domain, NULL);
    if (result != SASL_OK) goto cleanup_localdomain;
  }

  return result;

cleanup_localdomain:
  sasl_FREE(serverconn->local_domain);

cleanup_conn:
  _sasl_conn_dispose(*pconn);
  sasl_FREE(*pconn);
  *pconn = NULL;
  return result;
}

int sasl_server_start(sasl_conn_t *conn,
		      const char *mech,
		      const char *clientin,
		      unsigned clientinlen,
		      char **serverout,
		      unsigned *serveroutlen,
		      const char **errstr)
{
  sasl_server_conn_t *s_conn=(sasl_server_conn_t *) conn;

  /* make sure mech is valid mechanism
     if not return appropriate error */
  mechanism_t *m;
  m=mechlist->mech_list;

  if (errstr)
    *errstr = NULL;

  while (m!=NULL)
  {
    if ( strcasecmp(mech,m->plug->mech_name)==0)
    {
      break;
    }
    m=m->next;
  }
  
  if (m==NULL)
    return SASL_NOMECH;


  s_conn->mech=m;

  /* call the security layer given by mech */
  s_conn->sparams->local_domain=s_conn->local_domain;
  s_conn->sparams->service=conn->service;
  s_conn->sparams->user_domain=s_conn->user_domain;

  s_conn->mech->plug->mech_new(s_conn->mech->plug->glob_context,
			       s_conn->sparams,
			       NULL,
			       0,
			       &(conn->context),
			       errstr);


  conn->oparams=sasl_ALLOC(sizeof(sasl_out_params_t));
  if (conn->oparams==NULL) return SASL_NOMEM;
  memset(conn->oparams, 0, sizeof(sasl_out_params_t));

  return s_conn->mech->plug->mech_step(conn->context,
				       s_conn->sparams,
				       clientin,
				       clientinlen,
				       serverout,
				       (int *) serveroutlen,
				       conn->oparams,
				       errstr);
			     
  /* if returns SASL_OK check to make sure
   * is valid username and then
   * correct password using sasl_checkpass
   */

}

int sasl_server_step(sasl_conn_t *conn,
		     const char *clientin,
		     unsigned clientinlen,
		     char **serverout,
		     unsigned *serveroutlen,
		     const char **errstr)
{
  sasl_server_conn_t *s_conn;
  s_conn= (sasl_server_conn_t *) conn;

  if (errstr)
    *errstr = NULL;

  return s_conn->mech->plug->mech_step(conn->context,
				       s_conn->sparams,
				       clientin,
				       clientinlen,
				       serverout,
				       (int *) serveroutlen,
				       conn->oparams,
				       errstr);


  /* call the security layer WRONG PARAMS*/
  /*  conn->mech_using->pluglist->mech_continue( conn, *clientin, clientinlen, **serverout, *serveroutlen, char **errstr);*/

  /* if returns SASL_OK check to make sure
   * is valid username and then
   * correct password using sasl_checkpass
   */
}


static unsigned mech_names_len()
{
  mechanism_t *listptr;
  unsigned result = 0;

  for (listptr = mechlist->mech_list;
       listptr;
       listptr = listptr->next)
    result += strlen(listptr->plug->mech_name);

  return result;
}

int sasl_listmech(sasl_conn_t *conn,
		  const char *user,
		  const char *prefix,
		  const char *sep,
		  const char *suffix,
		  char **result,
		  unsigned *plen,
		  unsigned *pcount)
{
  int lup;
  mechanism_t *listptr;  
  int resultlen;

  if (! conn || ! result)
    return SASL_FAIL;

  if (plen!=NULL)
    *plen=0;
  if (pcount!=NULL)
    *pcount=0;

  if (! mechlist)
    return SASL_FAIL;

  if (mechlist->mech_length<=0)
    return SASL_NOMECH;

  resultlen = strlen(prefix)
            + strlen(sep) * (mechlist->mech_length - 1)
	    + mech_names_len()
    	    + strlen(suffix)
	    + 1;
  *result=sasl_ALLOC(resultlen);
  if ((*result)==NULL) return SASL_NOMEM;

  strcpy (*result,prefix);

  listptr=mechlist->mech_list;  
   
  /* make list */
  for (lup=0;lup<mechlist->mech_length;lup++)
  {
    /* if user has rights add to list */
    /* XXX This should be done with a callback function */
    if ( 1) /* user_has_rights(user,listptr->name) ) */
    {      

      strcat(*result,listptr->plug->mech_name);
      if (pcount!=NULL)
	(*pcount)++;

      if (listptr->next!=NULL)
      {
	strcat(*result,sep);
      }

    }

    listptr=listptr->next;
  }

  strcat(*result,suffix);

  if (plen!=NULL)
    *plen=resultlen - 1;	/* one for the null */

  return SASL_OK;
  
}

