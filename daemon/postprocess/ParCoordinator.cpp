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
#include <stdarg.h>
#include <ctype.h>
#ifdef WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "nzbget.h"
#include "ParCoordinator.h"
#include "ParParser.h"
#include "Options.h"
#include "DiskState.h"
#include "Log.h"
#include "Util.h"

#ifndef DISABLE_PARCHECK
bool ParCoordinator::PostParChecker::RequestMorePars(int iBlockNeeded, int* pBlockFound)
{
	return m_pOwner->RequestMorePars(m_pPostInfo->GetNZBInfo(), GetParFilename(), iBlockNeeded, pBlockFound);
}

void ParCoordinator::PostParChecker::UpdateProgress()
{
	m_pOwner->UpdateParCheckProgress();
}

void ParCoordinator::PostParChecker::PrintMessage(Message::EKind eKind, const char* szFormat, ...)
{
	char szText[1024];
	va_list args;
	va_start(args, szFormat);
	vsnprintf(szText, 1024, szFormat, args);
	va_end(args);
	szText[1024-1] = '\0';

	m_pPostInfo->GetNZBInfo()->AddMessage(eKind, szText);
}

void ParCoordinator::PostParChecker::RegisterParredFile(const char* szFilename)
{
	m_pPostInfo->GetParredFiles()->push_back(strdup(szFilename));
}

bool ParCoordinator::PostParChecker::IsParredFile(const char* szFilename)
{
	for (PostInfo::ParredFiles::iterator it = m_pPostInfo->GetParredFiles()->begin(); it != m_pPostInfo->GetParredFiles()->end(); it++)
	{
		const char* szParredFile = *it;
		if (!strcasecmp(szParredFile, szFilename))
		{
			return true;
		}
	}
	return false;
}

ParChecker::EFileStatus ParCoordinator::PostParChecker::FindFileCrc(const char* szFilename,
	unsigned long* lCrc, SegmentList* pSegments)
{
	CompletedFile* pCompletedFile = NULL;

	for (CompletedFiles::iterator it = m_pPostInfo->GetNZBInfo()->GetCompletedFiles()->begin(); it != m_pPostInfo->GetNZBInfo()->GetCompletedFiles()->end(); it++)
	{
		CompletedFile* pCompletedFile2 = *it;
		if (!strcasecmp(pCompletedFile2->GetFileName(), szFilename))
		{
			pCompletedFile = pCompletedFile2;
			break;
		}
	}
	if (!pCompletedFile)
	{
		return ParChecker::fsUnknown;
	}

	debug("Found completed file: %s, CRC: %.8x, Status: %i", Util::BaseFileName(pCompletedFile->GetFileName()), pCompletedFile->GetCrc(), (int)pCompletedFile->GetStatus());

	*lCrc = pCompletedFile->GetCrc();

	if (pCompletedFile->GetStatus() == CompletedFile::cfPartial && pCompletedFile->GetID() > 0 &&
		!m_pPostInfo->GetNZBInfo()->GetReprocess())
	{
		FileInfo* pTmpFileInfo = new FileInfo(pCompletedFile->GetID());

		if (!g_pDiskState->LoadFileState(pTmpFileInfo, NULL, true))
		{
			delete pTmpFileInfo;
			return ParChecker::fsUnknown;
		}

		for (FileInfo::Articles::iterator it = pTmpFileInfo->GetArticles()->begin(); it != pTmpFileInfo->GetArticles()->end(); it++)
		{
			ArticleInfo* pa = *it;
			ParChecker::Segment* pSegment = new Segment(pa->GetStatus() == ArticleInfo::aiFinished,
				pa->GetSegmentOffset(), pa->GetSegmentSize(), pa->GetCrc());
			pSegments->push_back(pSegment);
		}

		delete pTmpFileInfo;
	}

	return pCompletedFile->GetStatus() == CompletedFile::cfSuccess ? ParChecker::fsSuccess :
		pCompletedFile->GetStatus() == CompletedFile::cfFailure &&
			!m_pPostInfo->GetNZBInfo()->GetReprocess() ? ParChecker::fsFailure :
		pCompletedFile->GetStatus() == CompletedFile::cfPartial && pSegments->size() > 0 &&
			!m_pPostInfo->GetNZBInfo()->GetReprocess()? ParChecker::fsPartial :
		ParChecker::fsUnknown;
}

