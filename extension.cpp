/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod Late Downloads Extension
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

#include "extension.h"

#include <networkstringtabledefs.h>
#include <filesystem.h>
#include <inetchannel.h>

#include <utlvector.h>
#include <UtlStringMap.h>

CExtension g_Extension;
SMEXT_LINK(&g_Extension);

INetworkStringTableContainer * g_pNSTC = NULL;

INetworkStringTable *g_pDownloadTable = NULL;
IBaseFileSystem * g_pBaseFileSystem = NULL;
IServerPluginHelpers * g_pPluginHelpers = NULL;

int g_TransferID = 0;

struct ActiveDownload {
	const char * filename;
	CUtlVector<int> clients;
	float maximalDuration;
};

CUtlVector<ActiveDownload> g_ActiveDownloads;
CUtlVector<float> g_BatchDeadlines;

volatile const char * g_pFlaggedFile = NULL;

SH_DECL_HOOK1_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool);
SH_DECL_HOOK2(IBaseFileSystem, Size, SH_NOATTRIB, 0, unsigned int, const char *, const char *);
SH_DECL_HOOK5_void(IServerPluginCallbacks, OnQueryCvarValueFinished, SH_NOATTRIB, 0, QueryCvarCookie_t, edict_t *, EQueryCvarValueStatus, const char *, const char *);

int g_GameFrameHookId = 0;
int g_SizeHookId = 0;
int g_OnQueryCvarValueFinishedHookId = 0;

IForward *g_pOnDownloadSuccess = NULL;
IForward *g_pOnDownloadFailure = NULL;

ConVar g_MinimalBandwidth("latedl_minimalbandwidth", "64", FCVAR_NONE, "Kick clients with lower bandwidth (in kbps). Zero to disable.");
ConVar g_MaximalDelay("latedl_maximaldelay", "500", FCVAR_NONE, "Acceptable additional delay (in ms) when sending files.");
ConVar g_RequireUpload("latedl_requireupload", "1", FCVAR_NONE, "Kick clients with \"sv_allowupload\" = 0. Zero to disable.");

