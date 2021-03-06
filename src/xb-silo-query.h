/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __XB_SILO_QUERY_H
#define __XB_SILO_QUERY_H

#include <glib-object.h>

#include "xb-node.h"
#include "xb-silo.h"

G_BEGIN_DECLS

GPtrArray	*xb_silo_query			(XbSilo		*self,
						 const gchar	*xpath,
						 guint		 limit,
						 GError		**error);
XbNode		*xb_silo_query_first		(XbSilo		*self,
						 const gchar	*xpath,
						 GError		**error);

G_END_DECLS

#endif /* __XB_SILO_QUERY_H */
