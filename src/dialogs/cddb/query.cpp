 /* fre:ac - free audio converter
  * Copyright (C) 2001-2018 Robert Kausch <robert.kausch@freac.org>
  *
  * This program is free software; you can redistribute it and/or
  * modify it under the terms of the GNU General Public License as
  * published by the Free Software Foundation, either version 2 of
  * the License, or (at your option) any later version.
  *
  * THIS PACKAGE IS PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR
  * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
  * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE. */

#include <dialogs/cddb/query.h>
#include <dialogs/cddb/multimatch.h>

#include <config.h>
#include <dllinterfaces.h>
#include <utilities.h>
#include <resources.h>

#include <cddb/cddblocal.h>
#include <cddb/cddbremote.h>

#include <cddb/cddbcache.h>
#include <cddb/cddbbatch.h>

#ifdef __WIN32__
#	include <smooth/init.win32.h>
#endif

using namespace smooth::GUI::Dialogs;

freac::cddbQueryDlg::cddbQueryDlg(const String &iQueryString)
{
	BoCA::Config	*config = BoCA::Config::Get();
	BoCA::I18n	*i18n	= BoCA::I18n::Get();

	i18n->SetContext("CDDB::Query");

	errorState	 = False;

	queryThread	 = NIL;
	queryString	 = iQueryString;

	stopQueryThread	 = False;

	mainWnd		 = new Window(i18n->TranslateString("CDDB query"), Point(config->GetIntValue(Config::CategorySettingsID, Config::SettingsWindowPosXID, Config::SettingsWindowPosXDefault), config->GetIntValue(Config::CategorySettingsID, Config::SettingsWindowPosYID, Config::SettingsWindowPosYDefault)) + Point(40, 40), Size(310, 84));
	mainWnd->SetRightToLeft(i18n->IsActiveLanguageRightToLeft());

	mainWnd_titlebar = new Titlebar(TB_CLOSEBUTTON);

	text_status	 = new Text(NIL, Point(7, 5));
	prog_status	 = new Progressbar(Point(7, 24), Size(200, 0), OR_HORZ, PB_NOTEXT, 0, 100, 0);

	btn_cancel	 = new Button(i18n->TranslateString("Cancel"), NIL, Point(215, 23), Size());
	btn_cancel->onAction.Connect(&cddbQueryDlg::Cancel, this);

	Add(mainWnd);

	mainWnd->Add(btn_cancel);
	mainWnd->Add(text_status);
	mainWnd->Add(prog_status);
	mainWnd->Add(mainWnd_titlebar);

	mainWnd->SetFlags(mainWnd->GetFlags() | WF_NOTASKBUTTON | WF_MODAL);
	mainWnd->SetIcon(ImageLoader::Load(String(Config::Get()->resourcesPath).Append("icons/freac.png")));

#ifdef __WIN32__
	mainWnd->SetIconDirect(LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
#endif
}

freac::cddbQueryDlg::~cddbQueryDlg()
{
	DeleteObject(mainWnd_titlebar);
	DeleteObject(mainWnd);
	DeleteObject(text_status);
	DeleteObject(prog_status);
	DeleteObject(btn_cancel);
}

const Error &freac::cddbQueryDlg::ShowDialog()
{
	mainWnd->Show();

	queryThread = NonBlocking0<>(&cddbQueryDlg::QueryThread, this).Call();

	mainWnd->WaitUntilClosed();

	queryThread->Wait();

	return error;
}

Void freac::cddbQueryDlg::Cancel()
{
	if (queryThread == NIL) return;

	mainWnd->Close();

	stopQueryThread = True;

	/* Wait up to one second for thread to finish.
	 */
	for (Int i = 0; i < 100; i++)
	{
		if (queryThread->GetStatus() != Threads::THREAD_RUNNING) return;

		S::System::System::Sleep(10);
	}

	/* Kill thead if it did not finish after one second.
	 */
	queryThread->Stop();
}

Int freac::cddbQueryDlg::QueryThread()
{
	BoCA::Config	*config = BoCA::Config::Get();

	Bool	 result = False;

	if (config->GetIntValue(Config::CategoryFreedbID, Config::FreedbEnableLocalID, Config::FreedbEnableLocalDefault))
	{
		CDDBLocal	 cddbLocal;

		result = Query(cddbLocal);
	}

	if (!result && config->GetIntValue(Config::CategoryFreedbID, Config::FreedbEnableRemoteID, Config::FreedbEnableRemoteDefault))
	{
		CDDBRemote	 cddbRemote;

		result = Query(cddbRemote);
	}

	mainWnd->Close();

	if (result) return Success();
	else	    return Error();
}

Bool freac::cddbQueryDlg::Query(CDDB &cddb)
{
	BoCA::Config	*config = BoCA::Config::Get();
	BoCA::I18n	*i18n	= BoCA::I18n::Get();

	i18n->SetContext("CDDB::Query");

	errorState  = False;
	errorString = NIL;

	Int	 result;

	prog_status->SetValue(0);
	text_status->SetText(i18n->AddEllipsis(i18n->TranslateString("Connecting to freedb server at").Append(" ").Append(config->GetStringValue(Config::CategoryFreedbID, Config::FreedbServerID, Config::FreedbServerDefault))));

	cddb.ConnectToServer();

	if (stopQueryThread) { cddb.CloseConnection(); return False; }

	prog_status->SetValue(20);
	text_status->SetText(i18n->AddEllipsis(i18n->TranslateString("Requesting CD information")));

	/* Perform query using query string.
	 */
	result = cddb.Query(queryString);

	if (stopQueryThread) { cddb.CloseConnection(); return False; }

	/* Process result.
	 */
	String	 category;
	Int	 discID	= 0;
	Bool	 fuzzy	= False;

	if (result == QUERY_RESULT_NONE)
	{
		errorState  = True;
		errorString = i18n->TranslateString("%1:", "Characters").Replace("%1", queryString.SubString(11, 8)).Append(" ").Append(i18n->TranslateString("No freedb entry for this disk.")).Append("\n\n").Append(queryString);
	}
	else if (result == QUERY_RESULT_SINGLE)
	{
		category = cddb.GetNthCategory(0);
		discID	 = cddb.GetNthDiscID(0);
	}
	else if (result == QUERY_RESULT_MULTIPLE || result == QUERY_RESULT_FUZZY)
	{
		if (result == QUERY_RESULT_FUZZY) fuzzy = True;

		if (config->GetIntValue(Config::CategoryFreedbID, Config::FreedbAutoSelectID, Config::FreedbAutoSelectDefault))
		{
			category = cddb.GetNthCategory(0);
			discID	 = cddb.GetNthDiscID(0);
		}
		else
		{
			cddbMultiMatchDlg	 dlg(cddb, fuzzy);

			for (Int i = 0; i < cddb.GetNumberOfMatches(); i++) dlg.AddEntry(cddb.GetNthCategory(i), cddb.GetNthTitle(i), cddb.GetNthDiscID(i));

			if (fuzzy) dlg.AddEntry(i18n->TranslateString("none"), NIL, 0);

			if (dlg.ShowDialog() == Success())
			{
				Int	 index = dlg.GetSelectedEntryNumber();

				if (index < cddb.GetNumberOfMatches() && index >= 0)
				{
					category = cddb.GetNthCategory(index);
					discID	 = cddb.GetNthDiscID(index);
				}
			}
		}
	}

	if (stopQueryThread) { cddb.CloseConnection(); return False; }

	/* Read actual CDDB data.
	 */
	Bool	 readError = False;

	if (category != NIL && discID != 0)
	{
		prog_status->SetValue(60);

		if (!cddb.Read(category, discID, cddbInfo)) readError = True;

		/* Update track offsets and disc ID when we had a fuzzy match.
		 */
		if (fuzzy)
		{
			cddbInfo.revision = -1;

			cddbInfo.UpdateFromQueryString(queryString);
		}
	}

	if (stopQueryThread) { cddbInfo = NIL; cddb.CloseConnection(); return False; }

	/* Process read errors.
	 */
	if (readError || result == QUERY_RESULT_ERROR)
	{
		errorState  = True;
		errorString = i18n->TranslateString("Some error occurred trying to connect to the freedb server.", "CDDB::Query::Errors");
		error	    = Error();
	}

	cddb.CloseConnection();

	prog_status->SetValue(100);
	text_status->SetText(NIL);

	if (category == NIL || discID == 0) return False;
	else				    return True;
}

freac::CDDBInfo freac::cddbQueryDlg::QueryCDDB(const BoCA::Track &track)
{
	const BoCA::Config	*config = BoCA::Config::Get();
	BoCA::I18n		*i18n	= BoCA::I18n::Get();

	String		 queryString = CDDB::QueryStringFromMCDI(track.GetInfo().mcdi);
	CDDBInfo	 cdInfo;

	/* Query CDDB cache first.
	 */
	if (config->GetIntValue(Config::CategoryFreedbID, Config::FreedbEnableCacheID, Config::FreedbEnableCacheDefault))
	{
		cdInfo = CDDBCache::Get()->GetCacheEntry(queryString);

		if (cdInfo != NIL) return cdInfo;
	}

	/* Perform local or remote CDDB query.
	 */
	if (config->GetIntValue(Config::CategoryFreedbID, Config::FreedbEnableLocalID, Config::FreedbEnableLocalDefault) ||
	    config->GetIntValue(Config::CategoryFreedbID, Config::FreedbEnableRemoteID, Config::FreedbEnableRemoteDefault))
	{
		cddbQueryDlg	 dlg(queryString);

		if (dlg.ShowDialog() == Error())
		{
			/* Ask whether to perform this query later.
			 */
			if (QuickMessage(dlg.GetErrorString().Append("\n\n").Append(i18n->TranslateString("Would you like to perform this query again later?", "CDDB::Query::Errors")), i18n->TranslateString("Error"), Message::Buttons::YesNo, Message::Icon::Error) == Message::Button::Yes)
			{
				CDDBBatch().AddQuery(queryString);
			}
		}
		else if (dlg.GetErrorString() != NIL)
		{
			/* Display info message if any.
			 */
			BoCA::Utilities::InfoMessage(dlg.GetErrorString());
		}

		cdInfo = dlg.GetCDDBInfo();
	}

	if (cdInfo != NIL) CDDBCache::Get()->AddCacheEntry(cdInfo);

	return cdInfo;
}
