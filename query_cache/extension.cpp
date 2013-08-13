/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod Query Cache Extension
 * Copyright (C) 2010 Recon. All rights reserved.
 *
 * This program contains code from the following programs (which are licensed
 *                                                         under the GPL):
 *    1. SrcdsQueryCache 
 *       (http://www.wantedgov.it/page/62-srcds-query-cache/) 
 *        by Beretta Claudio
 *    2. SRCDS Denial of Service Protect
 *       (http://forums.alliedmods.net/showthread.php?t=95312) by raydan
 *    3. Left 4 Downtown (http://forums.alliedmods.net/showthread.php?t=91132)
 *       by Downtown1
 *
 * ============================================================================= 
 * A huge thank you goes to CrimsonGT for all his help with this extension. 
 *
 * Thanks also to Luigi Auriemma (http://aluigi.org/),
 *  whose Winsock code from UDP proxy/pipe (http://aluigi.org/mytoolz.htm)
 *  helped me learn the Winsock API.
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
 * *****************************************************************************
 * This program is a SourceMod extension, and is effected by the following
 * exceptions from SourceMod's authors:
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

// Extension definition
#include "extension.h"

// Windows specific headers
#include <windows.h>
#include <winsock2.h>

// HL2 SDK headers
#include <bitbuf.h>
#include <iserver.h>
#include <icommandline.h>
#include <convar.h>

// SourceMod headers
#include <ISDKTools.h>

// Standard libary headers
#include <string>
using std::string;

// Standard c string size
#define S_STRLEN 256

// Windows path seperator
#define PATH_SEP "\\"

// Default game version
#define DEFAULT_GAME_VERSION "1.0.1.59"

// Default protocol version
#define DEFAULT_PROTO_VERSION 14

// Null entity packet
#define NULL_ENT_PACKET "\xff\xff\xff\xff\x71\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x00"

// Function prototypes
int RecvFromHook(int s, char *buf, int len, int flags, 
				 struct sockaddr *from, int *fromlen);
void gameVersionChanged(IConVar *var, const char *pOldValue,
						float flOldValue);
void gameDescChanged(IConVar *var, const char *pOldValue,
					 float flOldValue);
void protoVersionChanged(IConVar *var, const char *pOldValue,
                         float flOldValue);
void maxClientsChanged(IConVar *var, const char *pOldValue,
                       float flOldValue);
void EnableReceiveHook();
void DisableReceiveHook();
void BuildStaticReplyInfo();
void BuildReplyInfo();

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

QueryCache g_QueryCache;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_QueryCache);

IServer *g_pServer = NULL; //ptr to CBaseServer
ISDKTools *g_pSDKTools = NULL;

// Game version
ConVar *g_cvarGameVersion = NULL;

// Game description
ConVar *g_cvarGameDesc = NULL;

// Protocol version
ConVar *g_cvarProtoVersion = NULL;

// Max clients
ConVar *g_cvarMaxClients = NULL;

// Receive hook
bool g_recvfrom_hooked = false;
int (*g_real_recvfrom_ptr) (int , char *, int , int , struct sockaddr *, int *);

// A2S_INFO cache
time_t g_a2s_time = 0;
char g_replyStore[1024];
bf_write g_replyPacket(g_replyStore, 1024);

// A2S_INFO data cache
char g_gameDir[S_STRLEN];
char g_gameDesc[S_STRLEN];
char g_gameVersion[S_STRLEN];
int g_appID;
int g_maxClients;
int g_realMaxClients;
int g_protoVersion;

bool QueryCache::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	// What could go wrong?
	return true;
}

bool QueryCache::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
    // Get ICvar (code from Left4Downtown)
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);	
	ConVar_Register(0, this);

    // TOOD: Error handling?
    return true;
}

void QueryCache::SDK_OnAllLoaded()
{
	// Get IServer	
	SM_GET_LATE_IFACE(SDKTOOLS, g_pSDKTools);	
	g_pServer = g_pSDKTools->GetIServer();

   	// Build the static query reply info
	BuildStaticReplyInfo();


	// Game version
	g_cvarGameVersion = new ConVar("qcache_gameversion", g_gameVersion, FCVAR_SPONLY,
									"Sets the game version reported to clients in AS2_INFO responses.");
	g_cvarGameVersion->InstallChangeCallback(&gameVersionChanged);	
	
	// Game description
	g_cvarGameDesc = new ConVar("qcache_gamedesc", g_gameDesc, FCVAR_SPONLY,
								"Sets the game description reported to clients in AS2_INFO responses.");
	g_cvarGameDesc->InstallChangeCallback(&gameDescChanged);

    // Protocol version
    g_protoVersion = DEFAULT_PROTO_VERSION;
    char protoVersionStr[5];
    itoa(g_protoVersion, protoVersionStr, 10);
    g_cvarProtoVersion = new ConVar("qcache_protoversion", protoVersionStr, FCVAR_SPONLY,
                                    "Sets the protocol version reported to clients in AS2_INFO responses.");
    g_cvarProtoVersion->InstallChangeCallback(&protoVersionChanged);

    // Max clients
    g_realMaxClients = g_maxClients = g_pServer->GetMaxClients();
    char maxClientsStr[5];
    itoa(g_realMaxClients, maxClientsStr, 10);
    g_cvarMaxClients = new ConVar("qcache_maxclients", maxClientsStr, FCVAR_SPONLY,
                                  "Sets the max clients value reported to clients in AS2_INFO responses. "
                                  "This value must be less than or equal to the actual max clients value.",
                                  true, 1, true, g_realMaxClients);
    g_cvarMaxClients->InstallChangeCallback(&maxClientsChanged);


	// Init winsock
	WSAData wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		// TODO: Can't open winsock, error
	}	

	// Activate the receive hook
	EnableReceiveHook();	
}

