/*
 *  This file is part of nzbget
 *
 *  Copyright (C) 2007-2015 Andrey Prygunkov <hugbug@users.sourceforge.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * $Revision$
 * $Date$
 *
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef WIN32
#include "win32.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <sys/stat.h>

#include "nzbget.h"
#include "Scanner.h"
#include "Options.h"
#include "Log.h"
#include "QueueCoordinator.h"
#include "ScanScript.h"
#include "Util.h"

Scanner::FileData::FileData(const char* szFilename)
{
	m_szFilename = strdup(szFilename);
	m_iSize = 0;
	m_tLastChange = 0;
}

Scanner::FileData::~FileData()
{
	free(m_szFilename);
}


Scanner::QueueData::QueueData(const char* szFilename, const char* szNZBName, const char* szCategory,
	int iPriority, const char* szDupeKey, int iDupeScore, EDupeMode eDupeMode,
	NZBParameterList* pParameters, bool bAddTop, bool bAddPaused, NZBInfo* pUrlInfo,
	EAddStatus* pAddStatus, int* pNZBID)
{
	m_szFilename = strdup(szFilename);
	m_szNZBName = strdup(szNZBName);
	m_szCategory = strdup(szCategory ? szCategory : "");
	m_iPriority = iPriority;
	m_szDupeKey = strdup(szDupeKey ? szDupeKey : "");
	m_iDupeScore = iDupeScore;
	m_eDupeMode = eDupeMode;
	m_bAddTop = bAddTop;
	m_bAddPaused = bAddPaused;
	m_pUrlInfo = pUrlInfo;
	m_pAddStatus = pAddStatus;
	m_pNZBID = pNZBID;

	if (pParameters)
	{
		m_Parameters.CopyFrom(pParameters);
	}
}

Scanner::QueueData::~QueueData()
{
	free(m_szFilename);
	free(m_szNZBName);
	free(m_szCategory);
	free(m_szDupeKey);
}

void Scanner::QueueData::SetAddStatus(EAddStatus eAddStatus)
{
	if (m_pAddStatus)
	{
		*m_pAddStatus = eAddStatus;
	}
}

void Scanner::QueueData::SetNZBID(int iNZBID)
{
	if (m_pNZBID)
	{
		*m_pNZBID = iNZBID;
	}
}


Scanner::Scanner()
{
	debug("Creating Scanner");

	m_bRequestedNZBDirScan = false;
	m_bScanning = false;
	m_iNZBDirInterval = 0;
	m_iPass = 0;
	m_bScanScript = false;
}

Scanner::~Scanner()
{
	debug("Destroying Scanner");

	for (FileList::iterator it = m_FileList.begin(); it != m_FileList.end(); it++)
    {
        delete *it;
	}
	m_FileList.clear();

	ClearQueueList();
}

void Scanner::InitOptions()
{
	m_iNZBDirInterval = g_pOptions->GetNzbDirInterval() * 1000;
	const char* szScanScript = g_pOptions->GetScanScript();
	m_bScanScript = szScanScript && strlen(szScanScript) > 0;
}

void Scanner::ClearQueueList()
{
	for (QueueList::iterator it = m_QueueList.begin(); it != m_QueueList.end(); it++)
    {
        delete *it;
	}
	m_QueueList.clear();
}

void Scanner::Check()
{
	m_mutexScan.Lock();

	if (m_bRequestedNZBDirScan || 
		(!g_pOptions->GetPauseScan() && g_pOptions->GetNzbDirInterval() > 0 && 
		 m_iNZBDirInterval >= g_pOptions->GetNzbDirInterval() * 1000))
	{
		// check nzbdir every g_pOptions->GetNzbDirInterval() seconds or if requested
		bool bCheckStat = !m_bRequestedNZBDirScan;
		m_bRequestedNZBDirScan = false;
		m_bScanning = true;
		CheckIncomingNZBs(g_pOptions->GetNzbDir(), "", bCheckStat);
		if (!bCheckStat && m_bScanScript)
		{
			// if immediate scan requested, we need second scan to process files extracted by NzbProcess-script
			CheckIncomingNZBs(g_pOptions->GetNzbDir(), "", bCheckStat);
		}
		m_bScanning = false;
		m_iNZBDirInterval = 0;

		// if NzbDirFileAge is less than NzbDirInterval (that can happen if NzbDirInterval
		// is set for rare scans like once per hour) we make 4 scans:
		//   - one additional scan is neccessary to check sizes of detected files;
		//   - another scan is required to check files which were extracted by NzbProcess-script;
		//   - third scan is needed to check sizes of extracted files.
		if (g_pOptions->GetNzbDirInterval() > 0 && g_pOptions->GetNzbDirFileAge() < g_pOptions->GetNzbDirInterval())
		{
			int iMaxPass = m_bScanScript ? 3 : 1;
			if (m_iPass < iMaxPass)
			{
				// scheduling another scan of incoming directory in NzbDirFileAge seconds.
				m_iNZBDirInterval = (g_pOptions->GetNzbDirInterval() - g_pOptions->GetNzbDirFileAge()) * 1000;
				m_iPass++;
			}
			else
			{
				m_iPass = 0;
			}
		}

		DropOldFiles();
		ClearQueueList();
	}
	m_iNZBDirInterval += 200;

	m_mutexScan.Unlock();
}

/**
* Check if there are files in directory for incoming nzb-files
* and add them to download queue
*/
void Scanner::CheckIncomingNZBs(const char* szDirectory, const char* szCategory, bool bCheckStat)
{
	DirBrowser dir(szDirectory);
	while (const char* filename = dir.Next())
	{
		char fullfilename[1023 + 1]; // one char reserved for the trailing slash (if needed)
		snprintf(fullfilename, 1023, "%s%s", szDirectory, filename);
		fullfilename[1023 - 1] = '\0';
		bool bIsDirectory = Util::DirectoryExists(fullfilename);
		// check subfolders
		if (bIsDirectory && strcmp(filename, ".") && strcmp(filename, ".."))
		{
			fullfilename[strlen(fullfilename) + 1] = '\0';
			fullfilename[strlen(fullfilename)] = PATH_SEPARATOR;
			const char* szUseCategory = filename;
			char szSubCategory[1024];
			if (strlen(szCategory) > 0)
			{
				snprintf(szSubCategory, 1023, "%s%c%s", szCategory, PATH_SEPARATOR, filename);
				szSubCategory[1024 - 1] = '\0';
				szUseCategory = szSubCategory;
			}
			CheckIncomingNZBs(fullfilename, szUseCategory, bCheckStat);
		}
		else if (!bIsDirectory && CanProcessFile(fullfilename, bCheckStat))
		{
			ProcessIncomingFile(szDirectory, filename, fullfilename, szCategory);
		}
	}
}