void ParCoordinator::PostParRenamer::UpdateProgress()
{
	m_pOwner->UpdateParRenameProgress();
}

void ParCoordinator::PostParRenamer::PrintMessage(Message::EKind eKind, const char* szFormat, ...)
{
	char szText[1024];
	va_list args;
	va_start(args, szFormat);
	vsnprintf(szText, 1024, szFormat, args);
	va_end(args);
	szText[1024-1] = '\0';
	
	m_pPostInfo->GetNZBInfo()->AddMessage(eKind, szText);
}

void ParCoordinator::PostParRenamer::RegisterParredFile(const char* szFilename)
{
	m_pPostInfo->GetParredFiles()->push_back(strdup(szFilename));
}

/**
 *  Update file name in the CompletedFiles-list of NZBInfo
 */
void ParCoordinator::PostParRenamer::RegisterRenamedFile(const char* szOldFilename, const char* szNewFileName)
{
	for (CompletedFiles::iterator it = m_pPostInfo->GetNZBInfo()->GetCompletedFiles()->begin(); it != m_pPostInfo->GetNZBInfo()->GetCompletedFiles()->end(); it++)
	{
		CompletedFile* pCompletedFile = *it;
		if (!strcasecmp(pCompletedFile->GetFileName(), szOldFilename))
		{
			pCompletedFile->SetFileName(szNewFileName);
			break;
		}
	}
}

#endif

ParCoordinator::ParCoordinator()
{
	debug("Creating ParCoordinator");

#ifndef DISABLE_PARCHECK
	m_bStopped = false;
	m_ParChecker.m_pOwner = this;
	m_ParRenamer.m_pOwner = this;
#endif
}

ParCoordinator::~ParCoordinator()
{
	debug("Destroying ParCoordinator");
}

#ifndef DISABLE_PARCHECK
void ParCoordinator::Stop()
{
	debug("Stopping ParCoordinator");

	m_bStopped = true;

	if (m_ParChecker.IsRunning())
	{
		m_ParChecker.Stop();
		int iMSecWait = 5000;
		while (m_ParChecker.IsRunning() && iMSecWait > 0)
		{
			usleep(50 * 1000);
			iMSecWait -= 50;
		}
		if (m_ParChecker.IsRunning())
		{
			warn("Terminating par-check for %s", m_ParChecker.GetInfoName());
			m_ParChecker.Kill();
		}
	}
}
#endif

void ParCoordinator::PausePars(DownloadQueue* pDownloadQueue, NZBInfo* pNZBInfo)
{
	debug("ParCoordinator: Pausing pars");

	pDownloadQueue->EditEntry(pNZBInfo->GetID(), 
		DownloadQueue::eaGroupPauseExtraPars, 0, NULL);
}

#ifndef DISABLE_PARCHECK

/**
 * DownloadQueue must be locked prior to call of this function.
 */
void ParCoordinator::StartParCheckJob(PostInfo* pPostInfo)
{
	m_eCurrentJob = jkParCheck;
	m_ParChecker.SetPostInfo(pPostInfo);
	m_ParChecker.SetDestDir(pPostInfo->GetNZBInfo()->GetDestDir());
	m_ParChecker.SetNZBName(pPostInfo->GetNZBInfo()->GetName());
	m_ParChecker.SetParTime(time(NULL));
	m_ParChecker.SetDownloadSec(pPostInfo->GetNZBInfo()->GetDownloadSec());
	m_ParChecker.SetParQuick(g_pOptions->GetParQuick() && !pPostInfo->GetForceParFull());
	m_ParChecker.SetForceRepair(pPostInfo->GetForceRepair());
	m_ParChecker.PrintMessage(Message::mkInfo, "Checking pars for %s", pPostInfo->GetNZBInfo()->GetName());
	pPostInfo->SetWorking(true);
	m_ParChecker.Start();
}

/**
 * DownloadQueue must be locked prior to call of this function.
 */
