#pragma once

#include "resource.h"

class MainApp : public CWinApp {
	DECLARE_MESSAGE_MAP()

public:
	MainApp();

	virtual BOOL InitInstance();
};

extern MainApp shared_app;
