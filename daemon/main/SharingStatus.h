#ifndef SHARINGSTATUS_H
#define SHARINGSTATUS_H

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <time.h>

#ifdef WIN32
#include "win32.h"
#endif

#include "nzbget.h"
#include "DownloadInfo.h"
#include "WebDownloader.h"
#include "Util.h"
#include "Log.h"


class SharingStatus
{
private:
	std::string m_szMyName;
	std::string m_szStatusUrl;
	std::string m_szTempDir;
	std::string readUrl(std::string szUrl);

	bool m_bPollResume;
	time_t m_tLastPoll;
	double m_dPollInterval;
	
	bool Pause();
	bool TryResume();
public:
	SharingStatus(char* szMyName, char* szStatusUrl, char* szTempDir, int iPollInterval);
	~SharingStatus();
	bool ChangePauseState(bool bCurrentPauseState, bool bWantedPauseState);
	bool CheckPauseState(bool bCurrentlyPaused, bool bHasJob);
};

#endif