void ParCoordinator::StartParRenameJob(PostInfo* pPostInfo)
{
	const char* szDestDir = pPostInfo->GetNZBInfo()->GetDestDir();

	char szFinalDir[1024];
	if (pPostInfo->GetNZBInfo()->GetUnpackStatus() == NZBInfo::usSuccess)
	{
		pPostInfo->GetNZBInfo()->BuildFinalDirName(szFinalDir, 1024);
		szFinalDir[1024-1] = '\0';
		szDestDir = szFinalDir;
	}

	m_eCurrentJob = jkParRename;
	m_ParRenamer.SetPostInfo(pPostInfo);
	m_ParRenamer.SetDestDir(szDestDir);
	m_ParRenamer.SetInfoName(pPostInfo->GetNZBInfo()->GetName());
	m_ParRenamer.SetDetectMissing(pPostInfo->GetNZBInfo()->GetUnpackStatus() == NZBInfo::usNone);
	m_ParRenamer.PrintMessage(Message::mkInfo, "Checking renamed files for %s", pPostInfo->GetNZBInfo()->GetName());
	pPostInfo->SetWorking(true);
	m_ParRenamer.Start();
}

bool ParCoordinator::Cancel()
{
	if (m_eCurrentJob == jkParCheck)
	{
		if (!m_ParChecker.GetCancelled())
		{
			debug("Cancelling par-repair for %s", m_ParChecker.GetInfoName());
			m_ParChecker.Cancel();
			return true;
		}
	}
	else if (m_eCurrentJob == jkParRename)
	{
		if (!m_ParRenamer.GetCancelled())
		{
			debug("Cancelling par-rename for %s", m_ParRenamer.GetInfoName());
			m_ParRenamer.Cancel();
			return true;
		}
	}
	return false;
}

/**
 * DownloadQueue must be locked prior to call of this function.
 */
bool ParCoordinator::AddPar(FileInfo* pFileInfo, bool bDeleted)
{
	bool bSameCollection = m_ParChecker.IsRunning() &&
		pFileInfo->GetNZBInfo() == m_ParChecker.GetPostInfo()->GetNZBInfo();
	if (bSameCollection && !bDeleted)
	{
		char szFullFilename[1024];
		snprintf(szFullFilename, 1024, "%s%c%s", pFileInfo->GetNZBInfo()->GetDestDir(), (int)PATH_SEPARATOR, pFileInfo->GetFilename());
		szFullFilename[1024-1] = '\0';
		m_ParChecker.AddParFile(szFullFilename);
	}
	else
	{
		m_ParChecker.QueueChanged();
	}
	return bSameCollection;
}

void ParCoordinator::ParCheckCompleted()
{
	DownloadQueue* pDownloadQueue = DownloadQueue::Lock();

	PostInfo* pPostInfo = m_ParChecker.GetPostInfo();

	// Update ParStatus (accumulate result)
	if ((m_ParChecker.GetStatus() == ParChecker::psRepaired ||
		m_ParChecker.GetStatus() == ParChecker::psRepairNotNeeded) &&
		pPostInfo->GetNZBInfo()->GetParStatus() <= NZBInfo::psSkipped)
	{
		pPostInfo->GetNZBInfo()->SetParStatus(NZBInfo::psSuccess);
		pPostInfo->SetParRepaired(m_ParChecker.GetStatus() == ParChecker::psRepaired);
	}
	else if (m_ParChecker.GetStatus() == ParChecker::psRepairPossible &&
		pPostInfo->GetNZBInfo()->GetParStatus() != NZBInfo::psFailure)
	{
		pPostInfo->GetNZBInfo()->SetParStatus(NZBInfo::psRepairPossible);
	}
	else
	{
		pPostInfo->GetNZBInfo()->SetParStatus(NZBInfo::psFailure);
	}

	int iWaitTime = pPostInfo->GetNZBInfo()->GetDownloadSec() - m_ParChecker.GetDownloadSec();
	pPostInfo->SetStartTime(pPostInfo->GetStartTime() + (time_t)iWaitTime);
	int iParSec = (int)(time(NULL) - m_ParChecker.GetParTime()) - iWaitTime;
	pPostInfo->GetNZBInfo()->SetParSec(pPostInfo->GetNZBInfo()->GetParSec() + iParSec);

	pPostInfo->GetNZBInfo()->SetParFull(m_ParChecker.GetParFull());

	pPostInfo->SetWorking(false);
	pPostInfo->SetStage(PostInfo::ptQueued);

	pDownloadQueue->Save();

	DownloadQueue::Unlock();
}