/**
 * Only files which were not changed during last g_pOptions->GetNzbDirFileAge() seconds
 * can be processed. That prevents the processing of files, which are currently being
 * copied into nzb-directory (eg. being downloaded in web-browser).
 */
bool Scanner::CanProcessFile(const char* szFullFilename, bool bCheckStat)
{
	const char* szExtension = strrchr(szFullFilename, '.');
	if (!szExtension ||
		!strcasecmp(szExtension, ".queued") ||
		!strcasecmp(szExtension, ".error") ||
		!strcasecmp(szExtension, ".processed"))
	{
		return false;
	}

	if (!bCheckStat)
	{
		return true;
	}

	long long lSize = Util::FileSize(szFullFilename);
	time_t tCurrent = time(NULL);
	bool bCanProcess = false;
	bool bInList = false;

	for (FileList::iterator it = m_FileList.begin(); it != m_FileList.end(); it++)
    {
        FileData* pFileData = *it;
		if (!strcmp(pFileData->GetFilename(), szFullFilename))
		{
			bInList = true;
			if (pFileData->GetSize() == lSize &&
				tCurrent - pFileData->GetLastChange() >= g_pOptions->GetNzbDirFileAge())
			{
				bCanProcess = true;
				delete pFileData;
				m_FileList.erase(it);
			}
			else
			{
				pFileData->SetSize(lSize);
				if (pFileData->GetSize() != lSize)
				{
					pFileData->SetLastChange(tCurrent);
				}
			}
			break;
		}
	}

	if (!bInList)
	{
		FileData* pFileData = new FileData(szFullFilename);
		pFileData->SetSize(lSize);
		pFileData->SetLastChange(tCurrent);
		m_FileList.push_back(pFileData);
	}

	return bCanProcess;
}

/**
 * Remove old files from the list of monitored files.
 * Normally these files are deleted from the list when they are processed.
 * However if a file was detected by function "CanProcessFile" once but wasn't 
 * processed later (for example if the user deleted it), it will stay in the list,
 * until we remove it here.
 */