void QueryCache::SDK_OnUnload()
{
	// Deactivate the receive hook
	DisableReceiveHook();    
	
	// Free dynamic memory
	delete g_cvarGameVersion;
	delete g_cvarGameDesc;
    delete g_cvarProtoVersion;
    delete g_cvarMaxClients;

	// Deactivate winsock
	WSACleanup();
}

// For convars
bool QueryCache::RegisterConCommandBase(ConCommandBase *pVar) 
{
    return META_REGCVAR(pVar); 
}

void gameDescChanged(IConVar *var, const char *pOldValue, float flOldValue)
{
	strcpy_s(&g_gameDesc[0], S_STRLEN, g_cvarGameDesc->GetString());
}

void gameVersionChanged(IConVar *var, const char *pOldValue, float flOldValue)
{	
	// Update the game version
	strcpy_s(&g_gameVersion[0], S_STRLEN, g_cvarGameVersion->GetString());
}

void protoVersionChanged(IConVar *var, const char *pOldValue, float flOldValue)
{	
	// Update the protocol version
	g_protoVersion = g_cvarProtoVersion->GetInt();
}

void maxClientsChanged(IConVar *var, const char *pOldValue, float flOldValue)
{	
    // Make sure someone didn't modify the cvar bounds
    // to get more clients then they're supposed to have
    int newValue = g_cvarMaxClients->GetInt();
    if (newValue <= g_realMaxClients)
    {
        g_maxClients = newValue;
    }	 
}