bool CExtension::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late) {
	GET_V_IFACE_ANY(GetEngineFactory, g_pNSTC, INetworkStringTableContainer, INTERFACENAME_NETWORKSTRINGTABLESERVER);
	if (!g_pNSTC) {
		snprintf(error, maxlen, "Couldn't get INetworkStringTableContainer!");
		return false;
	}
	GET_V_IFACE_ANY(GetFileSystemFactory, g_pBaseFileSystem, IBaseFileSystem, BASEFILESYSTEM_INTERFACE_VERSION);
	if (!g_pBaseFileSystem) {
		snprintf(error, maxlen, "Couldn't get IBaseFileSystem!");
		return false;
	}
	GET_V_IFACE_ANY(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	if (!g_pCVar) {
		snprintf(error, maxlen, "Couldn't get ICvar!");
		return false;
	}
	GET_V_IFACE_ANY(GetEngineFactory, g_pPluginHelpers, IServerPluginHelpers, INTERFACEVERSION_ISERVERPLUGINHELPERS);
	if (!g_pPluginHelpers) {
		snprintf(error, maxlen, "Couldn't get IServerPluginHelpers!");
		return false;
	}

	IServerPluginCallbacks * serverPluginCallbacks = g_SMAPI->GetVSPInfo(NULL);
	if (!serverPluginCallbacks) {
		snprintf(error, maxlen, "Couldn't get IServerPluginCallbacks!");
		return false;
	}

	g_GameFrameHookId = SH_ADD_VPHOOK(IServerGameDLL, GameFrame, gamedll, SH_MEMBER(this, &CExtension::OnGameFrame), false);
	g_SizeHookId = SH_ADD_VPHOOK(IBaseFileSystem, Size, g_pBaseFileSystem, SH_MEMBER(this, &CExtension::OnSize), false);
	g_OnQueryCvarValueFinishedHookId = SH_ADD_VPHOOK(IServerPluginCallbacks, OnQueryCvarValueFinished, serverPluginCallbacks, SH_MEMBER(this, &CExtension::OnQueryCvarValueFinished), false);
	
	ConVar_Register(0, this);
	return true;
}

void CExtension::SDK_OnUnload() {
	SH_REMOVE_HOOK_ID(g_GameFrameHookId);
	SH_REMOVE_HOOK_ID(g_SizeHookId);
	SH_REMOVE_HOOK_ID(g_OnQueryCvarValueFinishedHookId);
	forwards->ReleaseForward(g_pOnDownloadSuccess);
	forwards->ReleaseForward(g_pOnDownloadFailure);
}

void CExtension::OnQueryCvarValueFinished(QueryCvarCookie_t iCookie, edict_t *pPlayerEntity, EQueryCvarValueStatus eStatus, const char *pCvarName, const char *pCvarValue) {
	if (!g_RequireUpload.GetBool())
		return;
	if (pCvarName == NULL || V_strcmp(pCvarName, "sv_allowupload") != 0)
		return;
	int clientIndex = gamehelpers->IndexOfEdict(pPlayerEntity);
	if (eStatus != eQueryCvarValueStatus_ValueIntact || pCvarValue == NULL) {
		gamehelpers->AddDelayedKick(clientIndex, playerhelpers->GetGamePlayer(pPlayerEntity)->GetUserId(), "Couldn't get value of sv_allowupload!");
		return;
	}
	if (!V_atoi(pCvarValue)) {
		gamehelpers->AddDelayedKick(clientIndex, playerhelpers->GetGamePlayer(pPlayerEntity)->GetUserId(), "You need to allow sv_allowupload!");
	}
}

void CExtension::CheckClientUpload(int client) {
	if (!g_RequireUpload.GetBool())
		return;
	edict_t * clientEdict = gamehelpers->EdictOfIndex(client);
	if (g_pPluginHelpers->StartQueryCvarValue(clientEdict, "sv_allowupload") == InvalidQueryCvarCookie) {
		gamehelpers->AddDelayedKick(client, playerhelpers->GetGamePlayer(clientEdict)->GetUserId(), "Couldn't get value of sv_allowupload!");
	}
}

void OnDownloadSuccess(int iClient, const char * filename) {
	//smutils->LogMessage(myself, "Client %d successfully downloaded file '%s'!", iClient, filename);
	if (!g_pOnDownloadSuccess)
		return;
	g_pOnDownloadSuccess->PushCell(iClient);
	g_pOnDownloadSuccess->PushString(filename);
	g_pOnDownloadSuccess->Execute();
}

void OnDownloadFailure(int iClient, const char * filename) {
	smutils->LogMessage(myself, "Client %d failed to download file '%s'!", iClient, filename);
	if (!g_pOnDownloadFailure)
		return;
	g_pOnDownloadFailure->PushCell(iClient);
	g_pOnDownloadFailure->PushString(filename);
	g_pOnDownloadFailure->Execute();
}

void CExtension::OnGameFrame(bool simulating) {
	FOR_EACH_VEC(g_ActiveDownloads, dit) {
		ActiveDownload& activeDownload = g_ActiveDownloads[dit];
		FOR_EACH_VEC(activeDownload.clients, cit) {
			int iClient = activeDownload.clients[cit];
			INetChannel * chan = (INetChannel*)engine->GetPlayerNetInfo(iClient);
			bool resendSuccess;
			if (!chan) {
				smutils->LogError(myself, "Lost client %d when sending file '%s'!", iClient, activeDownload.filename);
				OnDownloadFailure(iClient, activeDownload.filename);
				goto deactivate;
			}
			
			//Iä! Iä! Cthulhu fhtagn!
			g_pFlaggedFile = activeDownload.filename;
#ifdef DEMO_AWARE
			resendSuccess = chan->SendFile(activeDownload.filename, g_TransferID++, false);
#else
			resendSuccess = chan->SendFile(activeDownload.filename, g_TransferID++);
#endif
			if (!resendSuccess) {
				smutils->LogError(myself, "Failed to track progress of sending file '%s' to client %d ('%s', %s)!", activeDownload.filename, iClient, chan->GetName(), chan->GetAddress());
				OnDownloadFailure(iClient, activeDownload.filename);
				goto deactivate;
			}
			if (g_pFlaggedFile != NULL) {
				g_pFlaggedFile = NULL;
				if (activeDownload.maximalDuration <= 0 || Plat_FloatTime() <= g_BatchDeadlines[iClient])
					continue; //still queued, all ok!
				smutils->LogError(myself, "Client %d ('%s', %s) had insufficient bandwidth (<%d kbps), failed to receive '%s' in time! Kicking!", iClient, chan->GetName(), chan->GetAddress(), g_MinimalBandwidth.GetInt(), activeDownload.filename);
				OnDownloadFailure(iClient, activeDownload.filename);
				playerhelpers->GetGamePlayer(gamehelpers->EdictOfIndex(iClient))->Kick("You have insufficient bandwidth!");
				goto deactivate;
			}

			OnDownloadSuccess(iClient, activeDownload.filename);
		deactivate:
			activeDownload.clients.FastRemove(cit);
			//reset iterator
			cit--;
		}

		if (activeDownload.clients.Count() == 0) {
			OnDownloadSuccess(0, activeDownload.filename);
			g_ActiveDownloads.FastRemove(dit);
			//reset iterator
			dit--;
		}
	}
}

/*
To clarify what is going on:
Inside the INetChannel::Sendfile() function, several calls to other functions are made.
The one we're insterested in checks wheter the file we're trying to send is already in the send queue.
If that is the case, the function returns True right away.
If not, the function proceeds to the file sending procedure.

If the file was not in the queue, it must've been successfully delivered to the client.

We can tell that by listening on one of the subsequent calls.
One of these calls gets the size of the file we're trying to send.
If this function gets called with our file in parameter, we know that the client have already received the file.

At this moment, we could for example claim that the file doesn't exists.
That would prevent the file from being re-send to the client, but would cause an error message in the server log.

We could also do nothing at all, letting the client re-download the file.
The client would then reject the file and spew an error message in the client log.

Or we could say that the file has size of 0 bytes. The client would re-download these 0 bytes pretty quickly.

This saves a lot of bandwidth and miraculously doesn't cause any kind of error anywhere.
*/

unsigned int CExtension::OnSize(const char *pFileName, const char *pPathID) {
	if (pFileName != NULL && g_pFlaggedFile == pFileName) {
		g_pFlaggedFile = NULL;
		RETURN_META_VALUE(MRES_SUPERCEDE, 0);
	}
	RETURN_META_VALUE(MRES_IGNORED, 0);
}

bool ReloadDownloadTable() {
	g_pDownloadTable = g_pNSTC->FindTable("downloadables");
	return g_pDownloadTable != NULL;
}

void CExtension::OnCoreMapStart(edict_t *pEdictList, int edictCount, int clientMax) {
	if (!ReloadDownloadTable())
		smutils->LogError(myself, "Couldn't load download table!");
	g_ActiveDownloads.RemoveAll();
	g_BatchDeadlines.SetCount(clientMax + 1);
	g_BatchDeadlines.FillWithValue(0);
}

int AddStaticDownloads(CUtlVector<const char*> const & filenames, CUtlVector<const char *> & addedFiles) {
	bool lock = engine->LockNetworkStringTables(true);

	int added = 0;

	FOR_EACH_VEC(filenames, fit) {
		const char * filename = filenames[fit];
		if (g_pDownloadTable->FindStringIndex(filename) != INVALID_STRING_INDEX) {
			OnDownloadSuccess(0, filename);
			continue;
		}
		if (g_pDownloadTable->AddString(true, filename) == INVALID_STRING_INDEX) {
			smutils->LogError(myself, "Couldn't add file '%s' to download table!", filename);
			OnDownloadFailure(0, filename);
			continue;
		}
		addedFiles.AddToTail(filename);
		added++;
	}

	engine->LockNetworkStringTables(lock);

	return added;
}

int SendFiles(CUtlVector<const char*> const & filenames) {
	int minimalBandwidth = g_MinimalBandwidth.GetInt();
	int maximalDelay = g_MaximalDelay.GetInt();
	float now = Plat_FloatTime();

	int sent = 0;
	FOR_EACH_VEC(filenames, fit) {
		const char * filename = filenames[fit];
		bool failed = false;
		ActiveDownload& activeDownload = g_ActiveDownloads[g_ActiveDownloads.AddToTail()];
		activeDownload.filename = filename;
		activeDownload.maximalDuration = 0;
		if (minimalBandwidth > 0) {
			int numBytes = g_pBaseFileSystem->Size(filename);
			activeDownload.maximalDuration = 0.001f * maximalDelay + (numBytes * 8) / (minimalBandwidth * 1000.f);
		}
		for (int iClient = 1; iClient <= playerhelpers->GetMaxClients(); iClient++)
		{
			INetChannel * chan = (INetChannel*)engine->GetPlayerNetInfo(iClient);
			if (!chan)
				continue;
#ifdef DEMO_AWARE
			if (chan->SendFile(filename, g_TransferID, false)) {
#else
			if (chan->SendFile(filename, g_TransferID)) {
#endif
				g_TransferID++;
				activeDownload.clients.AddToTail(iClient);
				if (g_BatchDeadlines[iClient] < now)
					g_BatchDeadlines[iClient] = now;
				g_BatchDeadlines[iClient] += activeDownload.maximalDuration;
			}
			else {
				failed = true;
			}
		}
		if (failed) {
			if (activeDownload.clients.Count() > 0) {
				smutils->LogError(myself, "This shouldn't have happend! The file %d '%s' have been succesfully sent to some clients, but not to the others. Please inform the author of this extension that this happend!", g_TransferID, filename);
				//provide some info for unfortunate future me
				for (int iClient = 1; iClient <= playerhelpers->GetMaxClients(); iClient++) {
					INetChannel * chan = (INetChannel*)engine->GetPlayerNetInfo(iClient);
					if (!chan)
						continue;
					//this is probably slow
					if (activeDownload.clients.HasElement(iClient)) {
						smutils->LogError(myself, "Additional info: Good client %d ('%s', %s)!", iClient, chan->GetName(), chan->GetAddress());
					}
					else {
						smutils->LogError(myself, "Additional info: Bad client %d ('%s', %s)!", iClient, chan->GetName(), chan->GetAddress());
						OnDownloadFailure(iClient, filename);
					}
				}
				sent++;
			}
			else {
				smutils->LogError(myself, "Failed to send file %d '%s'!", g_TransferID, filename);
				OnDownloadFailure(0, filename);
				g_ActiveDownloads.FastRemove(g_ActiveDownloads.Count() - 1);
			}
		}
		else{
			sent++;
		}
	}

	if (g_RequireUpload.GetBool()) {
		//poll sv_allowupload value
		for (int iClient = 1; iClient <= playerhelpers->GetMaxClients(); iClient++)
		{
			if(engine->GetPlayerNetInfo(iClient))
				CExtension::CheckClientUpload(iClient);
		}
	}

	return sent;
}

cell_t AddLateDownloads(IPluginContext *pContext, const cell_t *params) {
	int argc = params[0];
	if (argc != 2)
		return 0;
	cell_t * fileArray = NULL;
	pContext->LocalToPhysAddr(params[1], &fileArray);

	if (fileArray == NULL)
		return 0;

	cell_t numFiles = params[2];
	CUtlVector<const char *> filenames(0, numFiles);

	for (int i = 0; i < numFiles; i++) {
		//Pawn arrays are weird!
		const char * str = (char*)(&fileArray[i]) + fileArray[i];
		filenames.AddToTail(str);
	}

	CUtlVector<const char *> addedFiles(0, numFiles);
	int added = AddStaticDownloads(filenames, addedFiles);
	if (added == 0)
		return 0;

	int sent = SendFiles(addedFiles);
	return sent;
}

cell_t AddLateDownload(IPluginContext *pContext, const cell_t *params) {
	int argc = params[0];
	if (argc != 1)
		return 0;

	char * str;
	CUtlVector<const char *> filenames(0, 1);
	CUtlVector<const char *> addedFiles(0, 1);

	pContext->LocalToString(params[1], &str);
	filenames.AddToTail(str);

	int added = AddStaticDownloads(filenames, addedFiles);
	if (added == 0)
		return 0;
	int sent = SendFiles(addedFiles);
	return sent;
}

const sp_nativeinfo_t g_Natives[] =
{
	{ "AddLateDownloads", AddLateDownloads },
	{ "AddLateDownload", AddLateDownload },
	{ NULL, NULL },
};

void CExtension::SDK_OnAllLoaded() {
	sharesys->AddNatives(myself, g_Natives);
	g_pOnDownloadSuccess = forwards->CreateForward("OnDownloadSuccess", ET_Ignore, 2, NULL, Param_Cell, Param_String);
	g_pOnDownloadFailure = forwards->CreateForward("OnDownloadFailure", ET_Ignore, 2, NULL, Param_Cell, Param_String);
	playerhelpers->AddClientListener(this);
}