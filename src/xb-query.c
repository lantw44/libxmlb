/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"XbSilo"

#include "config.h"

#include <gio/gio.h>

#include "xb-machine.h"
#include "xb-opcode-private.h"
#include "xb-query-private.h"
#include "xb-silo-private.h"
#include "xb-stack-private.h"

typedef struct {
	GObject			 parent_instance;
	GPtrArray		*sections;		/* of XbQuerySection */
	XbSilo			*silo;
	gchar			*xpath;
	guint			 limit;
} XbQueryPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (XbQuery, xb_query, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (xb_query_get_instance_private (o))

/**
 * xb_query_get_sections:
 * @self: a #XbQuery
 *
 * Gets the sections that make up the query.
 *
 * Returns: (transfer none) (element-type XbQuerySection): sections
 *
 * Since: 0.1.4
 **/
GPtrArray *
xb_query_get_sections (XbQuery *self)
{
	XbQueryPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (XB_IS_QUERY (self), NULL);
	return priv->sections;
}

/**
 * xb_query_get_xpath:
 * @self: a #XbQuery
 *
 * Gets the XPath string that created the query.
 *
 * Returns: string
 *
 * Since: 0.1.4
 **/
const gchar *
xb_query_get_xpath (XbQuery *self)
{
	XbQueryPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (XB_IS_QUERY (self), NULL);
	return priv->xpath;
}

/**
 * xb_query_get_limit:
 * @self: a #XbQuery
 *
 * Gets the results limit on this query, where 0 is 'all'.
 *
 * Returns: integer, default 0
 *
 * Since: 0.1.4
 **/
guint
xb_query_get_limit (XbQuery *self)
{
	XbQueryPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (XB_IS_QUERY (self), 0);
	return priv->limit;
}

/**
 * xb_query_set_limit:
 * @self: a #XbQuery
 * @limit: integer
 *
 * Sets the results limit on this query, where 0 is 'all'.
 *
 * Since: 0.1.4
 **/
void
xb_query_set_limit (XbQuery *self, guint limit)
{
	XbQueryPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (XB_IS_QUERY (self));
	priv->limit = limit;
}

static XbOpcode *
xb_query_get_bound_opcode (XbQuery *self, guint idx)
{
	XbQueryPrivate *priv = GET_PRIVATE (self);
	guint idx_cnt = 0;

	for (guint i = 0; i < priv->sections->len; i++) {
		XbQuerySection *section = g_ptr_array_index (priv->sections, i);
		if (section->predicates == NULL)
			continue;
		for (guint j = 0; j < section->predicates->len; j++) {
			XbStack *stack = g_ptr_array_index (section->predicates, j);
			for (guint k = 0; k < xb_stack_get_size (stack); k++) {
				XbOpcode *op = xb_stack_peek (stack, k);
				if (xb_opcode_is_bound (op)) {
					if (idx == idx_cnt++)
						return op;
				}
			}
		}
	}
	return NULL;
}

/**
 * xb_query_bind_str:
 * @self: a #XbQuery
 * @idx: an integer index
 * @str: string to assign to the bound variable
 * @error: a #GError, or %NULL
 *
 * Assigns a string to a bound value specified using `?`.
 *
 * Returns: %TRUE if the @idx existed
 *
 * Since: 0.1.4
 **/
gboolean
xb_query_bind_str (XbQuery *self, guint idx, const gchar *str, GError **error)
{
	XbOpcode *op;

	g_return_val_if_fail (XB_IS_QUERY (self), FALSE);

	/* get the correct opcode */
	op = xb_query_get_bound_opcode (self, idx);
	if (op == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_ARGUMENT,
			     "no bound opcode with index %u", idx);
		return FALSE;
	}
	xb_opcode_bind_str (op, str, NULL);
	return TRUE;
}

/**
 * xb_query_bind_val:
 * @self: a #XbQuery
 * @idx: an integer index
 * @val: value to assign to the bound variable
 * @error: a #GError, or %NULL
 *
 * Assigns a string to a bound value specified using `?`.
 *
 * Returns: %TRUE if the @idx existed
 *
 * Since: 0.1.4
 **/
gboolean
xb_query_bind_val (XbQuery *self, guint idx, guint32 val, GError **error)
{
	XbOpcode *op;

	g_return_val_if_fail (XB_IS_QUERY (self), FALSE);

	/* get the correct opcode */
	op = xb_query_get_bound_opcode (self, idx);
	if (op == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_ARGUMENT,
			     "no bound opcode with index %u", idx);
		return FALSE;
	}
	xb_opcode_bind_val (op, val);
	return TRUE;
}

