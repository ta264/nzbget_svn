/*
 *  This file is part of nzbget
 *
 *  Copyright (C) 2004 Sven Henkel <sidddy@users.sourceforge.net>
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


#ifndef LOG_H
#define LOG_H

#include <deque>
#include <list>
#include <time.h>

#include "Thread.h"

void error(const char* msg, ...);
void warn(const char* msg, ...);
void info(const char* msg, ...);
void detail(const char* msg, ...);

#ifdef DEBUG
#ifdef HAVE_VARIADIC_MACROS
	void debug(const char* szFilename, const char* szFuncname, int iLineNr, const char* msg, ...);
#else
	void debug(const char* msg, ...);
#endif
#endif

class Message
{
public:
	enum EKind
	{
		mkInfo,
	    mkWarning,
	    mkError,
	    mkDebug,
	    mkDetail
	};

private:
	unsigned int		m_iID;
	EKind				m_eKind;
	time_t				m_tTime;
	char*				m_szText;

	friend class Log;

public:
						Message(unsigned int iID, EKind eKind, time_t tTime, const char* szText);
						~Message();
	unsigned int		GetID() { return m_iID; }
	EKind				GetKind() { return m_eKind; }
	time_t				GetTime() { return m_tTime; }
	const char*			GetText() { return m_szText; }
};

typedef std::deque<Message*> MessageListBase;

class MessageList: public MessageListBase
{
public:
						~MessageList();
	void				Clear();
};

class Debuggable
{
protected:
	virtual void		LogDebugInfo() = 0;
	friend class Log;
};

class Log
{
public:
	typedef std::list<Debuggable*>	Debuggables;

private:
	Mutex				m_mutexLog;
	MessageList			m_Messages;
	Debuggables			m_Debuggables;
	Mutex				m_mutexDebug;
	char*				m_szLogFilename;
	unsigned int		m_iIDGen;
	time_t				m_tLastWritten;
#ifdef DEBUG
	bool				m_bExtraDebug;
#endif

						Log();
						~Log();
	void				Filelog(const char* msg, ...);
	void				AddMessage(Message::EKind eKind, const char* szText);
	void				RotateLog();

	friend void error(const char* msg, ...);
	friend void warn(const char* msg, ...);
	friend void info(const char* msg, ...);
	friend void detail(const char* msg, ...);
#ifdef DEBUG
#ifdef HAVE_VARIADIC_MACROS
	friend void debug(const char* szFilename, const char* szFuncname, int iLineNr, const char* msg, ...);
#else	
	friend void debug(const char* msg, ...);
#endif
#endif
	
public:
	static void			Init();
	static void			Final();
	MessageList*		LockMessages();
	void				UnlockMessages();
	void				Clear();
	void				ResetLog();
	void				InitOptions();
	void				RegisterDebuggable(Debuggable* pDebuggable);
	void				UnregisterDebuggable(Debuggable* pDebuggable);
	void				LogDebugInfo();
};

#ifdef DEBUG
#ifdef HAVE_VARIADIC_MACROS
#define debug(...)   debug(__FILE__, FUNCTION_MACRO_NAME, __LINE__, __VA_ARGS__)
#endif
#else
#define debug(...)   do { } while(0)
#endif

extern Log* g_pLog;

#endif
