/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <adwaita.h>
#include "gs-app.h"
#include "gs-shell.h"

G_BEGIN_DECLS

#define GS_TYPE_AKMODS_DIALOG (gs_akmods_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GsAkmodsDialog, gs_akmods_dialog, GS, AKMODS_DIALOG, AdwWindow)

void		gs_akmods_dialog_run	(GtkWindow *parent,
					 GsShell *shell,
					 GsApp *app);

G_END_DECLS
