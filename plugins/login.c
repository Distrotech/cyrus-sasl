/* Login SASL plugin
 * contributed by Rainer Schoepf <schoepf@uni-mainz.de>
 * based on PLAIN, by Tim Martin <tmartin@andrew.cmu.edu>
 * $Id: login.c,v 1.6.2.4 2001/06/25 16:44:10 rjs3 Exp $
 */
/* 
 * Copyright (c) 2000 Carnegie Mellon University.  All rights reserved.
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

#include <config.h>
#include <stdio.h>
#include <ctype.h>
#include <sasl.h>
#include <saslplug.h>

#include "plugin_common.h"

#ifdef WIN32
/* This must be after sasl.h */
# include "saslLOGIN.h"
#endif /* WIN32 */

static const char rcsid[] = "$Implementation: Carnegie Mellon SASL " VERSION " $";

#undef L_DEFAULT_GUARD
#define L_DEFAULT_GUARD (0)

#define USERNAME "Username:"
#define PASSWORD "Password:"

typedef struct context {
    int state;
    sasl_secret_t *username;
    sasl_secret_t *password;
} context_t;

static const char blank_out[] = "";

static int start(void *glob_context __attribute__((unused)), 
		 sasl_server_params_t *sparams,
		 const char *challenge __attribute__((unused)),
		 unsigned challen __attribute__((unused)),
		 void **conn)
{
  context_t *text;

  /* holds state are in */
  text=sparams->utils->malloc(sizeof(context_t));
  if (text==NULL) return SASL_NOMEM;

  memset(text, 0, sizeof(context_t));

  text->state=1;

  *conn=text;

  return SASL_OK;
}

static void dispose(void *conn_context, const sasl_utils_t *utils)
{
  context_t *text;
  text=conn_context;

  if (!text)
    return;

  /* free sensitive info */
  _plug_free_secret(utils, &(text->username));
  _plug_free_secret(utils, &(text->password));

  utils->free(text);
}

static void mech_free(void *global_context, const sasl_utils_t *utils)
{
    if(global_context) utils->free(global_context);  
}

/* fills in password; remember to free password and wipe it out correctly */
static
int verify_password(sasl_server_params_t *params, 
		    const char *user, const char *pass)
{
    int result;

    /* if it's null, checkpass will default */
    result = params->utils->checkpass(params->utils->conn,
				      user, 0, pass, 0);
    
    return result;
}

static int
server_continue_step (void *conn_context,
		      sasl_server_params_t *params,
		      const char *clientin,
		      unsigned clientinlen,
		      const char **serverout,
		      unsigned *serveroutlen,
		      sasl_out_params_t *oparams)
{
  context_t *text;
  text=conn_context;

  oparams->mech_ssf=0;
  oparams->maxoutbuf = 0;
  
  oparams->encode = NULL;
  oparams->decode = NULL;

  oparams->user = NULL;
  oparams->authid = NULL;

  oparams->param_version = 0;

  /* nothing more to do; authenticated */

  VL (("Login: server state #%i\n",text->state));

  if (text->state == 1) {
      text->state = 2;

      /* Check inlen, (possibly we have already the user name) */
      /* In this case fall through to state 2 */
      if (clientinlen == 0) {
	  /* get username */
	  
	  VL (("out=%s len=%i\n",USERNAME,strlen(USERNAME)));
  
	  *serveroutlen = strlen(USERNAME);
	  *serverout = USERNAME;
	  
	  return SASL_CONTINUE;
      }
  }

  if (text->state == 2) {
    VL (("in=%s len=%i\n",clientin,clientinlen));

    /* Catch really long usernames */
    if(clientinlen > 1024) return SASL_BADPROT;

    /* get username */
    text->username = (sasl_secret_t *) params->utils->malloc(sizeof(sasl_secret_t)+clientinlen+1);
    if (! text->username) return SASL_NOMEM;

    strncpy(text->username->data,clientin,clientinlen);
    text->username->data[clientinlen] = '\0';
    text->username->len = clientinlen;

    VL (("Got username: %s\n",text->username->data));

    /* Request password */

    VL (("out=%s len=%i\n",PASSWORD,strlen(PASSWORD)));
    *serveroutlen = strlen(PASSWORD);
    *serverout = PASSWORD;

    text->state = 3;

    return SASL_CONTINUE;
  }

  if (text->state == 3) {
    int result;

    /* Catch really long passwords */
    if(clientinlen > 1024) return SASL_BADPROT;

    /* get password */

    text->password = params->utils->malloc (sizeof(sasl_secret_t) + clientinlen + 1);
    if (! text->password)
      return SASL_NOMEM;

    strncpy(text->password->data,clientin,clientinlen);
    text->password->data[clientinlen] = '\0';
    text->password->len = clientinlen;

    /* verify_password - return sasl_ok on success */

    VL (("Verifying password...\n"));
    result = verify_password(params, text->username->data, text->password->data);

    if (result != SASL_OK)
      return result;

    VL (("Password OK"));

    result = params->canon_user(params->utils->conn, text->username->data,
				0, text->username->data, 0, 0, oparams);
    if(result != SASL_OK) return result;

    if (params->transition)
    {
	params->transition(params->utils->conn,
			   text->password->data, text->password->len);
    }
    
    *serverout = blank_out;
    *serveroutlen = 0;

    text->state++; /* so fails if called again */

    oparams->doneflag = 1;

    return SASL_OK;
  }

  return SASL_FAIL; /* should never get here */
}

