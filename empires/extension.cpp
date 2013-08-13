/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod Sample Extension
 * Copyright (C) 2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

/**
 * Forked to SourceMod Empires Extension by Recon
 * http://microbits.info 
 */

#include "extension.h"
#include "edict.h"
#include "iplayerinfo.h"

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

Empires g_Empires;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_Empires);


bool Empires::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	if (strcmp(g_pSM->GetGameFolderName(), "empires") != 0)
	{
		snprintf(error, maxlength, "Cannot load Empires extension on mods other than Empires.");
		return false;
	}	

	playerhelpers->RegisterCommandTargetProcessor(this);

	return true;
}

void Empires::SDK_OnUnload()
{
	playerhelpers->UnregisterCommandTargetProcessor(this);
}

size_t UTIL_Format(char *buffer, size_t maxlength, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	size_t len = vsnprintf(buffer, maxlength, fmt, ap);
	va_end(ap);

	if (len >= maxlength)
	{
		buffer[maxlength - 1] = '\0';
		return (maxlength - 1);
	}
	else
	{
		return len;
	}
}

bool Empires::ProcessCommandTarget(cmd_target_info_t *info)
{
	int max_clients;
	IPlayerInfo *pInfo;
	unsigned int team_index = 0;
	IGamePlayer *pPlayer, *pAdmin;
	bool excludeSelf = false;

	if ((info->flags & COMMAND_FILTER_NO_MULTI) == COMMAND_FILTER_NO_MULTI)
	{
		return false;
	}

	if (info->admin)
	{
		if ((pAdmin = playerhelpers->GetGamePlayer(info->admin)) == NULL)
		{
			return false;
		}
		if (!pAdmin->IsInGame())
		{
			return false;
		}
	}
	else
	{
		pAdmin = NULL;
	}

	// Spec target groups
	if (strcmp(info->pattern, "@spec") == 0)
	{
		team_index = 1;
	}
	else if (strcmp(info->pattern, "@mspec") == 0)
	{
		team_index = 1;

		// Make sure there is a valid admin
		// Performance opt
		excludeSelf = (info->admin > 0);
	}

	// NF target groups
	else if (strcmp(info->pattern, "@nf") == 0)
	{
		team_index = 2;
	}
	else if (strcmp(info->pattern, "@mnf") == 0)
	{
		team_index = 2;

		// Make sure there is a valid admin
		// Performance opt
		excludeSelf = (info->admin > 0);
	}

	// BE target groups
	else if (strcmp(info->pattern, "@be") == 0)
	{
		team_index = 3;
	}
	else if (strcmp(info->pattern, "@mbe") == 0)
	{
		team_index = 3;

		// Make sure there is a valid admin
		// Performance opt
		excludeSelf = (info->admin > 0);
	}
	
	// No target group
	else
	{
		return false;
	}
	

	// Init the number of targets
	info->num_targets = 0;

	// Get the max number of clients
	max_clients = playerhelpers->GetMaxClients();
	for (int i = 1; 
		 i <= max_clients && (cell_t)info->num_targets < info->max_targets; 
		 i++)
	{

		// Exclude invalid targets
		if ((pPlayer = playerhelpers->GetGamePlayer(i)) == NULL)
		{
			continue;
		}
		if (!pPlayer->IsInGame())
		{
			continue;
		}
		if ((pInfo = pPlayer->GetPlayerInfo()) == NULL)
		{
			continue;
		}
		if (pInfo->GetTeamIndex() != (int)team_index)
		{
			continue;
		}
		if (playerhelpers->FilterCommandTarget(pAdmin, pPlayer, info->flags) 
			!= COMMAND_TARGET_VALID)
		{
			continue;
		}
		if (excludeSelf && i == info->admin)
		{
			continue;
		}

		// Add this target
		info->targets[info->num_targets] = i;
		info->num_targets++;
	}

	// Handle no targets
	if (info->num_targets == 0)
	{
		info->reason = COMMAND_TARGET_EMPTY_FILTER;
	}
	else
	{
		info->reason = COMMAND_TARGET_VALID;
	}

	// Set the target name
	info->target_name_style = COMMAND_TARGETNAME_RAW;
	if (team_index == 1)
	{
		UTIL_Format(info->target_name, info->target_name_maxlength, "Spectator");
	}
	else if (team_index == 2)
	{
		UTIL_Format(info->target_name, info->target_name_maxlength, "Northern Faction");
	}
	else if (team_index == 3)
	{
		UTIL_Format(info->target_name, info->target_name_maxlength, "Brenodi Empire");
	}

	// We're done
	return true;
}