/* SASL server API implementation
 * Rob Siemborski
 * Tim Martin
 * $Id: sasldb.c,v 1.1.2.6 2001/07/30 17:40:14 rjs3 Exp $
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

#include <config.h>

/* checkpw stuff */

#include <stdio.h>
#include <assert.h>

#include "sasl.h"
#include "saslutil.h"
#include "saslplug.h"
#include "../sasldb/sasldb.h"

#include "plugin_common.h"

/* returns the realm we should pretend to be in */
static int parseuser(const sasl_utils_t *utils,
		     char **user, char **realm, const char *user_realm, 
		     const char *serverFQDN, const char *input)
{
    int ret;
    char *r;

    assert(user && serverFQDN);

    r = strchr(input, '@');
    if (!r) {
	/* hmmm, the user didn't specify a realm */
	if(user_realm && user_realm[0]) {
	    ret = _plug_strdup(utils, user_realm, realm, NULL);
	} else {
	    /* Default to serverFQDN */
	    ret = _plug_strdup(utils, serverFQDN, realm, NULL);
	}
	
	if (ret == SASL_OK) {
	    ret = _plug_strdup(utils, input, user, NULL);
	}
    } else {
	r++;
	ret = _plug_strdup(utils, r, realm, NULL);
	*--r = '\0';
	*user = utils->malloc(r - input + 1);
	if (*user) {
	    strncpy(*user, input, r - input +1);
	} else {
	    MEMERROR( utils );
	    ret = SASL_NOMEM;
	}
	*r = '@';
    }

    return ret;
}

static void sasldb_auxprop_lookup(void *glob_context __attribute__((unused)),
				  sasl_server_params_t *sparams,
				  unsigned flags,
				  const char *user,
				  unsigned ulen) 
{
    char *userid = NULL;
    char *realm = NULL;
    const char *user_realm = NULL;
    int ret;
    const struct propval *to_fetch, *cur;
    char value[8192];
    size_t value_len;
    char *user_buf;
    
    if(!sparams || !user) return;

    user_buf = sparams->utils->malloc(ulen + 1);
    if(!user_buf)
	goto done;

    memcpy(user_buf, user, ulen);
    user_buf[ulen] = '\0';

    if(sparams->user_realm) {
	user_realm = sparams->user_realm;
    } else {
	user_realm = sparams->serverFQDN;
    }

    ret = parseuser(sparams->utils, &userid, &realm, user_realm,
		    sparams->serverFQDN, user_buf);
    if(ret != SASL_OK) goto done;

    to_fetch = sparams->utils->prop_get(sparams->propctx);
    if(!to_fetch) goto done;

    for(cur = to_fetch; cur->name; cur++) {
	/* If it's there already, we want to see if it needs to be
	 * overridden */
	if(cur->values && !(flags & SASL_AUXPROP_OVERRIDE))
	    continue;
	else if(cur->values)
	    sparams->utils->prop_erase(sparams->propctx, cur->name);
	    
	ret = _sasldb_getdata(sparams->utils,
			      sparams->utils->conn, userid, realm,
			      cur->name, value, 8192, &value_len);
	if(ret != SASL_OK) {
	    /* We didn't find it, leave it as not found */
	    continue;
	}

	sparams->utils->prop_set(sparams->propctx, cur->name,
				 value, value_len);
    }

 done:
    if (userid) sparams->utils->free(userid);
    if (realm)  sparams->utils->free(realm);
    if (user_buf) sparams->utils->free(user_buf);
}

static sasl_auxprop_plug_t sasldb_auxprop_plugin = {
    0,           /* Features */
    0,           /* spare */
    NULL,        /* glob_context */
    NULL,        /* auxprop_free */
    sasldb_auxprop_lookup, /* auxprop_lookup */
    NULL,        /* spares */
    NULL
};

int sasldb_auxprop_plug_init(const sasl_utils_t *utils,
                             int max_version,
                             int *out_version,
                             sasl_auxprop_plug_t **plug,
                             const char *plugname) 
{
    if(!out_version || !plug) return SASL_BADPARAM;

    /* We only support the "SASLDB" plugin */
    if(plugname && strcmp(plugname, "SASLDB")) return SASL_NOMECH;

    /* Do we have database support? */
    /* Note that we can use a NULL sasl_conn_t because our
     * sasl_utils_t is "blessed" with the global callbacks */
    if(_sasl_check_db(utils, NULL) != SASL_OK)
	return SASL_NOMECH;

    if(max_version < SASL_AUXPROP_PLUG_VERSION) return SASL_BADVERS;
    
    *out_version = SASL_AUXPROP_PLUG_VERSION;

    *plug = &sasldb_auxprop_plugin;

    return SASL_OK;
}
