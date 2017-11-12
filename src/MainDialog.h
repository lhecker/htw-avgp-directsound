#pragma once

#include "direct_sound.h"

class MainDialog : public CDialog {
	DECLARE_MESSAGE_MAP()

public:
	MainDialog(CWnd* pParent = nullptr);

#ifdef AFX_DESIGN_TIME
	enum {
		IDD = IDD_HTWAVGP_DIALOG
	};
#endif

private:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();

	HICON m_hIcon;
	direct_sound ds;
	buffer<int16_t, 2> buffer;

public:
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
};