static void
xb_query_section_free (XbQuerySection *section)
{
	if (section->predicates != NULL)
		g_ptr_array_unref (section->predicates);
	g_free (section->element);
	g_slice_free (XbQuerySection, section);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(XbQuerySection, xb_query_section_free)

static gboolean
xb_query_parse_predicate (XbQuery *self,
			  XbQuerySection *section,
			  const gchar *text,
			  gssize text_len,
			  GError **error)
{
	XbQueryPrivate *priv = GET_PRIVATE (self);
	XbStack *opcodes;

	/* parse */
	opcodes = xb_machine_parse (xb_silo_get_machine (priv->silo),
				    text, text_len, error);
	if (opcodes == NULL)
		return FALSE;

	/* create array if it does not exist */
	if (section->predicates == NULL)
		section->predicates = g_ptr_array_new_with_free_func ((GDestroyNotify) xb_stack_unref);
	g_ptr_array_add (section->predicates, opcodes);
	return TRUE;
}

static XbQuerySection *
xb_query_parse_section (XbQuery *self, const gchar *xpath, GError **error)
{
	XbQueryPrivate *priv = GET_PRIVATE (self);
	g_autoptr(XbQuerySection) section = g_slice_new0 (XbQuerySection);
	guint start = 0;

	/* common XPath sections */
	if (g_strcmp0 (xpath, "parent::*") == 0 ||
	    g_strcmp0 (xpath, "..") == 0) {
		section->kind = XB_SILO_QUERY_KIND_PARENT;
		return g_steal_pointer (&section);
	}

	/* parse element and predicate */
	for (guint i = 0; xpath[i] != '\0'; i++) {
		if (start == 0 && xpath[i] == '[') {
			if (section->element == NULL)
				section->element = g_strndup (xpath, i);
			start = i;
			continue;
		}
		if (start > 0 && xpath[i] == ']') {
			if (!xb_query_parse_predicate (self,
						       section,
						       xpath + start + 1,
						       i - start - 1,
						       error)) {
				return NULL;
			}
			start = 0;
			continue;
		}
	}

	/* incomplete predicate */
	if (start != 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_ARGUMENT,
			     "predicate %s was unfinished, missing ']'",
			     xpath + start);
		return NULL;
	}

	if (section->element == NULL)
		section->element = g_strdup (xpath);
	if (g_strcmp0 (section->element, "child::*") == 0 ||
	    g_strcmp0 (section->element, "*") == 0) {
		section->kind = XB_SILO_QUERY_KIND_WILDCARD;
		return g_steal_pointer (&section);
	}
	section->element_idx = xb_silo_get_strtab_idx (priv->silo, section->element);
	if (section->element_idx == XB_SILO_UNSET) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_ARGUMENT,
			     "element name %s is unknown in silo",
			     section->element);
		return NULL;
	}
	return g_steal_pointer (&section);
}

static gboolean
xb_query_parse (XbQuery *self, const gchar *xpath, GError **error)
{
	XbQueryPrivate *priv = GET_PRIVATE (self);
	XbQuerySection *section;
	g_autoptr(GString) acc = g_string_new (NULL);

//	g_debug ("parsing XPath %s", xpath);
	for (gsize i = 0; xpath[i] != '\0'; i++) {

		/* escaped chars */
		if (xpath[i] == '\\') {
			if (xpath[i+1] == '/' ||
			    xpath[i+1] == 't' ||
			    xpath[i+1] == 'n') {
				g_string_append_c (acc, xpath[i+1]);
				i += 1;
				continue;
			}
		}

		/* split */
		if (xpath[i] == '/') {
			if (acc->len == 0) {
				g_set_error_literal (error,
						     G_IO_ERROR,
						     G_IO_ERROR_NOT_FOUND,
						     "xpath section empty");
				return FALSE;
			}
			section = xb_query_parse_section (self, acc->str, error);
			if (section == NULL)
				return FALSE;
			g_ptr_array_add (priv->sections, section);
			g_string_truncate (acc, 0);
			continue;
		}
		g_string_append_c (acc, xpath[i]);
	}

	/* add any remaining section */
	section = xb_query_parse_section (self, acc->str, error);
	if (section == NULL)
		return FALSE;
	g_ptr_array_add (priv->sections, section);
	return TRUE;
}

/**
 * xb_query_new:
 * @silo: a #XbSilo
 * @xpath: The XPath query
 *
 * Creates a query to be used by @silo. It may be quicker to create a query
 * manually and re-use it multiple times.
 *
 * Returns: (transfer full): a #XbQuery
 *
 * Since: 0.1.4
 **/
XbQuery *
xb_query_new (XbSilo *silo, const gchar *xpath, GError **error)
{
	g_autoptr(XbQuery) self = g_object_new (XB_TYPE_QUERY, NULL);
	XbQueryPrivate *priv = GET_PRIVATE (self);

	/* create */
	priv->silo = g_object_ref (silo);
	priv->xpath = g_strdup (xpath);
	priv->sections = g_ptr_array_new_with_free_func ((GDestroyNotify) xb_query_section_free);

	/* add each section */
	if (!xb_query_parse (self, xpath, error))
		return NULL;

	/* nothing here! */
	if (priv->sections->len == 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "No query sections for '%s'",
			     xpath);
		return NULL;
	}

	/* success */
	return g_steal_pointer (&self);
}

static void
xb_query_init (XbQuery *self)
{
}

static void
xb_query_finalize (GObject *obj)
{
	XbQuery *self = XB_QUERY (obj);
	XbQueryPrivate *priv = GET_PRIVATE (self);
	g_object_unref (priv->silo);
	g_ptr_array_unref (priv->sections);
	g_free (priv->xpath);
	G_OBJECT_CLASS (xb_query_parent_class)->finalize (obj);
}

static void
xb_query_class_init (XbQueryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = xb_query_finalize;
}