int RecvFromHook(int s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{	
	// Receive the packet
	int ret = g_real_recvfrom_ptr(s, buf, len, flags, from, fromlen);

	// Packet is at least five bytes
	if(ret > 5)
	{
        // Block null entity packets
        if (strcmp(buf, NULL_ENT_PACKET) == 0)
        {           
			flags = WSAETIMEDOUT;
			return SOCKET_ERROR;
        }

		// Is this an A2S_INFO request?
		if(ret == 25 && buf[4] == 84) //84 == 'T'
		{			
			// Is the query cache valid (less than 5 seconds old)?
			if (time(NULL) - g_a2s_time > 5)
			{
				// No, refresh the cache
				BuildReplyInfo();						
			}

			// Send the packet
			sendto(s, (const char *)g_replyPacket.GetData(),
				   g_replyPacket.GetNumBytesWritten(), 0, from, *fromlen);
			
			// Block the server from handling the request
			flags = WSAETIMEDOUT;
			return SOCKET_ERROR;

			// DoS protect method
			//return 25;
		}        
	}

	// Not an AS2_INFO packet, allow it through
	return ret;
}

void DisableReceiveHook()
{	
	// Remove the recvfrom hook
	if(g_recvfrom_hooked)
	{
		g_pVCR->Hook_recvfrom = g_real_recvfrom_ptr;
		g_recvfrom_hooked = false;		
	}	
}

void EnableReceiveHook()
{
	// Create the recvfrom hook
	if(!g_recvfrom_hooked)
	{
		g_real_recvfrom_ptr = g_pVCR->Hook_recvfrom;
		g_pVCR->Hook_recvfrom = &RecvFromHook;
		g_recvfrom_hooked = true;		
	}	
}

void BuildStaticReplyInfo()
{
	// Info that doesn't change is saved here

	// Get the game dir	
	engine->GetGameDir(&g_gameDir[0], S_STRLEN);

	// Strip everything except the
	// actual game dir
	string temp(g_gameDir);
	int pos = temp.find_last_of(PATH_SEP);	
	strcpy_s(&g_gameDir[0], S_STRLEN, 
		(temp.substr(pos + 1, temp.size() - pos)).c_str());	

	// The game description can be overridden by
	// the qcache_gamedesc cvar
	//
	// Empires seems to report it's version in
	// the game description, make sure it's the latest:
	if (strcmp(g_gameDir, "empires") == 0)
	{
		// This is safe since we aren't pointing
		// to the game's real description
		strcpy(&g_gameDesc[0], "Empires v2.24d");
	}
	else
	{
		// Get the game description
		strcpy(&g_gameDesc[0], gamedll->GetGameDescription());		
	}

	// Get the app ID
	g_appID = engine->GetAppID();

	// Set the game version
	strcpy_s(&g_gameVersion[0], S_STRLEN, DEFAULT_GAME_VERSION);
}

void BuildReplyInfo()
{	
	// Delete the old packet
	g_replyPacket.Reset();	
	
	// Is the server passworded?
	int passByte = 00;
	const char *pGamePass = g_pServer->GetPassword();
	if(pGamePass)
	{
		passByte = 01;
	}

	// Begin packet
	g_replyPacket.WriteLong(-1);
	
	// Type
	g_replyPacket.WriteByte(73);

	// Protocol Version (0x07 (corrected to 14) is Steam Version,
	//					 0x30 is Goldsource)
	g_replyPacket.WriteByte(g_protoVersion);

	// Hostname
	g_replyPacket.WriteString(g_pServer->GetName());

	// Map name
	g_replyPacket.WriteString(g_pServer->GetMapName());

	// Game directory
	g_replyPacket.WriteString(g_gameDir);

	// Game description
	g_replyPacket.WriteString(g_gameDesc);

	// AppID
	g_replyPacket.WriteShort(g_appID);

	// Number of Players
	g_replyPacket.WriteByte(g_pServer->GetNumClients());

	// Max Players
	g_replyPacket.WriteByte(g_maxClients);

	// Number of Bots
	g_replyPacket.WriteByte(g_pServer->GetNumFakeClients());

	// "d" (100) for Dedicated
	g_replyPacket.WriteByte(100);

	// Operating System "w" (119) for Windows "l" (108) for Linux
	g_replyPacket.WriteByte(119);

	// Passworded or Not
	g_replyPacket.WriteByte(passByte);
	
	// VAC Secured
	g_replyPacket.WriteByte(01);
	
	// Game Version (end of packet)
	g_replyPacket.WriteString(g_gameVersion);

	// Set the cache time
	g_a2s_time = time(NULL);
}