/**
* Unpause par2-files
* returns true, if the files with required number of blocks were unpaused,
* or false if there are no more files in queue for this collection or not enough blocks
*/
bool ParCoordinator::RequestMorePars(NZBInfo* pNZBInfo, const char* szParFilename, int iBlockNeeded, int* pBlockFound)
{
	DownloadQueue* pDownloadQueue = DownloadQueue::Lock();
	
	Blocks blocks;
	blocks.clear();
	int iBlockFound = 0;
    int iCurBlockFound = 0;

	FindPars(pDownloadQueue, pNZBInfo, szParFilename, &blocks, true, true, &iCurBlockFound);
    iBlockFound += iCurBlockFound;
	if (iBlockFound < iBlockNeeded)
	{
		FindPars(pDownloadQueue, pNZBInfo, szParFilename, &blocks, true, false, &iCurBlockFound);
        iBlockFound += iCurBlockFound;
	}
	if (iBlockFound < iBlockNeeded)
	{
		FindPars(pDownloadQueue, pNZBInfo, szParFilename, &blocks, false, false, &iCurBlockFound);
        iBlockFound += iCurBlockFound;
	}

	if (iBlockFound >= iBlockNeeded)
	{
		// 1. first unpause all files with par-blocks less or equal iBlockNeeded
		// starting from the file with max block count.
		// if par-collection was built exponentially and all par-files present,
		// this step selects par-files with exact number of blocks we need.
		while (iBlockNeeded > 0)
		{               
			BlockInfo* pBestBlockInfo = NULL;
			for (Blocks::iterator it = blocks.begin(); it != blocks.end(); it++)
			{
				BlockInfo* pBlockInfo = *it;
				if (pBlockInfo->m_iBlockCount <= iBlockNeeded &&
				   (!pBestBlockInfo || pBestBlockInfo->m_iBlockCount < pBlockInfo->m_iBlockCount))
				{
					pBestBlockInfo = pBlockInfo;
				}
			}
			if (pBestBlockInfo)
			{
				if (pBestBlockInfo->m_pFileInfo->GetPaused())
				{
					m_ParChecker.PrintMessage(Message::mkInfo, "Unpausing %s%c%s for par-recovery", pNZBInfo->GetName(), (int)PATH_SEPARATOR, pBestBlockInfo->m_pFileInfo->GetFilename());
					pBestBlockInfo->m_pFileInfo->SetPaused(false);
					pBestBlockInfo->m_pFileInfo->SetExtraPriority(true);
				}
				iBlockNeeded -= pBestBlockInfo->m_iBlockCount;
				blocks.remove(pBestBlockInfo);
				delete pBestBlockInfo;
			}
			else
			{
				break;
			}
		}
			
		// 2. then unpause other files
		// this step only needed if the par-collection was built not exponentially 
		// or not all par-files present (or some of them were corrupted)
		// this step is not optimal, but we hope, that the first step will work good 
		// in most cases and we will not need the second step often
		while (iBlockNeeded > 0)
		{
			BlockInfo* pBlockInfo = blocks.front();
			if (pBlockInfo->m_pFileInfo->GetPaused())
			{
				m_ParChecker.PrintMessage(Message::mkInfo, "Unpausing %s%c%s for par-recovery", pNZBInfo->GetName(), (int)PATH_SEPARATOR, pBlockInfo->m_pFileInfo->GetFilename());
				pBlockInfo->m_pFileInfo->SetPaused(false);
				pBlockInfo->m_pFileInfo->SetExtraPriority(true);
			}
			iBlockNeeded -= pBlockInfo->m_iBlockCount;
		}
	}

	DownloadQueue::Unlock();

	if (pBlockFound)
	{
		*pBlockFound = iBlockFound;
	}

	for (Blocks::iterator it = blocks.begin(); it != blocks.end(); it++)
	{
		delete *it;
	}
	blocks.clear();

	bool bOK = iBlockNeeded <= 0;

	return bOK;
}