void Scanner::DropOldFiles()
{
	time_t tCurrent = time(NULL);

	int i = 0;
	for (FileList::iterator it = m_FileList.begin(); it != m_FileList.end(); )
    {
        FileData* pFileData = *it;
		if ((tCurrent - pFileData->GetLastChange() >= 
			(g_pOptions->GetNzbDirInterval() + g_pOptions->GetNzbDirFileAge()) * 2) ||
			// can occur if the system clock was adjusted
			tCurrent < pFileData->GetLastChange())
		{
			debug("Removing file %s from scan file list", pFileData->GetFilename());

			delete pFileData;
			m_FileList.erase(it);
			it = m_FileList.begin() + i;
		}
		else
		{
			it++;
			i++;
		}
	}
}

void Scanner::ProcessIncomingFile(const char* szDirectory, const char* szBaseFilename,
	const char* szFullFilename, const char* szCategory)
{
	const char* szExtension = strrchr(szBaseFilename, '.');
	if (!szExtension)
	{
		return;
	}

	char* szNZBName = strdup("");
	char* szNZBCategory = strdup(szCategory);
	NZBParameterList* pParameters = new NZBParameterList();
	int iPriority = 0;
	bool bAddTop = false;
	bool bAddPaused = false;
	char* szDupeKey = strdup("");
	int iDupeScore = 0;
	EDupeMode eDupeMode = dmScore;
	EAddStatus eAddStatus = asSkipped;
	bool bAdded = false;
	QueueData* pQueueData = NULL;
	NZBInfo* pUrlInfo = NULL;
	int iNZBID = 0;

	for (QueueList::iterator it = m_QueueList.begin(); it != m_QueueList.end(); it++)
    {
		QueueData* pQueueData1 = *it;
		if (Util::SameFilename(pQueueData1->GetFilename(), szFullFilename))
		{
			pQueueData = pQueueData1;
			free(szNZBName);
			szNZBName = strdup(pQueueData->GetNZBName());
			free(szNZBCategory);
			szNZBCategory = strdup(pQueueData->GetCategory());
			iPriority = pQueueData->GetPriority();
			free(szDupeKey);
			szDupeKey = strdup(pQueueData->GetDupeKey());
			iDupeScore = pQueueData->GetDupeScore();
			eDupeMode = pQueueData->GetDupeMode();
			bAddTop = pQueueData->GetAddTop();
			bAddPaused = pQueueData->GetAddPaused();
			pParameters->CopyFrom(pQueueData->GetParameters());
			pUrlInfo = pQueueData->GetUrlInfo();
		}
	}

	InitPPParameters(szNZBCategory, pParameters, false);

	bool bExists = true;

	if (m_bScanScript && strcasecmp(szExtension, ".nzb_processed"))
	{
		ScanScriptController::ExecuteScripts(szFullFilename, 
			pUrlInfo ? pUrlInfo->GetURL() : "", szDirectory,
			&szNZBName, &szNZBCategory, &iPriority, pParameters, &bAddTop,
			&bAddPaused, &szDupeKey, &iDupeScore, &eDupeMode);
		bExists = Util::FileExists(szFullFilename);
		if (bExists && strcasecmp(szExtension, ".nzb"))
		{
			char bakname2[1024];
			bool bRenameOK = Util::RenameBak(szFullFilename, "processed", false, bakname2, 1024);
			if (!bRenameOK)
			{
				char szSysErrStr[256];
				error("Could not rename file %s to %s: %s", szFullFilename, bakname2, Util::GetLastErrorMessage(szSysErrStr, sizeof(szSysErrStr)));
			}
		}
	}

	if (!strcasecmp(szExtension, ".nzb_processed"))
	{
		char szRenamedName[1024];
		bool bRenameOK = Util::RenameBak(szFullFilename, "nzb", true, szRenamedName, 1024);
		if (bRenameOK)
		{
			bAdded = AddFileToQueue(szRenamedName, szNZBName, szNZBCategory, iPriority,
				szDupeKey, iDupeScore, eDupeMode, pParameters, bAddTop, bAddPaused, pUrlInfo, &iNZBID);
		}
		else
		{
			char szSysErrStr[256];
			error("Could not rename file %s to %s: %s", szFullFilename, szRenamedName, Util::GetLastErrorMessage(szSysErrStr, sizeof(szSysErrStr)));
			eAddStatus = asFailed;
		}
	}
	else if (bExists && !strcasecmp(szExtension, ".nzb"))
	{
		bAdded = AddFileToQueue(szFullFilename, szNZBName, szNZBCategory, iPriority,
			szDupeKey, iDupeScore, eDupeMode, pParameters, bAddTop, bAddPaused, pUrlInfo, &iNZBID);
	}

	delete pParameters;

	free(szNZBName);
	free(szNZBCategory);
	free(szDupeKey);

	if (pQueueData)
	{
		pQueueData->SetAddStatus(eAddStatus == asFailed ? asFailed : bAdded ? asSuccess : asSkipped);
		pQueueData->SetNZBID(iNZBID);
	}
}

