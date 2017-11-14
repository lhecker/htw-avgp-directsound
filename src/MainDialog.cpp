#include "stdafx.h"

#include "MainDialog.h"

#include "MainApp.h"

BEGIN_MESSAGE_MAP(MainDialog, CDialog)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
END_MESSAGE_MAP()

MainDialog::MainDialog(CWnd* pParent) : CDialog(IDD_HTWAVGP_DIALOG, pParent) {
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void MainDialog::DoDataExchange(CDataExchange* pDX) {
	CDialog::DoDataExchange(pDX);
}

BOOL MainDialog::OnInitDialog() {
	CDialog::OnInitDialog();

	// Set the icon for this dialog. The framework does this automatically
	// when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE); // Set big icon
	SetIcon(m_hIcon, FALSE); // Set small icon

	ds = direct_sound(m_hWnd);
	auto provider = double_buffer<int16_t, 2>::create_sine_wave_provider(264);
	buffer = ds.create_double_buffer<int16_t, 2>(44100, 1, provider);
	buffer.play(true);

	return TRUE; // return TRUE unless you set the focus to a control
}

// If you add a minimize button to your dialog, you will need the code below
// to draw the icon. For MFC applications using the document/view model,
// this is automatically done for you by the framework.
void MainDialog::OnPaint() {
	if (!IsIconic()) {
		CDialog::OnPaint();
		return;
	}

	CPaintDC dc(this); // device context for painting

	SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.m_hDC), 0);

	// Center icon in client rectangle
	int cxIcon = GetSystemMetrics(SM_CXICON);
	int cyIcon = GetSystemMetrics(SM_CYICON);
	CRect rect;
	GetClientRect(&rect);
	int x = (rect.Width() - cxIcon + 1) / 2;
	int y = (rect.Height() - cyIcon + 1) / 2;

	// Draw the icon
	dc.DrawIcon(x, y, m_hIcon);
}

// The system calls this function to obtain the cursor to
// display while the user drags the minimized window.
HCURSOR MainDialog::OnQueryDragIcon() {
	return static_cast<HCURSOR>(m_hIcon);
}
