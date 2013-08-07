/*
 * This file is part of nzbget
 *
 * Copyright (C) 2012-2013 Andrey Prygunkov <hugbug@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * $Revision$
 * $Date$
 *
 */

/*
 * In this module:
 *   1) Messages tab.
 */
 
/*** MESSAGES TAB *********************************************************************/
 
var Messages = (new function($)
{
	'use strict';
	
	// Controls
	var $MessagesTable;
	var $MessagesTabBadge;
	var $MessagesTabBadgeEmpty;
	var $MessagesRecordsPerPage;

	// State
	var messages;
	var maxMessages = null;
	var lastID = 0;
	var updateTabInfo;
	var notification = null;
	var curFilter = 'ALL';
	var activeTab = false;

	this.init = function(options)
	{
		updateTabInfo = options.updateTabInfo;
		
		$MessagesTable = $('#MessagesTable');
		$MessagesTabBadge = $('#MessagesTabBadge');
		$MessagesTabBadgeEmpty = $('#MessagesTabBadgeEmpty');
		$MessagesRecordsPerPage = $('#MessagesRecordsPerPage');

		var recordsPerPage = UISettings.read('MessagesRecordsPerPage', 10);
		$MessagesRecordsPerPage.val(recordsPerPage);

		$MessagesTable.fasttable(
			{
				filterInput: '#MessagesTable_filter',
				filterClearButton: '#MessagesTable_clearfilter',
				pagerContainer: '#MessagesTable_pager',
				infoContainer: '#MessagesTable_info',
				filterCaseSensitive: false,
				pageSize: recordsPerPage,
				maxPages: UISettings.miniTheme ? 1 : 5,
				pageDots: !UISettings.miniTheme,
				fillFieldsCallback: fillFieldsCallback,
				fillSearchCallback: fillSearchCallback,
				filterCallback: filterCallback,
				renderCellCallback: renderCellCallback,
				updateInfoCallback: updateInfo
			});
	}

	this.applyTheme = function()
	{
		$MessagesTable.fasttable('setPageSize', UISettings.read('MessagesRecordsPerPage', 10), 
			UISettings.miniTheme ? 1 : 5, !UISettings.miniTheme);
	}

	this.show = function()
	{
		activeTab = true;
		this.redraw();
	}
	
	this.hide = function()
	{
		activeTab = false;
	}
	
	this.update = function()
	{
		if (maxMessages === null)
		{
			maxMessages = parseInt(Options.option('LogBufferSize'));
			initFilterButtons();
		}
		
		if (lastID === 0)
		{
			RPC.call('log', [0, maxMessages], loaded);
		}
		else
		{
			RPC.call('log', [lastID+1, 0], loaded);
		}
	}

	function loaded(newMessages)
	{
		merge(newMessages);
		RPC.next();
	}
	
	function merge(newMessages)
	{
		if (lastID === 0)
		{
			messages = newMessages;
		}
		else
		{
			messages = messages.concat(newMessages);
			messages.splice(0, messages.length-maxMessages);
		}

		if (messages.length > 0)
		{
			lastID = messages[messages.length-1].ID;
		}
	}

	this.redraw = function()
	{
		var data = [];

		for (var i=0; i < messages.length; i++)
		{
			var message = messages[i];
			
			var item =
			{
				id: message.ID,
				message: message
			};

			data.unshift(item);
		}

		$MessagesTable.fasttable('update', data);

		Util.show($MessagesTabBadge, messages.length > 0);
		Util.show($MessagesTabBadgeEmpty, messages.length === 0 && UISettings.miniTheme);
	}

	function fillFieldsCallback(item)
	{
		var message = item.message;

		var kind;
		switch (message.Kind)
		{
			case 'INFO': kind = '<span class="label label-status label-success">info</span>'; break;
			case 'DETAIL': kind = '<span class="label label-status label-info">detail</span>'; break;
			case 'WARNING': kind = '<span class="label label-status label-warning">warning</span>'; break;
			case 'ERROR': kind = '<span class="label label-status label-important">error</span>'; break;
			case 'DEBUG': kind = '<span class="label label-status">debug</span>'; break;
		}

		var text = Util.textToHtml(message.Text);
		if (!item.time)
		{
			item.time = Util.formatDateTime(message.Time + UISettings.timeZoneCorrection*60*60);
		}

		if (!UISettings.miniTheme)
		{
			item.fields = [kind, item.time, text];
		}
		else
		{
			var info = kind + ' <span class="label">' + item.time + '</span> ' + text;
			item.fields = [info];
		}
	}

	function fillSearchCallback(item)
	{
		if (!item.time)
		{
			item.time = Util.formatDateTime(item.message.Time + UISettings.timeZoneCorrection*60*60);
		}

		item.search = item.message.Kind + ' ' + item.time + ' ' + item.message.Text;
	}

	function renderCellCallback(cell, index, item)
	{
		if (index === 1)
		{
			cell.className = 'text-center';
		}
	}
	
	function updateInfo(stat)
	{
		updateTabInfo($MessagesTabBadge, stat);
		if (activeTab)
		{
			updateFilterButtons();
		}
	}

	this.recordsPerPageChange = function()
	{
		var val = $MessagesRecordsPerPage.val();
		UISettings.write('MessagesRecordsPerPage', val);
		$MessagesTable.fasttable('setPageSize', val);
	}

	function filterCallback(item)
	{
		return !activeTab || curFilter === 'ALL' || item.message.Kind === curFilter;
	}

	function initFilterButtons()
	{
		var detail = ['both', 'screen'].indexOf(Options.option('DetailTarget')) > -1
		var info = ['both', 'screen'].indexOf(Options.option('InfoTarget')) > -1;
		var warning = ['both', 'screen'].indexOf(Options.option('WarningTarget')) > -1;
		var error = ['both', 'screen'].indexOf(Options.option('ErrorTarget')) > -1;
		Util.show($('#Messages_Badge_DETAIL, #Messages_Badge_DETAIL2').closest('.btn'), detail);
		Util.show($('#Messages_Badge_INFO, #Messages_Badge_INFO2 ').closest('.btn'), info);
		Util.show($('#Messages_Badge_WARNING, #Messages_Badge_WARNING2').closest('.btn'), warning);
		Util.show($('#Messages_Badge_ERROR, #Messages_Badge_ERROR2').closest('.btn'), error);
		Util.show($('#Messages_Badge_ALL, #Messages_Badge_ALL2').closest('.btn'), detail || info || warning || error);
	}
	
	function updateFilterButtons()
	{
		var countDebug = 0;
		var countDetail = 0;
		var countInfo = 0;
		var countWarning = 0;
		var countError = 0;

		var data = $MessagesTable.fasttable('availableContent');

		for (var i=0; i < data.length; i++)
		{
			var message = data[i].message;
			switch (message.Kind)
			{
				case 'INFO': countInfo++; break;
				case 'DETAIL': countDetail++; break;
				case 'WARNING': countWarning++; break;
				case 'ERROR': countError++; break;
				case 'DEBUG': countDebug++; break;
			}
		}
		$('#Messages_Badge_ALL,#Messages_Badge_ALL2').text(countDebug + countDetail + countInfo + countWarning + countError);
		$('#Messages_Badge_DETAIL,#Messages_Badge_DETAIL2').text(countDetail);
		$('#Messages_Badge_INFO,#Messages_Badge_INFO2').text(countInfo);
		$('#Messages_Badge_WARNING,#Messages_Badge_WARNING2').text(countWarning);
		$('#Messages_Badge_ERROR,#Messages_Badge_ERROR2').text(countError);

		$('#MessagesTab_Toolbar .btn').removeClass('btn-inverse');
		$('#Messages_Badge_' + curFilter + ',#Messages_Badge_' + curFilter + '2').closest('.btn').addClass('btn-inverse');
		$('#MessagesTab_Toolbar .badge').removeClass('badge-active');
		$('#Messages_Badge_' + curFilter + ',#Messages_Badge_' + curFilter + '2').addClass('badge-active');
	}

	this.filter = function(type)
	{
		curFilter = type;
		Messages.redraw();
	}

	this.clearClick = function()
	{
		ConfirmDialog.showModal('MessagesClearConfirmDialog', messagesClear);
	}

	function messagesClear()
	{
		Refresher.pause();

		RPC.call('clearlog', [], function()
		{
			RPC.call('writelog', ['INFO', 'Messages have been deleted'], function()
			{
				notification = '#Notif_Messages_Cleared';
				lastID = 0;
				editCompleted();
			});
		});
	}

	function editCompleted()
	{
		Refresher.update();
		if (notification)
		{
			Notification.show(notification);
			notification = null;
		}
	}
	
}(jQuery));
