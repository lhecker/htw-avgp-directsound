#include "stdafx.h"

#include "MainApp.h"

#include "MainDialog.h"

// The one and only MainApp object
MainApp shared_app;

BEGIN_MESSAGE_MAP(MainApp, CWinApp)
	ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()

MainApp::MainApp() {
}

BOOL MainApp::InitInstance() {
	CWinApp::InitInstance();

	winrt::init_apartment();

	// Create the shell manager, in case the dialog contains
	// any shell tree view or shell list view controls.
	auto pShellManager = std::make_unique<CShellManager>();

	// Activate "Windows Native" visual manager for enabling themes in MFC controls
	CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));

	const auto dlg = std::make_unique<MainDialog>();
	m_pMainWnd = dlg.get();

	INT_PTR nResponse = dlg->DoModal();
	if (nResponse == IDOK) {
		// TODO: Place code here to handle when the dialog is dismissed with OK
	} else if (nResponse == IDCANCEL) {
		// TODO: Place code here to handle when the dialog is dismissed with Cancel
	} else if (nResponse == -1) {
		TRACE(traceAppMsg, 0, "Warning: dialog creation failed, so application is terminating unexpectedly.\n");
		TRACE(traceAppMsg, 0, "Warning: if you are using MFC controls on the dialog, you cannot #define _AFX_NO_MFC_CONTROLS_IN_DIALOGS.\n");
	}

	// Delete the shell manager created above.
	pShellManager.release();

#if !defined(_AFXDLL) && !defined(_AFX_NO_MFC_CONTROLS_IN_DIALOGS)
	ControlBarCleanUp();
#endif

	// Since the dialog has been closed, return FALSE so that we exit the
	// application, rather than start the application's message pump.
	return FALSE;
}