void ParCoordinator::FindPars(DownloadQueue* pDownloadQueue, NZBInfo* pNZBInfo, const char* szParFilename,
	Blocks* pBlocks, bool bStrictParName, bool bExactParName, int* pBlockFound)
{
    *pBlockFound = 0;
	
	// extract base name from m_szParFilename (trim .par2-extension and possible .vol-part)
	char* szBaseParFilename = Util::BaseFileName(szParFilename);
	char szMainBaseFilename[1024];
	int iMainBaseLen = 0;
	if (!ParParser::ParseParFilename(szBaseParFilename, &iMainBaseLen, NULL))
	{
		// should not happen
        pNZBInfo->PrintMessage(Message::mkError, "Internal error: could not parse filename %s", szBaseParFilename);
		return;
	}
	int maxlen = iMainBaseLen < 1024 ? iMainBaseLen : 1024 - 1;
	strncpy(szMainBaseFilename, szBaseParFilename, maxlen);
	szMainBaseFilename[maxlen] = '\0';
	for (char* p = szMainBaseFilename; *p; p++) *p = tolower(*p); // convert string to lowercase

	for (FileList::iterator it = pNZBInfo->GetFileList()->begin(); it != pNZBInfo->GetFileList()->end(); it++)
	{
		FileInfo* pFileInfo = *it;
		int iBlocks = 0;
		if (ParParser::ParseParFilename(pFileInfo->GetFilename(), NULL, &iBlocks) &&
			iBlocks > 0)
		{
			bool bUseFile = true;

			if (bExactParName)
			{
				bUseFile = ParParser::SameParCollection(pFileInfo->GetFilename(), Util::BaseFileName(szParFilename));
			}
			else if (bStrictParName)
			{
				// the pFileInfo->GetFilename() may be not confirmed and may contain
				// additional texts if Subject could not be parsed correctly

				char szLoFileName[1024];
				strncpy(szLoFileName, pFileInfo->GetFilename(), 1024);
				szLoFileName[1024-1] = '\0';
				for (char* p = szLoFileName; *p; p++) *p = tolower(*p); // convert string to lowercase
				
				char szCandidateFileName[1024];
				snprintf(szCandidateFileName, 1024, "%s.par2", szMainBaseFilename);
				szCandidateFileName[1024-1] = '\0';
				if (!strstr(szLoFileName, szCandidateFileName))
				{
					snprintf(szCandidateFileName, 1024, "%s.vol", szMainBaseFilename);
					szCandidateFileName[1024-1] = '\0';
					bUseFile = strstr(szLoFileName, szCandidateFileName);
				}
			}

            bool bAlreadyAdded = false;
            // check if file is not in the list already
			if (bUseFile)
			{
				for (Blocks::iterator it = pBlocks->begin(); it != pBlocks->end(); it++)
				{
					BlockInfo* pBlockInfo = *it;
					if (pBlockInfo->m_pFileInfo == pFileInfo)
					{
						bAlreadyAdded = true;
						break;
                	}
        		}
			}
                
			// if it is a par2-file with blocks and it was from the same NZB-request
			// and it belongs to the same file collection (same base name),
			// then OK, we can use it
            if (bUseFile && !bAlreadyAdded)
            {
				BlockInfo* pBlockInfo = new BlockInfo();
				pBlockInfo->m_pFileInfo = pFileInfo;
				pBlockInfo->m_iBlockCount = iBlocks;
				pBlocks->push_back(pBlockInfo);
				*pBlockFound += iBlocks;
			}
		}
	}
}

void ParCoordinator::UpdateParCheckProgress()
{
	DownloadQueue::Lock();

	PostInfo* pPostInfo = m_ParChecker.GetPostInfo();
	if (m_ParChecker.GetFileProgress() == 0)
	{
		pPostInfo->SetProgressLabel(m_ParChecker.GetProgressLabel());
	}
	pPostInfo->SetFileProgress(m_ParChecker.GetFileProgress());
	pPostInfo->SetStageProgress(m_ParChecker.GetStageProgress());
    PostInfo::EStage StageKind[] = { PostInfo::ptLoadingPars, PostInfo::ptVerifyingSources, PostInfo::ptRepairing, PostInfo::ptVerifyingRepaired };
	PostInfo::EStage eStage = StageKind[m_ParChecker.GetStage()];
	time_t tCurrent = time(NULL);

	if (pPostInfo->GetStage() != eStage)
	{
		pPostInfo->SetStage(eStage);
		pPostInfo->SetStageTime(tCurrent);
		if (pPostInfo->GetStage() == PostInfo::ptRepairing)
		{
			m_ParChecker.SetRepairTime(tCurrent);
		}
		else if (pPostInfo->GetStage() == PostInfo::ptVerifyingRepaired)
		{
			int iRepairSec = (int)(tCurrent - m_ParChecker.GetRepairTime());
			pPostInfo->GetNZBInfo()->SetRepairSec(pPostInfo->GetNZBInfo()->GetRepairSec() + iRepairSec);
		}
	}

	bool bParCancel = false;
	if (!m_ParChecker.GetCancelled())
	{
		if ((g_pOptions->GetParTimeLimit() > 0) &&
			m_ParChecker.GetStage() == ParChecker::ptRepairing &&
			((g_pOptions->GetParTimeLimit() > 5 && tCurrent - pPostInfo->GetStageTime() > 5 * 60) ||
			(g_pOptions->GetParTimeLimit() <= 5 && tCurrent - pPostInfo->GetStageTime() > 1 * 60)))
		{
			// first five (or one) minutes elapsed, now can check the estimated time
			int iEstimatedRepairTime = (int)((tCurrent - pPostInfo->GetStartTime()) * 1000 / 
				(pPostInfo->GetStageProgress() > 0 ? pPostInfo->GetStageProgress() : 1));
			if (iEstimatedRepairTime > g_pOptions->GetParTimeLimit() * 60)
			{
				debug("Estimated repair time %i seconds", iEstimatedRepairTime);
				m_ParChecker.PrintMessage(Message::mkWarning, "Cancelling par-repair for %s, estimated repair time (%i minutes) exceeds allowed repair time", m_ParChecker.GetInfoName(), iEstimatedRepairTime / 60);
				bParCancel = true;
			}
		}
	}

	if (bParCancel)
	{
		m_ParChecker.Cancel();
	}

	DownloadQueue::Unlock();
	
	CheckPauseState(pPostInfo);
}