void Scanner::InitPPParameters(const char* szCategory, NZBParameterList* pParameters, bool bReset)
{
	bool bUnpack = g_pOptions->GetUnpack();
	const char* szPostScript = g_pOptions->GetPostScript();
	
	if (!Util::EmptyStr(szCategory))
	{
		Options::Category* pCategory = g_pOptions->FindCategory(szCategory, false);
		if (pCategory)
		{
			bUnpack = pCategory->GetUnpack();
			if (!Util::EmptyStr(pCategory->GetPostScript()))
			{
				szPostScript = pCategory->GetPostScript();
			}
		}
	}

	if (bReset)
	{
		for (ScriptConfig::Scripts::iterator it = g_pScriptConfig->GetScripts()->begin(); it != g_pScriptConfig->GetScripts()->end(); it++)
		{
			ScriptConfig::Script* pScript = *it;
			char szParam[1024];
			snprintf(szParam, 1024, "%s:", pScript->GetName());
			szParam[1024-1] = '\0';
			pParameters->SetParameter(szParam, NULL);
		}
	}

	pParameters->SetParameter("*Unpack:", bUnpack ? "yes" : "no");
	
	if (!Util::EmptyStr(szPostScript))
	{
		// split szPostScript into tokens and create pp-parameter for each token
		Tokenizer tok(szPostScript, ",;");
		while (const char* szScriptName = tok.Next())
		{
			char szParam[1024];
			snprintf(szParam, 1024, "%s:", szScriptName);
			szParam[1024-1] = '\0';
			pParameters->SetParameter(szParam, "yes");
		}
	}
}

bool Scanner::AddFileToQueue(const char* szFilename, const char* szNZBName, const char* szCategory,
	int iPriority, const char* szDupeKey, int iDupeScore, EDupeMode eDupeMode,
	NZBParameterList* pParameters, bool bAddTop, bool bAddPaused, NZBInfo* pUrlInfo, int* pNZBID)
{
	const char* szBasename = Util::BaseFileName(szFilename);

	info("Adding collection %s to queue", szBasename);

	NZBFile* pNZBFile = NZBFile::Create(szFilename, szCategory);
	bool bOK = pNZBFile != NULL;
	if (!bOK)
	{
		error("Could not add collection %s to queue", szBasename);
	}

	char bakname2[1024];
	if (!Util::RenameBak(szFilename, pNZBFile ? "queued" : "error", false, bakname2, 1024))
	{
		bOK = false;
		char szSysErrStr[256];
		error("Could not rename file %s to %s: %s", szFilename, bakname2, Util::GetLastErrorMessage(szSysErrStr, sizeof(szSysErrStr)));
	}

	if (bOK)
	{
		NZBInfo* pNZBInfo = pNZBFile->GetNZBInfo();
		pNZBInfo->SetQueuedFilename(bakname2);

		if (szNZBName && strlen(szNZBName) > 0)
		{
			pNZBInfo->SetName(NULL);
#ifdef WIN32
			char* szAnsiFilename = strdup(szNZBName);
			WebUtil::Utf8ToAnsi(szAnsiFilename, strlen(szAnsiFilename) + 1);
			pNZBInfo->SetFilename(szAnsiFilename);
			free(szAnsiFilename);
#else
			pNZBInfo->SetFilename(szNZBName);
#endif
			pNZBInfo->BuildDestDirName();
		}

		pNZBInfo->SetDupeKey(szDupeKey);
		pNZBInfo->SetDupeScore(iDupeScore);
		pNZBInfo->SetDupeMode(eDupeMode);
		pNZBInfo->SetPriority(iPriority);
		if (pUrlInfo)
		{
			pNZBInfo->SetURL(pUrlInfo->GetURL());
			pNZBInfo->SetUrlStatus(pUrlInfo->GetUrlStatus());
		}

		if (pNZBFile->GetPassword())
		{
			pNZBInfo->GetParameters()->SetParameter("*Unpack:Password", pNZBFile->GetPassword());
		}

		pNZBInfo->GetParameters()->CopyFrom(pParameters);

		for (::FileList::iterator it = pNZBInfo->GetFileList()->begin(); it != pNZBInfo->GetFileList()->end(); it++)
		{
			FileInfo* pFileInfo = *it;
			pFileInfo->SetPaused(bAddPaused);
		}

		g_pQueueCoordinator->AddNZBFileToQueue(pNZBFile, pUrlInfo, bAddTop);

		if (pNZBID)
		{
			*pNZBID = pNZBInfo->GetID();
		}
	}

	delete pNZBFile;

	return bOK;
}

