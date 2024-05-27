/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN_AKMODS (gs_plugin_akmods_get_type ())

G_DECLARE_FINAL_TYPE (GsPluginAkmods, gs_plugin_akmods, GS, PLUGIN_AKMODS, GsPlugin)

G_END_DECLS
