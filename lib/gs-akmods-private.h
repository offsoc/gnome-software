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

#define GS_AKMODS_KEY_PATH "/etc/pki/akmods/certs"
#define GS_AKMODS_KEY_FILENAME GS_AKMODS_KEY_PATH "/public_key.der"

typedef enum {
	GS_AKMODS_STATE_ENROLLED	= 0,
	GS_AKMODS_STATE_NOT_FOUND	= 1,
	GS_AKMODS_STATE_NOT_ENROLLED	= 2,
	GS_AKMODS_STATE_PENDING		= 3,
	GS_AKMODS_STATE_ERROR		= 4
} GsAkmodsState;

typedef enum {
	GS_SECUREBOOT_STATE_UNKNOWN = -1,
	GS_SECUREBOOT_STATE_DISABLED = 0,
	GS_SECUREBOOT_STATE_ENABLED,
	GS_SECUREBOOT_STATE_NOT_SUPPORTED
} GsSecurebootState;

GsAkmodsState		gs_akmods_get_key_state_sync		(GCancellable  *cancellable,
								 GError       **error);
GsAkmodsState		gs_akmods_enroll_sync			(const GString *password,
								 GCancellable  *cancellable,
								 GError       **error);
GsSecurebootState	gs_akmods_get_secureboot_state_sync	(void);
GsSecurebootState	gs_akmods_get_last_secureboot_state	(void);

G_END_DECLS