void Scanner::ScanNZBDir(bool bSyncMode)
{
	m_mutexScan.Lock();
	m_bScanning = true;
	m_bRequestedNZBDirScan = true;
	m_mutexScan.Unlock();

	while (bSyncMode && (m_bScanning || m_bRequestedNZBDirScan))
	{
		usleep(100 * 1000);
	}
}

Scanner::EAddStatus Scanner::AddExternalFile(const char* szNZBName, const char* szCategory,
	int iPriority, const char* szDupeKey, int iDupeScore,  EDupeMode eDupeMode,
	NZBParameterList* pParameters, bool bAddTop, bool bAddPaused, NZBInfo* pUrlInfo,
	const char* szFileName, const char* szBuffer, int iBufSize, int* pNZBID)
{
	bool bNZB = false;
	char szTempFileName[1024];

	if (szFileName)
	{
		strncpy(szTempFileName, szFileName, 1024);
		szTempFileName[1024-1] = '\0';
	}
	else
	{
		int iNum = 1;
		while (iNum == 1 || Util::FileExists(szTempFileName))
		{
			snprintf(szTempFileName, 1024, "%snzb-%i.tmp", g_pOptions->GetTempDir(), iNum);
			szTempFileName[1024-1] = '\0';
			iNum++;
		}

		if (!Util::SaveBufferIntoFile(szTempFileName, szBuffer, iBufSize))
		{
			error("Could not create file %s", szTempFileName);
			return asFailed;
		}

		char buf[1024];
		strncpy(buf, szBuffer, 1024);
		buf[1024-1] = '\0';
		bNZB = !strncmp(buf, "<?xml", 5) && strstr(buf, "<nzb");
	}

	// move file into NzbDir, make sure the file name is unique
	char szValidNZBName[1024];
	strncpy(szValidNZBName, Util::BaseFileName(szNZBName), 1024);
	szValidNZBName[1024-1] = '\0';
	Util::MakeValidFilename(szValidNZBName, '_', false);

#ifdef WIN32
	WebUtil::Utf8ToAnsi(szValidNZBName, 1024);
#endif

	const char* szExtension = strrchr(szNZBName, '.');
	if (bNZB && (!szExtension || strcasecmp(szExtension, ".nzb")))
	{
		strncat(szValidNZBName, ".nzb", 1024 - strlen(szValidNZBName) - 1);
	}

	char szScanFileName[1024];
	snprintf(szScanFileName, 1024, "%s%s", g_pOptions->GetNzbDir(), szValidNZBName);

	char *szExt = strrchr(szValidNZBName, '.');
	if (szExt)
	{
		*szExt = '\0';
		szExt++;
	}

	int iNum = 2;
	while (Util::FileExists(szScanFileName))
	{
		if (szExt)
		{
			snprintf(szScanFileName, 1024, "%s%s_%i.%s", g_pOptions->GetNzbDir(), szValidNZBName, iNum, szExt);
		}
		else
		{
			snprintf(szScanFileName, 1024, "%s%s_%i", g_pOptions->GetNzbDir(), szValidNZBName, iNum);
		}
		szScanFileName[1024-1] = '\0';
		iNum++;
	}

	m_mutexScan.Lock();

	if (!Util::MoveFile(szTempFileName, szScanFileName))
	{
		char szSysErrStr[256];
		error("Could not move file %s to %s: %s", szTempFileName, szScanFileName, Util::GetLastErrorMessage(szSysErrStr, sizeof(szSysErrStr)));
		remove(szTempFileName);
		m_mutexScan.Unlock(); // UNLOCK
		return asFailed;
	}

	char* szUseCategory = strdup(szCategory ? szCategory : "");
	Options::Category *pCategory = g_pOptions->FindCategory(szUseCategory, true);
	if (pCategory && strcmp(szUseCategory, pCategory->GetName()))
	{
		free(szUseCategory);
		szUseCategory = strdup(pCategory->GetName());
		detail("Category %s matched to %s for %s", szCategory, szUseCategory, szNZBName);
	}

	EAddStatus eAddStatus = asSkipped;
	QueueData* pQueueData = new QueueData(szScanFileName, szNZBName, szUseCategory, iPriority,
		szDupeKey, iDupeScore, eDupeMode, pParameters, bAddTop, bAddPaused, pUrlInfo,
		&eAddStatus, pNZBID);
	free(szUseCategory);
	m_QueueList.push_back(pQueueData);

	m_mutexScan.Unlock();

	ScanNZBDir(true);

	return eAddStatus;
}