static const sasl_server_plug_t plugins[] = 
{
  {
    "LOGIN",
    0,
    SASL_SEC_NOANONYMOUS,
    0,
    NULL,
    &start,
    &server_continue_step,
    &dispose,
    &mech_free,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
  }
};

int sasl_server_plug_init(sasl_utils_t *utils __attribute__((unused)),
			  int maxversion,
			  int *out_version,
			  const sasl_server_plug_t **pluglist,
			  int *plugcount,
			  const char *plugname __attribute__((unused)))
{
  if (maxversion<SASL_SERVER_PLUG_VERSION)
    return SASL_BADVERS;

  *pluglist=plugins;

  *plugcount=1;  
  *out_version=SASL_SERVER_PLUG_VERSION;

  return SASL_OK;
}

/* put in sasl_wrongmech */
static int c_start(void *glob_context __attribute__((unused)),
		   sasl_client_params_t *params,
		   void **conn)
{
  context_t *text;

  VL (("Client start\n"));

  /* holds state are in */
  text = params->utils->malloc(sizeof(context_t));
  if (text==NULL) return SASL_NOMEM;

  memset(text, 0, sizeof(context_t));

  text->state=1;

  *conn=text;

  return SASL_OK;
}

/* 
 * Trys to find the prompt with the lookingfor id in the prompt list
 * Returns it if found. NULL otherwise
 */

static sasl_interact_t *find_prompt(sasl_interact_t **promptlist,
				    unsigned int lookingfor)
{
  sasl_interact_t *prompt;

  if (promptlist && *promptlist)
    for (prompt = *promptlist;
	 prompt->id != SASL_CB_LIST_END;
	 ++prompt)
      if (prompt->id==lookingfor)
	return prompt;

  return NULL;
}

/*
 * Somehow retrieve the userid
 * This is the same as in digest-md5 so change both
 */
static int get_userid(sasl_client_params_t *params,
		      const char **userid,
		      sasl_interact_t **prompt_need)
{
  int result;
  sasl_getsimple_t *getuser_cb;
  void *getuser_context;
  sasl_interact_t *prompt;
  const char *id;

  /* see if we were given the userid in the prompt */
  prompt=find_prompt(prompt_need,SASL_CB_USER);
  if (prompt!=NULL)
    {
	*userid = prompt->result;
	return SASL_OK;
    }

  /* Try to get the callback... */
  result = params->utils->getcallback(params->utils->conn,
				      SASL_CB_USER,
				      &getuser_cb,
				      &getuser_context);
  if (result == SASL_OK && getuser_cb) {
    id = NULL;
    result = getuser_cb(getuser_context,
			SASL_CB_USER,
			&id,
			NULL);
    if (result != SASL_OK)
      return result;
    if (! id)
      return SASL_BADPARAM;
    *userid = id;
  }

  return result;
}

static int get_password(sasl_client_params_t *params,
		      sasl_secret_t **password,
		      sasl_interact_t **prompt_need)
{

  int result;
  sasl_getsecret_t *getpass_cb;
  void *getpass_context;
  sasl_interact_t *prompt;

  /* see if we were given the password in the prompt */
  prompt=find_prompt(prompt_need,SASL_CB_PASS);
  if (prompt!=NULL)
  {
    /* We prompted, and got.*/
	
    if (! prompt->result)
      return SASL_FAIL;

    /* copy what we got into a secret_t */
    *password = (sasl_secret_t *) params->utils->malloc(sizeof(sasl_secret_t)+
						       prompt->len+1);
    if (! *password) return SASL_NOMEM;

    (*password)->len=prompt->len;
    memcpy((*password)->data, prompt->result, prompt->len);
    (*password)->data[(*password)->len]=0;

    return SASL_OK;
  }


  /* Try to get the callback... */
  result = params->utils->getcallback(params->utils->conn,
				      SASL_CB_PASS,
				      &getpass_cb,
				      &getpass_context);

  if (result == SASL_OK && getpass_cb)
    result = getpass_cb(params->utils->conn,
			getpass_context,
			SASL_CB_PASS,
			password);

  return result;
}


/*
 * Make the necessary prompts
 */