void ParCoordinator::CheckPauseState(PostInfo* pPostInfo)
{
	if (g_pOptions->GetPausePostProcess() && !pPostInfo->GetNZBInfo()->GetForcePriority())
	{
		time_t tStageTime = pPostInfo->GetStageTime();
		time_t tStartTime = pPostInfo->GetStartTime();
		time_t tParTime = m_ParChecker.GetParTime();
		time_t tRepairTime = m_ParChecker.GetRepairTime();
		time_t tWaitTime = time(NULL);
		
		// wait until Post-processor is unpaused
		while (g_pOptions->GetPausePostProcess() && !pPostInfo->GetNZBInfo()->GetForcePriority() && !m_bStopped)
		{
			usleep(50 * 1000);
			
			// update time stamps
			
			time_t tDelta = time(NULL) - tWaitTime;
			
			if (tStageTime > 0)
			{
				pPostInfo->SetStageTime(tStageTime + tDelta);
			}
			if (tStartTime > 0)
			{
				pPostInfo->SetStartTime(tStartTime + tDelta);
			}
			if (tParTime > 0)
			{
				m_ParChecker.SetParTime(tParTime + tDelta);
			}
			if (tRepairTime > 0)
			{
				m_ParChecker.SetRepairTime(tRepairTime + tDelta);
			}
		}
	}
}

void ParCoordinator::ParRenameCompleted()
{
	DownloadQueue* pDownloadQueue = DownloadQueue::Lock();
	
	PostInfo* pPostInfo = m_ParRenamer.GetPostInfo();
	pPostInfo->GetNZBInfo()->SetRenameStatus(m_ParRenamer.GetStatus() == ParRenamer::psSuccess ? NZBInfo::rsSuccess : NZBInfo::rsFailure);

	if (m_ParRenamer.HasMissedFiles() && pPostInfo->GetNZBInfo()->GetParStatus() <= NZBInfo::psSkipped)
	{
		m_ParRenamer.PrintMessage(Message::mkInfo, "Requesting par-check/repair for %s to restore missing files ", m_ParRenamer.GetInfoName());
		pPostInfo->SetRequestParCheck(true);
	}

	pPostInfo->SetWorking(false);
	pPostInfo->SetStage(PostInfo::ptQueued);
	
	pDownloadQueue->Save();

	DownloadQueue::Unlock();
}

void ParCoordinator::UpdateParRenameProgress()
{
	DownloadQueue::Lock();
	
	PostInfo* pPostInfo = m_ParRenamer.GetPostInfo();
	pPostInfo->SetProgressLabel(m_ParRenamer.GetProgressLabel());
	pPostInfo->SetStageProgress(m_ParRenamer.GetStageProgress());
	time_t tCurrent = time(NULL);
	
	if (pPostInfo->GetStage() != PostInfo::ptRenaming)
	{
		pPostInfo->SetStage(PostInfo::ptRenaming);
		pPostInfo->SetStageTime(tCurrent);
	}
	
	DownloadQueue::Unlock();
	
	CheckPauseState(pPostInfo);
}

#endif