static int make_prompts(sasl_client_params_t *params,
			sasl_interact_t **prompts_res,
			int user_res,
			int pass_res)
{
  int num=1;
  sasl_interact_t *prompts;

  if (user_res==SASL_INTERACT) num++;
  if (pass_res==SASL_INTERACT) num++;

  if (num==1) return SASL_FAIL;

  prompts=params->utils->malloc(sizeof(sasl_interact_t)*(num+1));
  if ((prompts) ==NULL) return SASL_NOMEM;
  *prompts_res=prompts;

  if (user_res==SASL_INTERACT)
  {
    /* We weren't able to get the callback; let's try a SASL_INTERACT */
    (prompts)->id=SASL_CB_USER;
    (prompts)->challenge="Authorization Name";
    (prompts)->prompt="Please enter your authorization name";
    (prompts)->defresult=NULL;

    prompts++;
  }

  if (pass_res==SASL_INTERACT)
  {
    /* We weren't able to get the callback; let's try a SASL_INTERACT */
    (prompts)->id=SASL_CB_PASS;
    (prompts)->challenge="Password";
    (prompts)->prompt="Please enter your password";
    (prompts)->defresult=NULL;

    prompts++;
  }

  /* add the ending one */
  (prompts)->id=SASL_CB_LIST_END;
  (prompts)->challenge=NULL;
  (prompts)->prompt   =NULL;
  (prompts)->defresult=NULL;

  return SASL_OK;
}



static int client_continue_step (void *conn_context,
				 sasl_client_params_t *params,
				 const char *serverin __attribute__((unused)),
				 unsigned serverinlen __attribute__((unused)),
				 sasl_interact_t **prompt_need,
				 const char **clientout,
				 unsigned *clientoutlen,
				 sasl_out_params_t *oparams)
{
  int result;
  const char *user;

  context_t *text;
  text=conn_context;

  VL(("Login step #%i\n",text->state));

  if (text->state==1)
  {
    int user_result=SASL_OK;
    int pass_result=SASL_OK;

    /* check if sec layer strong enough */
    if (params->props.min_ssf>0)
      return SASL_TOOWEAK;

    /* try to get the userid */
    if (oparams->user==NULL)
    {
      VL (("Trying to get userid\n"));
      user_result=get_userid(params,
			     &user,
			     prompt_need);

      if ((user_result!=SASL_OK) && (user_result!=SASL_INTERACT))
	return user_result;
    }

    /* try to get the password */
    if (text->password==NULL)
    {
      VL (("Trying to get password\n"));
      pass_result=get_password(params,
			       &text->password,
			       prompt_need);
      
      if ((pass_result!=SASL_OK) && (pass_result!=SASL_INTERACT))
	return pass_result;
    }

    /* free prompts we got */
    if (prompt_need) {
	params->utils->free(*prompt_need);
	*prompt_need = NULL;
    }

    /* if there are prompts not filled in */
    if ((user_result==SASL_INTERACT) ||	(pass_result==SASL_INTERACT))
    {
      /* make the prompt list */
      result=make_prompts(params,prompt_need,
			  user_result, pass_result);
      if (result!=SASL_OK) return result;
      
      VL(("returning prompt(s)\n"));
      return SASL_INTERACT;
    }

    params->canon_user(params->utils->conn, user, 0, user, 0, 0, oparams);
    
    if (!oparams->authid || !text->password)
      return SASL_BADPARAM;

    VL (("Got username and password\n"));

    /* Watch for initial client send */
    if(clientout) {
	*clientout = blank_out;
	*clientoutlen = 0;
    }
    
    /* set oparams */
    oparams->mech_ssf=0;
    oparams->maxoutbuf=0;
    oparams->encode=NULL;
    oparams->decode=NULL;
    oparams->param_version = 0;

    text->state = 2;

    return SASL_CONTINUE;
  }

  if (text->state == 2) {
    /* server should have sent request for username */
    if (serverinlen != strlen(USERNAME) || strcmp(USERNAME,serverin))
      return SASL_BADPROT;

    if(!clientout) return SASL_BADPARAM;

    if(clientoutlen) *clientoutlen = strlen(oparams->user);
    *clientout = oparams->user;

    text->state = 3;

    return SASL_CONTINUE;
  }

  if (text->state == 3) {
    if (serverinlen != strlen(PASSWORD) || strcmp(PASSWORD,serverin))
      return SASL_BADPROT;

    if(!clientout) return SASL_BADPARAM;

    if(clientoutlen) *clientoutlen = text->password->len;
    *clientout = text->password->data;

    /* set oparams */
    oparams->param_version = 0;
    oparams->doneflag = 1;

    text->state = 99;

    return SASL_OK;
  }

  return SASL_FAIL; /* should never get here */
}

static const sasl_client_plug_t client_plugins[] = 
{
  {
    "LOGIN",
    0,
    SASL_SEC_NOANONYMOUS,
    0,
    NULL,
    NULL,
    &c_start,
    &client_continue_step,
    &dispose,
    &mech_free,
    NULL,
    NULL,
    NULL
  }
};

int sasl_client_plug_init(sasl_utils_t *utils __attribute__((unused)),
			  int maxversion,
			  int *out_version,
			  const sasl_client_plug_t **pluglist,
			  int *plugcount,
			  const char *plugname __attribute__((unused)))
{
  if (maxversion<SASL_CLIENT_PLUG_VERSION)
    return SASL_BADVERS;

  *pluglist=client_plugins;

  *plugcount=1;
  *out_version=SASL_CLIENT_PLUG_VERSION;

  return SASL_OK;
}

